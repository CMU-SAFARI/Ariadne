/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>
#include <linux/tracepoint-defs.h>

DECLARE_TRACEPOINT(page_ref_set);
DECLARE_TRACEPOINT(page_ref_mod);
DECLARE_TRACEPOINT(page_ref_mod_and_test);
DECLARE_TRACEPOINT(page_ref_mod_and_return);
DECLARE_TRACEPOINT(page_ref_mod_unless);
DECLARE_TRACEPOINT(page_ref_freeze);
DECLARE_TRACEPOINT(page_ref_unfreeze);

#ifdef CONFIG_DEBUG_PAGE_REF

/*
 * Ideally we would want to use the trace_<tracepoint>_enabled() helper
 * functions. But due to include header file issues, that is not
 * feasible. Instead we have to open code the static key functions.
 *
 * See trace_##name##_enabled(void) in include/linux/tracepoint.h
 */
#define page_ref_tracepoint_active(t) tracepoint_enabled(t)

extern void __page_ref_set(struct page *page, int v);
extern void __page_ref_mod(struct page *page, int v);
extern void __page_ref_mod_and_test(struct page *page, int v, int ret);
extern void __page_ref_mod_and_return(struct page *page, int v, int ret);
extern void __page_ref_mod_unless(struct page *page, int v, int u);
extern void __page_ref_freeze(struct page *page, int v, int ret);
extern void __page_ref_unfreeze(struct page *page, int v);

#else

#define page_ref_tracepoint_active(t) false

static inline void __page_ref_set(struct page *page, int v)
{
}
static inline void __page_ref_mod(struct page *page, int v)
{
}
static inline void __page_ref_mod_and_test(struct page *page, int v, int ret)
{
}
static inline void __page_ref_mod_and_return(struct page *page, int v, int ret)
{
}
static inline void __page_ref_mod_unless(struct page *page, int v, int u)
{
}
static inline void __page_ref_freeze(struct page *page, int v, int ret)
{
}
static inline void __page_ref_unfreeze(struct page *page, int v)
{
}

#endif

static inline int page_ref_count(struct page *page)
{
	return atomic_read(&page->_refcount) & 0xffff;
}

static inline int page_count(struct page *page)
{
	return atomic_read(&compound_head(page)->_refcount) & 0xffff;
}

static inline void set_page_count(struct page *page, int v)
{
	int temp = 0;
	temp = atomic_read(&page->_refcount);
	v = (temp & 0xffff0000) | (v & 0xffff);
	atomic_set(&page->_refcount, v);
	if (page_ref_tracepoint_active(page_ref_set))
		__page_ref_set(page, v);
}

/*
 * Setup the page count before being freed into the page allocator for
 * the first time (boot or memory hotplug)
 */
static inline void init_page_count(struct page *page)
{
	set_page_count(page, 1);
}

static inline void page_ref_add(struct page *page, int nr)
{
	atomic_add(nr & 0xffff, &page->_refcount);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, nr & 0xffff);
}

static inline void page_ref_sub(struct page *page, int nr)
{
	atomic_sub(nr & 0xffff, &page->_refcount);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, -nr);
}

static inline int page_ref_sub_return(struct page *page, int nr)
{
	int ret = atomic_sub_return(nr & 0xffff, &page->_refcount) & 0xffff;

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -nr, ret);
	return ret;
}

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_refcount);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, 1);
}

static inline void page_ref_dec(struct page *page)
{
	atomic_dec(&page->_refcount);
	if (page_ref_tracepoint_active(page_ref_mod))
		__page_ref_mod(page, -1);
}

static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	int ret;
	int temp = atomic_read(&page->_refcount);
	nr = (nr & 0xffff) | (temp & 0xffff0000); 
	ret = atomic_sub_and_test(nr, &page->_refcount);

	if (ret == 1)
	{
		/* code */
	} else {
		atomic_add((temp & 0xffff0000), &page->_refcount);
	}
	if (page_ref_tracepoint_active(page_ref_mod_and_test))
		__page_ref_mod_and_test(page, -nr, ret);
	return ret;
}

static inline int page_ref_inc_return(struct page *page)
{
	int ret = atomic_inc_return(&page->_refcount) & 0xffff;

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, 1, ret);
	return ret;
}

static inline int page_ref_dec_and_test(struct page *page)
{
	int ret;
	int temp = atomic_read(&page->_refcount);
	atomic_sub(temp & 0xffff0000, &page->_refcount);
	ret = atomic_dec_and_test(&page->_refcount);
	if (ret == 1)
	{
		/* code */
		atomic_add((temp & 0xffff0000), &page->_refcount);
	} else {
		atomic_add((temp & 0xffff0000), &page->_refcount);
	}

	if (page_ref_tracepoint_active(page_ref_mod_and_test))
		__page_ref_mod_and_test(page, -1, ret);
	return ret;
}

static inline int page_ref_dec_return(struct page *page)
{
	int ret = atomic_dec_return(&page->_refcount) & 0xffff;

	if (page_ref_tracepoint_active(page_ref_mod_and_return))
		__page_ref_mod_and_return(page, -1, ret);
	return ret;
}

static inline int page_ref_add_unless(struct page *page, int nr, int u)
{
	int raw_count = atomic_read(&page->_refcount);
	int appid_mask = raw_count & 0xffff0000;
	int ret;
	u = appid_mask | u;
	// nr = appid_mask | nr;
	
	ret = atomic_add_unless(&page->_refcount, nr, u) & 0xffff;

	if (page_ref_tracepoint_active(page_ref_mod_unless))
		__page_ref_mod_unless(page, nr, ret);
	return ret;
}

static inline int page_ref_freeze(struct page *page, int count)
{
	int ret;
	int raw_count = atomic_read(&page->_refcount);
	int appid_mask = raw_count & 0xffff0000;
	count = appid_mask | count;
	ret = likely(atomic_cmpxchg(&page->_refcount, count, appid_mask) == count);
	if (page_ref_tracepoint_active(page_ref_freeze))
		__page_ref_freeze(page, count, ret);
	return ret;
}

static inline void page_ref_unfreeze(struct page *page, int count)
{
	int temp = 0;
	VM_BUG_ON_PAGE(page_count(page) != 0, page);
	VM_BUG_ON(count == 0);
	temp = atomic_read(&page->_refcount);
	count = (temp & 0xffff0000) | (count & 0xffff);
	atomic_set_release(&page->_refcount, count);
	if (page_ref_tracepoint_active(page_ref_unfreeze))
		__page_ref_unfreeze(page, count);
}

static inline int page_app_id(struct page *page)
{
	return (atomic_read(&page->_refcount) >> 16) & 0xffff;
}

static inline void set_app_id(struct page *page, int v)
{
	int temp = 0;
	temp = atomic_read(&page->_refcount);
	v = (temp & 0xffff) | ((v << 16) & 0xffff0000);
	atomic_set(&page->_refcount, v);
	if (page_ref_tracepoint_active(page_ref_set))
		__page_ref_set(page, v);
}

// EXPORT_SYMBOL(page_app_id);
// EXPORT_SYMBOL(set_app_id);
#endif
