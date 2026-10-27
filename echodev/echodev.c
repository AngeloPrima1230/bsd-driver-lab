/*-
 * SPDX-License-Identifier: BSD-2-Clause
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

static MALLOC_DEFINE(M_ECHO, "echodev", "echodev buffer");

struct echo_softc {
	struct cdev	*dev;
	struct sx	 lock;
	char		*buf;
	size_t		 len;
	size_t		 cap;
};

static struct echo_softc *echo_sc;

static d_read_t		echo_read;
static d_write_t	echo_write;
static d_ioctl_t	echo_ioctl;

static struct cdevsw echo_cdevsw = {
	.d_version =	D_VERSION,
	.d_name =	"echodev",
	.d_read =	echo_read,
	.d_write =	echo_write,
	.d_ioctl =	echo_ioctl,
};

static int
echo_read(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	struct echo_softc *sc = dev->si_drv1;
	off_t off = uio->uio_offset;
	size_t n;
	int error = 0;

	sx_slock(&sc->lock);
	if (off >= 0 && (size_t)off < sc->len) {
		n = MIN((size_t)uio->uio_resid, sc->len - off);
		error = uiomove(sc->buf + off, n, uio);
	}
	sx_sunlock(&sc->lock);
	return (error);
}

static int
echo_write(struct cdev *dev, struct uio *uio, int ioflag __unused)
{
	struct echo_softc *sc = dev->si_drv1;
	size_t n = uio->uio_resid;
	int error;

	sx_xlock(&sc->lock);
	if (n > sc->cap) {
		sx_xunlock(&sc->lock);
		return (EFBIG);
	}
	error = uiomove(sc->buf, n, uio);
	sc->len = error ? 0 : n;
	sx_xunlock(&sc->lock);
	return (error);
}

static int
echo_setcap(struct echo_softc *sc, uint32_t cap)
{
	char *nb, *ob;

	if (cap == 0 || cap > ECHO_MAXCAP)
		return (EINVAL);
	nb = malloc(cap, M_ECHO, M_WAITOK | M_ZERO);
	sx_xlock(&sc->lock);
	ob = sc->buf;
	sc->buf = nb;
	sc->cap = cap;
	sc->len = 0;
	sx_xunlock(&sc->lock);
	free(ob, M_ECHO);
	return (0);
}

static int
echo_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag __unused,
    struct thread *td __unused)
{
	struct echo_softc *sc = dev->si_drv1;

	switch (cmd) {
	case ECHOIOC_GETLEN:
		sx_slock(&sc->lock);
		*(uint32_t *)data = sc->len;
		sx_sunlock(&sc->lock);
		break;
	case ECHOIOC_CLEAR:
		sx_xlock(&sc->lock);
		sc->len = 0;
		sx_xunlock(&sc->lock);
		break;
	case ECHOIOC_SETCAP:
		return (echo_setcap(sc, *(uint32_t *)data));
	default:
		return (ENOTTY);
	}
	return (0);
}

static void
echo_destroy(struct echo_softc *sc)
{
	free(sc->buf, M_ECHO);
	sx_destroy(&sc->lock);
	free(sc, M_ECHO);
}

static int
echo_load(void)
{
	struct make_dev_args a;
	struct echo_softc *sc;
	int error;

	sc = malloc(sizeof(*sc), M_ECHO, M_WAITOK | M_ZERO);
	sx_init(&sc->lock, "echodev");
	sc->cap = ECHO_DEFCAP;
	sc->buf = malloc(sc->cap, M_ECHO, M_WAITOK | M_ZERO);

	make_dev_args_init(&a);
	a.mda_devsw = &echo_cdevsw;
	a.mda_uid = UID_ROOT;
	a.mda_gid = GID_WHEEL;
	a.mda_mode = 0600;
	a.mda_si_drv1 = sc;
	if ((error = make_dev_s(&a, &sc->dev, "echodev")) != 0) {
		echo_destroy(sc);
		return (error);
	}
	echo_sc = sc;
	return (0);
}

static int
echo_modevent(module_t mod __unused, int type, void *arg __unused)
{
	switch (type) {
	case MOD_LOAD:
		return (echo_load());
	case MOD_UNLOAD:
		destroy_dev(echo_sc->dev);
		echo_destroy(echo_sc);
		echo_sc = NULL;
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

DEV_MODULE(echodev, echo_modevent, NULL);
MODULE_VERSION(echodev, 1);
