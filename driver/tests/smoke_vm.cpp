// Runs only as /init in the disposable QEMU guest created by run_vm.py.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <linux/uinput.h>
#include "../../shared/memrw_ioctl.h"

static int failures;
static void check(bool result, const char* name)
{
    std::printf("%s %s (errno=%d)\n", result ? "PASS" : "FAIL", name, errno);
    std::fflush(stdout);
    if (!result) ++failures;
}
static bool file_contains(const char* path, const char* needle)
{
    char buffer[32768];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (count < 0) return false;
    buffer[count] = 0;
    return strstr(buffer, needle) != nullptr;
}
static bool load(const char* path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    bool ok = syscall(SYS_finit_module, fd, "", 0) == 0;
    close(fd);
    return ok;
}
static unsigned device_major()
{
    FILE* file = fopen("/proc/devices", "r");
    if (!file) return 0;
    char line[256], name[128];
    unsigned major = 0, found = 0;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "%u %127s", &major, name) == 2 &&
            strcmp(name, MEMRW_DEVICE_NAME) == 0) found = major;
    fclose(file);
    return found;
}
static void hotplug(int fd)
{
    // The simulated physical source is itself a visible uinput device in this VM.
    if (!load("/uinput.ko")) {
        check(false, "load guest uinput test source");
        return;
    }
    mknod("/dev/uinput", S_IFCHR | 0600, makedev(10, 223));
    for (int cycle = 0; cycle < 8; ++cycle) {
        int source = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (source < 0) { check(false, "open uinput source"); break; }
        ioctl(source, UI_SET_EVBIT, EV_KEY);
        ioctl(source, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(source, UI_SET_EVBIT, EV_REL);
        ioctl(source, UI_SET_RELBIT, REL_X);
        ioctl(source, UI_SET_RELBIT, REL_Y);
        uinput_setup setup{};
        strcpy(setup.name, "VM hotplug test pointer");
        setup.id.bustype = BUS_VIRTUAL;
        check(ioctl(source, UI_DEV_SETUP, &setup) == 0 &&
            ioctl(source, UI_DEV_CREATE) == 0, "connect input source");
        pid_t child = fork();
        if (child == 0) {
            mouse_request move{1, -1};
            for (int i = 0; i < 2000; ++i)
                if (ioctl(fd, IOCTL_MOUSE_MOVE, &move)) _exit(1);
            _exit(0);
        }
        check(child > 0, "spawn movement reader");
        check(ioctl(source, UI_DEV_DESTROY) == 0, "disconnect during mouse ioctls");
        close(source);
        if (child > 0) {
            int status = 0;
            waitpid(child, &status, 0);
            check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "concurrent movement survives unplug");
        }
    }
    check(syscall(SYS_delete_module, "uinput", O_NONBLOCK) == 0, "unload test input source");
}

