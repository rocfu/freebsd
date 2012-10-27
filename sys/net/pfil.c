/*	$FreeBSD$ */
/*	$NetBSD: pfil.c,v 1.20 2001/11/12 23:49:46 lukem Exp $	*/

/*-
 * Copyright (c) 1996 Matthew R. Green
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/pfil.h>

static struct mtx pfil_global_lock;
MTX_SYSINIT(pfil_global_lock, &pfil_global_lock, "pfil_head_list lock", MTX_DEF);

static int pfil_list_add(pfil_list_t *, struct packet_filter_hook *, int,
    uint8_t);

static int pfil_list_remove(pfil_list_t *,
    int (*)(void *, struct mbuf **, struct ifnet *, int, struct inpcb *),
    void *);

LIST_HEAD(pfilheadhead, pfil_head);
VNET_DEFINE(struct pfilheadhead, pfil_head_list);
#define	V_pfil_head_list	VNET(pfil_head_list)
VNET_DEFINE(struct rmlock, pfil_lock);
#define	V_pfil_lock	VNET(pfil_lock)

VNET_DEFINE(int, pfilforward) = 0;
SYSCTL_NODE(_net, OID_AUTO, pfil, CTLFLAG_RW, 0, "Packer filter interface");
SYSCTL_VNET_INT(_net_pfil, OID_AUTO, forward, CTLFLAG_RW,
    &VNET_NAME(pfilforward), 0,
    "Enable forwarding performed by packet filters");
/*
 * pfil_run_hooks() runs the specified packet filter hooks.
 *
 * The cookie, if set, skips all hooks before the hook with
 * the same cookie and continues with the next hook after it.
 */
int
pfil_run_hooks(struct pfil_head *ph, struct mbuf **mp, struct ifnet *ifp,
    int dir, struct inpcb *inp)
{

	return (pfil_run_inject(ph, mp, ifp, dir, inp, 0));
}

int
pfil_run_inject(struct pfil_head *ph, struct mbuf **mp, struct ifnet *ifp,
    int dir, struct inpcb *inp, int cookie)
{
	struct rm_priotracker rmpt;
	struct packet_filter_hook *pfh;
	struct mbuf *m = *mp;
	int rv = 0;

	PFIL_RLOCK(ph, &rmpt);
	KASSERT(ph->ph_nhooks >= 0, ("Pfil hook count dropped < 0"));
	for (pfh = pfil_hook_get(dir, ph); pfh != NULL;
	     pfh = TAILQ_NEXT(pfh, pfil_link)) {
		if (cookie != 0) {
			/* Continue on the next hook. */
			if (pfh->pfil_cookie == cookie)
				cookie = 0;
			continue;
		}
		if (pfh->pfil_func != NULL) {
			rv = (*pfh->pfil_func)(pfh->pfil_arg, &m, ifp, dir,
			    inp);
			if (rv != 0 || m == NULL)
				break;
		}
	}
	PFIL_RUNLOCK(ph, &rmpt);
	*mp = m;
	return (rv);
}

/*
 * pfil_try_rlock() acquires rm reader lock for specified head
 * if this is immediately possible,
 */
int
pfil_try_rlock(struct pfil_head *ph, struct rm_priotracker *tracker)
{
	return PFIL_TRY_RLOCK(ph, tracker);
}

/*
 * pfil_rlock() acquires rm reader lock for specified head.
 */
void
pfil_rlock(struct pfil_head *ph, struct rm_priotracker *tracker)
{
	PFIL_RLOCK(ph, tracker);
}

/*
 * pfil_runlock() releases reader lock for specified head.
 */
void
pfil_runlock(struct pfil_head *ph, struct rm_priotracker *tracker)
{
	PFIL_RUNLOCK(ph, tracker);
}

/*
 * pfil_wlock() acquires writer lock for specified head.
 */
void
pfil_wlock(struct pfil_head *ph)
{
	PFIL_WLOCK(ph);
}

/*
 * pfil_wunlock() releases writer lock for specified head.
 */
void
pfil_wunlock(struct pfil_head *ph)
{
	PFIL_WUNLOCK(ph);
}

/*
 * pfil_wowned() releases writer lock for specified head.
 */
int
pfil_wowned(struct pfil_head *ph)
{
	return PFIL_WOWNED(ph);
}
/*
 * pfil_head_register() registers a pfil_head with the packet filter hook
 * mechanism.
 */
int
pfil_head_register(struct pfil_head *ph)
{
	struct pfil_head *lph;

	PFIL_LIST_LOCK();
	LIST_FOREACH(lph, &V_pfil_head_list, ph_list) {
		if (ph->ph_type == lph->ph_type &&
		    ph->ph_un.phu_val == lph->ph_un.phu_val) {
			PFIL_LIST_UNLOCK();
			return (EEXIST);
		}
	}
	PFIL_LOCK_INIT(ph);
	ph->ph_nhooks = 0;
	TAILQ_INIT(&ph->ph_in);
	TAILQ_INIT(&ph->ph_out);
	LIST_INSERT_HEAD(&V_pfil_head_list, ph, ph_list);
	PFIL_LIST_UNLOCK();
	return (0);
}

