/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_SELECT_H_
#define	_SYS_SELECT_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#include <sys/_sigset.h>
#include <sys/timespec.h>

/*
 * XXX
 * Other things required for this header which we do not presently implement:
 *
 * struct timeval (with suseconds_t)
 */

typedef	unsigned long	__fd_mask;
#if __BSD_VISIBLE
typedef	__fd_mask	fd_mask;
#endif

#ifndef _SIGSET_T_DECLARED
#define	_SIGSET_T_DECLARED
typedef	__sigset_t	sigset_t;
#endif

/*
 * Select uses bit masks of file descriptors in longs.  These macros
 * manipulate such bit fields (the filesystem macros use chars).
 * FD_SETSIZE may be defined by the user, but the default here should
 * be enough for most uses.
 */
#ifndef	FD_SETSIZE
#define	FD_SETSIZE	1024U
#endif

#define	_NFDBITS	(sizeof(__fd_mask) * 8)	/* bits per mask */
#if __BSD_VISIBLE
#define	NFDBITS		_NFDBITS
#endif

#ifndef _howmany
#define	_howmany(x, y)	(((x) + ((y) - 1)) / (y))
#endif

typedef	struct fd_set {
	__fd_mask	fds_bits[_howmany(FD_SETSIZE, _NFDBITS)];
} fd_set;

#define	__fdset_mask(n)	((fd_mask)1 << ((n) % _NFDBITS))
#define	FD_CLR(n, p)	((p)->fds_bits[(n)/_NFDBITS] &= ~__fdset_mask(n))
#if __BSD_VISIBLE
/* XXX bcopy() not in scope, so <strings.h> is required; see also FD_ZERO(). */
#define	FD_COPY(f, t)	bcopy(f, t, sizeof(*(f)))
#endif
#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/_NFDBITS] & __fdset_mask(n))
#define	FD_SET(n, p)	((p)->fds_bits[(n)/_NFDBITS] |= __fdset_mask(n))
#define	FD_ZERO(p)	bzero(p, sizeof(*(p)))

#ifndef _KERNEL
struct timeval;

__BEGIN_DECLS
int pselect(int, fd_set *__restrict, fd_set *__restrict, fd_set *__restrict,
	const struct timespec *__restrict, const sigset_t *__restrict);
#ifndef _SELECT_DECLARED
#define	_SELECT_DECLARED
/* XXX missing restrict type-qualifier */
int	select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
__END_DECLS
#endif /* !_KERNEL */

#endif /* _SYS_SELECT_H_ */
