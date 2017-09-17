#include <stdint.h>             // uintptr_t
#include <string.h>             // strcmp

#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "common.h"
#include "sym.h"

#define FOREACH_LC(hdr, cmd) for(lc_t *cmd = (lc_t*)(hdr + 1), *end = (lc_t*)((uintptr_t)cmd + hdr->sizeofcmds); cmd < end; cmd = (lc_t*)((uintptr_t)cmd + cmd->cmdsize))

typedef struct mach_header_64       hdr_t;
typedef struct load_command         lc_t;
typedef struct segment_command_64   seg_t;
typedef struct symtab_command       stab_t;
typedef struct nlist_64             symtab_t;

uintptr_t find_sym(uintptr_t base, const char *name)
{
    static stab_t *stab = 0;
    static symtab_t *symtab = 0;
    static const char *strtab = 0;

    hdr_t *hdr = (hdr_t*)base;
    if(!stab)
    {
        FOREACH_LC(hdr, cmd)
        {
            if(cmd->cmd == LC_SYMTAB)
            {
                stab = (stab_t*)cmd;
                break;
            }
        }
        if(!stab)
        {
            LOG("Failed to find LC_SYMTAB");
            return 0;
        }
        LOG("symtab offset: 0x%08x", stab->symoff);
        LOG("strtab offset: 0x%08x", stab->stroff);
    }

    if(!symtab || !strtab)
    {
        FOREACH_LC(hdr, cmd)
        {
            if(cmd->cmd == LC_SEGMENT_64)
            {
                seg_t *seg = (seg_t*)cmd;
                if(seg->fileoff <= stab->symoff && seg->fileoff + seg->filesize > stab->symoff)
                {
                    symtab = (symtab_t*)(seg->vmaddr + stab->symoff - seg->fileoff);
                }
                if(seg->fileoff <= stab->stroff && seg->fileoff + seg->filesize > stab->stroff)
                {
                    strtab = (const char*)(seg->vmaddr + stab->stroff - seg->fileoff);
                }
            }
        }
        if(!symtab)
        {
            LOG("Failed to find symtab");
            return 0;
        }
        if(!strtab)
        {
            LOG("Failed to find strtab");
            return 0;
        }
    }

    for(size_t i = 0; i < stab->nsyms; ++i)
    {
        const char *sym = &strtab[symtab[i].n_un.n_strx];
        if(sym[0] == '_' && strcmp(&sym[1], name) == 0)
        {
            return symtab[i].n_value;
        }
    }

    LOG("Failed to find symbol: %s", name);
    return 0;
}
