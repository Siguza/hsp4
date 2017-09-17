# hsp4.kext

macOS kext to bring the kernel task port (back) to userland!

### Warning

This kext **severely** lowers the security of your operating system and is of **zero** use to ordinary users!  
**Do not install** unless you know what you are doing.

### Building

    make clean all

Installation should be clear, obviously requires SIP to be weakened/disabled.

### License

[MIT](https://github.com/Siguza/hsp4/blob/master/LICENSE).

## Technical background

The kernel task port is a powerful thing, and is really useful for kernel-space research or exploit development/debugging. Historically, while on iOS only a jailbreak would provide access to it, on macOS the root user has always been able obtain it via one way or another.

`task_for_pid` used to be the most straightforward such way - just pass a pid of `0` and you've got it. But Apple killed that way back in Snow Leopard already:

    kern_return_t task_for_pid(struct task_for_pid_args *args)
    {
        // ...

        /* Always check if pid == 0 */
        if (pid == 0) {
            (void ) copyout((char *)&t1, task_addr, sizeof(mach_port_name_t));
            AUDIT_MACH_SYSCALL_EXIT(KERN_FAILURE);
            return(KERN_FAILURE);
        }

        // ...
    }

Some time after that, it was revealed that root could still obtain the kernel task port via the `processor_set_tasks` API - which someone at Apple must've known, since they had specifically disabled this on iOS since day one, but just left it as it was on macOS. That is, until SIP was introduced. At that point, `processor_set_tasks` was changed to only include the kernel task port if SIP was disabled - which wasn't a fatal strike since you could just disable SIP, but another push against kernel task port access.

With the release of macOS Sierra 10.12.4 and iOS 10.3 however, Apple has taken a way more radical approach:

    task_t convert_port_to_task_with_exec_token(ipc_port_t port, uint32_t *exec_token)
    {
        // ...

        if (task == kernel_task && current_task() != kernel_task) {
            ip_unlock(port);
            return TASK_NULL;
        }

        // ...
    }

Instead of trying to prevent users from _obtaining_ it, they crippled the conversion API to stop them from _using_ it. So on the latest release you can still _get_ the kernel task port, but almost all APIs will treat it like `MACH_PORT_NULL` and refuse to _work_ on it.

And that's where this kext comes in. It brings the kernel task port back to userland, in the fashion started by the Pangu jailbreak team, i.e. via `realhost.special[4]`. From there, it can be retrieved by any root process by means of:

    task_t kernel_task;
    host_get_special_port(mach_host_self(), HOST_LOCAL_NODE, 4, &kernel_task);

## Implementation

The check against the `kernel_task` that Apple added is a simple pointer comparison, so the general idea of bypassing it is equally simple:

1. Remap the `kernel_task` to another virtual address.
2. Create a new IPC port of type `IKOT_TASK` with that remapped address as backing object.
3. Assign that port to `realhost.special[4]`.

In code, this looks about like this (error handling omitted):

    vm_map_remap(
        kernel_map,
        &remap_addr,
        sizeof(task_t),
        0,
        VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR,
        zone_map,
        kernel_task,
        false,
        &dummy,
        &dummy,
        VM_INHERIT_NONE
    );
    mach_vm_wire(&realhost, kernel_map, remap_addr, sizeof(task_t), VM_PROT_READ | VM_PROT_WRITE);
    ipc_port_t port = ipc_port_alloc_special(ipc_space_kernel);
    ipc_kobject_set(port, remap_addr, IKOT_TASK);
    realhost.special[4] = ipc_port_make_send(port);

A few notes:

- You have to pass `zone_map` as source, `kernel_map` will not work.
- `mach_vm_wire` is quite a dirty hack since it increases the user wire count rather than the kernel one, but for now it works. `vm_map_wire` would be the proper way to go, but that would require the address to be page-aligned and the size to be page-rounded, which is tedious and which `mach_vm_wire` does for us.

And that's pretty much all there is to it. Enjoy your tfp0! :)
