/*
 * Code for communication with ARM940 and control of it.
 * (C) notaz, 2006-2009
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#include "../libpicofe/input.h"
#include "../libpicofe/gp2x/soc_mmsp2.h"
#include "../libpicofe/gp2x/soc.h"
#include "../common/mp3.h"
#include "../common/arm_utils.h"
#include "../common/menu_pico.h"
#include "../common/emu.h"
#include <pico/pico_int.h>
#include <pico/sound/ym2612.h>
#include <pico/sound/ym2413.h>
#include <pico/sound/mix.h>
#include "code940/940shared.h"
#include "plat.h"
#include "940ctl.h"

static unsigned char *shared_mem = 0;
static _940_data_t *shared_data = 0;
_940_ctl_t *shared_ctl = 0;
unsigned char *mp3_mem = 0;

#define MP3_SIZE_MAX (0x400000 + 0x800000) // 12M
#define CODE940_FILE "pico940_v4.bin"

int crashed_940 = 0;

static int core940_owner;

/* chip that was handed back to the 920 because the 940 sound option was
 * turned off; nonzero means the local cores are authoritative */
static int ext_fm_released;

/* set when the image on the card was built from a different shared layout */
static int ext_fm_broken;

/* Verify that the loaded image was built from the same headers as this
 * binary. Without this a stale pico940_v4.bin makes the 920 read the wrong
 * part of the shared data (YM2612 state instead of samples), which is heard
 * as a constant buzz. */
static int check_shared_layout_940(void)
{
	/* a crash is reported separately and causes a reload next time */
	if (crashed_940)
		return 0;

	if (shared_ctl->sh_magic != SHARED_LAYOUT_MAGIC) {
		printf("940: image has no layout info (magic %08x)\n",
			(unsigned int)shared_ctl->sh_magic);
		return -1;
	}

	if (shared_ctl->sh_off_mp3dec != (int)offsetof(_940_data_t, mp3dec)
	    || shared_ctl->sh_off_ym_buffer != (int)offsetof(_940_data_t, ym_buffer)
	    || shared_ctl->sh_off_mp3_buffer != (int)offsetof(_940_data_t, mp3_buffer)
	    || shared_ctl->sh_off_ym2413_buffer != (int)offsetof(_940_data_t, ym2413_buffer))
	{
		printf("940: image layout mismatch (buf ids %i/%i/%i/%i)\n",
			shared_ctl->sh_off_ym_buffer, shared_ctl->sh_off_mp3_buffer,
			shared_ctl->sh_off_ym2413_buffer, shared_ctl->sh_off_mp3dec);
		return -1;
	}
	return 0;
}

/* the image can't be used, fall back to the 920's own sound core */
static void disable_ext_fm_940(void)
{
	printf("940: pico940_v4.bin does not match this build, using local sound\n");
	printf("940: rebuild it from the same source tree (make -C code940)\n");
	menu_update_msg("old 940 image, using local sound");
	PicoIn.opt &= ~POPT_EXT_FM;
	currentConfig.s_PicoOpt &= ~POPT_EXT_FM;
	core940_owner = CORE940_NONE;
	ext_fm_released = 0;
	ext_fm_broken = 1;
}

/* shared data buffers are addressed through the offsets the 940 published,
 * not through our own compile-time copy of the layout */
static void *shared_data_ptr(int off)
{
	return (unsigned char *)shared_data + off;
}

/* YM2413 write queue state (920 side) */
static unsigned ym2413_writecnt;
static int ym2413_clock, ym2413_rate;

static FILE *loaded_mp3 = 0;

/***********************************************************/

#define MAXOUT		(+32767)
#define MINOUT		(-32768)

/* limitter */
#define Limit(val, max,min) { \
	if ( val > max )      val = max; \
	else if ( val < min ) val = min; \
}

/* these will be managed locally on our side */
static UINT8 ST_address;	/* address register     */
static INT32 addr_A1;		/* address line A1      */

static int   writebuff_ptr = 0;


