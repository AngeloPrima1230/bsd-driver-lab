/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * echodev - minimal FreeBSD character device.
 *
 * write(2) stores data in a kernel buffer, read(2) returns it,
 * ioctl(2) queries/clears/resizes the buffer.
 *
 * Demonstrates: module lifecycle, make_dev_s(9), cdevsw, uiomove(9),
 * sx(9) locking (uiomove may sleep, so a mutex cannot be held across it),
 * malloc(9), and safe teardown with destroy_dev(9).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/sx.h>

#include "echodev.h"

static MALLOC_DEFINE(M_ECHODEV, "echodev", "echodev buffer");

struct echodev_softc {
	struct cdev	*cdev;
	struct sx	 lock;
	char		*buf;
	size_t		 len;	/* bytes stored */
	size_t		 cap;	/* buffer size */
};

static struct echodev_softc *echodev_sc;

static d_read_t		echodev_read;
static d_write_t	echodev_write;
static d_ioctl_t	echodev_ioctl;

static struct cdevsw echodev_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"echodev",
	.d_read =	echodev_read,
	.d_write =	echodev_write,
	.d_ioctl =	echodev_ioctl,
};

static int
echodev_read(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	struct echodev_softc *sc = dev->si_drv1;
	size_t amt;
	int error;

	sx_slock(&sc->lock);
	if (uio->uio_offset < 0 || (size_t)uio->uio_offset >= sc->len) {
		sx_sunlock(&sc->lock);
		return (0);	/* EOF */
	}
	amt = MIN((size_t)uio->uio_resid, sc->len - (size_t)uio->uio_offset);
	error = uiomove(sc->buf + uio->uio_offset, amt, uio);
	sx_sunlock(&sc->lock);
	return (error);
}

static int
echodev_write(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	struct echodev_softc *sc = dev->si_drv1;
	size_t amt;
	int error;

	sx_xlock(&sc->lock);
	if ((size_t)uio->uio_resid > sc->cap) {
		sx_xunlock(&sc->lock);
		return (EFBIG);
	}
	/* Each write replaces the stored contents. */
	amt = uio->uio_resid;
	error = uiomove(sc->buf, amt, uio);
	sc->len = (error == 0) ? amt : 0;
	sx_xunlock(&sc->lock);
	return (error);
}

static int
echodev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag __unused,
    struct thread *td __unused)
{
	struct echodev_softc *sc = dev->si_drv1;
	char *newbuf, *oldbuf;
	uint32_t newcap;

	switch (cmd) {
	case ECHODEV_GETLEN:
		sx_slock(&sc->lock);
		*(uint32_t *)data = (uint32_t)sc->len;
		sx_sunlock(&sc->lock);
		return (0);
	case ECHODEV_CLEAR:
		sx_xlock(&sc->lock);
		sc->len = 0;
		sx_xunlock(&sc->lock);
		return (0);
	case ECHODEV_SETCAP:
		newcap = *(uint32_t *)data;
		if (newcap == 0 || newcap > ECHODEV_MAX_CAP)
			return (EINVAL);
		/* Allocate before taking the lock; M_WAITOK may sleep. */
		newbuf = malloc(newcap, M_ECHODEV, M_WAITOK | M_ZERO);
		sx_xlock(&sc->lock);
		oldbuf = sc->buf;
		sc->buf = newbuf;
		sc->cap = newcap;
		sc->len = 0;
		sx_xunlock(&sc->lock);
		free(oldbuf, M_ECHODEV);
		return (0);
	default:
		return (ENOTTY);
	}
}

static void
echodev_free(struct echodev_softc *sc)
{
	free(sc->buf, M_ECHODEV);
	sx_destroy(&sc->lock);
	free(sc, M_ECHODEV);
}

static int
echodev_modevent(module_t mod __unused, int type, void *arg __unused)
{
	struct make_dev_args args;
	struct echodev_softc *sc;
	int error;

	switch (type) {
	case MOD_LOAD:
		sc = malloc(sizeof(*sc), M_ECHODEV, M_WAITOK | M_ZERO);
		sx_init(&sc->lock, "echodev");
		sc->cap = ECHODEV_DEFAULT_CAP;
		sc->buf = malloc(sc->cap, M_ECHODEV, M_WAITOK | M_ZERO);

		make_dev_args_init(&args);
		args.mda_devsw = &echodev_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_WHEEL;
		args.mda_mode = 0600;
		args.mda_si_drv1 = sc;
		error = make_dev_s(&args, &sc->cdev, "echodev");
		if (error != 0) {
			echodev_free(sc);
			return (error);
		}
		echodev_sc = sc;
		printf("echodev: loaded, /dev/echodev ready\n");
		return (0);
	case MOD_UNLOAD:
		sc = echodev_sc;
		/* Blocks until all threads have left the cdevsw methods. */
		destroy_dev(sc->cdev);
		echodev_free(sc);
		echodev_sc = NULL;
		printf("echodev: unloaded\n");
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

DEV_MODULE(echodev, echodev_modevent, NULL);
MODULE_VERSION(echodev, 1);
