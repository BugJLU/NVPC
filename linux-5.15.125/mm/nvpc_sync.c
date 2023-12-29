#include <linux/fs.h>
#include <linux/libnvdimm.h>
#include <linux/prefetch.h>
#include <linux/nvpc_rw.h>
#include <linux/nvpc_sync.h>
#include <linux/nvpc.h>

// #define pr_debug pr_info

struct nvpc_sync nvpc_sync;

void init_sync_absorb_area(void)
{
    nvpc_sync.super_log_0 = nvpc_get_addr_pg(0);
    // pr_debug("[NVPC DEBUG]: sl0: %#llx\n", nvpc_sync.super_log_0);

    memset(nvpc_sync.super_log_0, 0, PAGE_SIZE);
    ((first_head_entry*)(nvpc_sync.super_log_0))->magic = NVPC_LOG_HEAD_MAGIC;
    arch_wb_cache_pmem(nvpc_sync.super_log_0, PAGE_SIZE);
    nvpc_write_commit();

    mutex_init(&nvpc_sync.super_log_lock);
}

typedef bool(*log_head_iterator)(nvpc_sync_head_entry *, void *);

/* walk through super log without lock */
static nvpc_sync_head_entry *walk_log_heads(log_head_iterator it, void *opaque)
{
    void *current_superlog = nvpc_sync.super_log_0;
    nvpc_sync_head_entry* log_head_i; // = (nvpc_sync_head_entry*)current_superlog;

    // pr_debug("[NVPC DEBUG]: walk_log_heads entsz: %ld\n", sizeof(nvpc_sync_head_entry));
    // pr_debug("[NVPC DEBUG]: walk_log_heads sl0: %#llx\n", nvpc_sync.super_log_0);

    /* first entry is for magic num */
    log_head_i = (nvpc_sync_head_entry*)current_superlog + 1;
    // pr_debug("[NVPC DEBUG]: walk_log_heads ent1: %#llx\n", log_head_i);

walk_page:
    // pr_debug("[NVPC DEBUG]: walk_log_heads ---page--- upper: %#llx\n", current_superlog + PAGE_SIZE - sizeof(nvpc_sync_head_entry));
    while ((void*)log_head_i < 
           current_superlog + PAGE_SIZE - sizeof(nvpc_sync_head_entry))
    {
        // pr_debug("[NVPC DEBUG]: walk_log_heads lhi: %#llx\n", log_head_i);
        if (log_head_i->flags != NVPC_LOG_HEAD_FLAG_EMPTY)
            if (it(log_head_i, opaque))
                return log_head_i;

        log_head_i++;
    }

    if (NVPC_SL_HAS_NEXT(current_superlog))
    {
        current_superlog = NVPC_SL_NEXT(current_superlog);
        log_head_i = (nvpc_sync_head_entry*)current_superlog;
        goto walk_page;
    }
    
    return NULL;
}

/* get a new page from nvpc free list, then set 0 to it */
static void *create_new_log_page(void)
{
    void *kaddr;
    struct page *newpage = nvpc_get_new_page(NULL, 0);
    if (!newpage)
    {
        return NULL;
    }
    
    kaddr = page_to_virt(newpage);
    memset(kaddr, 0, PAGE_SIZE);
    arch_wb_cache_pmem(kaddr, PAGE_SIZE);
    // nvpc_write_commit();
    return kaddr;
}

static size_t create_n_new_log_pages(struct list_head *pages, size_t n)
{
    void *kaddr;
    struct page *page;
    
    size_t n_get = nvpc_get_n_new_page(pages, n);
    if (n_get < n)
    {
        nvpc_free_pages(pages);
        return 0;
    }
    
    list_for_each_entry(page, pages, lru) {
        kaddr = page_to_virt(page);
        memset(kaddr, 0, PAGE_SIZE);
        arch_wb_cache_pmem(kaddr, PAGE_SIZE);
    }
    return n_get;
}

/* get an empty log head from superlogs. no lock */
static nvpc_sync_head_entry *get_log_head_empty(void)
{
    void *current_superlog = nvpc_sync.super_log_0;
    nvpc_sync_head_entry* log_head_i; // = (nvpc_sync_head_entry*)current_superlog;

    /* first entry is for magic num */
    log_head_i = (nvpc_sync_head_entry*)current_superlog + 1;

walk_page:
    while ((void*)log_head_i < 
           current_superlog + PAGE_SIZE - sizeof(nvpc_sync_head_entry))
    {
        if (log_head_i->flags == NVPC_LOG_HEAD_FLAG_EMPTY)
            return log_head_i;
        
        log_head_i++;
    }

    if (NVPC_SL_HAS_NEXT(current_superlog))
    {
        current_superlog = NVPC_SL_NEXT(current_superlog);
        log_head_i = (nvpc_sync_head_entry*)current_superlog;
        goto walk_page;
    }

    // else: create a new sl, and return the log head on the new sl
    else
    {
        next_sl_page_entry* ne = NVPC_SL_ENTRY_NEXT(current_superlog);
        ne->flags = NVPC_LOG_HEAD_FLAG_NEXT;
        ne->next_sl_page = current_superlog = create_new_log_page();
        if (!current_superlog)
        {
            return NULL;
        }
        
        arch_wb_cache_pmem((void*)ne, sizeof(next_sl_page_entry));
        // nvpc_write_commit();

        return (nvpc_sync_head_entry *)current_superlog;
    }
}

/* iterator to find a log head for an inode */
static bool __find_log_inode_head_it(nvpc_sync_head_entry *ent, void *opaque)
{
    struct inode *ino = (struct inode *)opaque;
    log_inode_head_entry* lent = (log_inode_head_entry*)ent;
    return lent->s_dev == ino->i_sb->s_dev && lent->i_ino == ino->i_ino;
}

/* create the log and set up the log head */
static int init_log_inode_head(log_inode_head_entry * loghead, struct inode *inode)
{
    void *newlogpg;

    loghead->head_log_page = newlogpg = create_new_log_page();
    if (!newlogpg)
    {
        return -1;
    }
    loghead->s_dev = inode->i_sb->s_dev;
    loghead->i_ino = inode->i_ino;
    /* first entry in the new page */
    loghead->committed_log_tail = (nvpc_sync_log_entry *)newlogpg;
    
    arch_wb_cache_pmem((void*)loghead, sizeof(log_inode_head_entry));
    // nvpc_write_commit();
    return 0;
}

/*
 * create_log_head()
 * Walk through the superlog pages, find if the log head of inode 
 * is already in superlog. If not, alloc a new log_head.
 * Returns the log head of the inode.
 */
