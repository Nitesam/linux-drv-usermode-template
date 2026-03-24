# linux-drv-usermode-template

Kernel module + usermode app for cross-process memory access on Linux. Invisible to ring-3 anti-cheat. Modular game support — swap a header to target a different game.

## Structure

```
driver/
  memrw.c              kernel module (memory r/w, syscall hooks, stealth)
  Makefile

shared/
  memrw_ioctl.h        IOCTL definitions shared between driver and usermode

usermode/
  main.cpp             generic app shell (GLFW/ImGui, driver comms, stealth)
  mem_client.h         MemClient class wrapping driver IOCTLs
  game_interface.h     abstract GameModule interface
  logger.h             debug logger with separate console window
  games/
    hll/               Hell Let Loose module
      hll_module.h     GameModule implementation (state, UI, rendering)
      hll_reader.h     memory reader (pointer chains, player data)
      hll_offsets.h    game offsets and structs

setup.sh               build + sign + load everything
unload.sh              unhide module + rmmod
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

Requires Fedora (x86_64), kernel 6.x, Secure Boot with MOK signing.

```bash
sudo ./setup.sh    # builds driver + app, signs .ko, loads module
./unload.sh         # unload when done
```

First run generates a MOK key and prompts for enrollment — reboot to complete, then re-run setup.

## IOCTLs

| # | Name | Description |
|---|------|-------------|
| 200 | MEM_READ | read target process memory |
| 201 | MEM_WRITE | write target process memory |
| 202 | HIDE_PID | hide PID from /proc |
| 203 | MOUSE_MOVE | inject mouse input (disabled) |
| 204 | UNHIDE_MODULE | re-expose module for rmmod |
| 205 | FIND_PID | find PID by process name |
| 206 | GET_BASE_ADDR | get module base address |

## Stealth

- Module hides from lsmod, /proc/modules
- Syscall hooks (getdents64, newfstatat) hide device nodes and PIDs
- No /sys/class or /dev entries created by driver
- Usermode does zero /proc access — driver handles all process discovery
- Process masquerades as gsd-housekeeping, X11 window spoofed
- Device node created as hidden dot-file, permissions set at mknod time
# linux-drv-usermode-template
