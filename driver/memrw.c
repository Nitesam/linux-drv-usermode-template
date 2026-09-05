#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uio.h>
#include <linux/version.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/dirent.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/kprobes.h>
#include <linux/input.h>
#include <linux/random.h>
#include <linux/workqueue.h>

#include "../shared/memrw_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("input_helper");
MODULE_DESCRIPTION("uinput helper module");
MODULE_VERSION("1.0");

#ifdef MEMRW_DEBUG
  #define dbg_info(fmt, ...) pr_info("uinput_helper: " fmt, ##__VA_ARGS__)
  #define dbg_warn(fmt, ...) pr_warn("uinput_helper: " fmt, ##__VA_ARGS__)
  #define dbg_err(fmt, ...)  pr_err("uinput_helper: " fmt, ##__VA_ARGS__)
#else
  #define dbg_info(fmt, ...) do {} while (0)
  #define dbg_warn(fmt, ...) do {} while (0)
  #define dbg_err(fmt, ...)  do {} while (0)
#endif

static int            major_number;
static struct cdev    memrw_cdev;
static dev_t          memrw_dev;

static int hidden_pid = -1;

/*
 * Physical-pointer injection: feed input_event() into the first real relative
 * pointer so the deltas leave through the SAME device as genuine movement —
 * no second device appears in /proc/bus/input/devices. A virtual fallback
 * (plain BUS_VIRTUAL identity, honest name) covers the no-mouse case. Button
 * bits observed by the handler are snapshotted for IOCTL_MOUSE_BUTTONS.
 */
#define MEMRW_PHYS_NAME   "hiddev"
#define MEMRW_VIRT_NAME   "VirtualPS/2 VMware Mouse"
#define MEMRW_VIRT_PHYS   "isa0060/serio1/input0"

static struct input_dev   *real_dev     = NULL;
static struct input_handle *real_handle = NULL;
static atomic_t mouse_button_mask = ATOMIC_INIT(0);

static int mouse_button_bit(unsigned int code)
{
    switch (code) {
    case BTN_LEFT:    return 0;
    case BTN_RIGHT:   return 1;
    case BTN_MIDDLE:  return 2;
    case BTN_SIDE:
    case BTN_BACK:    return 3;
    case BTN_EXTRA:
    case BTN_FORWARD: return 4;
    default:          return -1;
    }
}

static void mouse_event(struct input_handle *handle, unsigned int type,
                        unsigned int code, int value)
{
    int bit;

    (void)handle;
    if (type != EV_KEY)
        return;

    bit = mouse_button_bit(code);
    if (bit < 0)
        return;

    if (value)
        atomic_or(BIT(bit), &mouse_button_mask);
    else
        atomic_and(~BIT(bit), &mouse_button_mask);
}

