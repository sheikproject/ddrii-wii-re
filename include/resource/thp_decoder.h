#ifndef DDRII_RESOURCE_THP_DECODER_H
#define DDRII_RESOURCE_THP_DECODER_H

#define THP_WORK_SIZE 0x1000

typedef enum ThpDecodeResult {
    THP_DECODE_OK = 0,
    THP_DECODE_BAD_SYNTAX = 3,
    THP_DECODE_UNSUPPORTED_FORMAT = 5,
    THP_DECODE_NO_INPUT_FILE = 25,
    THP_DECODE_NO_WORK_AREA = 26,
    THP_DECODE_NO_OUTPUT_BUFFER = 27,
    THP_DECODE_NOT_INITIALIZED = 29,
    THP_DECODE_NOT_PORTED = 1000
} ThpDecodeResult;

typedef struct ThpDecodedFrame {
    int width;
    int height;
    unsigned char *planeY;
    unsigned char *planeU;
    unsigned char *planeV;
    int planeYSize;
    int planeUSize;
    int planeVSize;
} ThpDecodedFrame;

int THPInit(void);
int THPVideoDecodeHost(
    const unsigned char *file,
    int fileSize,
    unsigned char *tileY,
    unsigned char *tileU,
    unsigned char *tileV,
    unsigned char *work);
int THPAudioDecodeHost(
    short *buffer,
    const unsigned char *audioFrame,
    int audioFrameSize,
    int flag,
    int *outChannelCount);
void ThpDecodedFrame_Clear(ThpDecodedFrame *frame);
int ThpDecodedFrame_Alloc(ThpDecodedFrame *frame, int width, int height);

#endif
