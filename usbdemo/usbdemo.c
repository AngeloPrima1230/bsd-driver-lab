/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * usbdemo - skeleton USB host driver for FreeBSD 14+.
 *
 * Matches a VID/PID, opens the first interrupt-IN endpoint of interface 0,
 * and continuously reads packets, logging them and counting them in a
 * sysctl (dev.usbdemo.N.packets).
 *
 * Demonstrates: newbus probe/attach/detach, usb_config transfer setup,
 * the USB_ST_SETUP / USB_ST_TRANSFERRED / error state machine, stall
 * recovery, and per-device sysctl nodes.  This is the same structure used
 * by in-tree drivers such as uled(4) and ukbd(4).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USBDEMO_MAX_PKT	64

enum {
	USBDEMO_INTR_RD,
	USBDEMO_N_TRANSFER,
};

struct usbdemo_softc {
	device_t		 sc_dev;
	struct usb_device	*sc_udev;
	struct mtx		 sc_mtx;
	struct usb_xfer		*sc_xfer[USBDEMO_N_TRANSFER];
	uint64_t		 sc_packets;
	uint64_t		 sc_errors;
};

/*
 * 0x16c0:0x05dc is the shared V-USB test ID, handy for AVR/Arduino-based
 * test devices.  Add real device IDs here.
 */
static const STRUCT_USB_HOST_ID usbdemo_devs[] = {
	{ USB_VPI(0x16c0, 0x05dc, 0) },
};

static device_probe_t	usbdemo_probe;
static device_attach_t	usbdemo_attach;
static device_detach_t	usbdemo_detach;
static usb_callback_t	usbdemo_intr_callback;

static const struct usb_config usbdemo_config[USBDEMO_N_TRANSFER] = {
	[USBDEMO_INTR_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = { .pipe_bof = 1, .short_xfer_ok = 1 },
		.bufsize = 0,		/* use wMaxPacketSize */
		.callback = &usbdemo_intr_callback,
	},
};

static void
usbdemo_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usbdemo_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[USBDEMO_MAX_PKT];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen > (int)sizeof(buf))
			actlen = sizeof(buf);
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, actlen);
		sc->sc_packets++;
		if (actlen > 0 && bootverbose)
			device_printf(sc->sc_dev, "rx %d bytes, [0]=0x%02x\n",
			    actlen, buf[0]);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:			/* error */
		if (error == USB_ERR_CANCELLED)
			break;		/* detaching */
		sc->sc_errors++;
		device_printf(sc->sc_dev, "transfer error: %s\n",
		    usbd_errstr(error));
		usbd_xfer_set_stall(xfer);
		goto tr_setup;
	}
}

static int
usbdemo_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0 || uaa->info.bIfaceIndex != 0)
		return (ENXIO);
	return (usbd_lookup_id_by_uaa(usbdemo_devs, sizeof(usbdemo_devs), uaa));
}

static int
usbdemo_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usbdemo_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	uint8_t iface_index = uaa->info.bIfaceIndex;
	usb_error_t uerr;

	device_set_usb_desc(dev);
	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	mtx_init(&sc->sc_mtx, "usbdemo", NULL, MTX_DEF);

	uerr = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    usbdemo_config, USBDEMO_N_TRANSFER, sc, &sc->sc_mtx);
	if (uerr != USB_ERR_NORMAL_COMPLETION) {
		device_printf(dev, "transfer setup failed: %s\n",
		    usbd_errstr(uerr));
		usbdemo_detach(dev);
		return (ENXIO);
	}

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "packets",
	    CTLFLAG_RD, &sc->sc_packets, 0, "Interrupt packets received");
	SYSCTL_ADD_U64(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "errors",
	    CTLFLAG_RD, &sc->sc_errors, 0, "Transfer errors");

	mtx_lock(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_xfer[USBDEMO_INTR_RD]);
	mtx_unlock(&sc->sc_mtx);

	device_printf(dev, "attached, max packet %u bytes\n",
	    usbd_xfer_max_len(sc->sc_xfer[USBDEMO_INTR_RD]));
	return (0);
}

static int
usbdemo_detach(device_t dev)
{
	struct usbdemo_softc *sc = device_get_softc(dev);

	/* Cancels and drains outstanding transfers; safe on partial attach. */
	usbd_transfer_unsetup(sc->sc_xfer, USBDEMO_N_TRANSFER);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static device_method_t usbdemo_methods[] = {
	DEVMETHOD(device_probe,		usbdemo_probe),
	DEVMETHOD(device_attach,	usbdemo_attach),
	DEVMETHOD(device_detach,	usbdemo_detach),
	DEVMETHOD_END
};

static driver_t usbdemo_driver = {
	.name = "usbdemo",
	.methods = usbdemo_methods,
	.size = sizeof(struct usbdemo_softc),
};

DRIVER_MODULE(usbdemo, uhub, usbdemo_driver, NULL, NULL);
MODULE_DEPEND(usbdemo, usb, 1, 1, 1);
MODULE_VERSION(usbdemo, 1);
USB_PNP_HOST_INFO(usbdemo_devs);