/* YM2612 write */
/* a = address */
/* v = value   */
/* returns 1 if sample affecting state changed */
int YM2612Write_940(unsigned int a, unsigned int v, int scanline)
{
	int upd = 1;	/* the write affects sample generation */

	a &= 3;

	//printf("%05i:%03i: ym w ([%i] %02x)\n", Pico.m.frame_count, Pico.m.scanline, a, v);

	switch (a)
	{
		case 0:	/* address port 0 */
			if (addr_A1 == 0 && ST_address == v)
				return 0; /* address already selected, don't send this command to 940 */
			ST_address = v;
			addr_A1 = 0;
			/* don't send DAC or timer related address changes to 940 */
			if (v == 0x24 || v == 0x25 || v == 0x26 || v == 0x2a)
				return 0;
			upd = 0;
			break;

		case 2:	/* address port 1 */
			if (addr_A1 == 1 && ST_address == v)
				return 0;
			ST_address = v;
			addr_A1 = 1;
			upd = 0;
			break;
	}

	//printf("ym pass\n");

	if (currentConfig.EmuOpt & 4)
	{
		UINT16 *writebuff = shared_ctl->writebuffsel ? shared_ctl->writebuff0 : shared_ctl->writebuff1;

		/* detect rapid ym updates */
		if (upd && !(writebuff_ptr & 0x80000000))
		{
			int mid = (Pico.m.pal ? 313 : 262) / 2;
			if (scanline >= mid) {
				//printf("%05i:%03i: rapid ym\n", Pico.m.frame_count, scanline);
				writebuff[writebuff_ptr++ & 0xffff] = 0xfffe;
				writebuff_ptr |= 0x80000000;
				//printf("%05i:%03i: ym w ([%02x] %02x, upd=%i)\n", Pico.m.frame_count, scanline, addr, v, upd);
			}
		}

		/* queue this write for 940 */
		if ((writebuff_ptr&0xffff) < 2047) {
			writebuff[writebuff_ptr++ & 0xffff] = (a<<8)|v;
		} else {
			printf("warning: writebuff_ptr > 2047 ([%i] %02x)\n", a, v);
		}
	}

	return 0; // cause the engine to do updates once per frame only
}


#define CHECK_BUSY(job) \
	(memregs[0x3b46>>1] & (1<<(job-1)))

static void wait_busy_940(int job)
{
	int i;

	job--;
	for (i = 0; (memregs[0x3b46>>1] & (1<<job)) && i < 0x10000; i++)
		spend_cycles(8*1024); // tested to be best for mp3 dec
	if (i < 0x10000) return;

	/* 940 crashed */
	printf("940 crashed (cnt: %i, ve: ", shared_ctl->loopc);
	for (i = 0; i < 8; i++)
		printf("%i ", shared_ctl->vstarts[i]);
	printf(")\n");
	printf("irq pending flags: DUALCPU %04x, SRCPND %08x (see 26), INTPND %08x\n",
		memregs[0x3b46>>1], memregl[0x4500>>2], memregl[0x4510>>2]);
	printf("last lr: %08x, lastjob: %i\n", shared_ctl->last_lr, shared_ctl->lastjob);
	printf("trying to interrupt..\n");
	memregs[0x3B3E>>1] = 0xffff;
	for (i = 0; memregs[0x3b46>>1] && i < 0x10000; i++)
		spend_cycles(8*1024);
	printf("i = 0x%x\n", i);
	printf("irq pending flags: DUALCPU %04x, SRCPND %08x (see 26), INTPND %08x\n",
		memregs[0x3b46>>1], memregl[0x4500>>2], memregl[0x4510>>2]);
	printf("last lr: %08x, lastjob: %i\n", shared_ctl->last_lr, shared_ctl->lastjob);

	menu_update_msg("940 crashed, too much overclock?");
	engineState = PGS_Menu;
	crashed_940 = 1;
}


static void add_job_940(int job)
{
	if (job <= 0 || job > 16) {
		printf("add_job_940: bad job: %i\n", job);
		return;
	}

	// generate interrupt for this job
	job--;
	memregs[(0x3B20+job*2)>>1] = 1;

//	printf("added %i, pending %04x\n", job+1, memregs[0x3b46>>1]);
}


void YM2612PicoStateLoad_940(void)
{
	UINT8 *REGS = YM2612GetRegs();

	/* make sure JOB940_PICOSTATELOAD gets done before next JOB940_YM2612UPDATEONE */
	add_job_940(JOB940_PICOSTATELOAD);
	if (CHECK_BUSY(JOB940_PICOSTATELOAD)) wait_busy_940(JOB940_PICOSTATELOAD);

	writebuff_ptr = 0;
	addr_A1 = *(INT32 *) (REGS + 0x200);
}