/*
 * pfil_head_unregister() removes a pfil_head from the packet filter hook
 * mechanism.  The producer of the hook promises that all outstanding
 * invocations of the hook have completed before it unregisters the hook.
 */
int
pfil_head_unregister(struct pfil_head *ph)
{
	struct packet_filter_hook *pfh, *pfnext;
		
	PFIL_LIST_LOCK();
	LIST_REMOVE(ph, ph_list);
	PFIL_LIST_UNLOCK();
	TAILQ_FOREACH_SAFE(pfh, &ph->ph_in, pfil_link, pfnext)
		free(pfh, M_IFADDR);
	TAILQ_FOREACH_SAFE(pfh, &ph->ph_out, pfil_link, pfnext)
		free(pfh, M_IFADDR);
	PFIL_LOCK_DESTROY(ph);
	return (0);
}

/*
 * pfil_head_get() returns the pfil_head for a given key/dlt.
 */
struct pfil_head *
pfil_head_get(int type, u_long val)
{
	struct pfil_head *ph;

	PFIL_LIST_LOCK();
	LIST_FOREACH(ph, &V_pfil_head_list, ph_list)
		if (ph->ph_type == type && ph->ph_un.phu_val == val)
			break;
	PFIL_LIST_UNLOCK();
	return (ph);
}

/*
 * pfil_add_hook() adds a function to the packet filter hook.  the
 * flags are:
 *	PFIL_IN		call me on incoming packets
 *	PFIL_OUT	call me on outgoing packets
 *	PFIL_ALL	call me on all of the above
 *	PFIL_WAITOK	OK to call malloc with M_WAITOK.
 *
 * The cookie is simply is a random value that should be unique.
 */
int
pfil_add_hook(int (*func)(void *, struct mbuf **, struct ifnet *, int,
  struct inpcb *), void *arg, int flags, struct pfil_head *ph)
{

	return (pfil_add_hook_order(func, arg, "unknown", flags,
	    PFIL_ORDER_DEFAULT, ph));
}

int
pfil_add_hook_order(int (*func)(void *, struct mbuf **, struct ifnet *, int,
    struct inpcb *), void *arg, char *name, int flags, uint8_t order,
    struct pfil_head *ph)
{
	struct packet_filter_hook *pfh1 = NULL;
	struct packet_filter_hook *pfh2 = NULL;
	int err;

	if (flags & PFIL_IN) {
		pfh1 = (struct packet_filter_hook *)malloc(sizeof(*pfh1), 
		    M_IFADDR, (flags & PFIL_WAITOK) ? M_WAITOK : M_NOWAIT);
		if (pfh1 == NULL) {
			err = ENOMEM;
			goto error;
		}
		pfh1->pfil_func = func;
		pfh1->pfil_arg = arg;
		pfh1->pfil_cookie = (int)random();
		pfh1->pfil_order = order;
		pfh1->pfil_name = name;
	}
	if (flags & PFIL_OUT) {
		pfh2 = (struct packet_filter_hook *)malloc(sizeof(*pfh1),
		    M_IFADDR, (flags & PFIL_WAITOK) ? M_WAITOK : M_NOWAIT);
		if (pfh2 == NULL) {
			err = ENOMEM;
			goto error;
		}
		pfh2->pfil_func = func;
		pfh2->pfil_arg = arg;
		pfh2->pfil_cookie = (int)random();
		pfh2->pfil_order = order;
		pfh2->pfil_name = name;
	}
	PFIL_WLOCK(ph);
	if (flags & PFIL_IN) {
		err = pfil_list_add(&ph->ph_in, pfh1, flags & ~PFIL_OUT, order);
		if (err)
			goto locked_error;
		ph->ph_nhooks++;
	}
	if (flags & PFIL_OUT) {
		err = pfil_list_add(&ph->ph_out, pfh2, flags & ~PFIL_IN, order);
		if (err) {
			if (flags & PFIL_IN)
				pfil_list_remove(&ph->ph_in, func, arg);
			goto locked_error;
		}
		ph->ph_nhooks++;
	}
	PFIL_WUNLOCK(ph);
	return (0);
locked_error:
	PFIL_WUNLOCK(ph);
error:
	if (pfh1 != NULL)
		free(pfh1, M_IFADDR);
	if (pfh2 != NULL)
		free(pfh2, M_IFADDR);
	return (err);
}

/*
 * pfil_remove_hook removes a specific function from the packet filter hook
 * list.
 */
int
pfil_remove_hook(int (*func)(void *, struct mbuf **, struct ifnet *, int,
    struct inpcb *), void *arg, int flags, struct pfil_head *ph)
{
	int err = 0;

