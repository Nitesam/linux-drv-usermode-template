# linux-drv-usermode-template


## Overview

Linux kernel module + userspace GUI for cross-process memory reading and virtual input injection. Designed to be invisible to ring-3 anti-cheat software.

## Architecture

```
┌─────────────────────────────────────────────────┐
│                 Userspace App                    │
│              (usermode/main.cpp)                 │
│                                                  │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ MemClient│  │ ImGui UI │  │ PID Discovery │  │
│  │ (IOCTL)  │  │ (GL/GLFW)│  │ (/proc scan)  │  │
│  └────┬─────┘  └──────────┘  └───────────────┘  │
│       │                                          │
│       │ ioctl() via ephemeral /tmp/ device node  │
└───────┼──────────────────────────────────────────┘
        │
════════╪══════════════════════════════════════════════
        │           kernel boundary
════════╪══════════════════════════════════════════════
        │
┌───────┴──────────────────────────────────────────┐
│              Kernel Module (memrw.ko)             │
│                                                   │
│  ┌────────────┐  ┌──────────┐  ┌──────────────┐  │
│  │ Memory R/W │  │ Syscall  │  │ Virtual      │  │
│  │ (access_   │  │ Hooks    │  │ Mouse        │  │
│  │ process_vm)│  │ (ftrace) │  │ (input_dev)  │  │
│  └────────────┘  └──────────┘  └──────────────┘  │
│                                                   │
│  ┌────────────┐  ┌──────────┐  ┌──────────────┐  │
│  │ Module     │  │ Yama     │  │ PID Hiding   │  │
│  │ Self-Hide  │  │ Bypass   │  │ (getdents64) │  │
│  └────────────┘  └──────────┘  └──────────────┘  │
└───────────────────────────────────────────────────┘
```

## Data Flow

### 1. Setup (setup.sh)

```
install deps → clone imgui → check MOK key → build driver → build usermode app → sign .ko → load module
```

- MOK key generated at `/root/NTS_WORK/mok/` for Secure Boot signing
- Module signed with `sign-file` using SHA256
- Module loaded with `insmod`

### 2. Module Initialization (memrw_init)

```
resolve kallsyms → disable yama → alloc chrdev → install getdents64 hook → install stat hook → schedule vmouse → hide module
```

Key points:
- Uses kprobe trick to resolve `kallsyms_lookup_name` (not exported since 5.7)
- No `class_create()` or `device_create()` — avoids /sys/class and /dev leaks
- Device registered only via `cdev_add()`, discoverable through /proc/devices
- Module hides itself from `lsmod`, `/proc/modules`, and `/sys/module`
- Virtual mouse created with 3s delay to break timing correlation

### 3. Userspace Startup (main.cpp)

```
mask argv[0] → mask comm → open driver → hide PID → find target → start GUI
```

- Process disguises as `gsd-housekeeping` (GNOME settings daemon)
- Driver discovery: reads `/proc/devices` → finds major number → `mknod` in `/tmp/` → opens fd → `unlink` immediately
- Target process found via `/proc/<pid>/cmdline` scan with case-insensitive basename matching (supports Proton/Wine paths)

### 4. Memory Read Operation

```
UI "Load" click
    → MemClient::read_mem()
        → ioctl(IOCTL_MEM_READ)
            → kernel: find_get_pid → pid_task → access_process_vm(FOLL_FORCE)
                → copy_to_user
    → hex view display
```

### 5. Stealth Mechanisms

| Layer | Technique | What it hides |
|-------|-----------|---------------|
| Kernel | `list_del(THIS_MODULE->list)` | /proc/modules, lsmod |
| Kernel | `kobject_del(mkobj.kobj)` | /sys/module/ |
| Kernel | getdents64 ftrace hook | directory listings (PID, device name) |
| Kernel | newfstatat ftrace hook | stat() on device/sysfs paths |
| Kernel | No class_create/device_create | /sys/class, /dev, udev |
| Kernel | Delayed vmouse registration | timing correlation |
| Kernel | Yama ptrace_scope = 0 | ptrace restrictions |
| Userspace | argv[0] overwrite | /proc/pid/cmdline |
| Userspace | prctl(PR_SET_NAME) | /proc/pid/comm, /proc/pid/status |
| Userspace | Ephemeral mknod + unlink | persistent /dev/ node |
| Userspace | IOCTL_HIDE_PID | /proc/ directory listing |

### 6. Virtual Mouse

Registered as `Logitech USB Optical Mouse` (VID `046d`, PID `c077`) on a plausible USB phys path. Supports `REL_X`, `REL_Y`, `BTN_LEFT`, `BTN_RIGHT`, `BTN_MIDDLE`.

### 7. Unload (unload.sh)

```
ioctl(UNHIDE_MODULE) → rmmod memrw → cleanup hooks → destroy vmouse → unregister cdev
```

Module must be unhidden before rmmod can find it.

## File Structure

```
├── .gitignore
├── FLOW.md              ← this file
├── README.md
├── setup.sh             ← full build + sign + load pipeline
├── unload.sh            ← safe module unload
├── shared/
│   └── memrw_ioctl.h    ← shared IOCTL definitions and structs
├── driver/
│   ├── Makefile
│   └── memrw.c          ← kernel module source
└── usermode/
    ├── CMakeLists.txt
    ├── main.cpp          ← ImGui app (read-only memory viewer)
    ├── mem_client.h      ← IOCTL client wrapper class
    └── imgui/            ← Dear ImGui (cloned by setup.sh)
```

## IOCTLs (magic 'U', base 200)

| Code | Direction | Description |
|------|-----------|-------------|
| 200 | IOWR | MEM_READ — read target process memory |
| 201 | IOW | MEM_WRITE — write target process memory |
| 202 | IOW | HIDE_PID — hide a PID from /proc listings |
| 203 | IOW | MOUSE_MOVE — inject relative mouse movement |
| 204 | IO | UNHIDE_MODULE — re-expose module for rmmod |

## Requirements

- Fedora with kernel 6.x (x86_64)
- Secure Boot: MOK key enrolled via `mokutil`
- Packages: kernel-devel, gcc, cmake, glfw-devel, mesa-libGL-devel, openssl
