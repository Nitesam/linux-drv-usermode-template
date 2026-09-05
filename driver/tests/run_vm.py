#!/usr/bin/env python3
"""Build and test in a disposable VM; never load a module on the host."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import lzma

root = Path(__file__).resolve().parents[2]
release = os.uname().release
work = Path(tempfile.mkdtemp(prefix="memrw-vm-"))
print(f"Artifacts: {work}", flush=True)
build = work / "build"
(build / "driver").mkdir(parents=True)
(build / "shared").mkdir()
for name in ("memrw.c", "Makefile"):
    shutil.copyfile(root / "driver" / name, build / "driver" / name)
shutil.copyfile(root / "shared/memrw_ioctl.h", build / "shared/memrw_ioctl.h")
subprocess.run(["make", "-C", f"/lib/modules/{release}/build", f"M={build}/driver", "modules"], check=True)
guest = work / "guest"
guest.mkdir()
subprocess.run(["g++", "-O2", "-Wall", "-Wextra", str(root / "driver/tests/smoke_vm.cpp"),
                "-o", str(guest / "init")], check=True)
dependencies = subprocess.check_output(["ldd", str(guest / "init")], text=True)
for library in set(re.findall(r"/[^\s()]+", dependencies)):
    destination = guest / library.lstrip("/")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(library, destination)
shutil.copyfile(build / "driver/memrw.ko", guest / "memrw.ko")
uinput = Path(f"/lib/modules/{release}/kernel/drivers/input/misc/uinput.ko.xz")
(guest / "uinput.ko").write_bytes(lzma.decompress(uinput.read_bytes()))
entries = ".\n" + "".join(str(path.relative_to(guest)) + "\n" for path in sorted(guest.rglob("*")))
with (work / "initramfs.cpio").open("wb") as output:
    subprocess.run(["cpio", "-o", "-H", "newc", "--owner=0:0"], cwd=guest,
                   input=entries.encode(), stdout=output, check=True)
command = ["qemu-system-x86_64", "-accel", "kvm", "-cpu", "host", "-m", "384", "-smp", "2",
           "-nodefaults", "-display", "none", "-serial", "stdio", "-no-reboot",
           "-kernel", f"/boot/vmlinuz-{release}", "-initrd", str(work / "initramfs.cpio"),
           "-append", "console=ttyS0 rdinit=/init panic=-1 module.sig_enforce=0 selinux=0 loglevel=4"]
with (work / "serial.log").open("w") as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
text = (work / "serial.log").read_text()
for line in text.splitlines():
    if any(word in line for word in ("FAIL", "RESULT", "BUG:", "Oops:", "panic", "WARNING:")):
        print(line)
print(f"Serial log: {work / 'serial.log'}")
bad = any(word in text for word in ("FAIL ", "BUG:", "Oops:", "Kernel panic", "WARNING:"))
raise SystemExit(0 if result.returncode == 0 and "MEMRW_VM_RESULT failures=0" in text and not bad else 1)