int main()
{
    // Refuse accidental execution on the host even by a privileged user.
    if (getpid() != 1) { std::fputs("Guest init only. Use run_vm.py.\n", stderr); return 1; }
    mkdir("/dev", 0755); mkdir("/proc", 0755); mkdir("/sys", 0755); mkdir("/tmp", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, nullptr);
    mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));
    int console = open("/dev/console", O_RDWR);
    for (int i = 0; i < 3; ++i) dup2(console, i);
    mount("proc", "/proc", "proc", 0, nullptr);
    mount("sysfs", "/sys", "sysfs", 0, nullptr);
    const bool yamaPresent = access("/proc/sys/kernel/yama/ptrace_scope", F_OK) == 0;
    int yama = open("/proc/sys/kernel/yama/ptrace_scope", O_WRONLY);
    if (yamaPresent) check(yama >= 0 && write(yama, "1\n", 2) == 2, "set guest Yama policy");
    if (yama >= 0) close(yama);

    for (int cycle = 0; cycle < 5; ++cycle) {
        if (!load("/memrw.ko")) { check(false, "load module"); break; }
        check(file_contains("/proc/modules", "memrw "), "module remains visible");
        if (yamaPresent)
            check(file_contains("/proc/sys/kernel/yama/ptrace_scope", "1"), "Yama preserved at load");
        unsigned major = device_major();
        check(major != 0, "visible device registered");
        unlink(MEMRW_DEVICE_PATH);
        check(mknod(MEMRW_DEVICE_PATH, S_IFCHR | 0600, makedev(major, 0)) == 0, "create visible node");
        struct stat st{};
        check(stat(MEMRW_DEVICE_PATH, &st) == 0 && (st.st_mode & 0777) == 0600,
            "stat works and device mode is restricted");
        int fd = open(MEMRW_DEVICE_PATH, O_RDWR);
        if (fd < 0) { check(false, "open device"); break; }
        for (int i = 0; i < 30; ++i)
            check(file_contains("/proc/bus/input/devices", "memrw virtual pointer"),
                "enumerate input device after registration returns");
        errno = 0;
        check(ioctl(fd, IOCTL_HIDE_PID, nullptr) == -1 && errno == EOPNOTSUPP, "PID hiding retired");
        check(ioctl(fd, IOCTL_UNHIDE_MODULE) == -1 && errno == EOPNOTSUPP, "module hiding retired");

        unsigned long value = 0x12345678;
        mem_request request{};
        request.pid = getpid(); request.addr = (unsigned long)&value; request.size = sizeof(value);
        check(ioctl(fd, IOCTL_MEM_READ, &request) == 0 && request.size == sizeof(value) &&
            memcmp(request.buf, &value, sizeof(value)) == 0, "privileged memory read");
        long page = sysconf(_SC_PAGESIZE);
        auto* memory = (unsigned char*)mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (memory != MAP_FAILED) {
            memset(memory, 0x5a, page);
            munmap(memory + page, page);
            request.addr = (unsigned long)(memory + page - 8); request.size = 16;
            memset(request.buf, 0xcc, sizeof(request.buf));
            check(ioctl(fd, IOCTL_MEM_READ, &request) == 0 && request.size == 8 &&
                request.buf[0] == 0x5a && request.buf[8] == 0 && request.buf[4095] == 0,
                "short read length and deterministic tail");
            request.size = 16;
            check(ioctl(fd, IOCTL_MEM_WRITE, &request) == -1 && errno == EIO,
                "short write reports failure");
            munmap(memory, page);
        } else check(false, "allocate partial memory range");

        pid_t child = fork();
        if (child == 0) {
            if (setgroups(0, nullptr) || setgid(1000) || setuid(1000)) _exit(2);
            const unsigned commands[] = {IOCTL_MEM_READ, IOCTL_MEM_WRITE, IOCTL_FIND_PID,
                IOCTL_GET_BASE_ADDR, IOCTL_SIG_SCAN_PATCH};
            for (unsigned command : commands)
                if (ioctl(fd, command, nullptr) != -1 || errno != EPERM) _exit(3);
            mouse_request move{0, 0};
            _exit(ioctl(fd, IOCTL_MOUSE_MOVE, &move) == 0 ? 0 : 4);
        }
        int status = 0;
        if (child > 0) waitpid(child, &status, 0);
        check(child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "inherited descriptor denies unprivileged memory ioctls");
        check(syscall(SYS_delete_module, "memrw", O_NONBLOCK) == -1 &&
            (errno == EWOULDBLOCK || errno == EBUSY), "open descriptor prevents unload");
        if (cycle == 0) hotplug(fd);
        close(fd);
        check(syscall(SYS_delete_module, "memrw", O_NONBLOCK) == 0, "ordinary module unload");
        check(!file_contains("/proc/modules", "memrw "), "module removed");
        if (yamaPresent)
            check(file_contains("/proc/sys/kernel/yama/ptrace_scope", "1"), "Yama preserved at unload");
    }
    std::printf("MEMRW_VM_RESULT failures=%d\n", failures);
    std::fflush(stdout);
    reboot(RB_POWER_OFF);
    return 1;
}
