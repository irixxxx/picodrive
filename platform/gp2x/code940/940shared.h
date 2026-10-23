#include <pico/sound/ym2612.h>

/* YM2413 output is rendered at the chip's native rate and resampled on the
 * 920, so this only has to hold one frame at ~50 or 60 Hz */
#define YM2413_BUF_MAX		1120
/* entries in the YM2413 write queue, (port << 8) | data, 0xffff terminates.
 * SMS FM music writes a few dozen registers per frame at most */
#define YM2413_WBUFF_SIZE	128

// max 16 jobs, lower num means higher prio
enum _940_job_t {
	JOB940_INITALL = 1,
	JOB940_INVALIDATE_DCACHE,
	JOB940_YM2612RESETCHIP,
	JOB940_YM2612UPDATEONE,
	JOB940_MP3DECODE,
	JOB940_PICOSTATELOAD,
	JOB940_PICOSTATESAVE2,
	JOB940_PICOSTATELOAD2_PREP,
	JOB940_PICOSTATELOAD2,
	JOB940_MP3RESET,
	JOB940_YM2413RESET,
	JOB940_YM2413UPDATEONE,
	JOB940_YM2413PACKSTATE,
	JOB940_YM2413UNPACKSTATE,
};

//#define MAX_940JOBS	2

/* Shared data layout version. Bump this whenever _940_data_t or the job
 * protocol changes; the 920 refuses to use an image built for another
 * version (see check_shared_layout_940()). */
#define SHARED_LAYOUT_MAGIC 0x9400c002

typedef struct
{
	/* Both cores touch the buffers, but only the 940 ever uses the
	 * ym2612 state. sizeof(YM2612) depends on the compiler ABI (double
	 * alignment), so it goes last: that keeps the offsets of everything
	 * the 920 accesses independent of it. */
	void	*mp3dec;			/* mp3 decoder's handle */
	int     ym_buffer[44100/50*2];		/* this is where the YM2612 samples will be mixed to */
	short   mp3_buffer[2][1152*2];		/* buffers for mp3 decoder's output */
	int     ym2413_buffer[YM2413_BUF_MAX];	/* YM2413 samples, native rate, mono */
	YM2612  ym2612;				/* current state of the emulated YM2612 */
} _940_data_t;


typedef struct
{
	int		vstarts[8];				/* debug: 00: number of starts from each of 8 vectors */
	int		last_lr;				/* debug: 20: last exception's lr */
//	int		jobs[MAX_940JOBS];			/* jobs for second core */
//	int		busy_;					/* unused */
	int		length;					/* number of samples to mix (882 max) */
	int		stereo;					/* mix samples as stereo, doubles sample count automatically */
	int		baseclock;				/* ym2612 settings */
	int		rate;
	int		writebuffsel;			/* which write buffer to use (from 940 side) */
	UINT16  writebuff0[2048];			/* list of writes to ym2612, 1024 for savestates, 1024 extra */
	UINT16  writebuff1[2048];
	int		ym_active_chs;
	int		mp3_len;			/* data len of loaded mp3 */
	int		mp3_offs;			/* current playback offset (just after last decoded frame) */
	int		mp3_buffsel;			/* which output buffer to decode to */
	int		loopc;				/* debug: main loop counter */
	int		mp3_errors;			/* debug: mp3 decoder's error counter */
	int		mp3_lasterr;			/* debug: mp3 decoder's last error */
	int		lastjob;			/* debug: last job id */
	int		ym2413_clk;			/* ym2413 settings: chip clock ... */
	int		ym2413_rate;			/* ... and native sample rate (clk/72) */
	int		ym2413_len;			/* samples to render for this frame */
	int		ym2413_state_len;		/* packed savestate size */
	UINT16  ym2413_wbuff[YM2413_WBUFF_SIZE];	/* queued (port << 8) | data writes */
	UINT8   ym2413_state[1024];		/* packed savestate buffer */

	/* Published by the 940 when its main loop starts: the layout that
	 * image was built with. The 920 compares these to its own before
	 * using the image, so a stale pico940_v4.bin is detected instead of
	 * being fed garbage (the 920 would read YM2612 state as samples).
	 * The YM2612 state itself is not listed: only the 940 uses it. */
	int		sh_magic;			/* SHARED_LAYOUT_MAGIC */
	int		sh_off_mp3dec;
	int		sh_off_ym_buffer;
	int		sh_off_mp3_buffer;
	int		sh_off_ym2413_buffer;
} _940_ctl_t;