static log_inode_head_entry *create_log_head(struct inode *inode)
{
    log_inode_head_entry *loghead;

    if (mutex_lock_interruptible(&nvpc_sync.super_log_lock) != 0)
    {
        return NULL;
    }

    loghead = (log_inode_head_entry *)walk_log_heads(__find_log_inode_head_it, (void*)inode);
    // pr_debug("[NVPC DEBUG]: walk_log_heads result: %#llx\n", loghead);
    if (loghead)
        return loghead;
    
    loghead = (log_inode_head_entry *)get_log_head_empty();
    // pr_debug("[NVPC DEBUG]: get_log_head_empty result: %#llx\n", loghead);
    if (!loghead)
        return NULL;
    
    if (init_log_inode_head(loghead, inode)) {
        /* for get_log_head_empty */
        nvpc_write_commit();
        mutex_unlock(&nvpc_sync.super_log_lock);
        return NULL;
    }
    
    nvpc_write_commit();
    mutex_unlock(&nvpc_sync.super_log_lock);
    return loghead;
}

log_inode_head_entry *nvpc_get_log_inode(struct inode *inode)
{
    spin_lock(&inode->i_lock);
    if (!inode->nvpc_sync_ilog.log_head)
    {
        log_inode_head_entry *lh;
        spin_unlock(&inode->i_lock);
        lh = create_log_head(inode);
        if (!lh)
            return NULL;
        
        spin_lock(&inode->i_lock);
        inode->nvpc_sync_ilog.log_head = lh;
        inode->nvpc_sync_ilog.log_tail = lh->committed_log_tail;
        inode->nvpc_sync_ilog.latest_logged_attr = NULL;
    }
    spin_unlock(&inode->i_lock);
    return inode->nvpc_sync_ilog.log_head;
}

typedef bool(*log_iterator)(nvpc_sync_log_entry *, void *);

/*
 * walk the log from one entry to another
 *
 * from: the start entry, cannot be NULL
 * to: the final entry, inclusive, can be NULL. if set to NULL, walk will stop 
 *      at the end of the last log page
 */
static nvpc_sync_log_entry *walk_log(nvpc_sync_log_entry *from, 
                            nvpc_sync_log_entry *to, 
                            log_iterator it, void *opaque, bool write)
                            /* 
                             * According to our test, prefetch is faster for reads 
                             * and slower for writes on pmem. So we use prefetch 
                             * when write is false, and do nothing when write is true.
                             */
{
    nvpc_sync_log_entry *current_log = from;

    /* loop until current_log moves to the last page */
    while (((uintptr_t)current_log & PAGE_MASK) != 
            ((uintptr_t)to & PAGE_MASK))
    {
        /* loop in one page */
        while ((uintptr_t)current_log < 
                (uintptr_t)NVPC_LOG_ENTRY_NEXT(current_log))
        {
            // prefetch next cl if next entry is aligned
            if (!(((uintptr_t)(current_log+1))%L1_CACHE_BYTES))
            {
                (write) ? 
                    0 : 
                    prefetch(current_log+1);
            }
            // prefetch next page if next entry is the last
            if ((uintptr_t)(current_log+1) == (uintptr_t)NVPC_LOG_ENTRY_NEXT(current_log) && 
                NVPC_LOG_HAS_NEXT(current_log))
            {
                (write) ? 
                    0 : 
                    prefetch((void*)NVPC_LOG_NEXT(current_log));
            }

            if (it(current_log, opaque))
                return current_log;
            
            current_log++;
        }
        
        /* already at the last page */
        if(!NVPC_LOG_HAS_NEXT(current_log))
            goto out;
            
        /* move to the next page */
        current_log = (nvpc_sync_log_entry *)NVPC_LOG_NEXT(current_log);
    }
    
    /* walk the last page */
    while (current_log <= to)
    {
        if (!(((uintptr_t)current_log)%L1_CACHE_BYTES))
            (write) ? 
                0 : 
                prefetch(current_log+1);

        if (it(current_log, opaque))
            return current_log;

        current_log++;
    }
out:
    return current_log;
}

/* 
 * Append the inode log for n entries.
 * Returns the first free log entry that can be written to.
 * This can only happen when the data or metadata is updated on the 
 * inode. When this happen, the i_rwsem ensures that only one thread
 * is advancing this lock. So we don't need any lock here. 
 * NVTODO: make this available for concurrent access with atomic ops.
 */
static nvpc_sync_log_entry *append_inode_n_log(struct inode *inode, size_t n_entries)
{
    // NVXXX: prealloc many pages? or one by one?
    nvpc_sync_log_entry *ent = inode->nvpc_sync_ilog.log_tail;
    nvpc_sync_log_entry *ent_old = ent;
    int ents_left = NVPC_PAGE_LOG_ENTRIES_LEFT(ent);

    // walk & check if there are already enough log entries
    while (NVPC_LOG_HAS_NEXT(ent))
    {
        ent = (nvpc_sync_log_entry *)NVPC_LOG_NEXT(ent);
        ents_left += NVPC_PAGE_LOG_ENTRIES;
    }
    

    /* we need more log pages, prepare them here */
    if (n_entries > ents_left)
    {
        size_t need_pages = (n_entries - ents_left + NVPC_PAGE_LOG_ENTRIES - 1) / NVPC_PAGE_LOG_ENTRIES;
        void *last_page;
        int tail_new_offset;
        if (need_pages > 1) /* more than one page, should not happen */
        {
            struct list_head pages;
            struct page *page;
            void *kaddr;
            nvpc_next_log_entry *nextent;
            INIT_LIST_HEAD(&pages);
            if (create_n_new_log_pages(&pages, need_pages) < need_pages)
            {
                /* cannot alloc more pages for the log */
                return NULL;
            }
            nextent = NVPC_LOG_ENTRY_NEXT(ent);
            list_for_each_entry(page, &pages, lru) {
                kaddr = page_to_virt(page);
                nextent->next_log_page = (uintptr_t)kaddr;
                nextent->raw.flags = NVPC_LOG_FLAG_NEXT;
                arch_wb_cache_pmem((void*)nextent, sizeof(nvpc_next_log_entry));
                nextent = NVPC_LOG_ENTRY_NEXT(kaddr);
            }
            last_page = kaddr;
        }
        else /* only one page needed */
        {
            void *newpage;
            nvpc_next_log_entry *nextent;
            newpage = create_new_log_page();
            if (!newpage)
                return NULL;
            
            nextent = NVPC_LOG_ENTRY_NEXT(ent);
            nextent->next_log_page = (uintptr_t)newpage;
            nextent->raw.flags = NVPC_LOG_FLAG_NEXT;
            arch_wb_cache_pmem((void*)nextent, sizeof(nvpc_next_log_entry));
            last_page = newpage;
        }
        // nvpc_write_commit();
        tail_new_offset = (n_entries - ents_left) % NVPC_PAGE_LOG_ENTRIES;
        /* preset log_tail to preserve n entries */
        inode->nvpc_sync_ilog.log_tail = (nvpc_sync_log_entry*)last_page + tail_new_offset;
    }
    else
    {
        ent = ent_old;
        
        /* preset log_tail to preserve n entries */
        while (n_entries)
        {
            size_t this_page_ents = min_t(size_t, NVPC_PAGE_LOG_ENTRIES_LEFT(ent), n_entries);
            n_entries -= this_page_ents;
            ent += this_page_ents;
            if (n_entries)
                ent = (nvpc_sync_log_entry *)NVPC_LOG_NEXT(ent);
        }
        
        inode->nvpc_sync_ilog.log_tail = ent;
    }

    /* return old tail as the first log to write to */
    if (ent_old == (nvpc_sync_log_entry*)NVPC_LOG_ENTRY_NEXT(ent_old))
    {
        ent_old = (nvpc_sync_log_entry*)((nvpc_next_log_entry*)ent_old)->next_log_page;
    }
    
    return ent_old;
}

