# linux-drv-usermode-template

Kernel module and userspace app for privileged cross-process memory access on
Linux, with a visible virtual pointer and modular game support.

## Structure

```
driver/
  memrw.c              kernel module (privileged memory r/w, visible virtual pointer)
  Makefile

shared/
  memrw_ioctl.h        IOCTL definitions shared between driver and usermode

usermode/
  main.cpp             generic app shell (GLFW/ImGui, driver communication)
  mem_client.h         MemClient class wrapping driver IOCTLs
  game_interface.h     abstract GameModule interface
  logger.h             debug logger with separate console window
  games/
    hll/               Hell Let Loose module
      hll_module.h     GameModule implementation (state, UI, rendering)
      hll_reader.h     memory reader (pointer chains, player data)
      hll_offsets.h    game offsets and structs

setup.sh               build + sign + load everything
unload.sh              normal rmmod (clients must close the device first)
```

## Adding a Game

1. Create `usermode/games/<name>/` with your offsets, reader, and module header
2. Implement `GameModule` from `game_interface.h`
3. Change two lines in `main.cpp`:

```cpp
#include "games/<name>/<name>_module.h"
static std::unique_ptr<GameModule> create_game() { return std::make_unique<YourModule>(); }
```

## Controls

- **INS** — toggle UI visibility

## Setup

Requires Fedora (x86_64) and development headers matching the running kernel.
Secure Boot installations also require an enrolled signing key.

```bash
sudo ./setup.sh    # builds driver + app, signs .ko, loads module
sudo ./unload.sh   # unload after closing clients
```

First run generates a MOK key and prompts for enrollment — reboot to complete, then re-run setup.

## IOCTLs

| # | Name | Description |
|---|------|-------------|
| 200 | MEM_READ | read target process memory |
| 201 | MEM_WRITE | write target process memory |
| 202 | HIDE_PID | retired: returns EOPNOTSUPP |
| 203 | MOUSE_MOVE | send movement through the visible virtual pointer |
| 204 | UNHIDE_MODULE | retired: returns EOPNOTSUPP |
| 205 | FIND_PID | find PID by process name |
| 206 | GET_BASE_ADDR | get module base address |

## Driver security and compatibility

The module and `/dev/uinput_helper` are visible. The driver no longer hooks
filesystem syscalls, modifies the module list, changes Yama, or impersonates
physical mouse hardware. Movement uses the `memrw virtual pointer` input device.
Rebuild clients together with the driver: the legacy source macro
`MEMRW_HIDDEN_NODE_PATH` now names `/dev/uinput_helper`.

Memory read/write, process discovery and signature patching require
`CAP_SYS_PTRACE` in the initial user namespace on every ioctl, including when a
file descriptor was inherited or passed from a privileged process. Mouse ioctls
follow the device permissions. `create_device.sh` creates mode 0600, owned by
`MEMRW_DEVICE_OWNER`, defaulting to `SUDO_USER` or root. No script clears logs.
The Fedora userspace memory backend still follows the ordinary Linux ptrace/LSM
access rules; loading this module does not relax them.

Close clients before `unload.sh`. The script does not kill processes or attempt
to repair a legacy hidden module's list pointers. If an old hidden version is
still loaded, reboot before loading this version. Build/reload scripts must not
be used while an old hidden instance is active.

## Isolated driver verification

Run `python3 driver/tests/run_vm.py` as an ordinary user with KVM access. It
builds a copy of the driver in a temporary directory and boots a disposable
guest using the installed kernel. It never loads a module on the host and
attaches no host disk or network device. Dependencies: matching kernel-devel,
g++, cpio, QEMU/KVM and the installed uinput module in `.ko.xz` format.

The guest checks repeated load/unload, input enumeration, hot-unplug during
movement, short reads/writes, retired hiding ioctls, module reference protection,
unchanged Yama policy and permission checks on an inherited descriptor. The
runner retains its build and serial log under `/tmp/memrw-vm-*` for review.