	PFIL_WLOCK(ph);
	if (flags & PFIL_IN) {
		err = pfil_list_remove(&ph->ph_in, func, arg);
		if (err == 0)
			ph->ph_nhooks--;
	}
	if ((err == 0) && (flags & PFIL_OUT)) {
		err = pfil_list_remove(&ph->ph_out, func, arg);
		if (err == 0)
			ph->ph_nhooks--;
	}
	PFIL_WUNLOCK(ph);
	return (err);
}

int
pfil_get_cookie(int (*func)(void *, struct mbuf **, struct ifnet *, int,
    struct inpcb *), void *arg, int flags, struct pfil_head *ph)
{
	pfil_list_t *list;
	struct packet_filter_hook *pfh;
	struct rm_priotracker tracker;
	int cookie = 0;

	PFIL_RLOCK(ph, &tracker);
	if (flags & PFIL_IN)
		list = &ph->ph_in;
	else if (flags & PFIL_OUT)
		list = &ph->ph_out;
	else
		goto out;

	TAILQ_FOREACH(pfh, list, pfil_link)
		if (pfh->pfil_func == func &&
		    pfh->pfil_arg == arg)
			cookie = pfh->pfil_cookie;
out:
	PFIL_RUNLOCK(ph, &tracker);
	return (cookie);
}

static int
pfil_list_add(pfil_list_t *list, struct packet_filter_hook *pfh1, int flags,
    uint8_t order)
{
	struct packet_filter_hook *pfh;

	/*
	 * First make sure the hook is not already there.
	 */
	TAILQ_FOREACH(pfh, list, pfil_link)
		if (pfh->pfil_func == pfh1->pfil_func &&
		    pfh->pfil_arg == pfh1->pfil_arg)
			return (EEXIST);

	/*
	 * Insert the input list in reverse order of the output list so that
	 * the same path is followed in or out of the kernel.
	 */
	if (flags & PFIL_IN) {
		TAILQ_FOREACH(pfh, list, pfil_link) {
			if (pfh->pfil_order <= order)
				break;
		}
		if (pfh == NULL)
			TAILQ_INSERT_HEAD(list, pfh1, pfil_link);
		else
			TAILQ_INSERT_BEFORE(pfh, pfh1, pfil_link);
	} else {
		TAILQ_FOREACH_REVERSE(pfh, list, pfil_list, pfil_link)
			if (pfh->pfil_order >= order)
				break;
		if (pfh == NULL)
			TAILQ_INSERT_TAIL(list, pfh1, pfil_link);
		else
			TAILQ_INSERT_AFTER(list, pfh, pfh1, pfil_link);
	}
	return (0);
}

/*
 * pfil_list_remove is an internal function that takes a function off the
 * specified list.
 */
static int
pfil_list_remove(pfil_list_t *list,
    int (*func)(void *, struct mbuf **, struct ifnet *, int, struct inpcb *),
    void *arg)
{
	struct packet_filter_hook *pfh;

	TAILQ_FOREACH(pfh, list, pfil_link)
		if (pfh->pfil_func == func && pfh->pfil_arg == arg) {
			TAILQ_REMOVE(list, pfh, pfil_link);
			free(pfh, M_IFADDR);
			return (0);
		}
	return (ENOENT);
}

/*
 * Stuff that must be initialized for every instance (including the first of
 * course).
 */
static int
vnet_pfil_init(const void *unused)
{

	LIST_INIT(&V_pfil_head_list);
	PFIL_LOCK_INIT_REAL(&V_pfil_lock, "shared");
	return (0);
}

/*
 * Called for the removal of each instance.
 */
static int
vnet_pfil_uninit(const void *unused)
{

	/*  XXX should panic if list is not empty */
	PFIL_LOCK_DESTROY_REAL(&V_pfil_lock);
	return (0);
}

/* Define startup order. */
#define	PFIL_SYSINIT_ORDER	SI_SUB_PROTO_BEGIN
#define	PFIL_MODEVENT_ORDER	(SI_ORDER_FIRST) /* On boot slot in here. */
#define	PFIL_VNET_ORDER		(PFIL_MODEVENT_ORDER + 2) /* Later still. */

/*
 * Starting up.
 *
 * VNET_SYSINIT is called for each existing vnet and each new vnet.
 */
VNET_SYSINIT(vnet_pfil_init, PFIL_SYSINIT_ORDER, PFIL_VNET_ORDER,
    vnet_pfil_init, NULL);
 
/*
 * Closing up shop.  These are done in REVERSE ORDER.  Not called on reboot.
 *
 * VNET_SYSUNINIT is called for each exiting vnet as it exits.
 */
VNET_SYSUNINIT(vnet_pfil_uninit, PFIL_SYSINIT_ORDER, PFIL_VNET_ORDER,
    vnet_pfil_uninit, NULL);