/*
 * Move alongside the log for n entries
 */
// nvpc_sync_log_entry *advance_inode_n_log(nvpc_sync_log_entry *from, size_t n_entries)
// {

// }

/* returns 0 on success */
int write_oop(struct inode *inode, struct page *page, loff_t file_off, uint16_t len, 
        nvpc_sync_log_entry **new_head, nvpc_sync_log_entry **new_tail)
{
    nvpc_sync_write_entry *ent;
    WARN_ON(!PageNVPC(page));
    // WARN_ON(inode->i_sb->s_nvpc_flags & SB_NVPC_ON);

    ent = (nvpc_sync_write_entry *)append_inode_n_log(inode, 1);
    if (!ent)
        return -ENOMEM;
    
    ent->raw.flags = NVPC_LOG_FLAG_WRITE;
    ent->raw.id = inode->nvpc_sync_ilog.log_cntr++;
    ent->file_offset = file_off;
    ent->data_len = len;
    ent->page_index = nvpc_get_off_pg(page_to_virt(page));
    arch_wb_cache_pmem((void*)ent, sizeof(nvpc_sync_write_entry));
    
    /* commit later in write_commit() */
    
    if (new_head) *new_head = (nvpc_sync_log_entry*)ent;
    /* warn: needs atomic under concurrency */
    if (new_tail) *new_tail = inode->nvpc_sync_ilog.log_tail;

    return 0;
}

// NVTODO: try to seperate copy and flush, use 2 walks
static bool __write_ioviter_to_log_it(nvpc_sync_log_entry *ent, void *opaque)
{
    struct iov_iter *from = (struct iov_iter *)opaque;
    _copy_from_iter_flushcache(ent, NVPC_LOG_ENTRY_SIZE, from);
    // WARN_ON(wrsz > NVPC_LOG_ENTRY_SIZE);
    return false;
}

/* returns 0 on success */
int write_ip(struct inode *inode, struct iov_iter *from, loff_t file_off, 
        nvpc_sync_log_entry **new_head, nvpc_sync_log_entry **new_tail)
{
    size_t len = iov_iter_count(from);
    size_t nentries = (len + NVPC_LOG_ENTRY_SIZE - 1) / NVPC_LOG_ENTRY_SIZE + 1;
    nvpc_sync_write_entry *ent0;
    nvpc_sync_log_entry *last;
    // WARN_ON(inode->i_sb->s_nvpc_flags & SB_NVPC_ON);
    ent0 = (nvpc_sync_write_entry *)append_inode_n_log(inode, nentries);
    if (!ent0)
        return -ENOMEM;
    
    last = walk_log((nvpc_sync_log_entry *)(ent0+1), inode->nvpc_sync_ilog.log_tail-1, 
                __write_ioviter_to_log_it, from, true);
    WARN_ON(last != inode->nvpc_sync_ilog.log_tail);
    
    ent0->raw.id = inode->nvpc_sync_ilog.log_cntr++;
    ent0->raw.flags = NVPC_LOG_FLAG_WRITE;
    ent0->file_offset = file_off;
    ent0->data_len = len;
    ent0->page_index = 0;
    arch_wb_cache_pmem((void*)ent0, sizeof(nvpc_sync_write_entry));

    /* commit later in write_commit() */

    if (new_head) *new_head = (nvpc_sync_log_entry*)ent0;
    /* warn: needs atomic under concurrency */
    if (new_tail) *new_tail = inode->nvpc_sync_ilog.log_tail;

    return 0;
}

void write_commit(struct inode *inode, nvpc_sync_log_entry *old_tail, nvpc_sync_log_entry *tail)
{
    /* spin here, wait for the last log to finish, useless now */
    /* needs to be atomic if we have concurrency here */
    // while (inode->nvpc_sync_ilog.log_head->committed_log_tail != old_tail)
    // {
    //     ;
    // }
    if (inode->nvpc_sync_ilog.log_head->committed_log_tail != old_tail)
    {
        WARN(1, "[NVPC BUG]: log tail not match! expected_tail: %px, committed_tail: %px\n", old_tail, inode->nvpc_sync_ilog.log_head->committed_log_tail);
    }
    
    
    inode->nvpc_sync_ilog.log_head->committed_log_tail = tail;
    arch_wb_cache_pmem(
        (void*)&inode->nvpc_sync_ilog.log_head->committed_log_tail, 
        sizeof(nvpc_sync_write_entry*)
    );
    // NVTODO: when to remove I_NVPC_DATA? maybe the cleanup thread
    inode->i_state &= I_NVPC_DATA;
    
    nvpc_write_commit();
}

struct nvpc_sync_transaction_part
{
    nvpc_sync_write_entry* ent; // start entry of this trans
    struct page *page;          // oop page, NULL for ip
    nvpc_sync_write_entry* _oldent; 
    struct page *_oldpage;
    bool free_n_mark;
    bool rollback;
    loff_t file_off;
    // struct xa_state xas;
};

struct nvpc_sync_transaction
{
    int count;
    struct inode *inode;
    struct nvpc_sync_transaction_part *parts;
};

