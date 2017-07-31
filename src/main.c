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
#   define OFF_MACH_VM_WIRE             0x1b4470
#   define OFF_IPC_PORT_ALLOC_SPECIAL    0xd4e10
#   define OFF_IPC_KOBJECT_SET           0xefd30
#   define OFF_IPC_PORT_MAKE_SEND        0xd4820
#   define OFF_IPC_SPACE_KERNEL         0x8633e0
#   define OFF_ZONE_MAP                 0x869538
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

typedef kern_return_t (*fn_mach_vm_remap)(vm_map_t target, mach_vm_address_t *dst, mach_vm_size_t size, mach_vm_offset_t mask, int flags, vm_map_t source, mach_vm_address_t src, boolean_t copy, vm_prot_t *cur, vm_prot_t *mac, vm_inherit_t inherit);
typedef kern_return_t (*fn_mach_vm_wire)(host_priv_t host, vm_map_t map, mach_vm_address_t addr, mach_vm_size_t size, vm_prot_t prot);
typedef ipc_port_t    (*fn_ipc_port_alloc_special)(ipc_space_t space);
typedef void          (*fn_ipc_kobject_set)(ipc_port_t port, ipc_kobject_t kobject, ipc_kobject_type_t type);
typedef ipc_port_t    (*fn_ipc_port_make_send)(ipc_port_t port);

#define F(name) static fn_##name _##name = NULL
F(mach_vm_remap);
F(mach_vm_wire);
F(ipc_port_alloc_special);
F(ipc_kobject_set);
F(ipc_port_make_send);
#undef F
static ipc_space_t *_ipc_space_kernel = NULL;
static vm_map_t    *_zone_map         = NULL;

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
    kern_return_t ret;

    host_t host = host_priv_self();
    LOG_PTR("realhost", host);
    if(!host) return KERN_RESOURCE_SHORTAGE;

    uintptr_t base;
    for(base = ((uintptr_t)host) & 0xfffffffffff00000; *(uint32_t*)base != 0xfeedfacf; base -= 0x100000);
    _mach_vm_remap          = (void*)(base + OFF_MACH_VM_REMAP);
    _mach_vm_wire           = (void*)(base + OFF_MACH_VM_WIRE);
    _ipc_port_alloc_special = (void*)(base + OFF_IPC_PORT_ALLOC_SPECIAL);
    _ipc_kobject_set        = (void*)(base + OFF_IPC_KOBJECT_SET);
    _ipc_port_make_send     = (void*)(base + OFF_IPC_PORT_MAKE_SEND);
    _ipc_space_kernel       = (void*)(base + OFF_IPC_SPACE_KERNEL);
    _zone_map               = (void*)(base + OFF_ZONE_MAP);

    task_t task = kernel_task;
    LOG_PTR("kernel_task", task);
    if(!task) return KERN_RESOURCE_SHORTAGE;

    //vm_map_t map = task->map;
    vm_map_t map = get_task_map(task);
    LOG_PTR("kernel_map", map);
    if(!map) return KERN_RESOURCE_SHORTAGE;

    vm_map_t zmap = *_zone_map;
    LOG_PTR("zone_map", zmap);
    if(!zmap) return KERN_RESOURCE_SHORTAGE;

    ipc_space_t space = *_ipc_space_kernel;
    LOG_PTR("ipc_space_kernel", space);
    if(!space) return KERN_RESOURCE_SHORTAGE;

    mach_vm_offset_t remap_addr;
    vm_prot_t cur, max;
    ret = _mach_vm_remap(map, &remap_addr, SIZEOF_TASK, 0, VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR, zmap, (mach_vm_address_t)task, false, &cur, &max, VM_INHERIT_NONE);
    LOG("mach_vm_remap: %u", ret);
    if(ret != KERN_SUCCESS) return ret;
    LOG_PTR("remap_addr", remap_addr);

    ret = _mach_vm_wire(host, map, remap_addr, SIZEOF_TASK, VM_PROT_READ | VM_PROT_WRITE);
    LOG("mach_vm_wire: %u", ret);
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
    // TODO
    return KERN_SUCCESS;
}

__private_extern__ kmod_start_func_t *_realmain = &kext_load;
__private_extern__ kmod_stop_func_t *_antimain = &kext_unload;

extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(net.siguza.hsp4, "1.0.0", _start, _stop);
