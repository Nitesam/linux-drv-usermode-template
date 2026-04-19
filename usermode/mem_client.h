#ifndef MEM_CLIENT_H
#define MEM_CLIENT_H

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <errno.h>

#include "memrw_ioctl.h"

class MemClient {
public:
    MemClient() : fd_(-1) {}
    ~MemClient() { close_driver(); }

    bool open_driver() {
        fd_ = open(MEMRW_HIDDEN_NODE_PATH, O_RDWR);
        if (fd_ >= 0) {
            last_error_.clear();
            return true;
        }

        last_error_ = std::string("Cannot open ") + MEMRW_HIDDEN_NODE_PATH
                    + ": " + strerror(errno);
        return false;
    }

    void close_driver() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool is_open() const { return fd_ >= 0; }

    bool read_mem(int pid, unsigned long addr, size_t size, unsigned char *out_buf) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }
        if (size == 0 || size > MEMRW_BUF_SIZE) {
            last_error_ = "Invalid size (must be 1-4096)";
            return false;
        }

        struct mem_request req;
        memset(&req, 0, sizeof(req));
        req.pid  = pid;
        req.addr = addr;
        req.size = size;

        int ret = ioctl(fd_, IOCTL_MEM_READ, &req);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MEM_READ failed: ") + strerror(errno);
            return false;
        }

        memcpy(out_buf, req.buf, size);
        last_error_.clear();
        return true;
    }

    bool write_mem(int pid, unsigned long addr, size_t size, const unsigned char *in_buf) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }
        if (size == 0 || size > MEMRW_BUF_SIZE) {
            last_error_ = "Invalid size (must be 1-4096)";
            return false;
        }

        struct mem_request req;
        memset(&req, 0, sizeof(req));
        req.pid  = pid;
        req.addr = addr;
        req.size = size;
        memcpy(req.buf, in_buf, size);

        int ret = ioctl(fd_, IOCTL_MEM_WRITE, &req);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MEM_WRITE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool hide_self() {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        int pid = getpid();
        int ret = ioctl(fd_, IOCTL_HIDE_PID, &pid);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_HIDE_PID failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool move_mouse(int dx, int dy) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        struct mouse_request mreq;
        mreq.dx = dx;
        mreq.dy = dy;

        int ret = ioctl(fd_, IOCTL_MOUSE_MOVE, &mreq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_MOUSE_MOVE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    bool unhide_module() {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return false;
        }

        int ret = ioctl(fd_, IOCTL_UNHIDE_MODULE);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_UNHIDE_MODULE failed: ") + strerror(errno);
            return false;
        }

        last_error_.clear();
        return true;
    }

    int find_pid(const char *name) {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return -1;
        }

        struct pid_request preq;
        memset(&preq, 0, sizeof(preq));
        strncpy(preq.name, name, sizeof(preq.name) - 1);

        int ret = ioctl(fd_, IOCTL_FIND_PID, &preq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_FIND_PID failed: ") + strerror(errno);
            return -1;
        }

        last_error_.clear();
        return preq.pid;
    }

    uint64_t get_base_address(int pid, const char *name = "") {
        if (fd_ < 0) {
            last_error_ = "Driver not open";
            return 0;
        }

        struct base_addr_request breq;
        memset(&breq, 0, sizeof(breq));
        breq.pid = pid;
        strncpy(breq.name, name, sizeof(breq.name) - 1);

        int ret = ioctl(fd_, IOCTL_GET_BASE_ADDR, &breq);
        if (ret < 0) {
            last_error_ = std::string("IOCTL_GET_BASE_ADDR failed: ") + strerror(errno);
            return 0;
        }

        last_error_.clear();
        return breq.addr;
    }

    const std::string &last_error() const { return last_error_; }

private:
    int         fd_;
    std::string last_error_;
};

#endif