// e.g. (oop->ip->...->ip)->oop, mark and free those in the bracket
static inline void __nvpc_mark_free_chained_entries(nvpc_sync_write_entry *last)
{
    nvpc_sync_write_entry *curr = last;
    // loop while current is ip
    while (!curr->page_index)
    {
        // mark entry as expired
        curr->raw.flags = NVPC_LOG_FLAG_WREXP;
        arch_wb_cache_pmem(curr, sizeof(nvpc_sync_write_entry*));

        // current ip has previous write link
        if (curr->last_write) 
            curr = (nvpc_sync_write_entry *)nvpc_get_addr(curr->last_write);
        // done if this is the first ip
        else 
            return;
    }

    // mark entry as expired
    curr->raw.flags = NVPC_LOG_FLAG_WREXP;
    arch_wb_cache_pmem(curr, sizeof(nvpc_sync_write_entry*));
    // free the old oop page
    nvpc_free_page(virt_to_page(nvpc_get_addr_pg(curr->page_index)), 0);
}

/* 
 * commit
 *
 * Go through transaction parts, search inode->nvpc_sync_ilog.inode_log_pages
 * for previous entry, remove that page (if possible), mark the entry as expire, 
 * then update inode_log_pages to current entry.
 * If fail anywhere, return error
 */
static inline int nvpc_sync_commit_transaction(struct nvpc_sync_transaction *trans)
{
    int i;
    nvpc_sync_write_entry *prev_ent;

    XA_STATE(ilog_xas, &trans->inode->nvpc_sync_ilog.inode_log_pages, 0);

    xas_lock_bh(&ilog_xas);

    // try xa_store first, because it may fail
    for (i = 0; i <= trans->count; i++)
    {
        if (!trans->parts[i].ent)  // last part, do nothing
            continue;
        
        pr_debug("[NVPC DEBUG]: sync commit: trans i %d\n", i);
        
        // prev_ent = xa_load(&trans->inode->nvpc_sync_ilog.inode_log_pages, 
        //     trans->parts[i].file_off >> PAGE_SHIFT);
        xas_set(&ilog_xas, trans->parts[i].file_off >> PAGE_SHIFT);
        do {
            xas_reset(&ilog_xas);
            prev_ent = xas_load(&ilog_xas);
        } while (xas_retry(&ilog_xas, prev_ent));
        
        if (prev_ent)
        {
            pr_debug("[NVPC DEBUG]: sync commit: prev off %llu len %llu\n", prev_ent->file_offset, (uint64_t)(prev_ent->data_len));
            if (!trans->parts[i].page)  // ip
            {
                // chain link to previous entry
                trans->parts[i].ent->last_write = nvpc_get_off(prev_ent);
                // don't free page and mark entry, they are still useful
                trans->parts[i]._oldpage = NULL;
                trans->parts[i]._oldent = prev_ent;
                trans->parts[i].free_n_mark = false;
                trans->parts[i].rollback = true;
                arch_wb_cache_pmem(trans->parts[i].ent, sizeof(nvpc_sync_write_entry*));
            }
            else    // oop
            {
                // previous is ip, go through the chain to mark and free
                if (prev_ent->page_index == 0)
                {
                    // TODO: this can be async
                    // no old page, follow the prev_ent to free the chain.
                    trans->parts[i]._oldpage = NULL;
                    trans->parts[i]._oldent = prev_ent;
                    trans->parts[i].free_n_mark = true;
                    trans->parts[i].rollback = true;
                }
                else // previous is oop, just mark and free that page
                {
                    // free the old page
                    void *oldpg_addr = nvpc_get_addr_pg(prev_ent->page_index);
                    struct page *oldpg = virt_to_page(oldpg_addr);
                    WARN_ON(!PageNVPC(oldpg));
                    /* only free the page if it is not in page cache and is not current page */
                    if (oldpg->mapping != trans->inode->i_mapping && oldpg != trans->parts[i].page)
                    {
                        // store old page / ent in the part, later we will free or recover this old page
                        trans->parts[i]._oldpage = oldpg;
                        trans->parts[i]._oldent = prev_ent;
                        trans->parts[i].free_n_mark = true;
                        trans->parts[i].rollback = true;
                    }
                    else
                    {
                        // no need to free page or mark entry, no xa rollback, we are most likely in relax mode
                        trans->parts[i]._oldpage = NULL;
                        trans->parts[i]._oldent = NULL;
                        trans->parts[i].free_n_mark = false;
                        trans->parts[i].rollback = false;
                    }
                }
            }
        }
        else
        {
            if (!trans->parts[i].page)  // ip and no previous, set last_write to 0
            {
                trans->parts[i].ent->last_write = 0;
                arch_wb_cache_pmem(trans->parts[i].ent, sizeof(nvpc_sync_write_entry*));
            }
            // do nothing in second pass; remove entry in xa rollback
            trans->parts[i]._oldpage = NULL;
            trans->parts[i]._oldent = NULL;
            trans->parts[i].free_n_mark = false;
            trans->parts[i].rollback = true;
        }
        
        // update xarray
        // ret = xa_store(&trans->inode->nvpc_sync_ilog.inode_log_pages, 
        //     trans->parts[i].file_off >> PAGE_SHIFT, trans->parts[i].ent, GFP_ATOMIC);
        do {
            xas_store(&ilog_xas, trans->parts[i].ent);
        } while (xas_nomem(&ilog_xas, GFP_ATOMIC));

        // if (xa_err(ret))
        //     goto err;
        if (xas_error(&ilog_xas))
            goto err;
        
    }
    // second pass, free old pages and mark old entries, this will never fail
    for (i = 0; i <= trans->count; i++)
    {
        if (trans->parts[i].free_n_mark)
        {
            // previous is oop, just mark and free
            if (trans->parts[i]._oldpage)
            {
                // mark previous entry as expired
                trans->parts[i]._oldent->raw.flags = NVPC_LOG_FLAG_WREXP;
                // free the old page
                nvpc_free_page(trans->parts[i]._oldpage, 0);
                arch_wb_cache_pmem(trans->parts[i]._oldent, sizeof(nvpc_sync_write_entry*));
            }
            // previous is ip, follow the chain to mark and free
            else
            {
                // e.g. (oop->ip->...->ip)->oop, mark and free those in the bracket
                __nvpc_mark_free_chained_entries(trans->parts[i]._oldent);
            }
            
        }
    }

    xas_unlock_bh(&ilog_xas);
    nvpc_write_commit();
    
    return 0;
err:
    // xa rollback
    for (; i >= 0; i--)
    {
        if (trans->parts[i].rollback)
        {
            xas_set(&ilog_xas, trans->parts[i].file_off >> PAGE_SHIFT);
            // if (trans->parts[i]._oldpage)
            // {
            //     // old entry exists, set it back
            //     // xa_store(&trans->inode->nvpc_sync_ilog.inode_log_pages, 
            //     //     trans->parts[i].file_off >> PAGE_SHIFT, trans->parts[i]._oldent, GFP_ATOMIC);
            //     xas_store(&ilog_xas, trans->parts[i]._oldent);
            // }
            // else
            // {
            //     // old entry doesn't exist, clear it
            //     // xa_erase(&trans->inode->nvpc_sync_ilog.inode_log_pages, 
            //     //     trans->parts[i].file_off >> PAGE_SHIFT);
            //     xas_store(&ilog_xas, NULL);
            // }
            xas_store(&ilog_xas, trans->parts[i]._oldent);
        }
    }

    xas_unlock_bh(&ilog_xas);
    
    return -1;
}

