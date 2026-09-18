#include "resource/thp_decoder.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int gThpInitialized;

static int THPReadBe32Audio(const unsigned char *data) {
    return ((int)data[0] << 24) |
           ((int)data[1] << 16) |
           ((int)data[2] << 8) |
           (int)data[3];
}

static short THPReadBeS16Audio(const unsigned char *data) {
    return (short)(((unsigned int)data[0] << 8) | (unsigned int)data[1]);
}

static short THPClampS16Audio(int value) {
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (short)value;
}

typedef struct ThpAdpcmChannel {
    const unsigned char *base;
    const unsigned char *pos;
    const unsigned char *end;
    const unsigned char *coefficients;
    int counter;
    int predictor;
    int scale;
    int prev1;
    int prev2;
} ThpAdpcmChannel;

static int THPAdpcmChannel_Init(
    ThpAdpcmChannel *channel,
    const unsigned char *base,
    const unsigned char *end,
    const unsigned char *coefficients,
    const unsigned char *prevSamples) {
    unsigned char header;

    if (channel == 0 || base == 0 || base >= end || coefficients == 0 || prevSamples == 0) {
        return 0;
    }

    header = *base;
    channel->base = base;
    channel->pos = base + 1;
    channel->end = end;
    channel->coefficients = coefficients;
    channel->counter = 2;
    channel->predictor = (header >> 4) & 7;
    channel->scale = header & 0xf;
    channel->prev1 = THPReadBeS16Audio(prevSamples);
    channel->prev2 = THPReadBeS16Audio(prevSamples + 2);
    return 1;
}

static int THPAdpcmChannel_ReadNibble(ThpAdpcmChannel *channel, int *outNibble) {
    unsigned char value;

    if (channel == 0 || outNibble == 0) {
        return 0;
    }

    if ((channel->counter & 0xf) == 0) {
        if (channel->pos >= channel->end) {
            return 0;
        }
        value = *channel->pos++;
        channel->predictor = (value >> 4) & 7;
        channel->scale = value & 0xf;
        channel->counter += 2;
    }

    if (channel->pos >= channel->end) {
        return 0;
    }
    value = *channel->pos;
    if ((channel->counter & 1) == 0) {
        *outNibble = (value >> 4) & 0xf;
    }
    else {
        *outNibble = value & 0xf;
        channel->pos++;
    }
    channel->counter++;
    if (*outNibble >= 8) {
        *outNibble -= 16;
    }
    return 1;
}

static int THPAdpcmChannel_DecodeSample(ThpAdpcmChannel *channel, short *outSample) {
    int nibble;
    int coef1;
    int coef2;
    int value;

    if (!THPAdpcmChannel_ReadNibble(channel, &nibble)) {
        return 0;
    }

    coef1 = THPReadBeS16Audio(channel->coefficients + channel->predictor * 4);
    coef2 = THPReadBeS16Audio(channel->coefficients + channel->predictor * 4 + 2);
    value = (((nibble << channel->scale) << 11) +
             channel->prev1 * coef1 +
             channel->prev2 * coef2 +
             0x400) >> 11;
    *outSample = THPClampS16Audio(value);
    channel->prev2 = channel->prev1;
    channel->prev1 = *outSample;
    return 1;
}

typedef struct ThpHuffmanTable {
    unsigned char counts[16];
    unsigned char values[256];
    int valueCount;
    int valid;
} ThpHuffmanTable;

typedef struct ThpDecoderComponent {
    int quantTable;
    int dcTable;
    int acTable;
    int dcPredictor;
} ThpDecoderComponent;

typedef struct ThpDecoderContext {
    const unsigned char *start;
    const unsigned char *pos;
    const unsigned char *end;
    int width;
    int height;
    int mcuColumns;
    int restartEnabled;
    int restartInterval;
    int restartCountdown;
    unsigned int quantMask;
    unsigned int huffmanMask;
    unsigned short quantTables[4][64];
    ThpHuffmanTable huffmanTables[2][4];
    unsigned int bitBuffer;
    int bitCount;
    ThpDecoderComponent components[3];
} ThpDecoderContext;

static const unsigned char kThpZigZag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static double gIdctBasis[8][8];
static int gIdctBasisReady;

