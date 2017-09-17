#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>             // uintptr_t, uint32_t
#include <sys/syslog.h>         // log

#define LOG(str, args...) \
do \
{ \
    log(LOG_KERN, "[hsp4] " str "\n", ##args); \
} while(0)

#define LOG_PTR(str, ptr) \
do \
{ \
    uintptr_t _p = (uintptr_t)(ptr); \
    LOG(str ": 0x%08x%08x", ((uint32_t*)&_p)[1], ((uint32_t*)&_p)[0]); \
} while(0)

#endif