/* 
 * rollback
 *
 * Go through transaction parts, free each page (if not NULL).
 */
static inline void nvpc_sync_rollback_transaction(struct nvpc_sync_transaction *trans)
{
    int i;
    nvpc_sync_write_entry *prev_ent;

    for (i = 0; i <= trans->count; i++)
    {
        // XA_STATE(ilog_xas, &trans->inode->nvpc_sync_ilog.inode_log_pages, 0);
        if (!trans->parts[i].page)  // ip or last part, do nothing
            continue;
        
        // page cache page, do nothing
        if (trans->parts[i].page->mapping == trans->inode->i_mapping)
            continue;

        prev_ent = xa_load(&trans->inode->nvpc_sync_ilog.inode_log_pages, 
            trans->parts[i].file_off >> PAGE_SHIFT);
        
        // previous entry exists
        if (prev_ent)
        {
            // previous is oop
            if (prev_ent->page_index != 0)
            {
                // free the new page
                void *oldpg_addr = nvpc_get_addr_pg(prev_ent->page_index);
                struct page *oldpg = virt_to_page(oldpg_addr);
                WARN_ON(!PageNVPC(trans->parts[i].page));
                /* don't free the page if it is the previous page */
                if (oldpg == trans->parts[i].page)
                {
                    continue;
                }
            }
            // else, previous is ip, can free
        }
        // else, no previous entry, can free
        
        // free
        nvpc_free_page(trans->parts[i].page, 0);
    }
}

// do write log and update tail respectively, to support transaction

