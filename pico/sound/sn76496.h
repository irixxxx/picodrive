#ifndef SN76496_H
#define SN76496_H

extern int *sn76496_regs;

void SN76496Write(int data);
void SN76496Update(short *buffer,int length,int stereo);
void SN76496Config(int panning);
void SN76496_set_clockrate(int clock,int sample_rate);
int  SN76496_init(int clock,int sample_rate);

#endif