void YM2612PicoStateSave2_940(int tat, int tbt)
{
	UINT8 *ym_remote_regs, *ym_local_regs;
	add_job_940(JOB940_PICOSTATESAVE2);
	if (CHECK_BUSY(JOB940_PICOSTATESAVE2)) wait_busy_940(JOB940_PICOSTATESAVE2);

	ym_remote_regs = (UINT8 *) shared_ctl->writebuff0;
	ym_local_regs  = YM2612GetRegs();
	if (*(UINT32 *)(ym_remote_regs + 0x100) != 0x41534d59) {
		printf("code940 didn't return valid save data\n");
		return;
	}

	/* copy addin data only */
	memcpy(ym_local_regs,         ym_remote_regs,         0x20);
	memcpy(ym_local_regs + 0x100, ym_remote_regs + 0x100, 0x30);
	memcpy(ym_local_regs + 0x0b8, ym_remote_regs + 0x0b8, 0x48);
	memcpy(ym_local_regs + 0x1b8, ym_remote_regs + 0x1b8, 0x48);
	*(INT32 *)(ym_local_regs + 0x108) = tat;
	*(INT32 *)(ym_local_regs + 0x10c) = tbt;
}

int YM2612PicoStateLoad2_940(int *tat, int *tbt)
{
	UINT8 *ym_remote_regs, *ym_local_regs;
	ym_local_regs  = YM2612GetRegs();
	ym_remote_regs = (UINT8 *) shared_ctl->writebuff0;

	if (*(UINT32 *)(ym_local_regs + 0x100) != 0x41534d59)
		return -1;

	*tat = *(INT32 *)(ym_local_regs + 0x108);
	*tbt = *(INT32 *)(ym_local_regs + 0x10c);

	if (CHECK_BUSY(JOB940_YM2612UPDATEONE)) wait_busy_940(JOB940_YM2612UPDATEONE);

	/* flush writes */
	if (shared_ctl->writebuffsel == 1) {
		shared_ctl->writebuff0[writebuff_ptr & 0xffff] = 0xffff;
	} else {
		shared_ctl->writebuff1[writebuff_ptr & 0xffff] = 0xffff;
	}
	shared_ctl->writebuffsel ^= 1;
	writebuff_ptr = 0;
	add_job_940(JOB940_PICOSTATELOAD2_PREP);
	if (CHECK_BUSY(JOB940_PICOSTATELOAD2_PREP)) wait_busy_940(JOB940_PICOSTATELOAD2_PREP);

	memcpy(ym_remote_regs, ym_local_regs, 0x200);

	add_job_940(JOB940_PICOSTATELOAD2);
	if (CHECK_BUSY(JOB940_PICOSTATELOAD2)) wait_busy_940(JOB940_PICOSTATELOAD2);

	return 0;
}


static void internal_reset(void)
{
	writebuff_ptr = 0;
	ST_address = addr_A1 = -1;
}


/* this must be called after mmu hack, the allocated regions must not get cached */
void sharedmem940_init(void)
{
	if (shared_mem != NULL) return;

	shared_mem = (unsigned char *) mmap(0, 0x210000, PROT_READ|PROT_WRITE, MAP_SHARED, memdev, 0x2000000);
	if (shared_mem == MAP_FAILED)
	{
		printf("mmap(shared_data) failed with %i\n", errno);
		exit(1);
	}
	shared_data = (_940_data_t *) (shared_mem+0x100000);
	/* this area must not get buffered on either side */
	shared_ctl =  (_940_ctl_t *)  (shared_mem+0x200000);
	mp3_mem = (unsigned char *) mmap(0, MP3_SIZE_MAX, PROT_READ|PROT_WRITE, MAP_SHARED, memdev, 0x2400000);
	if (mp3_mem == MAP_FAILED)
	{
		printf("mmap(mp3_mem) failed with %i\n", errno);
		exit(1);
	}
	crashed_940 = 1;
}


void sharedmem940_finish(void)
{
	munmap(shared_mem, 0x210000);
	munmap(mp3_mem, MP3_SIZE_MAX);
	shared_mem = mp3_mem = NULL;
	shared_data = NULL;
	shared_ctl = NULL;
}