int nvpc_fsync_range(struct file *file, loff_t start, loff_t end, int datasync)
{
    // get the pages from start to end, decide to perform write ip or oop.
    // refer to generic_perform_write(), ext4_write_begin() and grab_cache_page_write_begin()
    
    size_t bytes_left = end - start + 1;
    size_t written = 0;
    loff_t pos = start;
    struct inode *inode = file->f_inode;
    struct address_space *mapping = file->f_mapping;
    struct nvpc_sync_transaction trans;
    nvpc_sync_log_entry *old_tail;
    nvpc_sync_log_entry *new_tail;
    int ret = 0;
    bool fail = false;
    uint32_t old_id;
    // bool drained = false;

    pr_debug("[NVPC DEBUG]: nvpc_fsync_range @i %lu start %lld end %lld ds %d\n", inode->i_ino, start, end, datasync);

    nvpc_get_log_inode(inode);

    // this can be a very big lock ... refer to nvpc_sync.h for a fine grained solution
    mutex_lock(&inode->nvpc_sync_ilog.log_lock);

    old_id = inode->nvpc_sync_ilog.log_cntr;

    /* NVNEXT: not safe under concurrency, use the old_tail value from the first write */
    old_tail = new_tail = inode->nvpc_sync_ilog.log_tail;

    pr_debug("[NVPC DEBUG]: nvpc_fsync_range 0\n");

    trans.count = 0;
    trans.inode = inode;
    trans.parts = (struct nvpc_sync_transaction_part*)kmalloc_array(
        (end-start)/PAGE_SIZE+2, sizeof(struct nvpc_sync_transaction_part), GFP_ATOMIC);
    if (!trans.parts)
    {
        // drained = true;
        goto fallback;
    }

    pr_debug("[NVPC DEBUG]: nvpc_fsync_range 1\n");

    do
    {
        struct page *page;
        loff_t offset;
        size_t bytes;
        pgoff_t index;

        /* page should be in the page cache. but maybe it's just evicted? */
        int fgp_flags = FGP_LOCK|FGP_CREAT;

        offset = (pos & (PAGE_SIZE - 1));
        bytes = min_t(unsigned long, PAGE_SIZE - offset, bytes_left);
        index = pos >> PAGE_SHIFT;
        
        trans.parts[trans.count].page = NULL;
        trans.parts[trans.count].ent = NULL;

        page = pagecache_get_page(mapping, index, fgp_flags, mapping_gfp_mask(mapping));
        /* don't need wait_for_stable_page(), because we don't write to the page */

        pr_debug("[NVPC DEBUG]: nvpc_fsync_range 2\n");

        // WARN_ON(!page);
        if (unlikely(!page))
        {
            pr_debug("[NVPC DEBUG]: nvpc_fsync_range (!page)\n");
            /* -ENOMEM */
            fail = true;
            // drained = true;
            goto out;
        }

        /* only persist dirty non-persisted pages in npvc */
        if (!PageNVPCNpDirty(page))
        {
            pr_debug("[NVPC DEBUG]: nvpc_fsync_range (!PageNVPCNpDirty(page))\n");
            goto out;
        }
        
        /* write it to the log */
        pr_debug("[NVPC DEBUG]: nvpc_fsync_range 3\n");

        /* page already inside nvpc */
        if (PageNVPC(page))
        {
            struct page *log_pg;

            /* if strict mode */
            if (IS_NVPC_STRICT(inode))
            {
                // malloc log_pg, copy page to log_pg; mark lazy write
                // log_pg = nvpc_get_new_page(NULL, 0);
                // if (!log_pg)
                //     goto fallback;
                // copy_highpage(log_pg, page);
                log_pg = page;
                SetPageNVPCPendingCopy(log_pg); // pending for lazy write
            }
            /* if relaxed mode */
            else
            {
                log_pg = page;
            }

            pr_debug("[NVPC DEBUG]: nvpc_fsync_range 4\n");

            // do not need to write anything because the data is already in page cache

            arch_wb_cache_pmem(page_to_virt(log_pg), PAGE_SIZE);
            if (write_oop(inode, log_pg, pos, (uint16_t)bytes, 
                (nvpc_sync_log_entry**)&trans.parts[trans.count].ent, &new_tail))
            {
                fail = true;
                // drained = true;
                goto out;
            }
            trans.parts[trans.count].page = log_pg;
            trans.parts[trans.count].file_off = pos;

            pr_debug("[NVPC DEBUG]: nvpc_fsync_range 5\n");
        }
        else 
        {
            struct page *nv_pg = NULL;
            /* relax mode */
            if (!IS_NVPC_STRICT(inode)) 
            {
                nvpc_sync_write_entry *prev_ent;
                loff_t pgoff;
                // search if page is already in nvm, write or malloc
                prev_ent = xa_load(&inode->nvpc_sync_ilog.inode_log_pages, index);
                if (prev_ent)
                {
                    pgoff = prev_ent->page_index;
                    if (pgoff)
                        nv_pg = virt_to_page(nvpc_get_addr_pg(pgoff));
                }
            }
            pr_debug("[NVPC DEBUG]: nvpc_fsync_range 6\n");

            /* relax, and page has been copied to nvm */
            if (nv_pg)
            {
                // just write to the found page
                struct iov_iter i;
                struct kvec kvec;
                size_t ret;
                kvec.iov_base = page_to_virt(page) + offset;
                kvec.iov_len = bytes;
                iov_iter_kvec(&i, READ, &kvec, 1, bytes);
                ret = nvpc_write_nv_iter(&i, nvpc_get_off(page_to_virt(nv_pg)+offset), true);
                if (unlikely(ret < bytes))
                {
                    fail = true;
                    goto out;
                }
                pr_debug("[NVPC DEBUG]: oop @0\n");
                if (write_oop(inode, nv_pg, pos, (uint16_t)bytes, 
                    (nvpc_sync_log_entry**)&trans.parts[trans.count].ent, &new_tail))
                {
                    fail = true;
                    // drained = true;
                    goto out;
                }
                trans.parts[trans.count].page = nv_pg;
                trans.parts[trans.count].file_off = pos;
            }
            /* strict mode or nv_pg not found */
            else 
            {
                /* bytes is less than ip threshold, and there is previous oop write */
                if (bytes < NVPC_IPOOP_THR && PageNVPCPin(page))
                {
                    struct iov_iter i;
                    struct kvec kvec;
                    kvec.iov_base = page_to_virt(page) + offset;
                    kvec.iov_len = bytes;
                    iov_iter_kvec(&i, READ, &kvec, 1, bytes);
                    pr_debug("[NVPC DEBUG]: ip @1\n");
                    if (write_ip(inode, &i, pos, 
                        (nvpc_sync_log_entry**)&trans.parts[trans.count].ent, &new_tail))
                    {
                        fail = true;
                        // drained = true;
                        goto out;
                    }
                    // no trans.parts[trans.count].page for ip write
                    trans.parts[trans.count].file_off = pos;
                }
                /* large write, or no previous write, oop */
                else
                {
                    struct page *log_pg;
                    
                    /* strict mode or relax mode */
                    log_pg = nvpc_get_new_page(NULL, 0);
                    if (!log_pg)
                    {
                        fail = true;
                        // drained = true;
                        goto out;
                    }
                    copy_highpage(log_pg, page);
                    arch_wb_cache_pmem(page_to_virt(log_pg), PAGE_SIZE);
                    pr_debug("[NVPC DEBUG]: oop @2\n");
                    // oop will add log_pg to nvpc index, migration later can use this info
                    if (write_oop(inode, log_pg, pos, (uint16_t)bytes, 
                        (nvpc_sync_log_entry**)&trans.parts[trans.count].ent, &new_tail))
                    {
                        fail = true;
                        // drained = true;
                        goto out;
                    }
                    trans.parts[trans.count].page = log_pg;
                    trans.parts[trans.count].file_off = pos;
                }
            }
        }

        pr_debug("[NVPC DEBUG]: nvpc_fsync_range 7\n");

        ClearPageNVPCNpDirty(page);
        SetPageNVPCPin(page);
        
out:
        pr_debug("[NVPC DEBUG]: nvpc_fsync_range 8 out\n");
        if (!page)
            goto fallback;
        
        unlock_page(page);
        put_page(page);

        if (fail)
            goto fallback;

        written += bytes;
        bytes_left -= bytes;
        pos += bytes;
        trans.count++;

    } while (bytes_left);
    trans.parts[trans.count].page = NULL; // last part set to NULL
    trans.parts[trans.count].ent = NULL;
    
    // NVTODO: sync the metadata? maybe not necessary here

    pr_debug("[NVPC DEBUG]: nvpc_fsync_range 9\n");

    // first commit the persistent log tail
    write_commit(inode, old_tail, new_tail);

    // second commit the dram index and the expired mark
    if (nvpc_sync_commit_transaction(&trans))
        goto fallback;

    mutex_unlock(&inode->nvpc_sync_ilog.log_lock);

    pr_debug("[NVPC DEBUG]: nvpc_fsync_range 10\n");

    kfree(trans.parts);

    // write_commit(inode, old_tail, new_tail);
    return ret;

fallback:
    pr_debug("[NVPC DEBUG]: meet fallback under fsync\n");
    // walk logs and free pages that has been allocated
    if (trans.parts)
        nvpc_sync_rollback_transaction(&trans);
    inode->nvpc_sync_ilog.log_tail = old_tail;
    inode->nvpc_sync_ilog.log_cntr = old_id;
    mutex_unlock(&inode->nvpc_sync_ilog.log_lock);

    // if (drained)
    // {
    //     try_to_free_pages
    // }

    ret = file->f_op->fsync(file, start, end, datasync);
    return ret;
}

// NVTODO: support set_attr log, e.g. truncate
/* 
 * NVPC truncate log represents that the data before this point inside NVPC
 * is truncated. This truncate operation will be replayed on the inode anyway,  
 * even without being written back or synced before a power fail. 
 */
