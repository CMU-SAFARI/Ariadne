#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kobject.h>
#include <linux/memcontrol.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Controlled LRU_ACTIVE_ANON info dumper with app_id filter");

#define FILE_PATH "/data/local/tmp/anon_list/lru_active_anon_info"

static struct kobject *lru_kobj;
static bool dump_enabled = false;
static int target_app_id = -1;
struct file *lru_trace_file = NULL;
int start_lru_tracing = 0;
int lru_tracing_running = 0;
int ready_to_trace_lru = 0;

extern int page_app_id(struct page *page);
// static int dump_lru_active_anon(void);
// static struct file *file_open(const char *path, int flags, int rights) {
//     struct file *filp = NULL;
//     mm_segment_t oldfs;
//     oldfs = get_fs();
//     set_fs(KERNEL_DS);
//     filp = filp_open(path, flags, rights);
//     set_fs(oldfs);
//     if (IS_ERR(filp)) {
//         return NULL;
//     }
//     return filp;
// }

static void file_close(struct file *file) {
    filp_close(file, NULL);
}

// static int file_write(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) {
//     mm_segment_t oldfs;
//     int ret;
//     oldfs = get_fs();
//     set_fs(KERNEL_DS);
//     ret = vfs_write(file, data, size, &offset);
//     set_fs(oldfs);
//     return ret;
// }

void write_lru_page(unsigned long appid, unsigned long pfn, unsigned long is_free)
{
    // char order_str[80] = {0};
    // int len;
    // printk(KERN_ALERT"11111111111111111111111\n");
    if(start_lru_tracing == 0) {
         return;
    }
    
    if(lru_trace_file == NULL) {
        return;
    }

    if (target_app_id != -1) {
        if (appid != target_app_id) {
            return;
        }
    }

    if(appid != 10123 && appid != 10128 && appid != 10124 && appid != 10130 && appid != 10129) {
        return;
    }
    
    // printk(KERN_ALERT"2222222222222222222222222222\n");
    lru_tracing_running = 1;
    if(lru_trace_file) {
        // printk(KERN_ALERT"3333333333333333333333333333333333\n");
        // printk(KERN_ALERT"appid: %lu, pfn: %lu\n", appid, pfn);
        printk(KERN_ALERT"%lu,%lu,%lu\n", appid, pfn, is_free);
        // __kernel_write(lru_trace_file, order_str, strlen(order_str), &lru_trace_file->f_pos);
    }
    // printk(KERN_ALERT"44444444444444444444444444444444444444\n");
    lru_tracing_running = 0;
}


static ssize_t print_control_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", dump_enabled);
}

static ssize_t print_control_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count) {
    int ret = kstrtobool(buf, &dump_enabled);
    sscanf(buf, "%d\n", &ready_to_trace_lru);
    if(ready_to_trace_lru) {
        if(lru_trace_file == NULL) {
            lru_trace_file = filp_open(FILE_PATH, O_CREAT | O_RDWR | O_TRUNC, 0666);
            if (IS_ERR(lru_trace_file)) {
                printk("[HUBERY] open lru_trace_file failed\n");
                lru_trace_file = NULL;
                count = -1;
                goto out;
            }
            start_lru_tracing = 1;
        }
    } else {
        start_lru_tracing = 0;
        while(lru_tracing_running == 1);
        if(lru_trace_file != NULL) {
            file_close(lru_trace_file);
            lru_trace_file = NULL;
        }
    }
out:
    if (ret < 0)
        return ret;
    return count;
}

static ssize_t app_id_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", target_app_id);
}

static ssize_t app_id_store(struct kobject *kobj, struct kobj_attribute *attr,
                            const char *buf, size_t count) {
    sscanf(buf, "%d", &target_app_id);
    return count;
}


static struct kobj_attribute print_control_attr = __ATTR(print_control, 0664, print_control_show, print_control_store);
static struct kobj_attribute app_id_attr = __ATTR(app_id, 0664, app_id_show, app_id_store);

static int __init lru_active_anon_init(void) {
    int retval = 0;
    lru_kobj = kobject_create_and_add("lru_active_anon", kernel_kobj);
    if (!lru_kobj)
        return -ENOMEM;

    retval = sysfs_create_file(lru_kobj, &print_control_attr.attr);
    if (retval)
        kobject_put(lru_kobj);

    retval = sysfs_create_file(lru_kobj, &app_id_attr.attr);
    if (retval)
        kobject_put(lru_kobj);

    return retval;
}

static void __exit lru_active_anon_exit(void) {
    kobject_put(lru_kobj);
    printk(KERN_INFO "Exiting LRU_ACTIVE_ANON module\n");
}

module_init(lru_active_anon_init);
module_exit(lru_active_anon_exit);


// static int dump_lru_active_anon(void) {
//     struct file *file;
//     char data[256];
//     struct pglist_data *pgdat;
//     int ret;
//     int len;
//     struct lruvec *lruvec;
//     struct page *page;
//     unsigned long flags;
//     file = file_open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
//     if (file == NULL)
//         return -ENOENT;
//     len = snprintf(data, 256, "------------------------------------\n");
//     file_write(file, file->f_pos, data, len);
//     // Iterate over all node data
//     for_each_online_pgdat(pgdat) {
//         // Access lruvec for the node, usually obtained through some function like node_lruvec()
//         lruvec = &pgdat->__lruvec;
        
//         // Lock the lru list
//         spin_lock_irqsave(&pgdat->lru_lock, flags);

//         // Traverse the active anonymous LRU list
//         list_for_each_entry(page, &lruvec->lists[LRU_ACTIVE_ANON], lru) {
//             if (page_app_id(page) == target_app_id) {
//                 len = snprintf(data, 256, "%d, %d\n", page_app_id(page), page_to_pfn(page));
//                 ret = file_write(file, file->f_pos, data, len);
//                 if (ret < 0) {
//                     printk(KERN_ERR "Failed to write to file\n");
//                     file_close(file);
//                     return ret;
//                 }
//             }
//         }

//         // Unlock the lru list
//         spin_unlock_irqrestore(&pgdat->lru_lock, flags);
//     }
//     file_close(file);
//     return 0;
// }