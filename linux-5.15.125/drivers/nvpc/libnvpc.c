#include <linux/nvpc.h>
#include <linux/blkdev.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/stat.h>
#include <linux/mm.h>

// NVTODO: remove this
#define DEBUG

#define NVPC_NAME "libnvpc"

#define LIBNVPC_IOCTL_BASE 'N'

typedef unsigned char u8;

#define LIBNVPC_IOC_INIT    _IOR(LIBNVPC_IOCTL_BASE, 0, u8*)  
#define LIBNVPC_IOC_FINI    _IOR(LIBNVPC_IOCTL_BASE, 1, u8*)
#define LIBNVPC_IOC_USAGE   _IOR(LIBNVPC_IOCTL_BASE, 2, u8*)
#define LIBNVPC_IOC_TEST    _IOR(LIBNVPC_IOCTL_BASE, 3, u8*)

#define PATH_MAX 4096

/* flush write or cached write */
static bool nvpc_flush = true;
/* memory write */
static bool nvpc_wbarrier = true;

module_param(nvpc_flush, bool, S_IRUGO|S_IWUSR);
module_param(nvpc_wbarrier, bool, S_IRUGO|S_IWUSR);

typedef struct nvpc_init_s
{
    char dev_name[PATH_MAX];
    size_t lru_sz;
    size_t syn_sz;
    u8 promote_level;
} nvpc_init_t;

typedef struct nvpc_usage_s
{
    size_t lru_sz;
    size_t lru_free;
    size_t syn_sz;
    size_t syn_free;
} nvpc_usage_t;

static int set_nvpc_device_with_path(char *path);
static void release_nvpc_device(void);
static void get_nvpc_usage(nvpc_usage_t *usage);

#ifndef CONFIG_NVPC

#define NO_NVPC_INFO "Libnvpc: NVPC is not enabled in this kernel, nothing done.\n"

static int set_nvpc_device_with_path(char *path)
{
    pr_info(NO_NVPC_INFO);
    return 0;
}

static void release_nvpc_device()
{
    pr_info(NO_NVPC_INFO);
}

static void get_nvpc_usage(nvpc_usage_t *usage)
{
    pr_info(NO_NVPC_INFO);
}

#else

static struct block_device *nvpc_bdev;
static char nvpc_dev_name[PATH_MAX];