int nvpc_sync_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr)
{
    nvpc_sync_attr_entry *ent;
    nvpc_sync_log_entry *old_tail, *new_tail;

    // only deal with truncate. ignore time modify
    if (!(iattr->ia_valid & ATTR_SIZE))
        return 0;

    nvpc_get_log_inode(dentry->d_inode);
    pr_debug("[NVPC DEBUG]: truncate to %lld\n", iattr->ia_size);

    mutex_lock(&dentry->d_inode->nvpc_sync_ilog.log_lock);
    old_tail = dentry->d_inode->nvpc_sync_ilog.log_tail;
    ent = (nvpc_sync_attr_entry *)append_inode_n_log(dentry->d_inode, 1);
    if (!ent)
        return -ENOMEM;
    
    ent->raw.flags = NVPC_LOG_FLAG_ATTR;
    ent->raw.id = dentry->d_inode->nvpc_sync_ilog.log_cntr++;
    ent->new_size = iattr->ia_size;
    ent->last_attr = dentry->d_inode->nvpc_sync_ilog.latest_logged_attr ? 
        nvpc_get_off(dentry->d_inode->nvpc_sync_ilog.latest_logged_attr) :
        0;
    arch_wb_cache_pmem((void*)ent, sizeof(nvpc_sync_attr_entry));

    new_tail = dentry->d_inode->nvpc_sync_ilog.log_tail;
    write_commit(dentry->d_inode, old_tail, new_tail);
    dentry->d_inode->nvpc_sync_ilog.latest_logged_attr = ent;
    mutex_unlock(&dentry->d_inode->nvpc_sync_ilog.log_lock);

    return 0;
}

struct walk_state {
    char *s;
    char *curr;
    size_t len;
    size_t left;
};

static bool __print_log_it(nvpc_sync_log_entry *ent, void *opaque)
{
    nvpc_sync_write_entry *went = (nvpc_sync_write_entry *)ent;
    int i = 0;
    struct walk_state *ws = opaque;
    
    if (!ws->s)
    {
        pr_info("[NVPC DEBUG] ent %d: pgidx %u off %lld len %u \n", i, went->page_index, went->file_offset, went->data_len);
        if (went->page_index)
        {
            // oop
            ws->s = kmalloc(went->data_len+1, GFP_KERNEL);
            ws->s[went->data_len] = 0;
            memcpy(ws->s, nvpc_get_addr_pg(went->page_index)+(went->file_offset&(~PAGE_MASK)), went->data_len);
            pr_info("[NVPC DEBUG] %s\n", ws->s);
            kfree(ws->s);
            ws->s = NULL;
            ws->curr = NULL;
        }
        else
        {
            // ip, head
            ws->s = kmalloc(went->data_len+1, GFP_KERNEL);
            ws->s[went->data_len] = 0;
            ws->len = went->data_len;
            ws->left = went->data_len;
            ws->curr = ws->s;
        }
        
    }
    else
    {
        // ip, body
        size_t bytes = min_t(unsigned long, NVPC_LOG_ENTRY_SIZE, ws->left);
        memcpy(ws->curr, ent, NVPC_LOG_ENTRY_SIZE);
        ws->left -= bytes;
        ws->curr += bytes;
        if (ws->left == 0)
        {
            // ip, tail
            pr_info("[NVPC DEBUG] %s\n", ws->s);
            kfree(ws->s);
            ws->s = NULL;
            ws->curr = NULL;
        }
    }
    
    return false;
}

void nvpc_print_inode_log(struct inode *inode)
{
    // nvpc_sync_log_entry *h = (nvpc_sync_log_entry *)inode->nvpc_sync_ilog.log_head->head_log_page;
    nvpc_sync_log_entry *h = (nvpc_sync_log_entry *)nvpc_get_log_inode(inode)->head_log_page;
    struct walk_state ws = {
        .s = NULL, 
        .curr = NULL, 
        .left = 0, 
        .len = 0,
    };
    walk_log(h, inode->nvpc_sync_ilog.log_head->committed_log_tail, __print_log_it, &ws, false);
}

// use a bitmap to track if an ip write entry contributes to this page's newest content
typedef struct page_bytes_tracker_s {
    uint8_t bitmap[PAGE_SIZE / 8];
}page_bytes_tracker;

#define INIT_TRACKER(tracker) memset((tracker)->bitmap, 0, PAGE_SIZE / 8)

// return if current record is useful
bool nvpc_track_page_get_avail(page_bytes_tracker *tracker, int start, int len)
{
    int idx = start / 8;
    int off = start % 8;
    int curr_left;
    uint8_t curr_mask;

    bool avail = false;

    while (len && idx < PAGE_SIZE / 8)
    {
        curr_left = min_t(int, 8-off, len);
        curr_mask = ((1ul<<curr_left)-1) << off;
        avail = avail || ((~(tracker->bitmap[idx])) & curr_mask);
        tracker->bitmap[idx] |= curr_mask;
        len -= curr_left;
        idx++;
        off=0;
    }
    return avail;
}

static bool __copy_from_ip_log_it(nvpc_sync_log_entry *ent, void *opaque)
{
    struct iov_iter *to = (struct iov_iter *)opaque;
    size_t ret;
    ret = copy_to_iter(ent, NVPC_LOG_ENTRY_SIZE, to);

    // return true if there's nothing left
    return !iov_iter_count(to);
}

/* move the attr pointer to the next attr after ent, return if the required attr is found */
static inline bool __nvpc_find_relevant_attr(nvpc_sync_write_entry *ent, 
        nvpc_sync_attr_entry **attr)
{
    nvpc_sync_attr_entry *a1, *a2;
    a1 = *attr;
    a2 = NULL;
    
    // find the very next attr after current entry
    while (a1 && a1->raw.id > ent->raw.id)
    {
        a2 = a1;
        a1 = a1->last_attr ? (nvpc_sync_attr_entry *)nvpc_get_addr(a1->last_attr) : NULL;
    }

    if (!a2)
        return false;
    
    *attr = a2;
    return true;
}

// return a new page in DRAM, caller needs to free it
struct page *nvpc_get_page_from_entry(nvpc_sync_write_entry *ent, nvpc_sync_attr_entry *latest_attr)
{
    nvpc_sync_attr_entry *curr_attr;
    struct page *newpg;
    void *newpg_addr;
    WARN_ON(!ent || ent->raw.flags == NVPC_LOG_FLAG_WREXP);

    newpg_addr = (void*)get_zeroed_page(GFP_KERNEL);
    newpg = virt_to_page(newpg_addr);
    if (!newpg_addr)
        return NULL;
    curr_attr = latest_attr;

