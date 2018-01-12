/*-
 * Copyright (c) 2014-2017 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"


/*
 * On systems with poor default allocators for allocations greater than 16 KB,
 * we provide an option to use TCMalloc explicitly.
 * This is important on Windows which does not have a builtin mechanism
 * to replace C run-time memory management functions with alternatives.
 */
#ifdef HAVE_LIBTCMALLOC
#include <gperftools/tcmalloc.h>

#define	calloc			tc_calloc
#define	malloc			tc_malloc
#define	realloc 		tc_realloc
#define	posix_memalign 		tc_posix_memalign
#define	free 			tc_free
#endif

//#ifndef HAVE_LIBJEMALLOC
 /*
 * Bias arena index bits so that 0 encodes "use an automatically chosen arena".
 */
#define MALLOCX_ARENA(a)	((((int)(a))+1) << 20)

#define MALLOCX_ZERO	((int)0x40)

#  define MALLOCX_ALIGN(a)						\
     ((int)(((size_t)(a) < (size_t)INT_MAX) ? ffs((int)(a))-1 :	\
     ffs((int)(((size_t)(a))>>32))+31))

#ifndef _WIN32
#define je_rallocx rallocx
#define je_mallctl mallctl
#define je_mallocx mallocx
#define je_free free
#endif

void *je_mallocx(size_t size, int flags);

void *je_rallocx(void *ptr, size_t size,
    int flags);

int je_mallctl(const char *name,
    void *oldp, size_t *oldlenp, void *newp, size_t newlen);

void *je_calloc(size_t num, size_t size);
void 	*je_malloc(size_t size);
void *je_realloc(void *ptr, size_t size);
void je_free(void *ptr);


static unsigned wt_arena;

void __wt_arena_init() {
    // Init arena;
    // TODO: assert == 0
    size_t sz = sizeof(wt_arena);

    je_mallctl("arenas.create", (void *)&wt_arena, &sz, NULL, 0);
}


 
 //#include <jemalloc/jemalloc.h>

#define	calloc			je_calloc
#define	malloc			je_malloc
#define	realloc 		je_realloc
#define	posix_memalign 		je_posix_memalign
#define	free 			je_free
//#endif

/*
 * __wt_calloc --
 *	ANSI calloc function.
 */
