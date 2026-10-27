/*-
 * SPDX-License-Identifier: BSD-2-Clause
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

enum {
	UDEMO_INTR_RD,
	UDEMO_NXFER,
};

struct udemo_softc {
	device_t		 sc_dev;
	struct usb_device	*sc_udev;
	struct mtx		 sc_mtx;
	struct usb_xfer		*sc_xfer[UDEMO_NXFER];
	uint64_t		 sc_pkts;
	uint64_t		 sc_errs;
};

static const STRUCT_USB_HOST_ID udemo_devs[] = {
	{ USB_VPI(0x16c0, 0x05dc, 0) },
};

static device_probe_t	udemo_probe;
static device_attach_t	udemo_attach;
static device_detach_t	udemo_detach;
static usb_callback_t	udemo_intr_cb;

static const struct usb_config udemo_cfg[UDEMO_NXFER] = {
	[UDEMO_INTR_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = { .pipe_bof = 1, .short_xfer_ok = 1 },
		.bufsize = 0,
		.callback = &udemo_intr_cb,
	},
};

static void
udemo_intr_cb(struct usb_xfer *xfer, usb_error_t error)
{
	struct udemo_softc *sc = usbd_xfer_softc(xfer);
	uint8_t buf[64];
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		len = MIN(len, (int)sizeof(buf));
		usbd_copy_out(usbd_xfer_get_frame(xfer, 0), 0, buf, len);
		sc->sc_pkts++;
		if (bootverbose && len > 0)
			device_printf(sc->sc_dev, "rx %d bytes, [0]=0x%02x\n",
			    len, buf[0]);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:
		if (error == USB_ERR_CANCELLED)
			break;
		sc->sc_errs++;
		device_printf(sc->sc_dev, "xfer error: %s\n",
		    usbd_errstr(error));
		usbd_xfer_set_stall(xfer);
		goto tr_setup;
	}
}

static int
udemo_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0 || uaa->info.bIfaceIndex != 0)
		return (ENXIO);
	return (usbd_lookup_id_by_uaa(udemo_devs, sizeof(udemo_devs), uaa));
}

static int
udemo_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct udemo_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *kids;
	uint8_t iface = uaa->info.bIfaceIndex;
	usb_error_t err;

	device_set_usb_desc(dev);
	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;
	mtx_init(&sc->sc_mtx, "udemo", NULL, MTX_DEF);

	err = usbd_transfer_setup(uaa->device, &iface, sc->sc_xfer, udemo_cfg,
	    UDEMO_NXFER, sc, &sc->sc_mtx);
	if (err) {
		device_printf(dev, "usbd_transfer_setup: %s\n",
		    usbd_errstr(err));
		udemo_detach(dev);
		return (ENXIO);
	}

	ctx = device_get_sysctl_ctx(dev);
	kids = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "packets", CTLFLAG_RD,
	    &sc->sc_pkts, 0, "packets received");
	SYSCTL_ADD_U64(ctx, kids, OID_AUTO, "errors", CTLFLAG_RD,
	    &sc->sc_errs, 0, "transfer errors");

	mtx_lock(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_xfer[UDEMO_INTR_RD]);
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static int
udemo_detach(device_t dev)
{
	struct udemo_softc *sc = device_get_softc(dev);

	usbd_transfer_unsetup(sc->sc_xfer, UDEMO_NXFER);
	mtx_destroy(&sc->sc_mtx);
	return (0);
}

static device_method_t udemo_methods[] = {
	DEVMETHOD(device_probe,		udemo_probe),
	DEVMETHOD(device_attach,	udemo_attach),
	DEVMETHOD(device_detach,	udemo_detach),
	DEVMETHOD_END
};

static driver_t udemo_driver = {
	.name = "usbdemo",
	.methods = udemo_methods,
	.size = sizeof(struct udemo_softc),
};

DRIVER_MODULE(usbdemo, uhub, udemo_driver, NULL, NULL);
MODULE_DEPEND(usbdemo, usb, 1, 1, 1);
MODULE_VERSION(usbdemo, 1);
USB_PNP_HOST_INFO(udemo_devs);
