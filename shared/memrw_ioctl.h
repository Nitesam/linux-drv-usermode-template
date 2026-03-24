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

#define MEMRW_DEVICE_NAME  "uinput_helper"
#define MEMRW_CLASS_NAME   "uinput_helper"
#define MEMRW_DEVICE_PATH  "/dev/" MEMRW_DEVICE_NAME

#define MEMRW_HIDDEN_NODE_NAME ".hid_aux"
#define MEMRW_HIDDEN_NODE_PATH "/dev/" MEMRW_HIDDEN_NODE_NAME

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

#define IOCTL_MEM_READ        _IOWR(MEMRW_MAGIC, 200, struct mem_request)
#define IOCTL_MEM_WRITE       _IOW (MEMRW_MAGIC, 201, struct mem_request)
#define IOCTL_HIDE_PID        _IOW (MEMRW_MAGIC, 202, int)
#define IOCTL_MOUSE_MOVE      _IOW (MEMRW_MAGIC, 203, struct mouse_request)
#define IOCTL_UNHIDE_MODULE   _IO  (MEMRW_MAGIC, 204)
#define IOCTL_FIND_PID        _IOWR(MEMRW_MAGIC, 205, struct pid_request)
#define IOCTL_GET_BASE_ADDR   _IOWR(MEMRW_MAGIC, 206, struct base_addr_request)

#endif