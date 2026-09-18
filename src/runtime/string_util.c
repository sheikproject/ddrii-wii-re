#include "runtime/string_util.h"

#include <stdio.h>
#include <string.h>

int BoundedStringCompare(const char *left, const char *right, int maxLength) {
    /* 0x8013340C is equivalent to strncmp(left, right, maxLength). It compares
       bytes until maxLength is exhausted, a difference is found, or a null byte
       matches. */
    if (left == 0 || right == 0 || maxLength <= 0) {
        return 0;
    }

    return strncmp(left, right, (size_t)maxLength);
}

static int RuntimeStdoutWriter(void *context, const char *text, int length) {
    (void)context;
    if (text == 0 || length <= 0) {
        return 1;
    }
    return fwrite(text, 1, (size_t)length, stdout) == (size_t)length;
}

int RuntimeFormatWrite(RuntimeFormatWriteFn writer, void *context, const char *format, va_list args) {
    char buffer[1024];
    int length;

    /* 0x80130E80 is the shared printf-style formatter. The original parses each
       conversion and calls the supplied writer callback for literal and formatted
       spans. The host keeps the same callback contract and delegates formatting to
       the CRT until we need the exact Revolution SDK edge cases. */
    if (writer == 0 || format == 0) {
        return -1;
    }

    length = vsnprintf(buffer, sizeof(buffer), format, args);
    if (length < 0) {
        return -1;
    }
    if (length >= (int)sizeof(buffer)) {
        length = (int)sizeof(buffer) - 1;
    }
    if (!writer(context, buffer, length)) {
        return -1;
    }
    return length;
}

int RuntimeString_FormatBuffer(char *buffer, const char *format, ...) {
    va_list args;
    int length;

    /* 0x80131B38 formats into a caller buffer through RuntimeFormatWrite and writes
       a trailing NUL. */
    if (buffer == 0 || format == 0) {
        return -1;
    }

    va_start(args, format);
    length = vsnprintf(buffer, 0x200, format, args);
    va_end(args);
    if (length < 0) {
        buffer[0] = '\0';
        return -1;
    }
    if (length >= 0x200) {
        length = 0x1ff;
    }
    buffer[length] = '\0';
    return length;
}

int RuntimeDebugReportV(const char *format, va_list args) {
    /* 0x801A5710 builds a va_list-like frame and sends it through FUN_801318BC.
       FUN_801318BC checks the report sink state and then calls FUN_80130E80 with
       the low-level output callback. */
    return RuntimeFormatWrite(RuntimeStdoutWriter, 0, format, args);
}

int RuntimeDebugReport(const char *format, ...) {
    va_list args;
    int result;

    va_start(args, format);
    result = RuntimeDebugReportV(format, args);
    va_end(args);
    return result;
}

int RuntimeDebugSinkQueryAndSet(int *sinkState, int mode) {
    unsigned int flags;
    unsigned int state;

    /* 0x801374B8 queries/updates the report sink mode encoded in word +0x04.
       It returns 0 when the sink is null or disabled. */
    if (sinkState == 0) {
        return 0;
    }

    flags = (unsigned int)sinkState[1];
    if (((flags >> 0x16) & 7U) == 0) {
        return 0;
    }

    state = (flags >> 0x14) & 3U;
    if (state == 0) {
        if (mode < 0) {
            sinkState[1] = (int)((flags & 0xffcfffffU) | 0x100000U);
        }
        else if (mode > 0) {
            sinkState[1] = (int)((flags & 0xffcfffffU) | 0x200000U);
        }
    }
    else if (state == 2) {
        mode = 1;
    }
    else if (state == 1) {
        mode = -1;
    }

    return mode;
}
