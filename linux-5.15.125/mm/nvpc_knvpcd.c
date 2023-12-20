/*
 * NVPC: A page cache extention with non-volatile memory.
 * This is a prototype and may be unsafe now.
 * 
 * knvpcd: a kernel thread to shrink pages from NVPC like
 * kswapd.
 */

#include <linux/nvpc.h>
#include <linux/printk.h>
#include <linux/dax.h>

#include <linux/swap.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/vmstat.h>
#include <linux/pagevec.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>

#include <linux/uio.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/local_lock.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/pfn_t.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>

// for knvpcd
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include "internal.h"

struct scan_control {
	/* How many pages shrink_list() should reclaim */
	unsigned long nr_to_reclaim;

	/*
	 * Nodemask of nodes allowed by the caller. If NULL, all nodes
	 * are scanned.
	 */
	nodemask_t	*nodemask;

	/* Writepage batching in laptop mode; RECLAIM_WRITE */
	unsigned int may_writepage:1;

	/* Can mapped pages be reclaimed? */
	unsigned int may_unmap:1;

	/* Can pages be swapped as part of reclaim? */
	unsigned int may_swap:1;

	/* determine whether it should promote NVPC pages only */
	unsigned int nvpc_promote:1;

	/* Allocation order */
	s8 order;

	/* Incremented by the number of inactive pages that were scanned */
	unsigned long nr_scanned;

	/* Number of pages freed so far during a call to shrink_zones() */
	unsigned long nr_reclaimed;

	struct {
		unsigned int dirty;
		unsigned int unqueued_dirty;
		unsigned int congested;
		unsigned int writeback;
		unsigned int immediate;
		unsigned int file_taken;
		unsigned int taken;
	} nr;

};

static unsigned int promote_pages_from_nvpc(struct list_head *nvpc_pages)
{
	unsigned int nr_succeeded;
	struct nvpc *nvpc;
	int err;

	nvpc = get_nvpc();

	if (list_empty(nvpc_pages))
		return 0;

	// read_lock_irq(&nvpc->meta_lock);
	if (!nvpc->enabled && !nvpc->extend_lru)
	{
		nr_succeeded = 0;
		// read_unlock_irq(&nvpc->meta_lock);
		goto out;
	}
	
	// NVTODO: confirm all params
	err = migrate_pages(nvpc_pages, nvpc_alloc_promote_page, NULL, 0, 
						MIGRATE_ASYNC, MR_NVPC_LRU_PROMOTE, &nr_succeeded);
	// read_unlock_irq(&nvpc->meta_lock);

	// if (current_is_kswapd())
	// 	__count_vm_events(PGNVPC_PROMOTE_KSWAPD, nr_succeeded);
	// else
	// 	__count_vm_events(PGNVPC_PROMOTE_DIRECT, nr_succeeded);

out:
	return nr_succeeded;
}

static bool nvpc_can_promote(int nid, struct scan_control *sc) 
{
	/* NVPC is independent to NUMA subsystem, switch can not be used here */
	// if (!numa_demotion_enabled)
	// 	return false;
	if (sc) {
		if (sc->nvpc_promote)
			return true;
		/* It is pointless to do demotion in memcg reclaim */
		/* NVPC has no relation to memcg reclaim */
		// if (cgroup_reclaim(sc))
		// 	return false;
	}
	return false;
}

/*
 * nvpc_move_pages_to_lru() moves pages from private @list to appropriate LRU list.
 * On return, @list is reused as a list of pages to be freed by the caller.
 *
 * Returns the number of pages moved to the given lruvec.
 */
static unsigned int nvpc_move_pages_to_lru(struct lruvec *lruvec,
				      struct list_head *list)
{
	int nr_pages, nr_moved;
	LIST_HEAD(pages_to_free);
	struct page *page;

	nr_pages = 0;
	nr_moved = 0;

	while (!list_empty(list)) {
		page = lru_to_page(list);
		VM_BUG_ON_PAGE(PageLRU(page), page);
		list_del(&page->lru);
		if (unlikely(!page_evictable(page))) {
			spin_unlock_irq(&lruvec->lru_lock);
			putback_lru_page(page);
			spin_lock_irq(&lruvec->lru_lock);
			continue;
		}

		SetPageLRU(page);

		if (unlikely(put_page_testzero(page))) {
			__clear_page_lru_flags(page);

			if (unlikely(PageCompound(page))) {
				spin_unlock_irq(&lruvec->lru_lock);
				destroy_compound_page(page);
				spin_lock_irq(&lruvec->lru_lock);
			} else
				list_add(&page->lru, &pages_to_free);

			continue;
		}

		/*
		 * All pages were isolated from the same lruvec (and isolation
		 * inhibits memcg migration).
		 */
		VM_BUG_ON_PAGE(!page_matches_lruvec(page, lruvec), page);
		add_page_to_lru_list(page, lruvec);
		nr_pages = thp_nr_pages(page);
		nr_moved += nr_pages;
		if (PageActive(page))
			workingset_age_nonresident(lruvec, nr_pages);
	}