static void THPInitIdctBasis(void) {
    int x;

    if (gIdctBasisReady) {
        return;
    }
    for (x = 0; x < 8; x++) {
        int u;
        for (u = 0; u < 8; u++) {
            double scale = (u == 0) ? 0.7071067811865476 : 1.0;
            gIdctBasis[x][u] =
                scale * cos(((2.0 * (double)x + 1.0) * (double)u * M_PI) / 16.0);
        }
    }
    gIdctBasisReady = 1;
}

static unsigned short ThpReadBe16(const unsigned char *data) {
    return (unsigned short)(((unsigned int)data[0] << 8) | (unsigned int)data[1]);
}

static int ThpReadByte(ThpDecoderContext *ctx, unsigned char *out) {
    if (ctx->pos >= ctx->end) {
        return 0;
    }
    *out = *ctx->pos++;
    return 1;
}

static int ThpReadU16(ThpDecoderContext *ctx, unsigned short *out) {
    if (ctx->pos + 2 > ctx->end) {
        return 0;
    }
    *out = ThpReadBe16(ctx->pos);
    ctx->pos += 2;
    return 1;
}

static int ThpSkipBytes(ThpDecoderContext *ctx, int count) {
    if (count < 0 || ctx->pos + count > ctx->end) {
        return 0;
    }
    ctx->pos += count;
    return 1;
}

