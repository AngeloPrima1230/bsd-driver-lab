# Linux → FreeBSD driver porting cheat sheet

Notes I use when porting a Linux driver. There are two options: use **LinuxKPI**
(`compat/linuxkpi`, which is how drm-kmod and `iwlwifi` are brought over), or do a
**native port**. Use LinuxKPI when you want to keep in sync with upstream. Do a
native port when the driver is small or performance-critical.

| Area | Linux | FreeBSD |
|---|---|---|
| Module entry | `module_init` / `module_exit` | `DECLARE_MODULE`, `DEV_MODULE`, `DRIVER_MODULE` |
| Device model | `struct device`, `probe`/`remove` | newbus: `device_probe/attach/detach`, `device_get_softc()` |
| Char device | `cdev_add`, `file_operations` | `make_dev_s(9)`, `struct cdevsw` |
| Copy to/from user | `copy_to_user` / `copy_from_user` | `uiomove(9)`, `copyin(9)` / `copyout(9)` |
| Memory | `kmalloc(GFP_KERNEL/ATOMIC)` | `malloc(9)` with `M_WAITOK` / `M_NOWAIT` |
| DMA | `dma_alloc_coherent`, `dma_map_single` | `bus_dma(9)`: tag → map → load |
| Registers | `ioremap`, `readl`/`writel` | `bus_alloc_resource`, `bus_read_4`/`bus_write_4` |
| IRQ | `request_irq` | `bus_setup_intr` (filter + ithread) |
| Spinlock | `spinlock_t` | `mtx(9)` `MTX_SPIN`, or `MTX_DEF` for sleepable contexts |
| Sleepable lock | `struct mutex`, `rw_semaphore` | `sx(9)` |
| Deferred work | workqueue, tasklet | `taskqueue(9)`, `callout(9)` |
| Wait/wake | `wait_event`, `wake_up` | `msleep(9)`/`wakeup`, `cv(9)` |
| Networking buffer | `sk_buff` | `mbuf(9)` |
| Net device | `net_device` | `ifnet` (`if_t`, iflib for NICs) |
| WiFi stack | `mac80211` / `cfg80211` | `net80211(4)` (`ieee80211com`, `ieee80211vap`) |
| USB core | `usb_driver`, URBs, `usb_submit_urb` | `usb(4)`: `usb_config`, `usbd_transfer_setup/start`, callback state machine |
| Firmware | `request_firmware` | `firmware(9)` (`firmware_get`) |
| Config knobs | module params, sysfs | `sysctl(9)`, `TUNABLE_INT`, `device.hints` |
| Debug | `printk`, `dev_dbg`, ftrace | `device_printf`, `bootverbose`, DTrace, `kgdb` on crash dumps |

## Porting checklist

1. Identify the hardware: the PCI/USB IDs and the firmware blobs it needs. Check
   whether FreeBSD already has a driver for it (`man -k`, `sys/dev/`).
2. Pick LinuxKPI or a native port.
3. Map the locking first. Check which contexts each Linux lock is taken in.
4. Port the bus glue (probe/attach/detach), then the data path, then the
   control path (ioctl/sysctl/net80211 callbacks).
5. Test load/unload in a loop, hot-unplug, and run with `INVARIANTS` and
   `WITNESS` kernels to catch lock-order and assert failures.
