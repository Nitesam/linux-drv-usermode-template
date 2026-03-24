#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
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

#if 0
static struct input_dev *vmouse_dev = NULL;
static struct delayed_work vmouse_work;
static char vmouse_phys[64] = "virtual-0/input0";

static int create_virtual_mouse(void)
{
    int ret;
    u8 uniq_bytes[6];
    char uniq_str[18];

    vmouse_dev = input_allocate_device();
    if (!vmouse_dev)
        return -ENOMEM;

    get_random_bytes(uniq_bytes, sizeof(uniq_bytes));
    snprintf(uniq_str, sizeof(uniq_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             uniq_bytes[0], uniq_bytes[1], uniq_bytes[2],
             uniq_bytes[3], uniq_bytes[4], uniq_bytes[5]);

    vmouse_dev->name = "Logitech USB Optical Mouse";
    vmouse_dev->phys = vmouse_phys;
    vmouse_dev->uniq = uniq_str;
    vmouse_dev->id.bustype = BUS_USB;
    vmouse_dev->id.vendor  = 0x046d;
    vmouse_dev->id.product = 0xc077;
    vmouse_dev->id.version = 0x0111;

    set_bit(EV_REL, vmouse_dev->evbit);
    set_bit(REL_X,  vmouse_dev->relbit);
    set_bit(REL_Y,  vmouse_dev->relbit);
    set_bit(EV_KEY, vmouse_dev->evbit);
    set_bit(BTN_LEFT,   vmouse_dev->keybit);
    set_bit(BTN_RIGHT,  vmouse_dev->keybit);
    set_bit(BTN_MIDDLE, vmouse_dev->keybit);

    ret = input_register_device(vmouse_dev);
    if (ret) {
        input_free_device(vmouse_dev);
        vmouse_dev = NULL;
        dbg_err("vmouse register failed: %d\n", ret);
        return ret;
    }

    return 0;
}

static void vmouse_delayed_register(struct work_struct *work)
{
    int ret = create_virtual_mouse();
    if (ret)
        dbg_warn("delayed vmouse create failed: %d\n", ret);
}

static void destroy_virtual_mouse(void)
{
    cancel_delayed_work_sync(&vmouse_work);
    if (vmouse_dev) {
        input_unregister_device(vmouse_dev);
        vmouse_dev = NULL;
    }
}

static void inject_mouse_move(int dx, int dy)
{
    if (!vmouse_dev)
        return;

    input_report_rel(vmouse_dev, REL_X, dx);
    input_report_rel(vmouse_dev, REL_Y, dy);
    input_sync(vmouse_dev);
}
#endif

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

static void disable_yama_ptrace(void)
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

typedef asmlinkage long (*sys_newfstatat_t)(const struct pt_regs *);
static sys_newfstatat_t orig_newfstatat = NULL;

static asmlinkage long hooked_newfstatat(const struct pt_regs *regs)
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

static struct ftrace_hook newfstatat_hook = {
    .name     = "__x64_sys_newfstatat",
    .function = hooked_newfstatat,
    .original = &orig_newfstatat,
};

static bool stat_hook_installed = false;

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

static int do_find_pid_by_name(const char *name)
{
    struct task_struct *task;
    int found_pid = -1;
    size_t name_len = strlen(name);

    rcu_read_lock();
    for_each_process(task) {
        const char *comm = task->comm;
        size_t comm_len = strlen(comm);
        if (comm_len == name_len) {
            bool match = true;
            size_t i;
            for (i = 0; i < comm_len; ++i) {
                char a = comm[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                found_pid = task->pid;
                break;
            }
        }
        if (name_len > 15 && comm_len == 15) {
            bool match = true;
            size_t i;
            for (i = 0; i < 15; ++i) {
                char a = comm[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                found_pid = task->pid;
                break;
            }
        }
    }
    rcu_read_unlock();
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
                if (strstr(p, name)) {
                    result = vma->vm_start;
                    break;
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

        memset(req.buf, 0, req.size);
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

#if 0
    case IOCTL_MOUSE_MOVE: {
        struct mouse_request mreq;
        if (copy_from_user(&mreq, (void __user *)arg, sizeof(mreq)))
            return -EFAULT;

        inject_mouse_move(mreq.dx, mreq.dy);
        return 0;
    }
#endif

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

    disable_yama_ptrace();

    ret = alloc_chrdev_region(&memrw_dev, 0, 1, MEMRW_DEVICE_NAME);
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

    ret = install_hook(&newfstatat_hook);
    if (!ret)
        stat_hook_installed = true;

#if 0
    INIT_DELAYED_WORK(&vmouse_work, vmouse_delayed_register);
    schedule_delayed_work(&vmouse_work, msecs_to_jiffies(3000));
#endif

    hide_module();

    return 0;
}

static void __exit memrw_exit(void)
{
    if (hook_installed)
        remove_hook(&getdents64_hook);

    if (stat_hook_installed)
        remove_hook(&newfstatat_hook);



    cdev_del(&memrw_cdev);
    unregister_chrdev_region(memrw_dev, 1);
}

module_init(memrw_init);
module_exit(memrw_exit);