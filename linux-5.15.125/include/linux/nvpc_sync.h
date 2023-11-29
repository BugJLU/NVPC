#ifndef _LINUX_NVPC_SYNC_H
#define _LINUX_NVPC_SYNC_H

#include <linux/mutex.h>
#include <linux/xarray.h>
#include <linux/uio.h>
#include <linux/nvpc_base.h>

/*
 *  --super_log (1 page)-----------------       --super_log----------------------     
 *  |    log_inode_head_entry           |       |    log_inode_head_entry       |     
 *  |    log_inode_head_entry ----------|--+    |    log_inode_head_entry <-----|--+  
 *  |    ...                            |  |    |    ...                        |  |  
 *  |                                   |  |    |                               |  |  
 *  |    next super_log* ---------------|------>|    next super_log* ->         |  |  
 *  --NVM--------------------------------  |    --NVM----------------------------  |  
 *                                         |                                       |  
 *  --log_inode_head_entry---------------  |    --inode--------------------------  |  
 *  |   superblock dev name             |}-+    |   ...                         |  |  
 *  |   inode num                       |       |   log_inode_head_entry* ------|--+  
 *  |   inode committed_log_tail*-------|--+    |   inode_log_tail* ------------|-+   
 *  |   inode head_log_page* ----+      |  |    --DRAM--------------------------- |   
 *  -----------------------------|-------  |                                      |   
 *                               |         |   *//*****************************   |   
 *                               |         |   * Both inode_log_tail in inode *   |   
 *  ---------                    |         |   * and committed_log_tail       *   |   
 *  | PAGE  |<----------------+  |         |   * should be updated atomically *   |   
 *  ---------                 |  |         |   *****************************//*   |   
 *                            |  v         |                                      |   
 *  --inode_log (1 page)------|----------  |    --inode_log---------------------- |   
 *  |   log_entry (out-of-p) -+         |  +----|-> log_entry <-----------------|-+   
 *  |   log_entry                       |       |                               |     
 *  |   ...                             |       |                               |     
 *  |   next_log* ----------------------|-...-->|                               |     
 *  --NVM--------------------------------       ---------------------------------     
 *  
 * NOTE:
 *    - log_inode_head_entry->committed_log_tail is the pointer for committed logs, 
 *      while inode->log_tail is the pointer for allocated log entries. logs before
 *      committed_log_tail are successfully committed to NVM, log entries after 
 *      inode->log_tail are not used, entries between committed_log_tail and inode->
 *      log_tail are in use. This is for the performance of concurrency.
 *      _________________________________________________________________
 *      |_1_|_2_|_3_|_4_|_5_|_6_|_7_|_8_|_9_|_a_|___|___|___|___|___|___|
 *              ^                               ^
 *              committed_log_tail              inode->log_tail
 *      This is a planned work that is NOT implemented now. The coarse grained lock 
 *      of inode (i_rwsem) limits the writes to only one thread. So the functional 
 *      pointer is only committed_log_tail now.
 */



/* --- global context in DRAM --- */
struct nvpc_sync
{
    void *super_log_0;   /* pointer to the first nvpc page */
    struct mutex super_log_lock;
    // /* xarray for nvpc_sync_inode */
    // struct xarray inode_log_heads;
};



#define NVPC_ENTRY_SIZE_ASSERT(raw, entry) \
    static_assert(sizeof(raw) == sizeof(entry))


/* --- NVPC HEAD ENTRIES --- */

#define NVPC_LOG_HEAD_MAGIC 0xa317a317    /* 32 bit uint */

#define NVPC_LOG_HEAD_FLAG_EMPTY    0   /* empty log head */
#define NVPC_LOG_HEAD_FLAG_NEXT     (uint32_t)(~(0UL))

#define NVPC_HEAD_ENTRY_SIZE 32

/* basic head entry */
typedef struct __attribute__((__packed__)) nvpc_sync_head_entry_s
{
    uint32_t     flags;
    uint8_t     data[NVPC_HEAD_ENTRY_SIZE-4];
} nvpc_sync_head_entry;

#define NVPC_HEAD_ESASSERT(entry) \
    NVPC_ENTRY_SIZE_ASSERT(nvpc_sync_head_entry, entry)

/* first superlog head entry */
typedef union first_head_entry_u
{
    struct __attribute__((__packed__))
    {
        uint32_t magic;  /* always == 0xa317a317 */
    };
    nvpc_sync_head_entry raw;
} first_head_entry;
NVPC_HEAD_ESASSERT(first_head_entry);

/* log-inode head entry */
struct nvpc_sync_log_entry_s;
typedef union log_inode_head_entry_u
{
    struct __attribute__((__packed__))
    {
        dev_t           s_dev;  /* use UUID instead */
        unsigned long   i_ino;
        uintptr_t       *head_log_page;

        /* logs before this pointer are successfully committed to NVM */
        struct nvpc_sync_log_entry_s    *committed_log_tail;
        // uint32_t                        ref;
    };
    nvpc_sync_head_entry raw;
} log_inode_head_entry;
NVPC_HEAD_ESASSERT(log_inode_head_entry);

