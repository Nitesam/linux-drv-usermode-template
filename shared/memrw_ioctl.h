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

#define IOCTL_MEM_READ        _IOWR(MEMRW_MAGIC, 200, struct mem_request)
#define IOCTL_MEM_WRITE       _IOW (MEMRW_MAGIC, 201, struct mem_request)
#define IOCTL_HIDE_PID        _IOW (MEMRW_MAGIC, 202, int)
#define IOCTL_MOUSE_MOVE      _IOW (MEMRW_MAGIC, 203, struct mouse_request)
#define IOCTL_UNHIDE_MODULE   _IO  (MEMRW_MAGIC, 204)

#endif