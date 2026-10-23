#ifndef _940CTL_H_
#define _940CTL_H_
#include <stddef.h>

/* which sound chip currently owns the 940 */
enum { CORE940_NONE = 0, CORE940_YM2612, CORE940_YM2413 };

void sharedmem940_init(void);
void sharedmem940_finish(void);

void YM2612Init_940(int baseclock, int rate, int ssg);
void YM2612ResetChip_940(void);
int  YM2612UpdateOne_940(int *buffer, int length, int stereo, int is_buf_empty);

int  YM2612Write_940(unsigned int a, unsigned int v, int scanline);

int  YM2612PicoTick_940(int n);
void YM2612PicoStateLoad_940(void);
void YM2612PicoStateSave2_940(int tat, int tbt);
int  YM2612PicoStateLoad2_940(int *tat, int *tbt);

void YM2413Init_940(int clock, int rate, int preserve_state);
int  YM2413_940_is_active(void);
void YM2413_regWrite_940(unsigned data);
void YM2413_dataWrite_940(unsigned data);
void YM2413Render_940(int *buffer, int length);
size_t YM2413PicoStateSave_940(void *buf, size_t size);
void YM2413PicoStateLoad_940(const void *buf, size_t size);

/* handing a chip over between the cores when the 940 sound option is
 * toggled, using the packed savestate format */
void ExtFmRelease_940(void);
int  ExtFmStateIsLocal_940(void);

#endif /* _940CTL_H_ */
