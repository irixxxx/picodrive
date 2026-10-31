#include <pico/sound/ym2612.h>

/* one frame of native-rate YM2413 output; resampled on the 920 */
#define YM2413_BUF_MAX		1120
/* legacy queue declaration, kept so the shared layout stays stable */
#define YM2413_WBUFF_SIZE	128
/* YM2413 reuses the YM2612 double write buffers.  An entry is
 * (port << 8) | data; 0xffff terminates a list and 0xfffe marks a split. */
#define YM_WBUFF_MAX		2048
#define YM_WBUFF_END		0xffff
#define YM_WBUFF_SPLIT		0xfffe
#define YM2612_WBUFF_MAX	YM_WBUFF_MAX
#define YM2413_WBUFF_MAX	YM_WBUFF_MAX

/* Bumped on any shared protocol change; an old image leaves it zero. */
#define PICODRIVE_940_PROTOCOL	1

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

/* A shared buffer published by the 940. offset is relative to shared_data:
 * the two cores map the same physical memory at different addresses. */
typedef struct
{
	int		offset;
	int		length;		/* bytes */
} _940_buf_desc_t;

typedef struct
{
	/* Only the 940 uses the YM2612 state; keep it last so the offsets
	 * the 920 shares do not depend on sizeof(YM2612). */
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
	int		ym2612_flags;			/* ST_SSG | ST_DAC */
	int		writebuffsel;			/* which write buffer to use (from 940 side) */
	UINT16  writebuff0[YM2612_WBUFF_MAX];		/* list of writes to ym2612, 1024 for savestates, 1024 extra */
	UINT16  writebuff1[YM2612_WBUFF_MAX];
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

	/* Published by the 940 before accepting jobs; the 920 rejects zero or
	 * out-of-range descriptors. */
	_940_buf_desc_t sh_ym_buffer;
	_940_buf_desc_t sh_mp3_buffer;
	_940_buf_desc_t sh_ym2413_buffer;

	/* Published after startup; zero means a pre-versioned image. */
	int protocol;
} _940_ctl_t;
