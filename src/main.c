#include <stdint.h>             // uintptr_t, uint32_t
#include <string.h>             // strcmp

#include <kern/host.h>          // host_priv_self
#include <kern/task.h>          // kernel_task
#include <mach/boolean.h>       // boolean_t, FALSE
#include <mach/kern_return.h>   // KERN_*, kern_return_t
#include <mach/kmod.h>          // kmod_*_t
#include <mach/mach_host.h>     // mach_zone_*
#include <mach/mach_types.h>    // host_priv_t, ipc_space_t, task_t
#include <mach/port.h>          // ipc_port_t
#include <mach/vm_types.h>      // mach_vm_*_t, natural_t, vm_*_t
#include <mach-o/loader.h>      // MH_MAGIC_64

#include "common.h"
#include "sym.h"

#ifndef __x86_64__
#   error "Only x86_64 is supported"
#endif

struct host
{
    char _[0x10];
    ipc_port_t special[8];
};
#define IKOT_NONE 0
#define IKOT_TASK 2

typedef vm_offset_t ipc_kobject_t;
typedef natural_t   ipc_kobject_type_t;
typedef void        *vm_map_copy_t;

static vm_size_t     sizeof_task = 0;
static ipc_space_t   *_ipc_space_kernel = 0;
static vm_map_t      *_kernel_map = 0;
static vm_map_t      *_zone_map = 0;
static kern_return_t (*_mach_zone_info)(host_priv_t host, mach_zone_name_array_t *names, mach_msg_type_number_t *namesCnt, mach_zone_info_array_t *info, mach_msg_type_number_t *infoCnt) = 0;
static kern_return_t (*_vm_map_copyout)(vm_map_t dst_map, vm_map_address_t *dst_addr, vm_map_copy_t copy) = 0;
static kern_return_t (*_mach_vm_deallocate)(vm_map_t map, vm_offset_t start, vm_size_t size) = 0;
static kern_return_t (*_mach_vm_remap)(vm_map_t target, mach_vm_address_t *dst, mach_vm_size_t size, mach_vm_offset_t mask, int flags, vm_map_t source, mach_vm_address_t src, boolean_t copy, vm_prot_t *cur, vm_prot_t *mac, vm_inherit_t inherit) = 0;
static kern_return_t (*_mach_vm_wire)(host_priv_t host, vm_map_t map, mach_vm_address_t addr, mach_vm_size_t size, vm_prot_t prot) = 0;
static ipc_port_t    (*_ipc_port_alloc_special)(ipc_space_t space) = 0;
static void          (*_ipc_port_dealloc_special)(ipc_port_t port, ipc_space_t space) = 0;
static void          (*_ipc_kobject_set)(ipc_port_t port, ipc_kobject_t kobject, ipc_kobject_type_t type) = 0;
static ipc_port_t    (*_ipc_port_make_send)(ipc_port_t port) = 0;