/* Load (if needed) and start the 940, then zero the shared structs.
 * Used by both the YM2612 and the YM2413 init paths. */
static void start_940(void)
{
	reset940(1, 2);
	pause940(1);

	memregs[0x3B40>>1] = 0;      // disable DUALCPU interrupts for 920
	memregs[0x3B42>>1] = 1;      // enable  DUALCPU interrupts for 940

	memregl[0x4504>>2] = 0;        // make sure no FIQs will be generated
	memregl[0x4508>>2] = ~(1<<26); // unmask DUALCPU ints in the undocumented 940's interrupt controller


	if (crashed_940)
	{
		unsigned char ucData[1024];
		int nRead, nLen = 0;
		char binpath[512];
		FILE *fp;

		emu_make_path(binpath, CODE940_FILE, sizeof(binpath));
		fp = fopen(binpath, "rb");
		if(!fp)
		{
			memset(g_screen_ptr, 0, 320*240*2);
			text_out16(10, 100, "failed to open required file:");
			text_out16(10, 110, CODE940_FILE);
			gp2x_video_flip2();
			in_menu_wait(PBTN_MOK|PBTN_MBACK, NULL, 100);
			printf("failed to open %s\n", binpath);
			exit(1);
		}

		while(1)
		{
			nRead = fread(ucData, 1, 1024, fp);
			if(nRead <= 0)
				break;
			memcpy(shared_mem + nLen, ucData, nRead);
			nLen += nRead;
		}
		fclose(fp);
		crashed_940 = 0;

		/* The 940 has no C runtime, so nothing ever zeroes its .bss:
		 * the loop above only copies the image into memory. Clear
		 * everything past the loaded data, up to the shared data area
		 * at 0x100000 (the stack grows down from there). Only on
		 * (re)load -- zeroing while the 940 is running would wipe its
		 * live state. */
		if (nLen < 0x100000)
			memset(shared_mem + nLen, 0, 0x100000 - nLen);
	}

	memset(shared_data, 0, sizeof(*shared_data));
	memset(shared_ctl,  0, sizeof(*shared_ctl));

	memregs[0x3B46>>1] = 0xffff; // clear pending DUALCPU interrupts for 940
	memregl[0x4500>>2] = 0xffffffff; // clear pending IRQs in SRCPND
	memregl[0x4510>>2] = 0xffffffff; // clear pending IRQs in INTPND

	/* start the 940 */
	reset940(0, 2);
	pause940(0);
}


void YM2612Init_940(int baseclock, int rate, int ssg)
{
	static int oldrate;
	u8 state[sizeof(ym2612.REGS) + sizeof(ym2612.addr_A1)];
	int take_over, from_940;

	if (ext_fm_broken) {
		PicoIn.opt &= ~POPT_EXT_FM;
		currentConfig.s_PicoOpt &= ~POPT_EXT_FM;
		return;
	}

	// HACK
	if (shared_data == NULL || (core940_owner == CORE940_YM2612 && Pico.m.frame_count > 0
	    && !crashed_940 && rate == oldrate))
		return;

	/* the game is already running and some core has a live chip: keep
	 * its state for the 940 instance that is about to be initialized.
	 * GP2X savestates use the legacy YM2612 format, so hand the chip
	 * over with that same format. */
	from_940  = core940_owner == CORE940_YM2612;
	take_over = Pico.m.frame_count > 0 && !crashed_940 &&
		core940_owner != CORE940_YM2413;
	if (take_over) {
		ym2612_pack_state_old(from_940);
		memcpy(state, ym2612.REGS, sizeof(state));
	}

	printf("YM2612Init_940()\n");
	printf("Mem usage: shared_data: %i, shared_ctl: %i\n", sizeof(*shared_data), sizeof(*shared_ctl));

	start_940();
	core940_owner = CORE940_YM2612;

	/* cause local ym2612 to init REGS */
	YM2612Init_(baseclock, rate, ssg);

	internal_reset();

	loaded_mp3 = NULL;

	// YM2612ResetChip_940(); // will be done on JOB940_YM2612INIT

	/* now cause 940 to init it's ym2612 stuff */
	shared_ctl->baseclock = baseclock;
	shared_ctl->rate = rate;
	add_job_940(JOB940_INITALL);
	/* also waits for the 940 to publish its layout */
	if (CHECK_BUSY(JOB940_INITALL)) wait_busy_940(JOB940_INITALL);

	if (check_shared_layout_940() < 0) {
		disable_ext_fm_940();
		/* the image was unusable, but we do have the chip state */
		if (take_over) {
			memcpy(ym2612.REGS, state, sizeof(state));
			ym2612_unpack_state_old(0);
		}
		return;
	}

	if (take_over) {
		memcpy(ym2612.REGS, state, sizeof(state));
		ym2612_unpack_state_old(0);
	}

	ext_fm_released = 0;
	oldrate = rate;
}