    if (ent->page_index)
    {
        bool attr_found;
        // oop
        pr_debug("[NVPC DEBUG] oop off %llu len %llu\n", ent->file_offset, (uint64_t)ent->data_len);
        
        attr_found = __nvpc_find_relevant_attr(ent, &curr_attr);
        pr_debug("[NVPC DEBUG] curr_attr %px\n", curr_attr);
        
        copy_highpage(newpg, virt_to_page(nvpc_get_addr_pg(ent->page_index)));
        if (attr_found)
        {
            if ((curr_attr->new_size & PAGE_MASK) == (ent->file_offset & PAGE_MASK))
                memset(newpg_addr+(curr_attr->new_size & (~PAGE_MASK)), 0, PAGE_SIZE - (curr_attr->new_size & (~PAGE_MASK)));
            else if ((curr_attr->new_size & PAGE_MASK) < (ent->file_offset & PAGE_MASK))
            {
                free_page((unsigned long)newpg_addr);
                return NULL;
            }
        }
    }
    else
    {
        bool attr_found;
        // ip
        nvpc_sync_write_entry *curr = ent;
        page_bytes_tracker *tracker = kmalloc(sizeof(page_bytes_tracker), GFP_KERNEL);
        // NVXXX: this is too huge (takes 8 pages)
        nvpc_sync_write_entry **valid_list = kmalloc_array(PAGE_SIZE, sizeof(nvpc_sync_write_entry *), GFP_KERNEL);
        int valid_i = 0;

        if (!tracker || !valid_list)
        {
            return NULL;
        }

        INIT_TRACKER(tracker);
        
        // go back and track
        pr_debug("[NVPC DEBUG] ip track\n");
        
        // loop while current is ip
        while (!curr->page_index)
        {
            // track and check if this entry is useful

            attr_found = __nvpc_find_relevant_attr(curr, &curr_attr);
            // if curr attr is truncating the curr ent page, set all mask bits after the truncate to 1
            if (attr_found)
            {
                pr_debug("[NVPC DEBUG]: next_trunc: %lld\n", curr_attr->new_size);
                if ((curr_attr->new_size&PAGE_MASK) == (curr->file_offset&PAGE_MASK))
                    nvpc_track_page_get_avail(tracker, (curr_attr->new_size)&(~PAGE_MASK), PAGE_SIZE / 8);
                else if ((curr_attr->new_size&PAGE_MASK) < (curr->file_offset&PAGE_MASK))
                    nvpc_track_page_get_avail(tracker, 0, PAGE_SIZE / 8);
            }

            if (nvpc_track_page_get_avail(tracker, (curr->file_offset)&(~PAGE_MASK), curr->data_len))
            {
                valid_list[valid_i] = curr;
                valid_i++;
            }

            pr_debug("[NVPC DEBUG] ip valid_i: %d off %llu len %llu\n", valid_i, curr->file_offset, (uint64_t)curr->data_len);

            // current ip has previous write link
            if (curr->last_write) 
                curr = (nvpc_sync_write_entry *)nvpc_get_addr(curr->last_write);
            // done if this is the first ip
            else
            {
                curr = NULL;
                break;
            }
        }

        // if there's a whole page oop left
        if (curr)
        {
            bool attr_found;
            attr_found = __nvpc_find_relevant_attr(curr, &curr_attr);

            pr_debug("[NVPC DEBUG] oop off %llu len %llu\n", curr->file_offset, (uint64_t)curr->data_len);
                
            copy_highpage(newpg, virt_to_page(nvpc_get_addr_pg(curr->page_index)));

            if (attr_found)
            {
                pr_debug("[NVPC DEBUG]: next_trunc: %lld\n", curr_attr->new_size);
                if ((curr_attr->new_size & PAGE_MASK) == (ent->file_offset & PAGE_MASK))
                    memset(newpg_addr+(curr_attr->new_size & (~PAGE_MASK)), 0, PAGE_SIZE - (curr_attr->new_size & (~PAGE_MASK)));
                else if ((curr_attr->new_size & PAGE_MASK) < (ent->file_offset & PAGE_MASK))
                    memset(newpg_addr, 0, PAGE_SIZE);
            }
        }
        
        for (; valid_i > 0; valid_i--)
        {
            // walk ip and copy to new page
            struct iov_iter i;
            struct kvec kvec;
            curr = valid_list[valid_i-1];
            kvec.iov_base = newpg_addr + ((curr->file_offset)&(~PAGE_MASK));
            kvec.iov_len = curr->data_len;
            iov_iter_kvec(&i, WRITE, &kvec, 1, curr->data_len);
            walk_log((nvpc_sync_log_entry *)curr+1, NULL, __copy_from_ip_log_it, &i, false);
        }

        kfree(tracker);
        kfree(valid_list);
    }
    
    return newpg;
}

void nvpc_print_inode_pages(struct inode *inode)
{
    pgoff_t i;
    nvpc_sync_log_entry *ent;
    xa_for_each(&inode->nvpc_sync_ilog.inode_log_pages, i, ent) 
    {
        struct page *page;
        page = nvpc_get_page_from_entry(
            (nvpc_sync_write_entry*)ent, 
            (nvpc_sync_attr_entry*)inode->nvpc_sync_ilog.latest_logged_attr);
        if (!page)
            continue;

        pr_debug("[NVPC DEBUG]: raw %s\n[NVPC DEBUG]: ------------\n", (char*)page_to_virt(page));
        pr_debug("[NVPC DEBUG]: sz %lld\n", inode->i_size);
        
        // cut the page according to the current file size
        if ((inode->i_size & PAGE_MASK) == (((nvpc_sync_write_entry*)ent)->file_offset & PAGE_MASK))
            memset(page_to_virt(page)+(inode->i_size & (~PAGE_MASK)), 0, PAGE_SIZE - (inode->i_size & (~PAGE_MASK)));
        else if ((inode->i_size & PAGE_MASK) < (((nvpc_sync_write_entry*)ent)->file_offset & PAGE_MASK))
            memset(page_to_virt(page), 0, PAGE_SIZE);
        
        pr_info("[NVPC DEBUG]: fileoff %lu\n", i);
        pr_info("[NVPC DEBUG]: %s\n[NVPC DEBUG]: ------------\n", (char*)page_to_virt(page));

        free_page((unsigned long)page_to_virt(page));
    }
}

// NVTODO: log NVPC_LOG_FLAG_RM on write back

// NVTODO: lazy copy & remove pending copy mark inside generic_perform_write outside pagecache_get_page

// NVTODO: log compact

// NVTODO: log replay (refer to nvpc_print_inode_pages)

// NVTODO: mark file as O_SYNC if sync happens frequently

// NVTODO: demote to existing NVPC page to reduce memory usage

// NVTODO: only enable ip write when all fd are open in sync mode

// NVTODO: when OOM, fsync the inode and mark the inode as drained to prevent further access

// NVTODO: set / clear dirty (NVPCNpDirty) and other bits on migration