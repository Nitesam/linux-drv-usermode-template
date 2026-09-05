#ifndef MEMRW_IOCTL_H
#define MEMRW_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <cstddef>
#include <cstdint>
#include <linux/types.h>
#endif

#define MEMRW_MAGIC 'U'
#define MEMRW_BUF_SIZE 4096
#define MEMRW_SIG_MAX 256
#define MEMRW_PATCH_MAX 256

#define MEMRW_DEVICE_NAME  "uinput_helper"
#define MEMRW_CLASS_NAME   "uinput_helper"
#define MEMRW_DEVICE_PATH  "/dev/" MEMRW_DEVICE_NAME

#define MEMRW_HIDDEN_NODE_NAME MEMRW_DEVICE_NAME
#define MEMRW_HIDDEN_NODE_PATH MEMRW_DEVICE_PATH

struct mem_request {
    int    pid;
    unsigned long addr;
    size_t size;
    unsigned char buf[MEMRW_BUF_SIZE];
};

struct mouse_request {
    int dx;
    int dy;
};

/*
 * Snapshot atomico dei pulsanti osservati dall'input handler del kernel.
 * I bit sono condivisi con makcu::MouseButton nel client Linux:
 * 0=left, 1=right, 2=middle, 3=side/back, 4=extra/forward (Mouse5).
 */
struct mouse_buttons_response {
    __u32 mask;
};

struct pid_request {
    char name[256];
    int  pid;
};

struct base_addr_request {
    int      pid;
    char     name[256];
#ifdef __KERNEL__
    unsigned long addr;
#else
    uint64_t addr;
#endif
};

struct sig_patch_request {
    int           pid;
    unsigned long start_addr;
    size_t        scan_size;
    size_t        pattern_size;
    size_t        patch_size;
    size_t        patch_offset;
    unsigned long found_addr;
    int           flags;
    int           avx2_used;
    unsigned char pattern[MEMRW_SIG_MAX];
    unsigned char mask[MEMRW_SIG_MAX];
    unsigned char patch[MEMRW_PATCH_MAX];
};

#define MEMRW_SIG_FLAG_AVX2 0x1

#define IOCTL_MEM_READ        _IOWR(MEMRW_MAGIC, 200, struct mem_request)
#define IOCTL_MEM_WRITE       _IOW (MEMRW_MAGIC, 201, struct mem_request)
// Retired operations: the driver returns -EOPNOTSUPP.
#define IOCTL_HIDE_PID        _IOW (MEMRW_MAGIC, 202, int)
#define IOCTL_MOUSE_MOVE      _IOW (MEMRW_MAGIC, 203, struct mouse_request)
#define IOCTL_UNHIDE_MODULE   _IO  (MEMRW_MAGIC, 204)
#define IOCTL_FIND_PID        _IOWR(MEMRW_MAGIC, 205, struct pid_request)
#define IOCTL_GET_BASE_ADDR   _IOWR(MEMRW_MAGIC, 206, struct base_addr_request)
#define IOCTL_SIG_SCAN_PATCH  _IOWR(MEMRW_MAGIC, 207, struct sig_patch_request)
#define IOCTL_MOUSE_BUTTONS   _IOR (MEMRW_MAGIC, 208, struct mouse_buttons_response)

#endif
