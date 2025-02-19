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
#include <linux/module.h>

#define ZTRACE_DATA_OFS 50

// control swap tracing
struct file *zram_comp_trace_file = NULL;
void *comp_buffer;
int ready_to_ztrace = 0;
int start_zcomp_tracing = 0;
int zcomp_tracing_running = 0;

// detect foreground app
int fg_app_uid = 369;
static struct kobject *fgapp_kobj;
static struct kobject *ztracing_kobj;


void write_comp_content(struct page *page, int lock)
{
    void *data;
    unsigned long appid;
    unsigned long pfn;
    // lock = 0;
    if(start_zcomp_tracing == 0) {
         return;
    }

    if(page == NULL) {
        return;
    }
    
    if(zram_comp_trace_file == NULL) {
        return;
    }

    zcomp_tracing_running = 1;
    if(zram_comp_trace_file && page) {
        memset(comp_buffer, 0, ZTRACE_DATA_OFS + PAGE_SIZE);
        if(lock) {
            lock_page(page);
        }

        appid = page_app_id(page);
        pfn = page_to_pfn(page);
        data = kmap(page);
        if(data != NULL) {
            memcpy(comp_buffer + ZTRACE_DATA_OFS, data, PAGE_SIZE);
            kunmap(page);
            if(lock) {
                unlock_page(page);
            }
            sprintf(comp_buffer, "TEST^^^#%lu,%lu#^^^\n", appid, pfn);

            __kernel_write(zram_comp_trace_file, comp_buffer, ZTRACE_DATA_OFS + PAGE_SIZE, &zram_comp_trace_file->f_pos);
        } else {
            if(lock) {
                unlock_page(page);
            }
        }
    }
    zcomp_tracing_running = 0;
}
EXPORT_SYMBOL(write_comp_content);


static ssize_t fgapp_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", fg_app_uid);
}

static ssize_t fgapp_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &fg_app_uid);
	return count;
}

static struct kobj_attribute fgapp_attribute =
	__ATTR(fg_app_uid, 0664, fgapp_show, fgapp_store);


static struct attribute *fgapp_attrs[] = {
	&fgapp_attribute.attr,
	NULL,
};

static struct attribute_group fgapp_attr_group = {
	.attrs = fgapp_attrs,
};

static ssize_t ztracing_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", ready_to_ztrace);
}

static ssize_t ztracing_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &ready_to_ztrace);

    if(ready_to_ztrace) {
        if(zram_comp_trace_file == NULL) {
            zram_comp_trace_file = filp_open("/data/local/tmp/trace_swap/zram_comp_trace.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
            if (IS_ERR(zram_comp_trace_file)) {
                printk("[HUBERY] open zram_comp_trace_file failed\n");
                zram_comp_trace_file = NULL;
                count = -1;
                goto out;
            }
            start_zcomp_tracing = 1;
        }
    } else {
        start_zcomp_tracing = 0;
        while(zcomp_tracing_running == 1);
        if(zram_comp_trace_file != NULL) {
            filp_close(zram_comp_trace_file, NULL);
            zram_comp_trace_file = NULL;
        }
    }

out:
	return count;
}

static struct kobj_attribute ztracing_attribute =
	__ATTR(ready_to_ztrace, 0664, ztracing_show, ztracing_store);


static struct attribute *ztracing_attrs[] = {
	&ztracing_attribute.attr,
	NULL,
};

static struct attribute_group ztracing_attr_group = {
	.attrs = ztracing_attrs,
};

int __init ztrace_init (void)
{
	int retval;

    comp_buffer = kmalloc(ZTRACE_DATA_OFS + PAGE_SIZE, GFP_KERNEL);
    if (!comp_buffer) {
        return -ENOMEM;
    }
        
	fgapp_kobj = kobject_create_and_add("fg_app_uid", kernel_kobj);
	if (!fgapp_kobj)
		return -ENOMEM;

	retval = sysfs_create_group(fgapp_kobj, &fgapp_attr_group);
	if (retval)
		goto free_fgapp_obj;

    ztracing_kobj = kobject_create_and_add("ztracing_on", kernel_kobj);
	if (!ztracing_kobj)
		goto free_fgapp_obj;

	retval = sysfs_create_group(ztracing_kobj, &ztracing_attr_group);
	if (retval)
		goto free_ztracing_obj;


	return 0;
free_ztracing_obj:
    kobject_put(ztracing_kobj);
free_fgapp_obj:
    kobject_put(fgapp_kobj);
    return retval;
}

void __exit ztrace_exit(void)
{
    kobject_put(ztracing_kobj);
	kobject_put(fgapp_kobj);
}

module_init(ztrace_init);
module_exit(ztrace_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Developers");