#define SYM(s) \
do \
{ \
    uintptr_t _addr = find_sym(base, #s); \
    if(!_addr) \
    { \
        return KERN_RESOURCE_SHORTAGE; \
    } \
    LOG_PTR("sym(" #s ")", _addr); \
    _##s = (void*)_addr; \
} while(0)

static mach_vm_offset_t remap_addr = 0;

static kern_return_t kext_load(struct kmod_info *ki, void *data)
{
    kern_return_t ret;
    LOG("Initializing...");

    host_priv_t host = host_priv_self();
    LOG_PTR("realhost", host);
    if(!host) return KERN_RESOURCE_SHORTAGE;

    if(host->special[4])
    {
        LOG("realhost.special[4] exists already!");
         return KERN_NO_SPACE;
    }

    uintptr_t base;
    for(base = ((uintptr_t)host) & 0xfffffffffff00000; *(uint32_t*)base != MH_MAGIC_64; base -= 0x100000);
    LOG_PTR("kernel base", base);

    SYM(ipc_space_kernel);
    SYM(kernel_map);
    SYM(zone_map);
    SYM(mach_zone_info);
    SYM(vm_map_copyout);
    SYM(mach_vm_deallocate);
    SYM(mach_vm_remap);
    SYM(ipc_port_alloc_special);
    SYM(ipc_port_dealloc_special);
    SYM(ipc_kobject_set);
    SYM(ipc_port_make_send);

    // Renamed in High Sierra
    uintptr_t addr = find_sym(base, "mach_vm_wire_external");
    if(!addr)
    {
        addr = find_sym(base, "mach_vm_wire");
        if(!addr)
        {
            return KERN_RESOURCE_SHORTAGE;
        }
    }
    LOG_PTR("sym(mach_vm_wire)", addr);
    _mach_vm_wire = (void*)addr;

    task_t task = kernel_task;
    LOG_PTR("kernel_task", task);
    if(!task) return KERN_RESOURCE_SHORTAGE;

    vm_map_t kmap = *_kernel_map;
    LOG_PTR("kernel_map", kmap);
    if(!kmap) return KERN_RESOURCE_SHORTAGE;

    vm_map_t zmap = *_zone_map;
    LOG_PTR("zone_map", zmap);
    if(!zmap) return KERN_RESOURCE_SHORTAGE;

    ipc_space_t space = *_ipc_space_kernel;
    LOG_PTR("ipc_space_kernel", space);
    if(!space) return KERN_RESOURCE_SHORTAGE;

    vm_map_copy_t namesCopy;
    mach_msg_type_number_t nameCnt;
    vm_map_copy_t infoCopy;
    mach_msg_type_number_t infoCnt;
    ret = _mach_zone_info(host, (mach_zone_name_array_t*)&namesCopy, &nameCnt, (mach_zone_info_array_t*)&infoCopy, &infoCnt);
    LOG("mach_zone_info(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;

    mach_zone_name_t *names;
    mach_zone_info_t *info;
    ret = _vm_map_copyout(kmap, (vm_map_address_t*)&names, namesCopy);
    if(ret == KERN_SUCCESS)
    {
        ret = _vm_map_copyout(kmap, (vm_map_address_t*)&info, infoCopy);
    }
    LOG("vm_map_copyout(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;

    if(nameCnt != infoCnt)
    {
        LOG("nameCnt (%u) != infoCnt (%u)", nameCnt, infoCnt);
        _mach_vm_deallocate(kmap, (vm_offset_t)names, nameCnt * sizeof(*names));
        _mach_vm_deallocate(kmap, (vm_offset_t)info, infoCnt * sizeof(*info));
        return KERN_INVALID_VALUE;
    }
    for(size_t i = 0; i < nameCnt; ++i)
    {
        if(strcmp(names[i].mzn_name, "tasks") == 0)
        {
            sizeof_task = info[i].mzi_elem_size;
            break;
        }
    }
    ret = _mach_vm_deallocate(kmap, (vm_offset_t)names, nameCnt * sizeof(*names));
    if(ret == KERN_SUCCESS)
    {
        _mach_vm_deallocate(kmap, (vm_offset_t)info, infoCnt * sizeof(*info));
    }
    LOG("mach_vm_deallocate(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;

    if(!sizeof_task)
    {
        LOG("Failed to find tasks zone");
        return KERN_RESOURCE_SHORTAGE;
    }
    LOG("sizeof(task_t): 0x%lx", sizeof_task);

    vm_prot_t cur, max;
    ret = _mach_vm_remap(kmap, &remap_addr, sizeof_task, 0, VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR, zmap, (mach_vm_address_t)task, FALSE, &cur, &max, VM_INHERIT_NONE);
    LOG("mach_vm_remap(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;
    LOG_PTR("remap_addr", remap_addr);

    // mach_vm_wire is much easier to use, but increases user wires rather than kernel ones.
    // This is kinda bad for something as critical as tfp0, but for now it seems to work.
    // TODO: Would be nice to use vm_map_wire/vm_map_unwire one day.
    ret = _mach_vm_wire(host, kmap, remap_addr, sizeof_task, VM_PROT_READ | VM_PROT_WRITE);
    LOG("mach_vm_wire(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;

    ipc_port_t port = _ipc_port_alloc_special(space);
    LOG_PTR("port", port);
    if(!port) return KERN_RESOURCE_SHORTAGE;

    _ipc_kobject_set(port, remap_addr, IKOT_TASK);
    host->special[4] = _ipc_port_make_send(port);

    LOG("Success!");
    return KERN_SUCCESS;
}

static kern_return_t kext_unload(struct kmod_info *ki, void *data)
{
    kern_return_t ret;
    LOG("Cleaning up...");

    host_priv_t host = host_priv_self();
    LOG_PTR("realhost", host);
    if(!host) return KERN_RESOURCE_SHORTAGE;

    ipc_port_t port = host->special[4];
    if(!port)
    {
        LOG("realhost.special[4] doesn't exist...");
         return KERN_SUCCESS;
    }

    host->special[4] = 0;
    _ipc_kobject_set(port, IKOT_NONE, 0);
    _ipc_port_dealloc_special(port, *_ipc_space_kernel);

    ret = _mach_vm_wire(host, *_kernel_map, remap_addr, sizeof_task, VM_PROT_NONE);
    LOG("mach_vm_unwire(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;

    // TODO: Find a way to remove the memory entry without freeing the backing memory.
#if 0
    ret = _mach_vm_deallocate(*_kernel_map, remap_addr, sizeof_task);
    LOG("mach_vm_deallocate(): %u", ret);
    if(ret != KERN_SUCCESS) return ret;
#endif

    LOG("Done.");
    return KERN_SUCCESS;
}

__private_extern__ kmod_start_func_t *_realmain = &kext_load;
__private_extern__ kmod_stop_func_t *_antimain = &kext_unload;

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(net.siguza.hsp4, "1.0.0", _start, _stop);
