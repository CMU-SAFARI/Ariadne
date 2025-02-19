#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/gfp.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/swapops.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/frontswap.h>
#include <linux/blkdev.h>
#include <linux/psi.h>
#include <linux/uio.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>

#define SWAP_TRACE_DATA_OFS 50

// control swap tracing
struct file *swapout_trace_file = NULL;
struct file *launch_swapout_trace_file = NULL;
struct file *swapin_trace_file = NULL;
struct file *launch_trace_file = NULL;
struct file *launch_trace_file_b = NULL;
static int target_app_id = -1;
void *page_buffer;
void *swapin_trace_buffer;
void *ttid_trace_buffer;
void *ttfd_trace_buffer;
int ready_to_trace = 0;
int ready_to_trace_swapin = 0;
int ready_to_trace_swapout = 0;
int start_swapout_tracing = 0;
int start_swapin_tracing = 0;
int swapout_tracing_running = 0;
int swapin_tracing_running = 0;
int readahead_tracing_running = 0;
spinlock_t swapin_trace_lock;

// detect foreground app
int fgapp_uid = 259;
static struct kobject *fgapp_kobj;
static struct kobject *swap_tracing_kobj;


void write_page_content(struct page *page, int lock)
{
    void *data;
    unsigned long appid;
    unsigned long pfn;
    u64 sector;
    // char order_str[80] = {0};
    // lock = 0;
    if(start_swapout_tracing == 0) {
         return;
    }

    if(page == NULL) {
        return;
    }
    
    if(swapout_trace_file == NULL) {
        return;
    }

    appid = page_app_id(page);
    if (target_app_id != -1) {
        if(appid != target_app_id) {
            return;
        }
    } 
    if(appid != 10123 && appid != 10128 && appid != 10124 && appid != 10130 && appid != 10129) {
        return;
    }
    swapout_tracing_running = 1;
    if(swapout_trace_file && page) {
        memset(page_buffer, 0, SWAP_TRACE_DATA_OFS + PAGE_SIZE);
        // memset(page_buffer, 0, SWAP_TRACE_DATA_OFS);
        if(lock) {
            lock_page(page);
        }
        sector = swap_page_sector(page);
        pfn = page_to_pfn(page);
        data = kmap(page);
        if(data != NULL) {
            memcpy(page_buffer + SWAP_TRACE_DATA_OFS, data, PAGE_SIZE);
            kunmap(page);
            if(lock) {
                unlock_page(page);
            }
            sprintf(page_buffer, "TEST^^^#%lu,%lu,%llu#^^^\n", appid, pfn, sector);
            // sprintf(page_buffer, "%lu,%lu,%llu\n", appid, pfn, sector);
            __kernel_write(swapout_trace_file, page_buffer, SWAP_TRACE_DATA_OFS + PAGE_SIZE, &swapout_trace_file->f_pos);
            // sprintf(order_str, "%lu,%lu,%llu\n", appid, pfn, sector);
            // __kernel_write(swapout_trace_file, order_str, strlen(order_str), &swapout_trace_file->f_pos);
        } else {
            if(lock) {
                unlock_page(page);
            }
        }
    }
    swapout_tracing_running = 0;
}
EXPORT_SYMBOL(write_page_content);
            // sprintf(order_str, "%lu, %lu, %llu\n", appid, pfn, sector);
            // __kernel_write(swapout_trace_file, order_str, strlen(order_str), &swapout_trace_file->f_pos);
            // sprintf(page_buffer, "TEST^^^#%u,%lu#^^^\n", appid, pfn);
            // if (((ready_to_trace & 0x8) >> 3 ) == 1) {
            //     __kernel_write(launch_swapout_trace_file, page_buffer, SWAP_TRACE_DATA_OFS + PAGE_SIZE, &launch_swapout_trace_file->f_pos);
            // }

