#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../echodev/echodev.h"

static int nfail;

static void
check(int ok, const char *what)
{
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		nfail++;
}

int
main(void)
{
	const char msg[] = "hello from userland";
	size_t mlen = sizeof(msg) - 1;
	char buf[512];
	uint32_t v;
	ssize_t n;
	int fd;

	if ((fd = open("/dev/echodev", O_RDWR)) < 0)
		err(1, "/dev/echodev");

	check(write(fd, msg, mlen) == (ssize_t)mlen, "write");

	if (ioctl(fd, ECHOIOC_GETLEN, &v) < 0)
		err(1, "GETLEN");
	check(v == mlen, "getlen");

	memset(buf, 0, sizeof(buf));
	n = pread(fd, buf, sizeof(buf), 0);
	check(n == (ssize_t)mlen && memcmp(buf, msg, mlen) == 0, "read back");
	check(pread(fd, buf, sizeof(buf), 5) == (ssize_t)mlen - 5, "read at offset");

	if (ioctl(fd, ECHOIOC_CLEAR) < 0)
		err(1, "CLEAR");
	ioctl(fd, ECHOIOC_GETLEN, &v);
	check(v == 0, "clear");

	memset(buf, 'x', sizeof(buf));
	check(write(fd, buf, sizeof(buf)) < 0 && errno == EFBIG, "too big -> EFBIG");

	v = 1024;
	check(ioctl(fd, ECHOIOC_SETCAP, &v) == 0, "setcap 1024");
	check(write(fd, buf, sizeof(buf)) == (ssize_t)sizeof(buf), "write after setcap");

	v = 0;
	check(ioctl(fd, ECHOIOC_SETCAP, &v) < 0 && errno == EINVAL, "setcap 0 -> EINVAL");

	close(fd);
	printf("%d failed\n", nfail);
	return (nfail != 0);
}