/* YM2413 (SMS/GG FM sound unit). Rendered at the chip's native rate on the
 * 940 and resampled by the 920; the write queue is applied just before the
 * frame is rendered, which is the same granularity the 920-side code has. */
void YM2413Init_940(int clock, int rate, int preserve_state)
{
	u8 state[1024];
	size_t state_len = 0;

	if (shared_data == NULL)
		return;

	if (ext_fm_broken) {
		PicoIn.opt &= ~POPT_EXT_FM;
		currentConfig.s_PicoOpt &= ~POPT_EXT_FM;
		return;
	}

	if (core940_owner == CORE940_YM2413 && !crashed_940 &&
	    clock == ym2413_clock && rate == ym2413_rate)
	{
		if (preserve_state)
			return;

		/* just reset the chip, no need to restart the 940 */
		ym2413_writecnt = 0;
		shared_ctl->ym2413_wbuff[0] = 0xffff;
		shared_ctl->ym2413_clk = clock;
		shared_ctl->ym2413_rate = rate;
		add_job_940(JOB940_YM2413RESET);
		if (CHECK_BUSY(JOB940_YM2413RESET))
			wait_busy_940(JOB940_YM2413RESET);
		return;
	}

	printf("YM2413Init_940(%i, %i)\n", clock, rate);

	/* the game is already running and some chip has a live state (the
	 * 920 rendered while the option was off, or the 940 owned it before
	 * a clock/rate change): keep it for the new 940 instance */
	if (Pico.m.frame_count > 0 && !crashed_940 &&
	    core940_owner != CORE940_YM2612)
		state_len = ym2413_pack_state(state, sizeof(state));

	start_940();
	core940_owner = CORE940_YM2413;
	ym2413_clock = clock;
	ym2413_rate = rate;

	ym2413_writecnt = 0;
	shared_ctl->ym2413_wbuff[0] = 0xffff;

	shared_ctl->ym2413_clk = clock;
	shared_ctl->ym2413_rate = rate;
	add_job_940(JOB940_YM2413RESET);
	if (CHECK_BUSY(JOB940_YM2413RESET))
		wait_busy_940(JOB940_YM2413RESET);

	if (check_shared_layout_940() < 0) {
		disable_ext_fm_940();
		return;
	}

	if (state_len > 0)
		YM2413PicoStateLoad_940(state, state_len);

	ext_fm_released = 0;
}


void YM2413_regWrite_940(unsigned data)
{
	if (ym2413_writecnt < YM2413_WBUFF_SIZE - 1)
		shared_ctl->ym2413_wbuff[ym2413_writecnt++] = data & 0xff;
}

void YM2413_dataWrite_940(unsigned data)
{
	if (ym2413_writecnt < YM2413_WBUFF_SIZE - 1)
		shared_ctl->ym2413_wbuff[ym2413_writecnt++] = 0x100 | (data & 0xff);
}


/* the 940 only owns the YM2413 once YM2413Init_940() has set it up, which
 * only happens for SMS games (VGM playback keeps using the local chip).
 * Requiring the option as well means register writes fall back to the local
 * chip as soon as the user turns the 940 off, even before the handover. */
int YM2413_940_is_active(void)
{
	return core940_owner == CORE940_YM2413 && (PicoIn.opt & POPT_EXT_FM);
}