void write_page_order(struct page *page, int lock)
{
    unsigned long appid;
    // ktime_t now;
    // unsigned long pfn;
    u64 sector;
    char order_str[80] = {0};

    if(start_swapin_tracing == 0) {
         return;
    }

    if(page == NULL) {
        return;
    }
    
    if(swapin_trace_file == NULL) {
        return;
    }

    if(launch_trace_file == NULL) {
        return;
    }
    
    if(launch_trace_file_b == NULL) {
        return;
    }

    appid = page_app_id(page);
    if (target_app_id != -1) {
        if(appid != target_app_id) {
            return;
        }
    } 
    // else {
    if(appid != 10123 && appid != 10128 && appid != 10124 && appid != 10130 && appid != 10129) {
        return;
    }

    swapin_tracing_running = 1;
    if(swapin_trace_file && page) {
        // memset(page_buffer, 0, SWAP_TRACE_DATA_OFS + PAGE_SIZE);
        // memset(page_buffer, 0, SWAP_TRACE_DATA_OFS);
        if(lock) {
            lock_page(page);
        }
        // appid = page_app_id(page);
        sector = swap_page_sector(page);
        // pfn = page_to_pfn(page);
        if(lock) {
            unlock_page(page);
        }
        // now = ktime_get();
        sprintf(order_str, "%lu,%llu\n", appid, sector);
        // sprintf(order_str, "[%lld] %lu,%llu\n", ktime_to_us(now), appid, sector);
        spin_lock(&swapin_trace_lock);
        if (((ready_to_trace & 0x4) >> 2 ) == 1) {
            memcpy(ttid_trace_buffer, swapin_trace_buffer, strlen(swapin_trace_buffer));
            ready_to_trace = ready_to_trace ^ 0x4;
        }

        if (((ready_to_trace & 0x8) >> 3 ) == 1) {
            memcpy(ttfd_trace_buffer, swapin_trace_buffer, strlen(swapin_trace_buffer));
            ready_to_trace = ready_to_trace ^ 0x8;
        }
        // __kernel_write(swapin_trace_file, order_str, strlen(order_str), &swapin_trace_file->f_pos);
        if (strlen(swapin_trace_buffer) + strlen(order_str) < 1*1024*1024) {
            strcat(swapin_trace_buffer, order_str);
        } else {
            __kernel_write(swapin_trace_file, swapin_trace_buffer, strlen(swapin_trace_buffer), &launch_trace_file->f_pos);
            __kernel_write(swapin_trace_file, order_str, strlen(order_str), &launch_trace_file->f_pos);
            memset(swapin_trace_buffer, 0, 1*1024*1024);
        }
        spin_unlock(&swapin_trace_lock);
    }
    swapin_tracing_running = 0;
}
EXPORT_SYMBOL(write_page_order);

void write_page_readahead_order(struct page *page, int lock)
{
    unsigned long appid;
    // unsigned long pfn;
    u64 sector;
    char order_str[80] = {0};

    if(start_swapin_tracing == 0) {
         return;
    }

    if(page == NULL) {
        return;
    }
    
    if(launch_trace_file == NULL) {
        return;
    }

    appid = page_app_id(page);
    if (target_app_id != -1) {
        if(appid != target_app_id) {
            return;
        }
    } 
    // else {
    if(appid != 10123 && appid != 10128 && appid != 10124 && appid != 10130 && appid != 10129) {
        return;
    }


    readahead_tracing_running = 1;
    if(launch_trace_file && page) {
        // memset(page_buffer, 0, SWAP_TRACE_DATA_OFS + PAGE_SIZE);
        // memset(page_buffer, 0, SWAP_TRACE_DATA_OFS);
        if(lock) {
            lock_page(page);
        }
        // appid = page_app_id(page);
        sector = swap_page_sector(page);
        // pfn = page_to_pfn(page);
        if(lock) {
            unlock_page(page);
        }
        sprintf(order_str, "%lu,%llu\n", appid, sector);
        spin_lock(&swapin_trace_lock);
        //  __kernel_write(launch_trace_file, order_str, strlen(order_str), &launch_trace_file->f_pos);
        // write to swapin_trace_buffer
        if (strlen(swapin_trace_buffer) + strlen(order_str) < 1*1024*1024) {
            strcat(swapin_trace_buffer, order_str);
        } else {
            __kernel_write(launch_trace_file, swapin_trace_buffer, strlen(swapin_trace_buffer), &launch_trace_file->f_pos);
            memset(swapin_trace_buffer, 0, 1*1024*1024);
        }
        spin_unlock(&swapin_trace_lock);
    }
    readahead_tracing_running = 0;
}
EXPORT_SYMBOL(write_page_readahead_order);

