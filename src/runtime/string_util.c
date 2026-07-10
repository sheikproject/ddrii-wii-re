#include "runtime/string_util.h"

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