static void mouse_disconnect(struct input_handle *handle)
{
    if (real_handle == handle) {
        real_handle = NULL;
        real_dev    = NULL;
    }
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static int mouse_connect(struct input_handler *handler, struct input_dev *dev,
                         const struct input_device_id *id)
{
    struct input_handle *handle;
    int error;

    /* Relative pointers with a left button only; skip our virtual device. */
    if (!test_bit(EV_REL, dev->evbit) ||
        !test_bit(REL_X, dev->relbit) ||
        !test_bit(REL_Y, dev->relbit) ||
        !test_bit(BTN_LEFT, dev->keybit))
        return -ENODEV;
    if (dev->id.bustype == BUS_VIRTUAL)
        return -ENODEV;

    handle = kzalloc(sizeof(*handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;

    handle->dev     = dev;
    handle->handler = handler;
    handle->name    = MEMRW_PHYS_NAME;
    handle->private = NULL;

    error = input_register_handle(handle);
    if (error) {
        kfree(handle);
        return error;
    }

    error = input_open_device(handle);
    if (error) {
        input_unregister_handle(handle);
        kfree(handle);
        return error;
    }

    if (!real_dev) {
        real_handle = handle;
        real_dev    = dev;
    }
    dbg_info("bound to physical pointer: %s\n", dev->name ? dev->name : "?");
    return 0;
}

static const struct input_device_id mouse_ids[] = {
    { .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
      .evbit = { [BIT_WORD(EV_REL)] = BIT_MASK(EV_REL) } },
    { }
};

static struct input_handler mouse_handler = {
    .name       = MEMRW_PHYS_NAME,
    .id_table   = mouse_ids,
    .event      = mouse_event,
    .connect    = mouse_connect,
    .disconnect = mouse_disconnect,
};

static struct input_dev *vmouse_dev = NULL;
static DEFINE_MUTEX(vmouse_lock);

static int create_virtual_mouse(void)
{
    struct input_dev *dev;
    int ret;

    dev = input_allocate_device();
    if (!dev)
        return -ENOMEM;

    /* Heap-owned identity strings; the input core keeps the pointers. */
    dev->name = kstrdup(MEMRW_VIRT_NAME, GFP_KERNEL);
    dev->phys = kstrdup(MEMRW_VIRT_PHYS, GFP_KERNEL);
    if (!dev->name || !dev->phys) {
        kfree(dev->name);
        kfree(dev->phys);
        input_free_device(dev);
        return -ENOMEM;
    }
    dev->id.bustype = BUS_VIRTUAL;
    dev->id.version = 1;

    input_set_capability(dev, EV_REL, REL_X);
    input_set_capability(dev, EV_REL, REL_Y);
    input_set_capability(dev, EV_KEY, BTN_LEFT);
    input_set_capability(dev, EV_KEY, BTN_RIGHT);
    input_set_capability(dev, EV_KEY, BTN_MIDDLE);

    ret = input_register_device(dev);
    if (ret) {
        kfree(dev->name);
        kfree(dev->phys);
        input_free_device(dev);
        return ret;
    }
    vmouse_dev = dev;
    return 0;
}

static void destroy_virtual_mouse(void)
{
    mutex_lock(&vmouse_lock);
    if (vmouse_dev) {
        struct input_dev *dev = vmouse_dev;
        vmouse_dev = NULL;
        input_unregister_device(dev);
        kfree(dev->name);
        kfree(dev->phys);
    }
    mutex_unlock(&vmouse_lock);
}

static void inject_mouse_move(int dx, int dy)
{
    mutex_lock(&vmouse_lock);
    if (vmouse_dev) {
        input_report_rel(vmouse_dev, REL_X, dx);
        input_report_rel(vmouse_dev, REL_Y, dy);
        input_sync(vmouse_dev);
    }
    mutex_unlock(&vmouse_lock);
}

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
static kallsyms_lookup_name_t ksym_lookup = NULL;

static int resolve_kallsyms(void)
{
    struct kprobe kp = {
        .symbol_name = "kallsyms_lookup_name",
    };
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0)
        return ret;

    ksym_lookup = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    if (!ksym_lookup)
        return -EFAULT;

    return 0;
}

static void __maybe_unused disable_yama_ptrace(void)
{
    int *ptrace_scope = NULL;

    if (!ksym_lookup) return;

    ptrace_scope = (int *)ksym_lookup("yama_scope");
    if (!ptrace_scope)
        ptrace_scope = (int *)ksym_lookup("ptrace_scope");

    if (ptrace_scope)
        *ptrace_scope = 0;
}

struct ftrace_hook {
    const char        *name;
    void              *function;
    void              *original;
    unsigned long      address;
    struct ftrace_ops  ops;
};

static void notrace ftrace_thunk(unsigned long ip, unsigned long parent_ip,
                                 struct ftrace_ops *ops,
                                 struct ftrace_regs *fregs)
{
    struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);
    struct pt_regs *regs = ftrace_get_regs(fregs);

    if (!regs)
        return;

    if (!within_module(parent_ip, THIS_MODULE))
        regs->ip = (unsigned long)hook->function;
}

static int install_hook(struct ftrace_hook *hook)
{
    int ret;

    hook->address = ksym_lookup(hook->name);
    if (!hook->address)
        return -ENOENT;

    *((unsigned long *)hook->original) = hook->address;

    hook->ops.func        = ftrace_thunk;
    hook->ops.flags       = FTRACE_OPS_FL_SAVE_REGS
                          | FTRACE_OPS_FL_RECURSION
                          | FTRACE_OPS_FL_IPMODIFY;

    ret = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
    if (ret)
        return ret;

    ret = register_ftrace_function(&hook->ops);
    if (ret) {
        ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
        return ret;
    }

    return 0;
}

static void remove_hook(struct ftrace_hook *hook)
{
    unregister_ftrace_function(&hook->ops);
    ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
}

static bool should_hide_entry(const char *name)
{
    char pid_str[16];
    int  pid_len;

    if (hidden_pid > 0) {
        pid_len = snprintf(pid_str, sizeof(pid_str), "%d", hidden_pid);
        if (strlen(name) == pid_len && memcmp(name, pid_str, pid_len) == 0)
            return true;
    }

    if (strcmp(name, MEMRW_DEVICE_NAME) == 0)
        return true;

    if (strcmp(name, MEMRW_CLASS_NAME) == 0)
        return true;

    if (strcmp(name, MEMRW_HIDDEN_NODE_NAME) == 0)
        return true;

    /* The cdev is registered under this name in /proc/devices. */
    if (strcmp(name, "hidraw_aux") == 0)
        return true;

    /* The dot node the runtime opens (hidden from readdir via the hook). */
    if (strcmp(name, ".hid_aux") == 0)
        return true;

    return false;
}

typedef asmlinkage long (*sys_getdents64_t)(const struct pt_regs *);
static sys_getdents64_t orig_getdents64 = NULL;

static asmlinkage long hooked_getdents64(const struct pt_regs *regs)
{
    long ret;
    struct linux_dirent64 __user *dirent_user;
    struct linux_dirent64 *dirent_kern, *cur;
    unsigned long offset;

    ret = orig_getdents64(regs);
    if (ret <= 0)
        return ret;

    dirent_user = (struct linux_dirent64 __user *)regs->si;

    dirent_kern = kmalloc(ret, GFP_KERNEL);
    if (!dirent_kern)
        return ret;

    if (copy_from_user(dirent_kern, dirent_user, ret)) {
        kfree(dirent_kern);
        return ret;
    }

    offset = 0;
    while (offset < ret) {
        cur = (struct linux_dirent64 *)((char *)dirent_kern + offset);

        if (cur->d_reclen == 0)
            break;

        if (should_hide_entry(cur->d_name)) {
            if (offset + cur->d_reclen < ret) {
                memmove(cur,
                        (char *)cur + cur->d_reclen,
                        ret - offset - cur->d_reclen);
            }
            ret -= cur->d_reclen;
            continue;
        }

        offset += cur->d_reclen;
    }

    if (copy_to_user(dirent_user, dirent_kern, ret))
        ret = -EFAULT;
    kfree(dirent_kern);
    return ret;
}

static struct ftrace_hook getdents64_hook = {
    .name     = "__x64_sys_getdents64",
    .function = hooked_getdents64,
    .original = &orig_getdents64,
};

static bool hook_installed = false;

/* Retained for reference; intentionally uninstalled (see memrw_init). */
typedef asmlinkage long (*sys_newfstatat_t)(const struct pt_regs *) __maybe_unused;
static sys_newfstatat_t orig_newfstatat = NULL;

static __maybe_unused asmlinkage long hooked_newfstatat(const struct pt_regs *regs)
{
    char filename[256];
    const char __user *user_path = (const char __user *)regs->si;

    if (user_path && !copy_from_user(filename, user_path, sizeof(filename) - 1)) {
        filename[sizeof(filename) - 1] = '\0';

        if (strstr(filename, MEMRW_DEVICE_NAME) ||
            strstr(filename, MEMRW_CLASS_NAME)  ||
            strstr(filename, MEMRW_HIDDEN_NODE_NAME)) {
            return -ENOENT;
        }
    }

    return orig_newfstatat(regs);
}

static __maybe_unused struct ftrace_hook newfstatat_hook = {
    .name     = "__x64_sys_newfstatat",
    .function = hooked_newfstatat,
    .original = &orig_newfstatat,
};

static bool stat_hook_installed __maybe_unused = false;




static ssize_t do_mem_read(pid_t pid, unsigned long addr,
                           void *buf, size_t size)
{
    struct task_struct *task;
    struct pid *pid_struct;
    ssize_t bytes;

    if (size > MEMRW_BUF_SIZE)
        size = MEMRW_BUF_SIZE;

    pid_struct = find_get_pid(pid);
    if (!pid_struct)
        return -ESRCH;

    rcu_read_lock();
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        put_pid(pid_struct);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    bytes = access_process_vm(task, addr, buf, size, FOLL_FORCE);

    put_task_struct(task);
    put_pid(pid_struct);

    return bytes > 0 ? bytes : -EIO;
}

static ssize_t do_mem_write(pid_t pid, unsigned long addr,
                            const void *buf, size_t size)
{
    struct task_struct *task;
    struct pid *pid_struct;
    ssize_t bytes;

    if (size > MEMRW_BUF_SIZE)
        size = MEMRW_BUF_SIZE;

    pid_struct = find_get_pid(pid);
    if (!pid_struct)
        return -ESRCH;

    rcu_read_lock();
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        put_pid(pid_struct);
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    bytes = access_process_vm(task, addr, (void *)buf, size,
                              FOLL_FORCE | FOLL_WRITE);

    put_task_struct(task);
    put_pid(pid_struct);

    return bytes > 0 ? bytes : -EIO;
}

static char ascii_lower_char(char c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static bool str_case_equal(const char *a, const char *b)
{
    if (!a || !b)
        return false;

    while (*a && *b) {
        if (ascii_lower_char(*a) != ascii_lower_char(*b))
            return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool str_case_prefix_equal(const char *a, const char *b, size_t len)
{
    size_t i;

    if (!a || !b)
        return false;

    for (i = 0; i < len; ++i) {
        if (ascii_lower_char(a[i]) != ascii_lower_char(b[i]))
            return false;
    }
    return true;
}

static const char *basename_ptr_kernel(const char *path)
{
    const char *base = path;
    const char *p;

    if (!path)
        return "";

    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    return base;
}

static bool process_name_matches(const char *candidate, const char *name)
{
    size_t candidate_len;
    size_t name_len;

    if (!candidate || !name || name[0] == '\0')
        return false;

    candidate_len = strlen(candidate);
    name_len = strlen(name);

    if (candidate_len == name_len && str_case_equal(candidate, name))
        return true;

    if (name_len > 15 && candidate_len == 15 &&
        str_case_prefix_equal(candidate, name, 15))
        return true;

    return false;
}

static bool cmdline_arg_matches(const char *arg, const char *name)
{
    const char *base;

    if (!arg || arg[0] == '\0')
        return false;

    base = basename_ptr_kernel(arg);
    return process_name_matches(base, name);
}

static bool path_matches_process_name(const char *path, const char *name)
{
    const char *base;

    if (!path || path[0] == '\0')
        return false;

    base = basename_ptr_kernel(path);
    return process_name_matches(base, name);
}

static bool task_cmdline_matches_name(struct task_struct *task, const char *name)
{
    struct mm_struct *mm;
    unsigned long arg_start;
    unsigned long arg_end;
    unsigned long len;
    ssize_t bytes;
    char *buf;
    size_t pos = 0;
    bool matched = false;

    mm = get_task_mm(task);
    if (!mm)
        return false;

    mmap_read_lock(mm);
    arg_start = mm->arg_start;
    arg_end = mm->arg_end;
    mmap_read_unlock(mm);

    if (arg_end <= arg_start) {
        mmput(mm);
        return false;
    }

    len = arg_end - arg_start;
    if (len > 32768)
        len = 32768;

    buf = kzalloc(len + 1, GFP_KERNEL);
    if (!buf) {
        mmput(mm);
        return false;
    }

    bytes = access_process_vm(task, arg_start, buf, len, FOLL_FORCE);
    if (bytes <= 0)
        goto out;

    buf[bytes] = '\0';
    while (pos < bytes) {
        char *arg = buf + pos;
        size_t remaining = bytes - pos;
        size_t arg_len = strnlen(arg, remaining);

        if (arg_len > 0 && cmdline_arg_matches(arg, name)) {
            matched = true;
            break;
        }

        pos += arg_len + 1;
    }

out:
    kfree(buf);
    mmput(mm);
    return matched;
}

static bool task_vma_matches_name(struct task_struct *task, const char *name)
{
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    struct vma_iterator vmi;
    bool matched = false;

    mm = get_task_mm(task);
    if (!mm)
        return false;

    mmap_read_lock(mm);

    vma_iter_init(&vmi, mm, 0);
    for_each_vma(vmi, vma) {
        if (vma->vm_file) {
            char buf[512];
            char *p = d_path(&vma->vm_file->f_path, buf, sizeof(buf));
            if (!IS_ERR(p) && path_matches_process_name(p, name)) {
                matched = true;
                break;
            }
        }
    }

    mmap_read_unlock(mm);
    mmput(mm);
    return matched;
}

static bool task_matches_name_by_pid(pid_t pid, const char *name)
{
    struct pid *pid_struct;
    struct task_struct *task;
    bool matched = false;

    pid_struct = find_get_pid(pid);
    if (!pid_struct)
        return false;

    rcu_read_lock();
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        put_pid(pid_struct);
        return false;
    }
    get_task_struct(task);
    rcu_read_unlock();

    if (process_name_matches(task->comm, name) ||
        task_cmdline_matches_name(task, name) ||
        task_vma_matches_name(task, name))
        matched = true;

    put_task_struct(task);
    put_pid(pid_struct);
    return matched;
}

static int do_find_pid_by_name(const char *name)
{
    struct task_struct *task;
    pid_t *pids;
    int pid_count = 0;
    int i;
    int found_pid = -1;

    if (!name || name[0] == '\0')
        return -1;

    pids = kcalloc(4096, sizeof(*pids), GFP_KERNEL);
    if (!pids)
        return -1;

    rcu_read_lock();
    for_each_process(task) {
        if (pid_count >= 4096)
            break;
        pids[pid_count++] = task->pid;
    }
    rcu_read_unlock();

    for (i = 0; i < pid_count; ++i) {
        if (task_matches_name_by_pid(pids[i], name)) {
            found_pid = pids[i];
            break;
        }
    }

    kfree(pids);
    return found_pid;
}

static unsigned long do_get_base_address(pid_t pid, const char *name)
{
    struct task_struct *task;
    struct pid *pid_struct;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long result = 0;
    struct vma_iterator vmi;

    pid_struct = find_get_pid(pid);
    if (!pid_struct)
        return 0;

    rcu_read_lock();
    task = pid_task(pid_struct, PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        put_pid(pid_struct);
        return 0;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mm = get_task_mm(task);
    if (!mm) {
        put_task_struct(task);
        put_pid(pid_struct);
        return 0;
    }

    mmap_read_lock(mm);

    if (name[0] != '\0') {
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (vma->vm_file) {
                char buf[512];
                char *p = d_path(&vma->vm_file->f_path, buf, sizeof(buf));
                if (!IS_ERR(p) && path_matches_process_name(p, name)) {
                    result = vma->vm_start;
                    break;
                }
            }
        }
    }

    if (result == 0) {
        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (vma->vm_file && (vma->vm_flags & VM_EXEC)) {
                char buf[256];
                char *p = d_path(&vma->vm_file->f_path, buf, sizeof(buf));
                if (!IS_ERR(p)) {
                    if (name[0] == '\0') {
                        result = vma->vm_start;
                        break;
                    }
                }
            }
        }
    }

    if (result == 0 && name[0] != '\0') {
        unsigned long best_start = 0;
        unsigned long best_size  = 0;

        vma_iter_init(&vmi, mm, 0);
        for_each_vma(vmi, vma) {
            if (!vma->vm_file && (vma->vm_flags & VM_EXEC)) {
                unsigned long sz = vma->vm_end - vma->vm_start;
                if (sz > best_size) {
                    best_size  = sz;
                    best_start = vma->vm_start;
                }
            }
        }

        if (best_start > 0x1000)
            result = best_start - 0x1000;
    }

    mmap_read_unlock(mm);

    mmput(mm);
    put_task_struct(task);
    put_pid(pid_struct);
    return result;
}

static void unhide_module(void);

static long memrw_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mem_request req;
    int pid_to_hide;
    ssize_t result;

    switch (cmd) {
    case IOCTL_MEM_READ:
        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;

        if (req.size == 0 || req.size > MEMRW_BUF_SIZE)
            return -EINVAL;

        /* Zero the whole buffer: a short read must not leak stale kmalloc
         * bytes from this (or a previous) request back to userspace. */
        memset(req.buf, 0, sizeof(req.buf));
        result = do_mem_read(req.pid, req.addr, req.buf, req.size);
        if (result < 0)
            return result;

        req.size = result;
        if (copy_to_user((void __user *)arg, &req, sizeof(req)))
            return -EFAULT;

        return 0;

    case IOCTL_MEM_WRITE:
        if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
            return -EFAULT;

        if (req.size == 0 || req.size > MEMRW_BUF_SIZE)
            return -EINVAL;

        result = do_mem_write(req.pid, req.addr, req.buf, req.size);
        if (result < 0)
            return result;

        return 0;

    case IOCTL_HIDE_PID:
        if (copy_from_user(&pid_to_hide, (void __user *)arg, sizeof(int)))
            return -EFAULT;

        hidden_pid = pid_to_hide;
        return 0;

    case IOCTL_MOUSE_MOVE: {
        struct mouse_request mreq;
        if (copy_from_user(&mreq, (void __user *)arg, sizeof(mreq)))
            return -EFAULT;

        /* Prefer the physical device so deltas leave through the same node
         * as genuine movement; fall back to the virtual pointer. */
        if (real_dev) {
            input_event(real_dev, EV_REL, REL_X, mreq.dx);
            input_event(real_dev, EV_REL, REL_Y, mreq.dy);
            input_event(real_dev, EV_SYN, SYN_REPORT, 0);
        } else {
            inject_mouse_move(mreq.dx, mreq.dy);
        }
        return 0;
    }

    case IOCTL_MOUSE_BUTTONS: {
        struct mouse_buttons_response response = {
            .mask = (__u32)atomic_read(&mouse_button_mask),
        };

        if (copy_to_user((void __user *)arg, &response, sizeof(response)))
            return -EFAULT;
        return 0;
    }

    case IOCTL_UNHIDE_MODULE:
        unhide_module();
        return 0;

    case IOCTL_FIND_PID: {
        struct pid_request preq;
        if (copy_from_user(&preq, (void __user *)arg, sizeof(preq)))
            return -EFAULT;
        preq.name[sizeof(preq.name) - 1] = '\0';
        preq.pid = do_find_pid_by_name(preq.name);
        if (copy_to_user((void __user *)arg, &preq, sizeof(preq)))
            return -EFAULT;
        return 0;
    }

    case IOCTL_GET_BASE_ADDR: {
        struct base_addr_request breq;
        if (copy_from_user(&breq, (void __user *)arg, sizeof(breq)))
            return -EFAULT;
        breq.name[sizeof(breq.name) - 1] = '\0';
        breq.addr = do_get_base_address(breq.pid, breq.name);
        if (copy_to_user((void __user *)arg, &breq, sizeof(breq)))
            return -EFAULT;
        return 0;
    }

    default:
        return -ENOTTY;
    }
}

static int memrw_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int memrw_release(struct inode *inode, struct file *file)
{
    return 0;
}

static struct file_operations memrw_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = memrw_ioctl,
    .open           = memrw_open,
    .release        = memrw_release,
};