static int THPReadFrameHeader(ThpDecoderContext *ctx) {
    /* 0x80239310 / __THPReadFrameHeader.

       Reads SOF0. The game accepts only 8-bit precision, three components,
       Y sampling 0x22, and U/V sampling 0x11. */
    unsigned char precision;
    unsigned char componentCount;
    int componentIndex;

    if (!ThpSkipBytes(ctx, 2)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (!ThpReadByte(ctx, &precision)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (precision != 8) {
        return 10;
    }
    if (ctx->pos + 5 > ctx->end) {
        return THP_DECODE_BAD_SYNTAX;
    }
    ctx->height = (int)ThpReadBe16(ctx->pos);
    ctx->pos += 2;
    ctx->width = (int)ThpReadBe16(ctx->pos);
    ctx->pos += 2;
    if (!ThpReadByte(ctx, &componentCount)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (componentCount != 3) {
        return 12;
    }

    for (componentIndex = 0; componentIndex < 3; componentIndex++) {
        unsigned char sampling;
        unsigned char quantTable;

        if (!ThpSkipBytes(ctx, 1) || !ThpReadByte(ctx, &sampling)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        if ((componentIndex == 0 && sampling != 0x22) ||
            (componentIndex != 0 && sampling != 0x11)) {
            return 0x13;
        }
        if (!ThpReadByte(ctx, &quantTable)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        ctx->components[componentIndex].quantTable = (int)quantTable;
    }

    return THP_DECODE_OK;
}

static int THPReadScanHeader(ThpDecoderContext *ctx) {
    /* 0x80239450 / __THPReadScanHeader. */
    unsigned char componentCount;
    int componentIndex;

    if (!ThpSkipBytes(ctx, 2)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (!ThpReadByte(ctx, &componentCount)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (componentCount != 3) {
        return 12;
    }

    for (componentIndex = 0; componentIndex < 3; componentIndex++) {
        unsigned char selector;
        int dcTable;
        int acTable;

        if (!ThpSkipBytes(ctx, 1) || !ThpReadByte(ctx, &selector)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        dcTable = selector >> 4;
        acTable = selector & 0xf;
        ctx->components[componentIndex].dcTable = dcTable;
        ctx->components[componentIndex].acTable = acTable;
        if ((ctx->huffmanMask & (1U << dcTable)) == 0) {
            return 0xf;
        }
        if ((ctx->huffmanMask & (1U << (acTable + 1))) == 0) {
            return 0xf;
        }
    }

    if (!ThpSkipBytes(ctx, 3)) {
        return THP_DECODE_BAD_SYNTAX;
    }
    ctx->mcuColumns = (ctx->width + 0xf) >> 4;
    ctx->components[0].dcPredictor = 0;
    ctx->components[1].dcPredictor = 0;
    ctx->components[2].dcPredictor = 0;
    return THP_DECODE_OK;
}

static int THPReadQuantizationTable(ThpDecoderContext *ctx) {
    /* 0x80239570 / __THPReadQuantizationTable.

       The original builds scaled float IDCT tables here. This stage consumes the
       exact marker payload and records which tables exist; the scaled-table port
       belongs with the DCT body. */
    unsigned short length;
    int remaining;

    if (!ThpReadU16(ctx, &length) || length < 2) {
        return THP_DECODE_BAD_SYNTAX;
    }
    remaining = (int)length - 2;
    while (remaining > 0) {
        unsigned char info;
        int precision;
        int tableId;
        int byteCount;
        int i;

        if (!ThpReadByte(ctx, &info)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        remaining--;
        precision = info >> 4;
        tableId = info & 0xf;
        if (tableId >= 4 || precision > 1) {
            return THP_DECODE_UNSUPPORTED_FORMAT;
        }
        byteCount = precision == 0 ? 64 : 128;
        if (remaining < byteCount || ctx->pos + byteCount > ctx->end) {
            return THP_DECODE_BAD_SYNTAX;
        }
        for (i = 0; i < 64; i++) {
            int naturalIndex = kThpZigZag[i];
            if (precision == 0) {
                ctx->quantTables[tableId][naturalIndex] = ctx->pos[i];
            }
            else {
                ctx->quantTables[tableId][naturalIndex] = ThpReadBe16(ctx->pos + i * 2);
            }
        }
        ctx->pos += byteCount;
        remaining -= byteCount;
        ctx->quantMask |= 1U << tableId;
    }

    return remaining == 0 ? THP_DECODE_OK : THP_DECODE_BAD_SYNTAX;
}

static int THPReadHuffmanTableSpecification(ThpDecoderContext *ctx) {
    /* 0x80239910 / __THPReadHuffmanTableSpecification.

       The game builds decode tables at work +0x300/+0x800 after this. For now
       this consumes the payload and preserves the same availability bitmask used
       by __THPReadScanHeader. */
    unsigned short length;
    int remaining;

    if (!ThpReadU16(ctx, &length) || length < 2) {
        return THP_DECODE_BAD_SYNTAX;
    }
    remaining = (int)length - 2;
    while (remaining > 0) {
        unsigned char info;
        unsigned char counts[16];
        unsigned char *values;
        int tableClass;
        int tableId;
        int valueCount = 0;
        int i;

        if (!ThpReadByte(ctx, &info)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        remaining--;
        if (remaining < 16) {
            return THP_DECODE_BAD_SYNTAX;
        }
        memcpy(counts, ctx->pos, sizeof(counts));
        ctx->pos += 16;
        remaining -= 16;
        for (i = 0; i < 16; i++) {
            valueCount += counts[i];
        }
        if (remaining < valueCount || !ThpSkipBytes(ctx, valueCount)) {
            return THP_DECODE_BAD_SYNTAX;
        }
        remaining -= valueCount;

        tableClass = info >> 4;
        tableId = info & 0xf;
        if (tableId >= 4 || tableClass > 1) {
            return THP_DECODE_UNSUPPORTED_FORMAT;
        }
        values = ctx->huffmanTables[tableClass][tableId].values;
        memcpy(ctx->huffmanTables[tableClass][tableId].counts, counts, sizeof(counts));
        memcpy(values, ctx->pos - valueCount, (size_t)valueCount);
        ctx->huffmanTables[tableClass][tableId].valueCount = valueCount;
        ctx->huffmanTables[tableClass][tableId].valid = 1;
        if (tableClass == 0) {
            ctx->huffmanMask |= 1U << tableId;
        }
        else {
            ctx->huffmanMask |= 1U << (tableId + 1);
        }
    }

    return remaining == 0 ? THP_DECODE_OK : THP_DECODE_BAD_SYNTAX;
}

static int THPVideoDecodeParseHeaders(
    const unsigned char *file,
    int fileSize,
    ThpDecoderContext *ctx) {
    int result = THP_DECODE_OK;
    int haveScan = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->start = file;
    ctx->pos = file;
    ctx->end = file + fileSize;

    while (!haveScan) {
        unsigned char markerPrefix;
        unsigned char marker;

        if (!ThpReadByte(ctx, &markerPrefix) || markerPrefix != 0xff) {
            return THP_DECODE_BAD_SYNTAX;
        }
        while (ctx->pos < ctx->end && *ctx->pos == 0xff) {
            ctx->pos++;
        }
        if (!ThpReadByte(ctx, &marker)) {
            return THP_DECODE_BAD_SYNTAX;
        }

        switch (marker) {
        case 0xd8:
            break;
        case 0xdb:
            result = THPReadQuantizationTable(ctx);
            if (result != THP_DECODE_OK) {
                return result;
            }
            break;
        case 0xc4:
            result = THPReadHuffmanTableSpecification(ctx);
            if (result != THP_DECODE_OK) {
                return result;
            }
            break;
        case 0xc0:
            result = THPReadFrameHeader(ctx);
            if (result != THP_DECODE_OK) {
                return result;
            }
            break;
        case 0xdd:
            {
                unsigned short restartInterval;
                if (!ThpSkipBytes(ctx, 2)) {
                    return THP_DECODE_BAD_SYNTAX;
                }
                ctx->restartEnabled = 1;
                if (!ThpReadU16(ctx, &restartInterval)) {
                    return THP_DECODE_BAD_SYNTAX;
                }
                ctx->restartInterval = (int)restartInterval;
                ctx->restartCountdown = ctx->restartInterval;
            }
            break;
        case 0xda:
            result = THPReadScanHeader(ctx);
            if (result != THP_DECODE_OK) {
                return result;
            }
            haveScan = 1;
            break;
        case 0xd9:
            return 11;
        default:
            if (marker == 0xfe || (marker >= 0xe0 && marker <= 0xef)) {
                unsigned short length;
                if (!ThpReadU16(ctx, &length) || length < 2 ||
                    !ThpSkipBytes(ctx, (int)length - 2)) {
                    return THP_DECODE_BAD_SYNTAX;
                }
            }
            else {
                return 11;
            }
            break;
        }
    }

    return THP_DECODE_OK;
}

static int THPReadEntropyByte(ThpDecoderContext *ctx, int *outByte) {
    if (ctx->pos >= ctx->end) {
        return 0;
    }
    *outByte = *ctx->pos++;
    return 1;
}

static int THPReadBits(ThpDecoderContext *ctx, int bitCount, unsigned int *outBits) {
    while (ctx->bitCount < bitCount) {
        int nextByte;
        if (!THPReadEntropyByte(ctx, &nextByte)) {
            return 0;
        }
        ctx->bitBuffer = (ctx->bitBuffer << 8) | (unsigned int)nextByte;
        ctx->bitCount += 8;
    }

    *outBits = (ctx->bitBuffer >> (ctx->bitCount - bitCount)) & ((1U << bitCount) - 1U);
    ctx->bitCount -= bitCount;
    return 1;
}

static int THPExtendSign(unsigned int bits, int bitCount) {
    unsigned int threshold;

    if (bitCount == 0) {
        return 0;
    }
    threshold = 1U << (bitCount - 1);
    if (bits < threshold) {
        return (int)bits + ((-1) << bitCount) + 1;
    }
    return (int)bits;
}

static int THPHuffmanDecode(ThpDecoderContext *ctx, const ThpHuffmanTable *table, int *outValue) {
    int code = 0;
    int firstCode = 0;
    int valueIndex = 0;
    int length;

    if (table == 0 || !table->valid) {
        return 0;
    }

    for (length = 1; length <= 16; length++) {
        unsigned int bit;
        int count;

        if (!THPReadBits(ctx, 1, &bit)) {
            return 0;
        }
        code = (code << 1) | (int)bit;
        count = table->counts[length - 1];
        if (code >= firstCode && code - firstCode < count) {
            int index = valueIndex + code - firstCode;
            if (index >= table->valueCount) {
                return 0;
            }
            *outValue = table->values[index];
            return 1;
        }
        valueIndex += count;
        firstCode = (firstCode + count) << 1;
    }

    return 0;
}

static int THPDecodeBlock(ThpDecoderContext *ctx, int componentIndex, double *coefficients, int *hasAc) {
    const ThpDecoderComponent *component = &ctx->components[componentIndex];
    const ThpHuffmanTable *dcTable = &ctx->huffmanTables[0][component->dcTable];
    const ThpHuffmanTable *acTable = &ctx->huffmanTables[1][component->acTable];
    const unsigned short *quantTable;
    int symbol;
    unsigned int bits;
    int diff;
    int k;

    if (component->quantTable < 0 || component->quantTable >= 4 ||
        component->dcTable < 0 || component->dcTable >= 4 ||
        component->acTable < 0 || component->acTable >= 4) {
        return 0;
    }
    if ((ctx->quantMask & (1U << component->quantTable)) == 0) {
        return 0;
    }
    quantTable = ctx->quantTables[component->quantTable];
    memset(coefficients, 0, sizeof(double) * 64);
    *hasAc = 0;

    if (!THPHuffmanDecode(ctx, dcTable, &symbol)) {
        return 0;
    }
    if (symbol < 0 || symbol > 15) {
        return 0;
    }
    bits = 0;
    if (symbol != 0 && !THPReadBits(ctx, symbol, &bits)) {
        return 0;
    }
    diff = THPExtendSign(bits, symbol);
    ctx->components[componentIndex].dcPredictor += diff;
    coefficients[0] = (double)(ctx->components[componentIndex].dcPredictor * quantTable[0]);

    k = 1;
    while (k < 64) {
        int run;
        int size;
        int naturalIndex;

        if (!THPHuffmanDecode(ctx, acTable, &symbol)) {
            return 0;
        }
        run = symbol >> 4;
        size = symbol & 0xf;
        if (size == 0) {
            if (run == 0xf) {
                k += 16;
                continue;
            }
            break;
        }
        k += run;
        if (k >= 64 || !THPReadBits(ctx, size, &bits)) {
            return 0;
        }
        naturalIndex = kThpZigZag[k];
        coefficients[naturalIndex] = (double)(THPExtendSign(bits, size) * quantTable[naturalIndex]);
        if (coefficients[naturalIndex] != 0.0) {
            *hasAc = 1;
        }
        k++;
    }

    return 1;
}

static unsigned char THPClampByte(double value) {
    int intValue;

    if (value < 0.0) {
        return 0;
    }
    if (value > 255.0) {
        return 255;
    }
    intValue = (int)(value + 0.5);
    if (intValue < 0) {
        return 0;
    }
    if (intValue > 255) {
        return 255;
    }
    return (unsigned char)intValue;
}

static void THPInverseDctBlock(const double *coefficients, int hasAc, unsigned char *out, int stride) {
    double temp[64];
    int y;

    THPInitIdctBasis();
    if (!hasAc) {
        unsigned char value = THPClampByte(coefficients[0] * 0.125 + 128.0);
        for (y = 0; y < 8; y++) {
            memset(out + y * stride, value, 8);
        }
        return;
    }

    for (y = 0; y < 8; y++) {
        int x;
        for (x = 0; x < 8; x++) {
            double sum = 0.0;
            int u;
            for (u = 0; u < 8; u++) {
                sum += coefficients[y * 8 + u] * gIdctBasis[x][u];
            }
            temp[y * 8 + x] = sum;
        }
    }

    for (y = 0; y < 8; y++) {
        int x;
        for (x = 0; x < 8; x++) {
            double sum = 0.0;
            int v;
            for (v = 0; v < 8; v++) {
                sum += temp[v * 8 + x] * gIdctBasis[y][v];
            }
            out[y * stride + x] = THPClampByte(sum * 0.25 + 128.0);
        }
    }
}

static int THPDecodeBlockToPlane(
    ThpDecoderContext *ctx,
    int componentIndex,
    unsigned char *plane,
    int planeWidth,
    int planeHeight,
    int dstX,
    int dstY) {
    double coefficients[64];
    unsigned char pixels[64];
    int hasAc;
    int y;

    if (!THPDecodeBlock(ctx, componentIndex, coefficients, &hasAc)) {
        return 0;
    }
    else {
        THPInverseDctBlock(coefficients, hasAc, pixels, 8);
    }

    for (y = 0; y < 8; y++) {
        int x;
        int py = dstY + y;
        if (py < 0 || py >= planeHeight) {
            continue;
        }
        for (x = 0; x < 8; x++) {
            int px = dstX + x;
            if (px >= 0 && px < planeWidth) {
                plane[py * planeWidth + px] = pixels[y * 8 + x];
            }
        }
    }

    return 1;
}

static int THPConsumeRestartMarker(ThpDecoderContext *ctx) {
    int marker;

    ctx->bitBuffer = 0;
    ctx->bitCount = 0;
    if (ctx->pos >= ctx->end) {
        return 0;
    }
    if (*ctx->pos != 0xff) {
        return 1;
    }
    while (ctx->pos < ctx->end && *ctx->pos == 0xff) {
        ctx->pos++;
    }
    if (ctx->pos >= ctx->end) {
        return 0;
    }
    marker = *ctx->pos;
    if (marker < 0xd0 || marker > 0xd7) {
        return 0;
    }
    ctx->pos++;
    ctx->components[0].dcPredictor = 0;
    ctx->components[1].dcPredictor = 0;
    ctx->components[2].dcPredictor = 0;
    ctx->restartCountdown = ctx->restartInterval;
    return 1;
}

static int THPDecompressYuv(
    ThpDecoderContext *ctx,
    unsigned char *tileY,
    unsigned char *tileU,
    unsigned char *tileV) {
    int mcuRows;
    int mcuCols;
    int uvWidth;
    int uvHeight;
    int mcuIndex = 0;
    int mcuY;

    /* 0x80239F30 + 0x8023AE10 generic row path.

       OP.thp is 480x864, so it uses the generic row decoder. This emits linear
       host Y/U/V planes; the Wii path writes GX I8 texture memory and then draws
       it through THPGXYuv2RgbDraw. */
    mcuRows = (ctx->height + 15) >> 4;
    mcuCols = (ctx->width + 15) >> 4;
    uvWidth = (ctx->width + 1) >> 1;
    uvHeight = (ctx->height + 1) >> 1;
    ctx->bitBuffer = 0;
    ctx->bitCount = 0;

    for (mcuY = 0; mcuY < mcuRows; mcuY++) {
        int mcuX;
        for (mcuX = 0; mcuX < mcuCols; mcuX++) {
            int baseX = mcuX * 16;
            int baseY = mcuY * 16;
            int uvX = mcuX * 8;
            int uvY = mcuY * 8;

            if (ctx->restartEnabled && ctx->restartInterval > 0 &&
                mcuIndex != 0 && (mcuIndex % ctx->restartInterval) == 0) {
                if (!THPConsumeRestartMarker(ctx)) {
                    return 0;
                }
            }

            if (!THPDecodeBlockToPlane(ctx, 0, tileY, ctx->width, ctx->height, baseX, baseY) ||
                !THPDecodeBlockToPlane(ctx, 0, tileY, ctx->width, ctx->height, baseX + 8, baseY) ||
                !THPDecodeBlockToPlane(ctx, 0, tileY, ctx->width, ctx->height, baseX, baseY + 8) ||
                !THPDecodeBlockToPlane(ctx, 0, tileY, ctx->width, ctx->height, baseX + 8, baseY + 8) ||
                !THPDecodeBlockToPlane(ctx, 1, tileU, uvWidth, uvHeight, uvX, uvY) ||
                !THPDecodeBlockToPlane(ctx, 2, tileV, uvWidth, uvHeight, uvX, uvY)) {
                return 0;
            }
            mcuIndex++;
        }
    }

    return 1;
}

int THPInit(void) {
    /* 0x8023C430 / SDK THPInit.

       The real function initializes the RVL THP decoder tables and locked-cache
       state. The host decoder state is explicit, but keeping this entry point
       lets CzanMovieObj follow the game call graph. */
    gThpInitialized = 1;
    return 1;
}

void ThpDecodedFrame_Clear(ThpDecodedFrame *frame) {
    if (frame == 0) {
        return;
    }

    free(frame->planeY);
    free(frame->planeU);
    free(frame->planeV);
    memset(frame, 0, sizeof(*frame));
}

int ThpDecodedFrame_Alloc(ThpDecodedFrame *frame, int width, int height) {
    int ySize;
    int uvSize;

    if (frame == 0 || width <= 0 || height <= 0) {
        return 0;
    }

    ySize = width * height;
    uvSize = ySize / 4;
    if (frame->width == width &&
        frame->height == height &&
        frame->planeY != 0 &&
        frame->planeU != 0 &&
        frame->planeV != 0) {
        return 1;
    }

    ThpDecodedFrame_Clear(frame);
    frame->planeY = (unsigned char *)malloc((size_t)ySize);
    frame->planeU = (unsigned char *)malloc((size_t)uvSize);
    frame->planeV = (unsigned char *)malloc((size_t)uvSize);
    if (frame->planeY == 0 || frame->planeU == 0 || frame->planeV == 0) {
        ThpDecodedFrame_Clear(frame);
        return 0;
    }

    frame->width = width;
    frame->height = height;
    frame->planeYSize = ySize;
    frame->planeUSize = uvSize;
    frame->planeVSize = uvSize;
    return 1;
}

int THPVideoDecodeHost(
    const unsigned char *file,
    int fileSize,
    unsigned char *tileY,
    unsigned char *tileU,
    unsigned char *tileV,
    unsigned char *work) {
    ThpDecoderContext ctx;
    int parseResult;

    /* 0x80239040 / SDK THPVideoDecode.

       Verified contract:
       - input is the THP video component payload from a frame.
       - output is three I8 texture planes: Y, U, V.
       - work is a 0x1000-byte work area.

       The marker parser, SOF0 reader, scan-header reader, DQT reader, and DHT
       reader are now ported in the same stages as the game. The remaining work
       is the exported bitstream/MCU/DCT cluster:
       0x80239CE0, 0x80239F30, 0x8023AE10, 0x8023B070, 0x8023B6F0,
       0x8023BD90, 0x8023A290, and 0x8023A720. */
    if (!gThpInitialized) {
        return THP_DECODE_NOT_INITIALIZED;
    }
    if (file == 0 || fileSize <= 0) {
        return THP_DECODE_NO_INPUT_FILE;
    }
    if (tileY == 0 || tileU == 0 || tileV == 0) {
        return THP_DECODE_NO_OUTPUT_BUFFER;
    }
    if (work == 0) {
        return THP_DECODE_NO_WORK_AREA;
    }
    if (fileSize < 2 || file[0] != 0xff || file[1] != 0xd8) {
        return THP_DECODE_BAD_SYNTAX;
    }

    memset(work, 0, THP_WORK_SIZE);
    parseResult = THPVideoDecodeParseHeaders(file, fileSize, &ctx);
    if (parseResult != THP_DECODE_OK) {
        return parseResult;
    }

    if (ctx.width <= 0 || ctx.height <= 0) {
        return THP_DECODE_BAD_SYNTAX;
    }
    if (!THPDecompressYuv(&ctx, tileY, tileU, tileV)) {
        return THP_DECODE_BAD_SYNTAX;
    }

    return THP_DECODE_OK;
}

int THPAudioDecodeHost(
    short *buffer,
    const unsigned char *audioFrame,
    int audioFrameSize,
    int flag,
    int *outChannelCount) {
    ThpAdpcmChannel left;
    ThpAdpcmChannel right;
    const unsigned char *frameEnd;
    int rightOffset;
    int sampleCount;
    int i;
    int monoOut;

    (void)flag;

    if (outChannelCount != 0) {
        *outChannelCount = 0;
    }
    if (buffer == 0 || audioFrame == 0 || audioFrameSize < 0x52) {
        return 0;
    }

    rightOffset = THPReadBe32Audio(audioFrame);
    sampleCount = THPReadBe32Audio(audioFrame + 4);
    if (sampleCount <= 0 || sampleCount > 0x4000) {
        return 0;
    }
    frameEnd = audioFrame + audioFrameSize;
    monoOut = (rightOffset == 0);

    if (!THPAdpcmChannel_Init(&left, audioFrame + 0x50, frameEnd, audioFrame + 8, audioFrame + 0x48)) {
        return 0;
    }

    if (monoOut) {
        if (outChannelCount != 0) {
            *outChannelCount = 1;
        }
        for (i = 0; i < sampleCount; i++) {
            if (!THPAdpcmChannel_DecodeSample(&left, buffer + i)) {
                return i;
            }
        }
        return sampleCount;
    }

    if (rightOffset < 0x51 || rightOffset >= audioFrameSize ||
        !THPAdpcmChannel_Init(
            &right,
            audioFrame + 0x50 + rightOffset,
            frameEnd,
            audioFrame + 0x28,
            audioFrame + 0x4c)) {
        return 0;
    }

    if (outChannelCount != 0) {
        *outChannelCount = 2;
    }
    for (i = 0; i < sampleCount; i++) {
        if (!THPAdpcmChannel_DecodeSample(&left, buffer + i * 2 + 1) ||
            !THPAdpcmChannel_DecodeSample(&right, buffer + i * 2)) {
            return i;
        }
    }
    return sampleCount;
}