	/*
	 * To save our caller's stack, now use input list for pages to free.
	 */
	list_splice(&pages_to_free, list);

	return nr_moved;
}

unsigned int promote_page_list(struct list_head *page_list,
				     struct pglist_data *pgdat,
				     struct scan_control *sc,
				     bool ignore_references)
{
	LIST_HEAD(ret_pages);
	LIST_HEAD(nvpc_promote_pages);

	unsigned int nr_reclaimed = 0;

	bool do_promote_pass;
	bool do_nvpc_pass;
	struct nvpc *nvpc = get_nvpc();
	/* judge only by sc->nvpc_promote */
	/* If promotion is set, other operations will not be done */
	do_promote_pass = nvpc_can_promote(pgdat->node_id, sc); // exclusive promotion
	do_nvpc_pass = nvpc->enabled && nvpc->extend_lru;

	cond_resched();

retry:
	while (!list_empty(page_list)) {
		struct page *page;

		cond_resched();

		// get page from LRU, and del this page from LRU
		// to avoid some error
		page = lru_to_page(page_list);
		list_del(&page->lru);

		if (!trylock_page(page)) // whether this page is locked by others
		{
			pr_info("[KNVPCD WARN] trylock page failed! Keep it from promotion.\n"); // NVTODO: check whether this is a bug
			goto keep;
		}

		VM_BUG_ON_PAGE(PageActive(page), page); // must be inactive

		/* Account the number of base pages even though THP */
		sc->nr_scanned += compound_nr(page);

		VM_BUG_ON_PAGE(!PageNVPC(page), page);
		VM_BUG_ON_PAGE(PageAnon(page), page); // must be file-backed
		VM_BUG_ON_PAGE(PageSwapBacked(page), page); // must be file-backed
		VM_BUG_ON_PAGE(PageTransHuge(page), page); // huge pages not support yet
		VM_BUG_ON_PAGE(PageCompound(page), page); // huge pages not support yet
		VM_BUG_ON_PAGE(page_mapped(page), page); // page should not be mmapped, keep it in DRAM
		VM_BUG_ON_PAGE(!page_is_file_lru(page), page); // page should be in file LRU

		/* NVPC promotion */
		if (do_nvpc_pass && PageNVPC(page) && do_promote_pass) {
			u8 nvpc_lru_cnt = page_nvpc_lru_cnt(page);
			if (get_nvpc()->promote_level && nvpc_lru_cnt >= get_nvpc()->promote_level) {
				list_add(&page->lru, &nvpc_promote_pages);
				unlock_page(page);
				continue;
				// NVTODO: deal with workingset?
			}
		}

		unlock_page(page);
keep:
		list_add(&page->lru, &ret_pages);
		VM_BUG_ON_PAGE(PageLRU(page) || PageUnevictable(page), page);
	}
	/* 'page_list' is always empty here */

	/* Promote: Migrate NVPC pages to DRAM lru */
	if (do_promote_pass) {
		nr_reclaimed += promote_pages_from_nvpc(&nvpc_promote_pages);
		if (!list_empty(&nvpc_promote_pages))
		{
			list_splice_init(&nvpc_promote_pages, page_list);
			// do_nvpc_pass = false;
			goto retry; // No other methods can be used
		}
	}

	list_splice(&ret_pages, page_list); // page left unprocessed

	return nr_reclaimed;
}


static int too_many_isolated_nvpc(struct pglist_data *pgdat)
{
	unsigned long nvpc_lru, isolated;

	if (current_is_kswapd())
		return 0;

	nvpc_lru = node_page_state(pgdat, NR_NVPC_FILE);
	isolated = node_page_state(pgdat, NR_ISOLATED_NVPC);

	return nvpc_lru > isolated;
}

