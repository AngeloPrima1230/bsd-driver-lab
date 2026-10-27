# bsd-driver-lab

Small FreeBSD kernel drivers written in C, plus a userland test program.

| Directory | What it is |
|---|---|
| [`echodev/`](echodev) | A character device (`/dev/echodev`) with read, write and ioctl. Uses `sx(9)` locking, `uiomove(9)` and safe unload. |
| [`usbdemo/`](usbdemo) | A USB host driver skeleton: newbus probe/attach/detach, an interrupt-IN transfer state machine, stall recovery, and sysctl counters. |
| [`test/`](test) | A userland test suite for `echodev`, covering ioctls, offsets and error paths. |
| [`docs/linux-to-freebsd.md`](docs/linux-to-freebsd.md) | API mapping and checklist for porting Linux drivers. |

Target: **FreeBSD 14.x** (amd64). You need kernel sources in `/usr/src`.

## Build

```sh
# kernel sources, if missing
fetch https://download.freebsd.org/releases/amd64/$(freebsd-version -u | cut -d- -f1-2)/src.txz
tar -C / -xf src.txz

make            # builds both modules and the test
```

## Run

```sh
# char device
sudo kldload ./echodev/echodev.ko
sudo ./test/echotest
echo "hi" | sudo tee /dev/echodev && sudo cat /dev/echodev
sudo kldunload echodev

# USB driver (attaches to VID 0x16c0 / PID 0x05dc)
sudo kldload ./usbdemo/usbdemo.ko
dmesg | tail
sysctl dev.usbdemo.0
```

Expected test output:

```
ok   - write stores full message
ok   - GETLEN matches written size
ok   - read returns same data
ok   - read honours offset
ok   - CLEAR empties buffer
ok   - oversized write rejected with EFBIG
ok   - SETCAP grows buffer
ok   - write fits after resize
ok   - SETCAP rejects 0
PASSED (0 failures)
```

## Design notes

- **echodev** uses an `sx` lock instead of a mutex because `uiomove()` can
  page-fault and sleep. Sleeping while holding a non-sleepable mutex causes a
  `WITNESS` panic. `SETCAP` allocates with `M_WAITOK` *before* it takes the lock.
- **echodev unload**: `destroy_dev()` waits for every thread to leave the cdevsw
  methods before the softc is freed, so a concurrent read cannot touch freed memory.
- **usbdemo** follows the in-tree pattern used by drivers such as `uled(4)` and
  `ukbd(4)`. The callback works as a state machine. `USB_ERR_CANCELLED` signals
  detach. Any other error clears the stall and resubmits. `usbd_transfer_unsetup()`
  drains the transfers, which makes detach safe even after a partial attach.

## License

BSD-2-Clause
