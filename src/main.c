#include <kern/host.h>
#include <kern/task.h>
#include <mach/host_priv.h>
#include <mach/kern_return.h>
#include <mach/kmod.h>
#include <mach/mach_types.h>
#include <mach/mach_vm.h>
#include <sys/syslog.h>

#if defined(TARGET_10_12_5)
#   define OFF_MACH_VM_REMAP            0x1b4370
#   define OFF_GET_TASK_IPCSPACE        0x138c20
#   define OFF_IPC_PORT_ALLOC_SPECIAL    0xd4e10
#   define OFF_IPC_KOBJECT_SET           0xefd30
#   define SIZEOF_TASK                     0x550
#else
#   error "TODO: auto-detect offsets"
#endif

struct host
{
    char _[0x10];
    ipc_port_t special[8];
};
#define IKOT_TASK 2

extern vm_map_t     get_task_map(task_t t);

typedef vm_offset_t ipc_kobject_t;
typedef natural_t   ipc_kobject_type_t;

static kern_return_t (*_mach_vm_remap)(vm_map_t target, mach_vm_address_t *dst, mach_vm_size_t size, mach_vm_offset_t mask, int flags, vm_map_t source, mach_vm_address_t src, boolean_t copy, vm_prot_t *cur, vm_prot_t *mac, vm_inherit_t inherit);
static ipc_space_t   (*_get_task_ipcspace)(task_t t);
static ipc_port_t    (*_ipc_port_alloc_special)(ipc_space_t space);
static void          (*_ipc_kobject_set)(ipc_port_t port, ipc_kobject_t kobject, ipc_kobject_type_t type);

#define LOG(str, args...) \
do \
{ \
    log(LOG_KERN, "[hsp4] " str "\n", ##args); \
} while(0)

#define LOG_PTR(str, ptr) \
do \
{ \
    uintptr_t _p = (uintptr_t)(ptr); \
    LOG(str ": %08x%08x", ((uint32_t*)&_p)[1], ((uint32_t*)&_p)[0]); \
} while(0)

static kern_return_t kext_load(struct kmod_info *ki, void *data)
{
    host_t host = host_priv_self();
    LOG_PTR("realhost", host);
    if(!host) return KERN_RESOURCE_SHORTAGE;

    uintptr_t base;
    for(base = ((uintptr_t)host) & 0xfffffffffff00000; *(uint32_t*)base != 0xfeedfacf; base -= 0x100000);
    _mach_vm_remap          = (void*)(base + OFF_MACH_VM_REMAP);
    _get_task_ipcspace      = (void*)(base + OFF_GET_TASK_IPCSPACE);
    _ipc_port_alloc_special = (void*)(base + OFF_IPC_PORT_ALLOC_SPECIAL);
    _ipc_kobject_set        = (void*)(base + OFF_IPC_KOBJECT_SET);

    task_t task = kernel_task;
    LOG_PTR("kernel_task", task);
    if(!task) return KERN_RESOURCE_SHORTAGE;

    //vm_map_t map = task->map;
    vm_map_t map = get_task_map(task);
    LOG_PTR("kernel_map", map);
    if(!map) return KERN_RESOURCE_SHORTAGE;

    ipc_space_t space = _get_task_ipcspace(task);
    LOG_PTR("ipc_space_kernel", space);
    if(!space) return KERN_RESOURCE_SHORTAGE;

    mach_vm_offset_t remap_addr;
    vm_prot_t cur, max;
    kern_return_t ret = _mach_vm_remap(map, &remap_addr, SIZEOF_TASK, 0xfff, VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR, map, (mach_vm_address_t)kernel_task, false, &cur, &max, VM_INHERIT_NONE);
    LOG("mach_vm_remap: %u", ret);
    if(ret != KERN_SUCCESS) return ret;
    LOG_PTR("remap_addr", remap_addr);

    ipc_port_t port = _ipc_port_alloc_special(space);
    LOG_PTR("port", port);
    if(!port) return KERN_RESOURCE_SHORTAGE;

    _ipc_kobject_set(port, remap_addr, IKOT_TASK);
    host->special[4] = port;

    LOG("Success!");
    return KERN_SUCCESS;
}

static kern_return_t kext_unload(struct kmod_info *ki, void *data)
{
    // TODO
    return KERN_SUCCESS;
}

__private_extern__ kmod_start_func_t *_realmain = &kext_load;
__private_extern__ kmod_stop_func_t *_antimain = &kext_unload;

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(net.siguza.hsp4, "1.0.0", _start, _stop);