/* render 'length' samples at the native rate and copy them out */
void YM2413Render_940(int *buffer, int length)
{
	if (length <= 0)
		return;
	if (length > YM2413_BUF_MAX)
		length = YM2413_BUF_MAX;

	// should never be busy, the previous frame's render is waited for below
	if (CHECK_BUSY(JOB940_YM2413UPDATEONE))
		wait_busy_940(JOB940_YM2413UPDATEONE);

	shared_ctl->ym2413_wbuff[ym2413_writecnt] = 0xffff;
	shared_ctl->ym2413_len = length;
	add_job_940(JOB940_YM2413UPDATEONE);
	if (CHECK_BUSY(JOB940_YM2413UPDATEONE))
		wait_busy_940(JOB940_YM2413UPDATEONE);

	if (crashed_940)
		memset(buffer, 0, length * sizeof(*buffer));
	else
		memcpy(buffer, shared_data_ptr(shared_ctl->sh_off_ym2413_buffer),
		       length * sizeof(*buffer));

	ym2413_writecnt = 0;
}


size_t YM2413PicoStateSave_940(void *buf, size_t size)
{
	size_t len;

	if (CHECK_BUSY(JOB940_YM2413UPDATEONE))
		wait_busy_940(JOB940_YM2413UPDATEONE);

	add_job_940(JOB940_YM2413PACKSTATE);
	if (CHECK_BUSY(JOB940_YM2413PACKSTATE))
		wait_busy_940(JOB940_YM2413PACKSTATE);

	len = shared_ctl->ym2413_state_len;
	if (crashed_940 || len > size || len > sizeof(shared_ctl->ym2413_state))
		return 0;

	memcpy(buf, shared_ctl->ym2413_state, len);
	return len;
}


void YM2413PicoStateLoad_940(const void *buf, size_t size)
{
	if (size > sizeof(shared_ctl->ym2413_state))
		size = sizeof(shared_ctl->ym2413_state);

	if (CHECK_BUSY(JOB940_YM2413UPDATEONE))
		wait_busy_940(JOB940_YM2413UPDATEONE);

	memcpy(shared_ctl->ym2413_state, buf, size);
	shared_ctl->ym2413_state_len = size;
	add_job_940(JOB940_YM2413UNPACKSTATE);
	if (CHECK_BUSY(JOB940_YM2413UNPACKSTATE))
		wait_busy_940(JOB940_YM2413UNPACKSTATE);
}


/* The "second CPU for sound" option was turned off: the 940 must not keep
 * the chip, so have it pack its state and load that into the local core.
 * Both chips use their savestate format, which restores the derived
 * fields (pitch increments, envelope rates) from the packed data. The
 * YM2612 uses the legacy GP2X savestate format so that a state can also
 * be handed back the other way later. */
void ExtFmRelease_940(void)
{
	int chip = core940_owner;
	size_t len = 0;

	if (chip == CORE940_NONE) {
		/* a redundant call (PsndRerate also runs on every menu
		 * exit) must not forget that the local chip is authoritative */
		if (crashed_940)
			ext_fm_released = 0;
		return;
	}

	if (chip == CORE940_YM2612) {
		if (!crashed_940) {
			if (CHECK_BUSY(JOB940_YM2612UPDATEONE))
				wait_busy_940(JOB940_YM2612UPDATEONE);
			/* pull the packed state from the 940 into our REGS
			 * shadow, then load it into the local chip */
			ym2612_pack_state_old(1);
		}
		core940_owner = CORE940_NONE;
		if (!crashed_940)
			ym2612_unpack_state_old(0);
	}
	else if (chip == CORE940_YM2413) {
		if (!crashed_940) {
			if (CHECK_BUSY(JOB940_YM2413UPDATEONE))
				wait_busy_940(JOB940_YM2413UPDATEONE);
			add_job_940(JOB940_YM2413PACKSTATE);
			if (CHECK_BUSY(JOB940_YM2413PACKSTATE))
				wait_busy_940(JOB940_YM2413PACKSTATE);
			if (!crashed_940)
				len = shared_ctl->ym2413_state_len;
		}
		core940_owner = CORE940_NONE;
		if (len > 0 && len <= sizeof(shared_ctl->ym2413_state))
			ym2413_unpack_state(shared_ctl->ym2413_state, len);
	}

	/* if the 940 crashed there is no usable state to take over */
	ext_fm_released = crashed_940 ? 0 : chip;
}


/* nonzero if the 920 rendered the chip since the option was turned off */
int ExtFmStateIsLocal_940(void)
{
	return ext_fm_released;
}


void YM2612ResetChip_940(void)
{
	//printf("YM2612ResetChip_940()\n");
	if (shared_data == NULL) {
		printf("YM2612ResetChip_940: reset before init?\n");
		return;
	}

	YM2612ResetChip_();
	internal_reset();

	add_job_940(JOB940_YM2612RESETCHIP);
}


