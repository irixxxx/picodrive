/* Minimal Helix MP3 declarations for the host-core test.
 *
 * The real header is part of the external Helix sources and needs a target
 * platform define.  940.c only needs these types and error codes; the MP3
 * entry points are stubbed by host_core_test.c, so the host test does not
 * depend on the Helix sources being present.
 */

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
