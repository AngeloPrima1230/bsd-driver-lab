/*
 * echotest - userland test for /dev/echodev.
 * Run as root after: kldload ./echodev/echodev.ko
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../echodev/echodev.h"

#define	DEV	"/dev/echodev"

static int failures;

#define	CHECK(cond, msg) do {						\
	if (cond)							\
		printf("ok   - %s\n", msg);				\
	else {								\
		printf("FAIL - %s\n", msg);				\
		failures++;						\
	}								\
} while (0)

int
main(void)
{
	const char msg[] = "hello from userland";
	char buf[512];
	uint32_t len, cap;
	ssize_t n;
	int fd;

	if ((fd = open(DEV, O_RDWR)) < 0)
		err(1, "open %s (is echodev loaded?)", DEV);

	n = write(fd, msg, sizeof(msg) - 1);
	CHECK(n == (ssize_t)sizeof(msg) - 1, "write stores full message");

	if (ioctl(fd, ECHODEV_GETLEN, &len) < 0)
		err(1, "ECHODEV_GETLEN");
	CHECK(len == sizeof(msg) - 1, "GETLEN matches written size");

	memset(buf, 0, sizeof(buf));
	n = pread(fd, buf, sizeof(buf), 0);
	CHECK(n == (ssize_t)sizeof(msg) - 1 && memcmp(buf, msg, n) == 0,
	    "read returns same data");

	n = pread(fd, buf, sizeof(buf), 5);
	CHECK(n == (ssize_t)sizeof(msg) - 1 - 5, "read honours offset");

	if (ioctl(fd, ECHODEV_CLEAR) < 0)
		err(1, "ECHODEV_CLEAR");
	ioctl(fd, ECHODEV_GETLEN, &len);
	CHECK(len == 0, "CLEAR empties buffer");

	memset(buf, 'x', sizeof(buf));
	n = write(fd, buf, sizeof(buf));
	CHECK(n < 0 && errno == EFBIG, "oversized write rejected with EFBIG");

	cap = 1024;
	CHECK(ioctl(fd, ECHODEV_SETCAP, &cap) == 0, "SETCAP grows buffer");
	n = write(fd, buf, sizeof(buf));
	CHECK(n == (ssize_t)sizeof(buf), "write fits after resize");

	cap = 0;
	CHECK(ioctl(fd, ECHODEV_SETCAP, &cap) < 0 && errno == EINVAL,
	    "SETCAP rejects 0");

	close(fd);
	printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
	    failures, failures == 1 ? "" : "s");
	return (failures ? 1 : 0);
}