static int set_nvpc_device_with_path(char *path)
{
    struct nvpc *nvpc;
    struct dax_device *dax_dev;
    struct nvpc_opts opts;
    int ret;

    pr_info("Libnvpc: setting libnvpc device to %s.\n", path);

    nvpc = get_nvpc();
    if (!nvpc)
    {
        pr_err("Libnvpc error: Cannot get nvpc reference.\n");
        goto err;
    }
    if (nvpc->enabled)
    {
        pr_err("Libnvpc error: nvpc is already enabled.\n");
        goto err;
    }
    
    nvpc_bdev = blkdev_get_by_path(path, 
                FMODE_READ|FMODE_WRITE|FMODE_EXCL, nvpc);
    if (IS_ERR(nvpc_bdev) || !nvpc_bdev)
    {
        pr_err("Libnvpc error: Cannot find an idle device with name %s. Err: %ld\n", 
                path, PTR_ERR(nvpc_bdev));
        goto err;
    }
    pr_info("Libnvpc: bdev: %s\n", nvpc_bdev->bd_disk->disk_name);

    dax_dev = fs_dax_get_by_bdev(nvpc_bdev);
    if (!dax_dev)
    {
        pr_err("Libnvpc error: Cannot get dax device with name %s.\n", path);
        goto err1;
    }

    opts.dev = dax_dev;
    opts.nid = nvpc_bdev->bd_device.numa_node;
    opts.lru = true;
    opts.syn = true;
    opts.lru_sz = 0x80000;
    opts.syn_sz = 0x80000;
    opts.promote_level = 4;
    ret = init_nvpc(&opts);
    if (ret < 0)
    {
        pr_err("Libnvpc error: Cannot init nvpc: %d.\n", ret);
        goto err1;
    }
    
    pr_info("Libnvpc: libnvpc initialized on device %s with size %zu.\n", path, nvpc->len_pg<<PAGE_SHIFT);
    return 0;

err1:
    blkdev_put(nvpc_bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
err:
    return -1;
}

static void release_nvpc_device(void) 
{
    fini_nvpc();
    blkdev_put(nvpc_bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
    pr_info("Libnvpc: libnvpc device released.\n");
}

static void get_nvpc_usage(nvpc_usage_t *usage)
{
    nvpc_lru_size(&usage->lru_free, &usage->lru_sz);
    nvpc_syn_size(&usage->syn_free, &usage->syn_sz);
}

static void do_nvpc_test(long x)
{
    LIST_HEAD(list);
    struct page *p;
    int n = 0;
    pr_info("Libnvpc TEST: pfn:\t%lx\n", nvpc.pfn);
    pr_info("Libnvpc TEST: phys:\t%llx\n", PFN_PHYS(nvpc.pfn));
    pr_info("Libnvpc TEST: page:\t%p\n", pfn_to_page(nvpc.pfn));
    // pr_info("Libnvpc TEST: pfn2v:\t%p\n", pfn_to_virt(nvpc.pfn));
    pr_info("Libnvpc TEST: pfn2v:\t%p\n", __va((nvpc.pfn) << PAGE_SHIFT));
    pr_info("Libnvpc TEST: virt:\t%p\n", nvpc.dax_kaddr);
    pr_info("Libnvpc TEST: v2pg:\t%p\n", virt_to_page(nvpc.dax_kaddr));
    // pr_info("Libnvpc TEST: v2pfn:\t%p\n", virt_to_pfn(nvpc.dax_kaddr));
    pr_info("Libnvpc TEST: v2pfn:\t%lx\n", (__pa(nvpc.dax_kaddr) >> PAGE_SHIFT));
    pr_info("Libnvpc TEST: ---\n");
    pr_info("Libnvpc TEST: start\n");
    // while (p=nvpc_get_new_page(NULL, 0))
    // {
        p=nvpc_get_new_page(NULL, 0);
        pr_info("Libnvpc TEST: refcnt:\t%d\n", atomic_read(&p->_refcount));
        list_add(&p->lru, &list);
        n++;
    // }
    pr_info("Libnvpc TEST: all pages (%d) taken from free list\n", n);
    n=0;
    while (!list_empty(&list))
    {
        p = list_first_entry(&list, struct page, lru);
        list_del(&p->lru);
        nvpc_free_page(p, 0);
        n++;
    }
    pr_info("Libnvpc TEST: all pages (%d) returned to free list\n", n);
}

#endif

static int libnvpc_open(struct inode *inode, struct file *file)
{
    return 0;
}

static long libnvpc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret;
    nvpc_usage_t usage;

    ret = 0;
    switch (cmd)
    {
    case LIBNVPC_IOC_INIT:
        if (copy_from_user(nvpc_dev_name, (char *)arg, PATH_MAX))
            ret = -EFAULT;
        ret = set_nvpc_device_with_path(nvpc_dev_name);
        break;
    case LIBNVPC_IOC_FINI:
        release_nvpc_device();
        break;
    case LIBNVPC_IOC_USAGE:
        get_nvpc_usage(&usage);
        if (copy_to_user((nvpc_usage_t *)arg, &usage, sizeof(nvpc_usage_t)))
            ret = -EFAULT;
        break;
    case LIBNVPC_IOC_TEST:
        do_nvpc_test(arg);
        break;
    default:
        ret = -EPERM;
        break;
    }

    return ret;
}

/* read and write only for debugging */
ssize_t libnvpc_read_iter(struct kiocb *iocb, struct iov_iter *to) 
{
    return nvpc_read_nv_iter(to, iocb->ki_pos);
}
ssize_t libnvpc_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
    size_t ret;

    ret = nvpc_write_nv_iter(from, iocb->ki_pos, nvpc_flush);
    if (nvpc_flush && nvpc_wbarrier) nvpc_wmb();
    
    return ret;
}

static dev_t dev = 0;
static struct class *dev_class;
static struct cdev libnvpc_cdev;

struct file_operations fops = {
    .owner          = THIS_MODULE, 
    .open           = libnvpc_open,
    .unlocked_ioctl = libnvpc_ioctl,
    /* read and write only for debugging */
    .read_iter      = libnvpc_read_iter,
    .write_iter     = libnvpc_write_iter, 
};

static int mode_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", S_IRUGO | S_IWUGO);
    return 0;
}

static int __init libnvpc_init(void)
{
    if ((alloc_chrdev_region(&dev, 0, 1, NVPC_NAME)) <0) {
        pr_err("Libnvpc error: Cannot alloc device number for libnvpc.\n");
        goto err;
    }

    cdev_init(&libnvpc_cdev, &fops);
    if ((cdev_add(&libnvpc_cdev, dev, 1)) < 0) {
        pr_err("Libnvpc error: Cannot add libnvpc device to the system.\n");
        goto class_err;
    }

    dev_class = class_create(THIS_MODULE, NVPC_NAME);
    if (IS_ERR(dev_class)) {
        pr_err("Libnvpc error: Cannot create the struct class for libnvpc.\n");
        goto class_err;
    }
    dev_class->dev_uevent = mode_uevent;

    if (IS_ERR(device_create(dev_class, NULL, dev, NULL, NVPC_NAME))) {
        pr_err("Libnvpc error: Cannot create the device for libnvpc.\n");
        goto device_err;
    }


    pr_info("Libnvpc: libnvpc driver initialized successfully.\n");
    return 0;

// tfm_err:
//     device_destroy(dev_class, dev);
device_err:
    class_destroy(dev_class);
class_err:
    unregister_chrdev_region(dev,1);
err:
    return -1;
}

static void __exit libnvpc_exit(void)
{
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&libnvpc_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info("Libnvpc: libnvpc driver exited.\n");
}

MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(libnvpc_init);
module_exit(libnvpc_exit);