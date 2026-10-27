#ifndef _ECHODEV_H_
#define _ECHODEV_H_

#include <sys/types.h>
#include <sys/ioccom.h>

#define	ECHO_DEFCAP	256
#define	ECHO_MAXCAP	(64 * 1024)

#define	ECHOIOC_GETLEN	_IOR('E', 1, uint32_t)
#define	ECHOIOC_CLEAR	_IO('E', 2)
#define	ECHOIOC_SETCAP	_IOW('E', 3, uint32_t)

#endif
