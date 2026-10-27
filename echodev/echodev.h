/*
 * echodev - shared definitions between the kernel module and userland.
 */
#ifndef _ECHODEV_H_
#define _ECHODEV_H_

#include <sys/types.h>
#include <sys/ioccom.h>

#define	ECHODEV_DEFAULT_CAP	256
#define	ECHODEV_MAX_CAP		(64 * 1024)

/* Return number of bytes currently stored. */
#define	ECHODEV_GETLEN	_IOR('E', 1, uint32_t)
/* Discard stored data. */
#define	ECHODEV_CLEAR	_IO('E', 2)
/* Resize the buffer (drops stored data). */
#define	ECHODEV_SETCAP	_IOW('E', 3, uint32_t)

#endif /* !_ECHODEV_H_ */
