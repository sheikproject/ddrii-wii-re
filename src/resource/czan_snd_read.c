#include "resource/czan_snd_read.h"

void CzanSndRead_Open(int *sndRead, int pathOrResource) {
    /* 0x80197DD0 is identified by assert strings from zanSndRead.cpp. It opens a
       DVD/file-backed sound/read resource and initializes the read block.

       Confirmed behavior:
       - asserts when pathOrResource is null
       - opens path/resource through FUN_801B1620
       - resets the object through vtable +0x14
       - initializes file/read info through FUN_801B1930(fileHandle, sndRead +1)
       - stores old sndRead[0x0E] at sndRead[0x10]
       - stores self pointer in sndRead[0x0C]
       - stores the file handle in sndRead[0]
       - validates/reset-block state from sndRead[0x1B]
       - starts the first read through FUN_80198220(...) */
    (void)pathOrResource;
    if (sndRead == 0) {
        return;
    }
}