static struct list_head *saved_prev = NULL;
static bool module_hidden = false;

static void hide_module(void)
{
    if (module_hidden) return;

    saved_prev = THIS_MODULE->list.prev;
    list_del(&THIS_MODULE->list);

    module_hidden = true;
}

static void unhide_module(void)
{
    if (!module_hidden || !saved_prev) return;

    list_add(&THIS_MODULE->list, saved_prev);

    module_hidden = false;
}

static int __init memrw_init(void)
{
    int ret;

    ret = resolve_kallsyms();
    if (ret)
        return ret;

    /* Yama stays at the distribution default: the memory reader runs under
     * a dedicated uid with CAP_SYS_PTRACE, which bypasses ptrace_scope=1
     * without weakening the kernel-wide policy (visible to usermode ACs). */

    ret = alloc_chrdev_region(&memrw_dev, 0, 1, "hidraw_aux");
    if (ret < 0)
        return ret;

    major_number = MAJOR(memrw_dev);

    cdev_init(&memrw_cdev, &memrw_fops);
    memrw_cdev.owner = THIS_MODULE;
    ret = cdev_add(&memrw_cdev, memrw_dev, 1);
    if (ret < 0) {
        unregister_chrdev_region(memrw_dev, 1);
        return ret;
    }

    ret = install_hook(&getdents64_hook);
    if (!ret)
        hook_installed = true;

    /* The newfstatat hook is intentionally NOT installed: with the runtime
     * under a dedicated uid the node is 0600 to another user, so stat-ok +
     * open-EACCES is normal Linux behaviour. Forcing stat to ENOENT while
     * open succeeds is a self-inflicted hook signature. */

    /* Register input after the hooks so the very first event pass is already
     * filtered; the virtual pointer is created only if no physical mouse has
     * been bound by the time userspace first moves. */
    ret = input_register_handler(&mouse_handler);
    if (ret)
        dbg_warn("mouse handler register failed: %d\n", ret);

    ret = create_virtual_mouse();
    if (ret)
        dbg_warn("virtual pointer register failed: %d\n", ret);

    hide_module();

    return 0;
}

static void __exit memrw_exit(void)
{
    if (hook_installed)
        remove_hook(&getdents64_hook);

    input_unregister_handler(&mouse_handler);
    destroy_virtual_mouse();

    cdev_del(&memrw_cdev);
    unregister_chrdev_region(memrw_dev, 1);
}

module_init(memrw_init);
module_exit(memrw_exit);