// NVTODO: nr_to_scan is not used
static unsigned long
shrink_nvpc_list(struct lruvec *lruvec, struct scan_control *sc)
{
	LIST_HEAD(page_list);
	unsigned long nr_reclaimed;
	unsigned long nr_taken;
	struct pglist_data *pgdat = lruvec_pgdat(lruvec);
	bool stalled = false;

	while (unlikely(too_many_isolated_nvpc(pgdat))) {
		if (stalled)
			return 0;

		/* wait a bit for the reclaimer. */
		msleep(100);
		stalled = true;

		/* We are about to die and free our memory. Return now. */
		if (fatal_signal_pending(current))
			return SWAP_CLUSTER_MAX;
	}

	/* in fact, nvpc pages will never be inside a pvec, so we don't drain */
	// lru_add_drain();

	spin_lock_irq(&lruvec->lru_lock);
	nr_taken = nvpc_promote_vec_isolate(&page_list); // NVTODO: change from isolate_lru_pages()
	__mod_node_page_state(pgdat, NR_ISOLATED_NVPC, nr_taken);
	spin_unlock_irq(&lruvec->lru_lock);

	if (nr_taken == 0)
		return 0;

	spin_lock_irq(&lruvec->lru_lock); // NVTEST: lock
	nr_reclaimed = promote_page_list(&page_list, pgdat, sc, false);
	spin_unlock_irq(&lruvec->lru_lock); // NVTEST: unlock here

	spin_lock_irq(&lruvec->lru_lock);
	nr_taken = nvpc_move_pages_to_lru(lruvec, &page_list);
	__mod_node_page_state(pgdat, NR_ISOLATED_NVPC, -nr_taken);
	spin_unlock_irq(&lruvec->lru_lock);

	// NVTODO: free nvpc pages that we won't use here?

	pr_info("[knvpcd nr info] nr_reclaimed: %lu, nr_taken: %lu\n", 
			nr_reclaimed, nr_taken);
	return nr_reclaimed;
}

static int do_knvpcd_work(struct nvpc * nvpc, pg_data_t* pgdat)
{
    int ret;

    // NVTODO: Evict pages from NVPC to Disk
    if (READ_ONCE(nvpc->knvpcd_evict))
    {
        // NVTODO: evict
    }

    // Demote pages from DRAM to NVPC
    if (READ_ONCE(nvpc->knvpcd_demote))
    {
		// NVTODO: forced demote
		unsigned int order;
		gfp_t gfp_mask;
		struct zone * zone; // for iter 
		struct zoneref * zref; // for iter
		struct zonelist *zonelist;
		enum zone_type highest_zoneidx;

		gfp_mask = GFP_KERNEL | __GFP_KSWAPD_RECLAIM; // NVTODO: review needed
		highest_zoneidx = ZONE_NORMAL; // NVTODO: review needed
				
		zonelist = node_zonelist(NUMA_NO_NODE, gfp_mask); // without any preferences of NUMA selection
		order = order_base_2(READ_ONCE(nvpc->knvpcd_nr_to_promote));

		for_each_zone_zonelist_nodemask(zone, zref, zonelist, highest_zoneidx, NULL)
		{
			wakeup_kswapd(zone, gfp_mask, order, highest_zoneidx);
		}

    }

    // NVTODO: call demotion if DRAM usage reaches watermark
    if (0)
    {
        // NVTODO: demote
    }

    // Promote pages from NVPC to DRAM
    if (READ_ONCE(nvpc->knvpcd_promote))
    {
	    struct lruvec *target_lruvec;
	    unsigned long nr_to_promote = READ_ONCE(nvpc->knvpcd_nr_to_promote);
	    unsigned long nr_reclaimed = 0;
	    unsigned long nr_to_scan;
	    struct blk_plug plug;
        struct scan_control sc = {
            .nvpc_promote = 1, // only promotion can be executed
	    };

        nr_to_scan = READ_ONCE(nvpc->knvpcd_nr_to_promote); // NVTODO: determine how many pages

	    target_lruvec = mem_cgroup_lruvec(NULL, pgdat);
	    // NVTODO: reclaim should be execute in this block (in the same way as kswapd)
	    
        blk_start_plug(&plug);

	    while (nr_to_scan) {
			unsigned int nr_reclaimed_this_time;
		    nr_to_scan -= min(nr_to_scan, SWAP_CLUSTER_MAX);
            nr_reclaimed_this_time = shrink_nvpc_list(target_lruvec, &sc);
			nr_reclaimed += nr_reclaimed_this_time;

			printk(KERN_WARNING "[NVPC DEBUG] nr_reclaimed_this_time: %u\n", nr_reclaimed_this_time);

            cond_resched();

			if (!nr_reclaimed_this_time) // no way to promote more
				break;

            if (nr_reclaimed < nr_to_promote)
                continue;
	    }

        blk_finish_plug(&plug);
	    
        if (!ret)
		    pr_warn("[knvpcd_warn] nvpc_promote failed!\n");
    }

    return 0;
}