/* next superlog page head entry */
typedef union next_sl_page_entry_u
{
    struct __attribute__((__packed__))
    {
        uint32_t flags;  /* always == -1 */
        uintptr_t *next_sl_page;
    };
    nvpc_sync_head_entry raw;
} next_sl_page_entry;
NVPC_HEAD_ESASSERT(next_sl_page_entry);

/* helpers to find next superlog page */
#define NVPC_SL_ENTRY_NEXT(addr) ((next_sl_page_entry*)(((uintptr_t)addr&PAGE_MASK)+PAGE_SIZE-NVPC_HEAD_ENTRY_SIZE))
#define NVPC_SL_HAS_NEXT(addr) (NVPC_SL_ENTRY_NEXT(addr)->flags == NVPC_LOG_HEAD_FLAG_NEXT)
#define NVPC_SL_NEXT(addr) (NVPC_SL_ENTRY_NEXT(addr)->next_sl_page)


/* --- NVPC LOG ENTRIES --- */

#define NVPC_LOG_FLAG_WRITE 0   /* write */
#define NVPC_LOG_FLAG_NEXT  1   /* next page */

/* log entry length is 16 bytes */
#define NVPC_LOG_ENTRY_SIZE     16
static_assert(PAGE_SIZE % NVPC_LOG_ENTRY_SIZE == 0);
#define NVPC_LOG_ENTRY_SHIFT    order_base_2(NVPC_LOG_ENTRY_SIZE)
#define NVPC_PAGE_ENTRIES       (PAGE_SIZE/NVPC_LOG_ENTRY_SIZE)
#define NVPC_PAGE_LOG_ENTRIES   (NVPC_PAGE_ENTRIES-1)
#define NVPC_PAGE_ENTRIES_LEFT(ent) \
            (((((uintptr_t)ent) & PAGE_MASK) + PAGE_SIZE - ((uintptr_t)ent)) >> NVPC_LOG_ENTRY_SHIFT)
#define NVPC_PAGE_LOG_ENTRIES_LEFT(ent) \
            (NVPC_PAGE_ENTRIES_LEFT(ent) - 1)

/* basic log entry */
typedef struct __attribute__((__packed__)) nvpc_sync_log_entry_s
{
    uint8_t     flags;
    uint8_t     data[NVPC_LOG_ENTRY_SIZE-1];
} nvpc_sync_log_entry;

#define NVPC_LOG_ESASSERT(entry) \
    NVPC_ENTRY_SIZE_ASSERT(nvpc_sync_log_entry, entry)

/* write log entry */
typedef union nvpc_sync_write_entry_u
{
    struct __attribute__((__packed__))
    {
        uint64_t    file_offset;    /* first 8 bit is flag, always 0 */
        uint16_t    data_len;       /* no more than page size, unit is byte */
        
        /* for out-of-place log if page_index != 0 */
        uint32_t    page_index;     /* unit is page, support up to 16TB */
        // uint16_t    page_offset; 
    };
    nvpc_sync_log_entry raw;
} nvpc_sync_write_entry;
NVPC_LOG_ESASSERT(nvpc_sync_write_entry);

/* next log page entry */
typedef union nvpc_next_log_entry_u
{
    struct __attribute__((__packed__))
    {
        uint64_t __reserved;
        uintptr_t next_log_page;
    };
    nvpc_sync_log_entry raw;
} nvpc_next_log_entry;
NVPC_LOG_ESASSERT(nvpc_next_log_entry);

// #define NVPC_LOG_NEXT_ENT(addr) ((nvpc_next_log_entry*)(((uintptr_t)addr & PAGE_MASK) + PAGE_SIZE - sizeof(nvpc_sync_log_entry)))

/* helpers to find next log page */
#define NVPC_LOG_ENTRY_NEXT(addr) ((nvpc_next_log_entry*)(((uintptr_t)addr&PAGE_MASK)+PAGE_SIZE-NVPC_LOG_ENTRY_SIZE))
#define NVPC_LOG_HAS_NEXT(addr) (NVPC_LOG_ENTRY_NEXT(addr)->raw.flags == NVPC_LOG_FLAG_NEXT)
#define NVPC_LOG_NEXT(addr) (NVPC_LOG_ENTRY_NEXT(addr)->next_log_page)

// #define NVPC_LOG_FILE_OFF_MASK ((1UL) << 56) - 1
// #define NVPC_LOG_FILE_OFF(x) (x.file_offset)|NVPC_LOG_FILE_OFF_MASK



/* --- Functions --- */

log_inode_head_entry *nvpc_get_log_inode(struct inode *inode);

nvpc_sync_log_entry *write_oop(struct inode *inode, struct page *page, loff_t file_off);
nvpc_sync_log_entry *write_ip(struct inode *inode, struct iov_iter *from, loff_t file_off);

#endif