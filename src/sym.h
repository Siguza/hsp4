#ifndef SYM_H
#define SYM_H

#include <stdint.h>             // uintptr_t

uintptr_t find_sym(uintptr_t base, const char *name);

#endif