int YM2612UpdateOne_940(int *buffer, int length, int stereo, int is_buf_empty)
{
	int *ym_buf = shared_data_ptr(shared_ctl->sh_off_ym_buffer);
	int ym_active_chs;

	//printf("YM2612UpdateOne_940()\n");

	if (CHECK_BUSY(JOB940_YM2612UPDATEONE)) wait_busy_940(JOB940_YM2612UPDATEONE);

	ym_active_chs = shared_ctl->ym_active_chs;

	// mix in ym buffer. is_buf_empty means nobody mixed there anything yet and it may contain trash
	if (is_buf_empty && ym_active_chs) memcpy(buffer, ym_buf, length << (stereo + 2));
	else memset32(buffer, 0, length<<stereo);

	if (shared_ctl->writebuffsel == 1) {
		shared_ctl->writebuff0[writebuff_ptr & 0xffff] = 0xffff;
	} else {
		shared_ctl->writebuff1[writebuff_ptr & 0xffff] = 0xffff;
	}
	writebuff_ptr = 0;

	/* predict sample counter for next frame */
	if (Pico.snd.len_e_add) {
		length = Pico.snd.len;
		if (Pico.snd.len_e_cnt + Pico.snd.len_e_add >= 0x10000) length++;
	}

	/* give 940 ym job */
	shared_ctl->writebuffsel ^= 1;
	shared_ctl->length = length;
	shared_ctl->stereo = stereo;

	add_job_940(JOB940_YM2612UPDATEONE);

	return ym_active_chs;
}


/***********************************************************/

// FIXME: double buffering no longer used..

int mp3dec_decode(FILE *f, int *file_pos, int file_len)
{
	if (!(PicoIn.opt & POPT_EXT_FM)) {
		return _mp3dec_decode(f, file_pos, file_len);
	}

	// check if playback was started, track not ended
	if (loaded_mp3 == NULL || shared_ctl->mp3_offs >= shared_ctl->mp3_len) {
		*file_pos = file_len;
		return 1;
	}

	/* do we have to wait? */
	if (CHECK_BUSY(JOB940_MP3DECODE))
		wait_busy_940(JOB940_MP3DECODE);

	memcpy(cdda_out_buffer,
		(short *)shared_data_ptr(shared_ctl->sh_off_mp3_buffer)
			+ shared_ctl->mp3_buffsel * (1152*2),
		sizeof(cdda_out_buffer));

	*file_pos = shared_ctl->mp3_offs;

	if (shared_ctl->mp3_offs < shared_ctl->mp3_len) {
		// ask to decode more
		//shared_ctl->mp3_buffsel ^= 1;
		add_job_940(JOB940_MP3DECODE);
	}

	return 0;
}

int mp3dec_start(FILE *f, int fpos_start)
{
	if (!(PicoIn.opt & POPT_EXT_FM)) {
		return _mp3dec_start(f, fpos_start);
	}

	if (loaded_mp3 != f)
	{
		if (PicoIn.osdMessage != NULL)
		{
			fseek(f, 0, SEEK_END);
			if (ftell(f) > 2*1024*1024)
				PicoIn.osdMessage("Loading MP3...");
		}
		fseek(f, 0, SEEK_SET);
		fread(mp3_mem, 1, MP3_SIZE_MAX, f);
		if (!feof(f))
			printf("Warning: mp3 was too large, not all data loaded.\n");
		shared_ctl->mp3_len = ftell(f);
		loaded_mp3 = f;

		// as we are going to change 940's cacheable area,
		// we must invalidate it's cache..
		if (CHECK_BUSY(JOB940_MP3DECODE))
			wait_busy_940(JOB940_MP3DECODE);
		add_job_940(JOB940_INVALIDATE_DCACHE);
		reset_timing = 1;
	}

	shared_ctl->mp3_offs = fpos_start;
	shared_ctl->mp3_buffsel = 0;

	add_job_940(JOB940_MP3RESET);
	if (CHECK_BUSY(JOB940_MP3RESET))
		wait_busy_940(JOB940_MP3RESET);

	// because we decode ahea, need to start now
	if (shared_ctl->mp3_offs < shared_ctl->mp3_len) {
		add_job_940(JOB940_MP3DECODE);
	}

	return 0;
}

