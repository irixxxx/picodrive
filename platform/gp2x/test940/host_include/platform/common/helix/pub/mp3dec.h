/* Minimal Helix declarations for the host test. 940.c only needs these
 * types and error codes; the entry points are stubbed. */

#ifndef TEST940_MP3DEC_H
#define TEST940_MP3DEC_H

typedef void *HMP3Decoder;

#define ERR_MP3_MAINDATA_UNDERFLOW  -7
#define ERR_MP3_INDATA_UNDERFLOW    -8

HMP3Decoder MP3InitDecoder(void);
void MP3FreeDecoder(HMP3Decoder hMP3Decoder);
int MP3Decode(HMP3Decoder hMP3Decoder, unsigned char **inbuf,
              int *bytesLeft, short *outbuf, int useSize);

#endif