static ssize_t fgapp_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", fgapp_uid);
}

static ssize_t fgapp_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &fgapp_uid);
	return count;
}

static struct kobj_attribute fgapp_attribute =
	__ATTR(fgapp_uid, 0664, fgapp_show, fgapp_store);


static ssize_t app_id_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", target_app_id);
}

static ssize_t app_id_store(struct kobject *kobj, struct kobj_attribute *attr,
                            const char *buf, size_t count) {
    sscanf(buf, "%d", &target_app_id);
    return count;
}

static struct kobj_attribute app_id_attr = __ATTR(app_id, 0664, app_id_show, app_id_store);


static struct attribute *fgapp_attrs[] = {
	&fgapp_attribute.attr,
	NULL,
};

static struct attribute_group fgapp_attr_group = {
	.attrs = fgapp_attrs,
};

static ssize_t swap_tracing_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", ready_to_trace);
}

static ssize_t swap_tracing_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &ready_to_trace);
    // 1st bit control swapin tracing, 2nd bit control swapout tracing
    if ((ready_to_trace & 0x1) == 1) {
        ready_to_trace_swapin = 1;
    } else {
        ready_to_trace_swapin = 0;
    }

    if (((ready_to_trace & 0x2) >> 1 ) == 1) {
        ready_to_trace_swapout = 1;
    } else {
        ready_to_trace_swapout = 0;
    }

    if(ready_to_trace_swapout) {
        if(swapout_trace_file == NULL) {
            swapout_trace_file = filp_open("/data/local/tmp/trace_swap/swapout_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            launch_swapout_trace_file = filp_open("/data/local/tmp/trace_swap/launch_swapout_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            if (IS_ERR(swapout_trace_file)) {
                printk("[HUBERY] open swapout_trace_file failed\n");
                swapout_trace_file = NULL;
                count = -1;
                goto out;
            }
            start_swapout_tracing = 1;
        }
    } else {
        start_swapout_tracing = 0;
        while(swapout_tracing_running == 1);
        if(swapout_trace_file != NULL) {
            filp_close(swapout_trace_file, NULL);
            filp_close(launch_swapout_trace_file, NULL);
            swapout_trace_file = NULL;
            launch_swapout_trace_file = NULL;
        }
    }

    if(ready_to_trace_swapin) {
        if(swapin_trace_file == NULL) {
            swapin_trace_file = filp_open("/data/local/tmp/trace_swap/swapin_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            launch_trace_file = filp_open("/data/local/tmp/trace_swap/ttid_swapin_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            launch_trace_file_b = filp_open("/data/local/tmp/trace_swap/ttfd_swapin_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            memset(swapin_trace_buffer, 0, 1*1024*1024);
            memset(ttid_trace_buffer, 0, 1*1024*1024);
            memset(ttfd_trace_buffer, 0, 1*1024*1024);
            if (IS_ERR(swapin_trace_file)) {
                printk("[HUBERY] open swapin_trace_file failed\n");
                swapin_trace_file = NULL;
                count = -1;
                goto out;
            }
            start_swapin_tracing = 1;
        }
    } else {
        start_swapin_tracing = 0;
        while(swapin_tracing_running == 1 || readahead_tracing_running == 1);
        if(swapin_trace_file != NULL) {
            if (strlen(ttid_trace_buffer) > 0) {
                __kernel_write(launch_trace_file, ttid_trace_buffer, strlen(ttid_trace_buffer), &launch_trace_file->f_pos);
                memset(ttid_trace_buffer, 0, 1*1024*1024);
            }

            if (strlen(ttfd_trace_buffer) > 0) {
                __kernel_write(launch_trace_file_b, ttfd_trace_buffer, strlen(ttfd_trace_buffer), &launch_trace_file_b->f_pos);
                memset(ttfd_trace_buffer, 0, 1*1024*1024);
            }

            if (strlen(swapin_trace_buffer) > 0) {
                __kernel_write(swapin_trace_file, swapin_trace_buffer, strlen(swapin_trace_buffer), &swapin_trace_file->f_pos);
                memset(swapin_trace_buffer, 0, 1*1024*1024);
            }
            filp_close(swapin_trace_file, NULL);
            filp_close(launch_trace_file, NULL);
            filp_close(launch_trace_file_b, NULL);
            swapin_trace_file = NULL;
            launch_trace_file = NULL;
            launch_trace_file_b = NULL;
        }
    }
out:
	return count;
}

static struct kobj_attribute swap_tracing_attribute =
	__ATTR(ready_to_trace, 0664, swap_tracing_show, swap_tracing_store);


static struct attribute *swap_tracing_attrs[] = {
	&swap_tracing_attribute.attr,
	NULL,
};

static struct attribute_group swap_tracing_attr_group = {
	.attrs = swap_tracing_attrs,
};

static int __init swap_trace_init(void)
{
	int retval;

    // page_buffer = kmalloc(SWAP_TRACE_DATA_OFS, GFP_KERNEL);
    page_buffer = kmalloc(SWAP_TRACE_DATA_OFS + PAGE_SIZE, GFP_KERNEL);
    if (!page_buffer) {
        return -ENOMEM;
    }

    swapin_trace_buffer = kmalloc(1*1024*1024, GFP_KERNEL);
    if (!swapin_trace_buffer) {
        return -ENOMEM;
    }
    
    ttid_trace_buffer = kmalloc(1*1024*1024, GFP_KERNEL);
    if (!ttid_trace_buffer) {
        return -ENOMEM;
    }

    ttfd_trace_buffer = kmalloc(1*1024*1024, GFP_KERNEL);
    if (!ttfd_trace_buffer) {
        return -ENOMEM;
    }

	fgapp_kobj = kobject_create_and_add("fgapp_uid", kernel_kobj);
	if (!fgapp_kobj)
		return -ENOMEM;

	retval = sysfs_create_group(fgapp_kobj, &fgapp_attr_group);
	if (retval)
		goto free_fgapp_obj;

    swap_tracing_kobj = kobject_create_and_add("swap_tracing_on", kernel_kobj);
	if (!swap_tracing_kobj)
		goto free_fgapp_obj;

	retval = sysfs_create_group(swap_tracing_kobj, &swap_tracing_attr_group);
	if (retval)
		goto free_swap_tracing_obj;

    retval = sysfs_create_file(swap_tracing_kobj, &app_id_attr.attr);
    if (retval)
        goto free_swap_tracing_obj;

    spin_lock_init(&swapin_trace_lock);
	return 0;
free_swap_tracing_obj:
    kobject_put(swap_tracing_kobj);
free_fgapp_obj:
    kobject_put(fgapp_kobj);
    return retval;
}

static void __exit swap_trace_exit(void)
{
    kobject_put(swap_tracing_kobj);
	kobject_put(fgapp_kobj);
    kfree(page_buffer);
    kfree(swapin_trace_buffer);
    kfree(ttid_trace_buffer);
    kfree(ttfd_trace_buffer);
    printk(KERN_INFO "Exiting swap_trace module\n");
}

module_init(swap_trace_init);
module_exit(swap_trace_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Riwei Pan");