int
__wt_calloc(WT_SESSION_IMPL *session, size_t number, size_t size, void *retp)
    WT_GCC_FUNC_ATTRIBUTE((visibility("default")))
{
	void *p;

	/*
	 * Defensive: if our caller doesn't handle errors correctly, ensure a
	 * free won't fail.
	 */
	*(void **)retp = NULL;

	/*
	 * !!!
	 * This function MUST handle a NULL WT_SESSION_IMPL handle.
	 */
	WT_ASSERT(session, number != 0 && size != 0);

	if (session != NULL)
		WT_STAT_CONN_INCR(session, memory_allocation);

    if ((p = je_mallocx(number * size, MALLOCX_ARENA(wt_arena) | MALLOCX_ZERO)) == NULL)
		WT_RET_MSG(session, __wt_errno(),
		    "memory allocation of %" WT_SIZET_FMT " bytes failed",
		    size * number);

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_malloc --
 *	ANSI malloc function.
 */
int
__wt_malloc(WT_SESSION_IMPL *session, size_t bytes_to_allocate, void *retp)
{
	void *p;

	/*
	 * Defensive: if our caller doesn't handle errors correctly, ensure a
	 * free won't fail.
	 */
	*(void **)retp = NULL;

	/*
	 * !!!
	 * This function MUST handle a NULL WT_SESSION_IMPL handle.
	 */
	WT_ASSERT(session, bytes_to_allocate != 0);

	if (session != NULL)
		WT_STAT_CONN_INCR(session, memory_allocation);

    if ((p = je_mallocx(bytes_to_allocate, MALLOCX_ARENA(wt_arena))) == NULL)
		WT_RET_MSG(session, __wt_errno(),
		    "memory allocation of %" WT_SIZET_FMT " bytes failed",
		    bytes_to_allocate);

	*(void **)retp = p;
	return (0);
}

/*
 * __realloc_func --
 *	ANSI realloc function.
 */
static int
__realloc_func(WT_SESSION_IMPL *session,
    size_t *bytes_allocated_ret, size_t bytes_to_allocate, bool clear_memory,
    void *retp)
{
	size_t bytes_allocated;
	void *p;

	/*
	 * !!!
	 * This function MUST handle a NULL WT_SESSION_IMPL handle.
	 *
	 * Sometimes we're allocating memory and we don't care about the
	 * final length -- bytes_allocated_ret may be NULL.
	 */
	p = *(void **)retp;
	bytes_allocated =
	    (bytes_allocated_ret == NULL) ? 0 : *bytes_allocated_ret;
	WT_ASSERT(session,
	    (p == NULL && bytes_allocated == 0) ||
	    (p != NULL &&
	    (bytes_allocated_ret == NULL || bytes_allocated != 0)));
	WT_ASSERT(session, bytes_to_allocate != 0);
	WT_ASSERT(session, bytes_allocated < bytes_to_allocate);

	if (session != NULL) {
		if (p == NULL)
			WT_STAT_CONN_INCR(session, memory_allocation);
		else
			WT_STAT_CONN_INCR(session, memory_grow);
	}

    if (p == NULL) {
        if ((p = je_mallocx(bytes_to_allocate, MALLOCX_ARENA(wt_arena))) == NULL)
            WT_RET_MSG(session, __wt_errno(),
                "memory allocation of %" WT_SIZET_FMT " bytes failed",
                bytes_to_allocate);
    }
    else 
        if ((p = je_rallocx(p, bytes_to_allocate, MALLOCX_ARENA(wt_arena))) == NULL)
		    WT_RET_MSG(session, __wt_errno(),
		        "memory allocation of %" WT_SIZET_FMT " bytes failed",
		        bytes_to_allocate);

	/*
	 * Clear the allocated memory, parts of WiredTiger depend on allocated
	 * memory being cleared.
	 */
	if (clear_memory)
		memset((uint8_t *)p + bytes_allocated,
		    0, bytes_to_allocate - bytes_allocated);

	/* Update caller's bytes allocated value. */
	if (bytes_allocated_ret != NULL)
		*bytes_allocated_ret = bytes_to_allocate;

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_realloc --
 *	WiredTiger's realloc API.
 */
int
__wt_realloc(WT_SESSION_IMPL *session,
    size_t *bytes_allocated_ret, size_t bytes_to_allocate, void *retp)
{
	return (__realloc_func(
	    session, bytes_allocated_ret, bytes_to_allocate, true, retp));
}

/*
 * __wt_realloc_noclear --
 *	WiredTiger's realloc API, not clearing allocated memory.
 */
int
__wt_realloc_noclear(WT_SESSION_IMPL *session,
    size_t *bytes_allocated_ret, size_t bytes_to_allocate, void *retp)
{
	return (__realloc_func(
	    session, bytes_allocated_ret, bytes_to_allocate, false, retp));
}

/*
 * __wt_realloc_aligned --
 *	ANSI realloc function that aligns to buffer boundaries, configured with
 * the "buffer_alignment" key to wiredtiger_open.
 */
int
__wt_realloc_aligned(WT_SESSION_IMPL *session,
    size_t *bytes_allocated_ret, size_t bytes_to_allocate, void *retp)
{
#if defined(HAVE_POSIX_MEMALIGN)
	WT_DECL_RET;

	/*
	 * !!!
	 * This function MUST handle a NULL WT_SESSION_IMPL handle.
	 */
	if (session != NULL && S2C(session)->buffer_alignment > 0) {
		void *p, *newp;
		size_t bytes_allocated;

		/*
		 * Sometimes we're allocating memory and we don't care about the
		 * final length -- bytes_allocated_ret may be NULL.
		 */
		p = *(void **)retp;
		bytes_allocated =
		    (bytes_allocated_ret == NULL) ? 0 : *bytes_allocated_ret;
		WT_ASSERT(session,
		    (p == NULL && bytes_allocated == 0) ||
		    (p != NULL &&
		    (bytes_allocated_ret == NULL || bytes_allocated != 0)));
		WT_ASSERT(session, bytes_to_allocate != 0);
		WT_ASSERT(session, bytes_allocated < bytes_to_allocate);

		/*
		 * We are going to allocate an aligned buffer.  When we do this
		 * repeatedly, the allocator is expected to start on a boundary
		 * each time, account for that additional space by never asking
		 * for less than a full alignment size.  The primary use case
		 * for aligned buffers is Linux direct I/O, which requires that
		 * the size be a multiple of the alignment anyway.
		 */
		bytes_to_allocate =
		    WT_ALIGN(bytes_to_allocate, S2C(session)->buffer_alignment);

		WT_STAT_CONN_INCR(session, memory_allocation);

        if (p == NULL) {
            if ((newp = je_mallocx(bytes_to_allocate, MALLOCX_ARENA(wt_arena) | MALLOCX_ALIGN(S2C(session)->buffer_alignment))) == NULL)
                WT_RET_MSG(session, __wt_errno(),
                    "memory allocation of %" WT_SIZET_FMT " bytes failed",
                    bytes_to_allocate);
        }
		else if ((newp = je_rallocx(p,
		    bytes_to_allocate, MALLOCX_ARENA(wt_arena) | MALLOCX_ALIGN(S2C(session)->buffer_alignment))) != 0)
			WT_RET_MSG(session, ret,
			     "memory allocation of %" WT_SIZET_FMT
			     " bytes failed", bytes_to_allocate);

		p = newp;

		/* Update caller's bytes allocated value. */
		if (bytes_allocated_ret != NULL)
			*bytes_allocated_ret = bytes_to_allocate;

		*(void **)retp = p;
		return (0);
	}
#endif
	/*
	 * If there is no posix_memalign function, or no alignment configured,
	 * fall back to realloc.
	 *
	 * Windows note: Visual C CRT memalign does not match POSIX behavior
	 * and would also double each allocation so it is bad for memory use.
	 */
	return (__realloc_func(
	    session, bytes_allocated_ret, bytes_to_allocate, false, retp));
}

/*
 * __wt_memdup --
 *	Duplicate a byte string of a given length.
 */
int
__wt_memdup(WT_SESSION_IMPL *session, const void *str, size_t len, void *retp)
{
	void *p;

	WT_RET(__wt_malloc(session, len, &p));

	WT_ASSERT(session, p != NULL);		/* quiet clang scan-build */

	memcpy(p, str, len);

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_strndup --
 *	ANSI strndup function.
 */
int
__wt_strndup(WT_SESSION_IMPL *session, const void *str, size_t len, void *retp)
{
	uint8_t *p;

	if (str == NULL) {
		*(void **)retp = NULL;
		return (0);
	}

	/* Copy and nul-terminate. */
	WT_RET(__wt_malloc(session, len + 1, &p));

	WT_ASSERT(session, p != NULL);		/* quiet clang scan-build */

	memcpy(p, str, len);
	p[len] = '\0';

	*(void **)retp = p;
	return (0);
}

/*
 * __wt_free_int --
 *	ANSI free function.
 */
void
__wt_free_int(WT_SESSION_IMPL *session, const void *p_arg)
    WT_GCC_FUNC_ATTRIBUTE((visibility("default")))
{
	void *p;

	p = *(void **)p_arg;
	if (p == NULL)				/* ANSI C free semantics */
		return;

	/*
	 * If there's a serialization bug we might race with another thread.
	 * We can't avoid the race (and we aren't willing to flush memory),
	 * but we minimize the window by clearing the free address, hoping a
	 * racing thread will see, and won't free, a NULL pointer.
	 */
	*(void **)p_arg = NULL;

	/*
	 * !!!
	 * This function MUST handle a NULL WT_SESSION_IMPL handle.
	 */
	if (session != NULL)
		WT_STAT_CONN_INCR(session, memory_free);

	free(p);
}
