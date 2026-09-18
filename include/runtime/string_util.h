#ifndef DDRII_RUNTIME_STRING_UTIL_H
#define DDRII_RUNTIME_STRING_UTIL_H

#include <stdarg.h>

int BoundedStringCompare(const char *left, const char *right, int maxLength);
typedef int (*RuntimeFormatWriteFn)(void *context, const char *text, int length);
int RuntimeFormatWrite(RuntimeFormatWriteFn writer, void *context, const char *format, va_list args);
int RuntimeString_FormatBuffer(char *buffer, const char *format, ...);
int RuntimeDebugReportV(const char *format, va_list args);
int RuntimeDebugReport(const char *format, ...);
int RuntimeDebugSinkQueryAndSet(int *sinkState, int mode);

#endif