/*
 * The background pageout daemon, started as a kernel thread
 * from the init process.
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
static int knvpcd(void *p)
{
	// NVTODO: we need to write back dirty pages in NVPC ***
	pg_data_t *pgdat = (pg_data_t *)p;
	struct task_struct *tsk = current;
	int ret;
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);
	wait_queue_entry_t wait;

	init_waitqueue_entry(&wait, current);

	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(tsk, cpumask);

	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "knvpcd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (knvpcd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD;
	set_freezable();

    WRITE_ONCE(nvpc.knvpcd_nr_to_reclaim, SWAP_CLUSTER_MAX);
    WRITE_ONCE(nvpc.knvpcd_nr_to_promote, SWAP_CLUSTER_MAX);

	while (!kthread_should_stop()) {
		nvpc.knvpcd_should_run = 0;
		wait_event_freezable(nvpc.knvpcd_wait, nvpc.knvpcd_should_run);

		WRITE_ONCE(nvpc.knvpcd_nr_to_reclaim, SWAP_CLUSTER_MAX);
		WRITE_ONCE(nvpc.knvpcd_nr_to_promote, SWAP_CLUSTER_MAX);

        ret = do_knvpcd_work(&nvpc, pgdat);

        if (!ret)
            pr_warn("[knvpcd_warn] do nvpc failed!\n");

        // NVTODO: undone trace mm
		// trace_mm_vmscan_knvpcd_wake(pgdat->node_id, alloc_order);
	}

	tsk->flags &= ~(PF_MEMALLOC | PF_SWAPWRITE | PF_KSWAPD);

	return 0;
}

/*
 * A zone is low on free memory or too fragmented for high-order memory.  If
 * knvpcd should reclaim (direct reclaim is deferred), wake it up for the zone's
 * pgdat.  It will wake up kcompactd after reclaiming memory.  If knvpcd reclaim
 * has failed or is not needed, still wake up kcompactd if only compaction is
 * needed.
 */
void wakeup_knvpcd(int nvpc_promote, int nvpc_demote, int nvpc_evict)
{
    if (!nvpc.enabled)
	{
		return;
	}

	if (!waitqueue_active(&nvpc.knvpcd_wait))
		return;

    if (READ_ONCE(nvpc.knvpcd_promote) < nvpc_promote)
        WRITE_ONCE(nvpc.knvpcd_promote, nvpc_promote);

    if (READ_ONCE(nvpc.knvpcd_demote) < nvpc_demote)
        WRITE_ONCE(nvpc.knvpcd_demote, nvpc_demote);

    if (READ_ONCE(nvpc.knvpcd_evict) < nvpc_evict)
        WRITE_ONCE(nvpc.knvpcd_evict, nvpc_evict);

    // NVTODO: undone
	// trace_mm_vmscan_wakeup_knvpcd(0, ZONE_NORMAL, order, gfp_flags);

	WRITE_ONCE(nvpc.knvpcd_should_run, 1); // knvpcd start condition

	wake_up_interruptible(&nvpc.knvpcd_wait);
}
EXPORT_SYMBOL_GPL(wakeup_knvpcd);


/** 
 * NVTODO: NVPC start on boot
 * 
 * rely on system booting to start nvpc
 */
void knvpcd_run(void)
{
	pg_data_t *pgdat = NODE_DATA(0);

	if (nvpc.knvpcd)
		return;

	nvpc.knvpcd = kthread_run(knvpcd, pgdat, "knvpcd%d", 0);
	if (IS_ERR(nvpc.knvpcd)) {
		/* failure at boot is fatal */
		BUG_ON(system_state < SYSTEM_RUNNING);
		pr_err("[knvpcd error] Failed to start knvpcd on node 0\n");
		nvpc.knvpcd = NULL;
	}
}
EXPORT_SYMBOL_GPL(knvpcd_run);

/**
 * NVTODO: It should never be used, because we don't support multi-node
 * 
 */
void knvpcd_stop(void)
{
	return;
}
EXPORT_SYMBOL_GPL(knvpcd_stop);

void knvpcd_lazy_init(void)
{
	if (!waitqueue_active(&nvpc.knvpcd_wait))
		init_waitqueue_head(&nvpc.knvpcd_wait);
	if (!waitqueue_active(&nvpc.pmemalloc_wait))
    	init_waitqueue_head(&nvpc.pmemalloc_wait);

    knvpcd_run();
}
EXPORT_SYMBOL_GPL(knvpcd_lazy_init);

static int __init knvpcd_init(void)
{
    init_waitqueue_head(&nvpc.knvpcd_wait);
    init_waitqueue_head(&nvpc.pmemalloc_wait);

    knvpcd_run();
	return 0;
}
module_init(knvpcd_init);