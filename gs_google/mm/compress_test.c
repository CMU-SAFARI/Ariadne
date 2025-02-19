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
#include <linux/lzo.h>
#include <linux/lz4.h>
#include <linux/module.h>

#define META_BUFFER_SIZE 128
#define ZRAM_MAP_LINE_SIZE 32
#define CHUNK_SIZE 128
#define BATCH_SIZE (1 << 25) //32MB
///////////////////////// global configuration
enum {
    COMPRESS_LZO = 0,
    COMPRESS_LZ4,
    NR_COMPRESS_ALG,
};

char *compress_alg_type[] = {
    "lz4",
    "lz4",
};

struct compress_algorithm {
    char *name;
	void *buffer;
	//struct crypto_comp *tfm;
    void *private;
};

struct compr_buffer {
    void *data;
    unsigned long max_bytes;
    unsigned int used_bytes;
    unsigned long nr_compr_page;
    struct list_head list;
};

struct compr_meta_info {
    unsigned long app_id;
    unsigned long pfn;
    unsigned long sector;
    unsigned long compression_scale;
    unsigned long compr_len;
};

struct page_meta {
    unsigned long app_id;
    unsigned long sector;
};

struct decompr_cache {
    struct page_meta page_meta;
    struct list_head list;
};

struct zram_map_entry {
    unsigned long app_id;
    unsigned long sector;
    unsigned long zram_slot_idx;
};

struct zram_meta {
    unsigned long compression_scale;
    unsigned long compression_window_size;
    unsigned long zram_slot_idx;
    struct page_meta page_meta[16];
    unsigned long compr_len[PAGE_SIZE / CHUNK_SIZE];
    loff_t data_offset;
};


char *cur_alg = NULL;
char trace_file_path[256] = {0};
char swapout_data_path[256] = {0};
char swapout_meta_path[256] = {0};
char swapin_meta_path[256] = {0};
int window_size = 1;
int medium_window_size = 1;
int large_window_size = 1;
unsigned long total_compr_time, total_decompr_time;
unsigned long total_decompr_time_small, total_decompr_time_medium, total_decompr_time_large;

static struct kobject *parent_dir_kobj;
/////////////////////////

//////////////////////// compress related
static inline struct compr_buffer *alloc_compr_buffer(unsigned long window_size)
{
    struct compr_buffer *cf;
    cf = kmalloc(sizeof(*cf), GFP_KERNEL);
    cf->max_bytes = CHUNK_SIZE * window_size * 2; // we use double size of window_size to store compressed data in case of compressed size is larger than original size
    cf->used_bytes = 0;
    cf->nr_compr_page = 0;
    cf->data = kmalloc(cf->max_bytes, GFP_KERNEL);
    return cf;
}

static inline void free_compr_buffer(struct compr_buffer *cf)
{
    kfree(cf->data);
    kfree(cf);
}

static inline struct decompr_cache *alloc_decompr_cache(void)
{
    struct decompr_cache *dc;
    dc = vmalloc(sizeof(*dc));
    return dc;
}

static inline void free_decompr_cache(struct decompr_cache *dc)
{
    vfree(dc);
}

static inline struct compress_algorithm *alloc_compress_algriothm(char *name, unsigned long window_size)
{
    struct compress_algorithm *alg;

    alg = kmalloc(sizeof(*alg), GFP_KERNEL);
    alg->name = name;
    // alg->tfm = crypto_alloc_comp(name, 0, 0);
    // we use double size of window_size to store compressed data in case of compressed size is larger than original size
    // alg->buffer = (void *) __get_free_pages(GFP_KERNEL | __GFP_ZERO, ilog2(window_size * 2)); // this buffer is not useless
    // if (IS_ERR_OR_NULL(alg->tfm) || !alg->buffer) {
    //     kfree(alg);
    //     return;
    // }
    alg->private = kvmalloc(LZO1X_MEM_COMPRESS, GFP_NOFS);
    // alg->private = kvmalloc(LZ4_MEM_COMPRESS, GFP_NOFS);
    return alg;
}

static inline void free_compress_algriothm(struct compress_algorithm *alg)
{
    kfree(alg->private);
    kfree(alg);
}


int compress_pages(struct compress_algorithm *alg, 
        const void *src, unsigned int src_len, void *dst, unsigned int *dst_len)
{
	// *dst_len = src_len * 2;

	// return crypto_comp_compress(alg->tfm,
	// 		src, src_len, 
    //         dst, dst_len);
    int ret;
    size_t _dst_len = 0;
    _dst_len = CHUNK_SIZE * window_size * 2;
	ret = lzo1x_1_compress(src, src_len, (unsigned char *) dst, &_dst_len, alg->private);
	if (ret != LZO_E_OK) {
        printk(KERN_ERR "Failed to compress data\n");
		return -EIO;
	}
    *dst_len = _dst_len;
    // ret = LZ4_compress_default(src, (unsigned char *) dst, src_len, _dst_len, alg->private);
    // *dst_len = ret;
    return 0;
}

int decompress_pages(struct compress_algorithm *alg,
		const void *src, unsigned int src_len, void *dst, unsigned int *dst_len)
{
    int ret;
    size_t _dst_len = *dst_len;
    ret = lzo1x_decompress_safe(src, src_len,
						(unsigned char *) dst, &_dst_len);
	if (ret != LZO_E_OK) {
        printk(KERN_ERR "Failed to decompress data, complen = %d, dist_len = %d, ret = %d\n", src_len, _dst_len, ret);
		return -EIO;
	}
    // ret = LZ4_decompress_safe(src, (unsigned char *)dst, src_len, _dst_len);
    // *dst_len = _dst_len;
    return 0;
}

static bool page_same_pattern(char *page, unsigned long *element)
{
	unsigned int pos;
	unsigned long *mem;
	unsigned long val;
	bool ret = true;

	mem = (unsigned long *)page;
	val = mem[0];
	for (pos = 1; pos < PAGE_SIZE / sizeof(*mem); pos++) {
		if (val != mem[pos]) {
			ret = false;
			goto out;
		}
	}

	*element = val;
out:
	return ret;
}

static bool chunk_same_pattern(char *page, unsigned long *element)
{
	unsigned int pos;
	unsigned long *mem;
	unsigned long val;
	bool ret = true;

	mem = (unsigned long *)page;
	val = mem[0];
	for (pos = 1; pos < CHUNK_SIZE * window_size / sizeof(*mem); pos++) {
		if (val != mem[pos]) {
			ret = false;
			goto out;
		}
	}

	*element = val;
out:
	return ret;
}

///////////////////////////////////////////////
static ssize_t run_test_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "compr time = %ld us, decompr time = %ld us\n", total_compr_time, total_decompr_time);
}

static ssize_t run_test_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	char real_trace_file_path[256] = {0};
    struct file *trace_file;
    struct compress_algorithm *alg;
    struct compr_buffer *cf, *tmp;
    unsigned long nr_page, file_size = 0;
    unsigned long nr_chunks, nr_chunks_remaining = 0;
    unsigned long nr_batches, nr_batch_remaining = 0;
    // struct list_head *pos, *n;
    
    char **data_pages;
    char **data_pages_next;
    // void *tmp_buffer;
    char *decompr_buffer, *shared_compr_buffer, *large_compr_buffer;
    struct list_head compr_buffer_list;
    struct list_head large_compr_buffer_list;
    ktime_t compr_start, compr_end, decompr_start, decompr_end;
    int large_compr_num = 0;
    unsigned int comp_len = 0;
    int i, ccnt, j = 0;
    int compr_cnt = 0;
    int same_pattern_num = 0;
    int total_compr_size = 0;

    INIT_LIST_HEAD(&compr_buffer_list);
    INIT_LIST_HEAD(&large_compr_buffer_list);

    printk(KERN_ALERT "[run_test_store] Run compress test. file = %s, window size = %d, alg = %s\n", trace_file_path, window_size, cur_alg);

    alg = alloc_compress_algriothm(cur_alg, window_size); // 先分配一个压缩算法
    if (!alg) {
        printk(KERN_ERR "Failed to allocate compress algorithm %s\n", cur_alg);
        return count;
    }

    sprintf(real_trace_file_path, "/data/local/tmp/compr_test/%s", trace_file_path);
    printk(KERN_ALERT "[run_test_store] Open trace file %s\n", real_trace_file_path);

    trace_file = filp_open(real_trace_file_path, O_RDONLY, 0644); // 打开trace文件, 这个文件位于/data/local/tmp/compr_test/目录下
    if (IS_ERR(trace_file)) {
        printk(KERN_ERR "Failed to open trace file %s\n", real_trace_file_path);
        goto free_alg;
    }

    file_size = i_size_read(trace_file->f_mapping->host);
    if(file_size % PAGE_SIZE == 0)
        nr_page = file_size / PAGE_SIZE;
    else
        nr_page = file_size / PAGE_SIZE + 1; // 计算这个文件有多少个4KB的页

    
    nr_batches = file_size / BATCH_SIZE;
    nr_batch_remaining = file_size % BATCH_SIZE;
    // calculate how many chunks in a batch
    nr_chunks = BATCH_SIZE / CHUNK_SIZE;
    nr_chunks_remaining = nr_batch_remaining / CHUNK_SIZE;
    printk(KERN_ALERT "[run_test_store] file size = %ld, nr_page = %ld, nr_chunks = %ld, nr_batches = %ld, nr_batch_remaining = %ld\n", file_size, nr_page, nr_chunks, nr_batches, nr_batch_remaining);
    printk(KERN_ALERT "[run_test_store] Start compress\n");
    shared_compr_buffer = kmalloc(window_size * CHUNK_SIZE, GFP_KERNEL); // 为了避免分配内存的开销影响了压缩性能，这里统一使用shared buffer
    data_pages = kmalloc(nr_chunks * sizeof(char *), GFP_KERNEL); // 分配一个数组保存从文件读取出来的每一个页的数据
    if (!data_pages) {
        printk(KERN_ERR "Failed to allocate memory for data pages\n");
        goto close_file;
    }

    data_pages_next = kmalloc(nr_chunks * sizeof(char *), GFP_KERNEL);
    if (!data_pages_next) {
        printk(KERN_ERR "Failed to allocate memory for data pages next\n");
        goto free_data_pages;
    }

    for (i = 0; i < nr_chunks; i++) {
        data_pages[i] = NULL;  
        data_pages_next[i] = NULL; 
    }
    if (!shared_compr_buffer) {
        printk(KERN_ERR "Failed to allocate memory for compress data\n");
        goto free_data_pages;
    }

    ccnt = 0;
    total_compr_time = 0;
    
    for (i = 0; i < nr_batches; i++){
        // read pages from file
        for (j = 0; j < nr_chunks; j++) {
            data_pages[j] = kmalloc(CHUNK_SIZE, GFP_KERNEL);  // 分配一个4KB的页用来保存读出来的数据
            if (!data_pages[j]) {
                printk(KERN_ERR "Failed to allocate memory for data page %d\n", j);
                goto free_data_pages;
            }
        }

        for(j = 0; j < nr_chunks; j++) {
            kernel_read(trace_file, data_pages[j], CHUNK_SIZE, &trace_file->f_pos); // 读取数据
        }        

        for (j = 0; j < nr_chunks; j++) {
            unsigned long element = 0;
            if(ccnt < window_size) { // 存放数据到window
                
                memcpy(shared_compr_buffer + ccnt * CHUNK_SIZE, data_pages[j], CHUNK_SIZE);
                ccnt++;
                continue;
            } else { // window满了就压缩
                unsigned int compr_len;
                struct compr_buffer *new_cf = alloc_compr_buffer(window_size); // 这里的compr_buffer的size是双倍window size去保存压缩后数据
                                                                            // 这是为了防止压缩后的数据比原数据大
                                                                            // 但是这种方式只能用于模拟，实际上这样做会导致大量的内存浪费
                                                                            // 在实际场景，需要使用先用一个shared buffer去保存压缩后的数据
                                                                            // 当确定压缩后的长度后，再分配内存保存压缩后的数据
                if (!new_cf) {
                    printk(KERN_ERR "Failed to allocate compress buffer\n");
                    goto free_compr_buffers;
                }
                if (chunk_same_pattern(shared_compr_buffer, &element)) {
                    // printk(KERN_ALERT "Chunk %d has same pattern %lu\n", j, element);
                    same_pattern_num++;
                    // continue;
                }
                compr_start = ktime_get();
                compress_pages(alg, shared_compr_buffer, window_size * CHUNK_SIZE, new_cf->data, &compr_len);
                compr_end = ktime_get();
                if (compr_len > window_size * CHUNK_SIZE) {
                    large_compr_num++;
                }
                total_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start)); // 计算压缩耗时
                new_cf->used_bytes = compr_len;
                new_cf->nr_compr_page = window_size;
                total_compr_size += compr_len;
                list_add_tail(&new_cf->list, &compr_buffer_list);
                ccnt = 0;
                memset(shared_compr_buffer, 0, window_size * CHUNK_SIZE);
                compr_cnt++;
                j--;
            }
        }
        // free data pages
        for (j = 0; j < nr_chunks; j++) {
            if (data_pages[j]) kfree(data_pages[j]);
            data_pages[j] = NULL;
        }

        if (i < (nr_batches - 1)) {
            for (j = 0; j < nr_chunks; j++){
                if (data_pages_next[j]) kfree(data_pages_next[j]);
                data_pages_next[j] = NULL;
            }
        }
    }

    kfree(shared_compr_buffer);
    printk(KERN_ALERT "[run_test_store] compr_cnt = %d\n", compr_cnt);
    printk(KERN_ALERT "[run_test_store] total_compr_size = %d\n", total_compr_size);
    printk(KERN_ALERT "[run_test_store] large_compr_num = %d\n", large_compr_num);
    printk(KERN_ALERT "[run_test_store] total compression time = %ld us\n", total_compr_time); 
    printk(KERN_ALERT "[run_test_store] Same pattern num = %d\n", same_pattern_num);

    // second level compression
    printk(KERN_ALERT "[run_test_store] Start second level compression\n");
    large_compr_buffer = kmalloc(large_window_size * window_size * CHUNK_SIZE, GFP_KERNEL);
    if (!large_compr_buffer) {
        printk(KERN_ERR "Failed to allocate memory for large compress data\n");
        goto free_compr_buffers;
    }
    comp_len = 0;
    ccnt = 0;
    compr_cnt = 0;
    total_compr_size = 0;
    // list_for_each_entry(cf, &compr_buffer_list, list) {
    //     memcpy(large_compr_buffer + comp_len, cf->data, cf->used_bytes);
    //     comp_len += cf->used_bytes;
    //     ccnt++;
    //     if (ccnt == large_window_size) {
    //         struct compr_buffer *new_cf = alloc_compr_buffer(window_size * large_window_size);
    //         compr_start = ktime_get();
    //         compress_pages(alg, large_compr_buffer, comp_len, new_cf->data, &new_cf->used_bytes);
    //         compr_end = ktime_get();
    //         total_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start));
    //         new_cf->nr_compr_page = large_window_size;
    //         total_compr_size += new_cf->used_bytes;
    //         list_add_tail(&new_cf->list, &large_compr_buffer_list);
    //         compr_cnt++;
    //         ccnt = 0;
    //         comp_len = 0;
    //     }
    // }
    kfree(large_compr_buffer);
    // printk(KERN_ALERT "compr_cnt = %d\n", compr_cnt);
    // printk(KERN_ALERT "large total_compr_size = %d\n", total_compr_size);
    // printk(KERN_ALERT "total large compression time = %ld us\n", total_compr_time); 
    // printk(KERN_ALERT "End second level compression\n");
    // decompression
    printk(KERN_ALERT "[run_test_store] Start decompress\n");
    decompr_buffer = kmalloc(window_size * CHUNK_SIZE, GFP_KERNEL); // 为了避免分配内存的开销影响了解压性能，这里统一使用shared buffer去存放解压后的数据
    if (!decompr_buffer) {
        printk(KERN_ERR "Failed to allocate memory for decompress data\n");
        goto free_compr_buffers;
    }

    total_decompr_time = 0;
    list_for_each_entry(cf, &compr_buffer_list, list) { // 这里进行解压, 没有内存分配的开销，纯解压性能
        
        unsigned int decompr_len = window_size * CHUNK_SIZE;
        decompr_start = ktime_get();
        decompress_pages(alg, cf->data, cf->used_bytes, decompr_buffer, &decompr_len);
        decompr_end = ktime_get();
        total_decompr_time += ktime_to_us(ktime_sub(decompr_end, decompr_start)); // 计算解压耗时
    }

    kfree(decompr_buffer);
    printk(KERN_ALERT "[run_test_store] compr time = %ld us, decompr time = %ld us\n", total_compr_time, total_decompr_time); // 打印耗时数据, 由于total_compr_time, total_decompr_time
                                                                                                                            // 是全局变量，所以可以在show函数中直接读取这两个变量的值
                                                                                                                            // 即 cat /sys/kernel/compress_test/run_test 就能看到结果
free_compr_buffers:
    list_for_each_entry_safe(cf, tmp, &compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

    list_for_each_entry_safe(cf, tmp, &large_compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

free_data_pages:
    for (i = 0; i < nr_chunks; i++){
        if (data_pages[i]) kfree(data_pages[i]);
        data_pages[i] = NULL;
        if (data_pages_next[i]) kfree(data_pages_next[i]);
        data_pages_next[i] = NULL;
    }
        
    kfree(data_pages);
    kfree(data_pages_next);
close_file:
    filp_close(trace_file, NULL);
free_alg:
    free_compress_algriothm(alg);
	return count;
}

static ssize_t replay_swapout_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "compr time = %ld us, decompr time = %ld us\n", total_compr_time, total_decompr_time);
}

static ssize_t replay_swapout_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    char real_swapot_meta_path[256] = {0};
    char real_swapot_data_path[256] = {0};
    struct file *meta_file;
    struct file *data_file;
    struct file *zram_map_file;
    struct file *zram_meta_file;
    struct file *zram_data_file;
    struct compress_algorithm *alg;
    struct compr_buffer *cf, *tmp;
    unsigned long nr_page, file_size = 0;
    unsigned long nr_chunks, nr_chunks_remaining = 0;
    unsigned long nr_batches, nr_batch_remaining = 0;
    unsigned long nr_pages_per_batch, nr_pages_remaining = 0;
    char **data_pages;
    struct compr_meta_info **meta_info;
    // char *meta_buf;
    char *shared_compr_buffer;
    struct compr_meta_info *shared_compr_meta_info;
    int nr_chunks_in_shared_buffer;
    struct list_head compr_buffer_list;
    struct list_head large_compr_buffer_list;
    ktime_t compr_start, compr_end;
    int i, ccnt, j, k, zscnt= 0;
    int compr_cnt = 0;

    int total_compr_size = 0;
    int small_compr_size = 0;
    int medium_compr_size = 0;
    int large_compr_size = 0;

    int total_compr_time = 0;
    int small_compr_time = 0;
    int medium_compr_time = 0;
    int large_compr_time = 0;

    // int meta_file_ret = 0;
    // char *meta_line, *meta_ptr;
    // loff_t meta_pos = 0;
    // struct zram_meta *temp_meta;
    

    INIT_LIST_HEAD(&compr_buffer_list);
    INIT_LIST_HEAD(&large_compr_buffer_list);
    compr_cnt = 0;
    zscnt = 0;
    k = 0;
    zram_map_file = filp_open("/data/local/tmp/compr_test/zram_map.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
    zram_meta_file = filp_open("/data/local/tmp/compr_test/zram_meta.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
    zram_data_file = filp_open("/data/local/tmp/compr_test/zram_data.txt", O_CREAT | O_RDWR | O_TRUNC, 0666);
    printk(KERN_ALERT "[run_test_store] Run compress test. meta_file = %s, data_file = %s, window size = %d, large window size = %d, alg = %s\n", swapout_meta_path, swapout_data_path, window_size, large_window_size, cur_alg);

    alg = alloc_compress_algriothm(cur_alg, window_size); // 先分配一个压缩算法
    if (!alg) {
        printk(KERN_ERR "Failed to allocate compress algorithm %s\n", cur_alg);
        return count;
    }

    sprintf(real_swapot_meta_path, "/data/local/tmp/compr_test/%s", swapout_meta_path);
    sprintf(real_swapot_data_path, "/data/local/tmp/compr_test/%s", swapout_data_path);

    meta_file = filp_open(real_swapot_meta_path, O_RDONLY, 0644);
    if (IS_ERR(meta_file)) {
        printk(KERN_ERR "Failed to open meta file %s\n", real_swapot_meta_path);
        goto free_alg;
    }

    data_file = filp_open(real_swapot_data_path, O_RDONLY, 0644);
    if (IS_ERR(data_file)) {
        printk(KERN_ERR "Failed to open data file %s\n", real_swapot_data_path);
        goto free_alg;
    }

    file_size = i_size_read(data_file->f_mapping->host);
    nr_page = file_size / PAGE_SIZE;
    nr_batches = file_size / BATCH_SIZE;
    nr_batch_remaining = file_size % BATCH_SIZE;
    // calculate how many chunks in a batch
    nr_chunks = BATCH_SIZE / CHUNK_SIZE;
    nr_chunks_remaining = nr_batch_remaining / CHUNK_SIZE;
    nr_pages_per_batch = BATCH_SIZE / PAGE_SIZE;
    nr_pages_remaining = nr_batch_remaining / PAGE_SIZE;
    printk(KERN_ALERT "[run_test_store] file size = %ld, nr_page = %ld, nr_chunks = %ld, nr_batches = %ld, nr_batch_remaining = %ld\n", file_size, nr_page, nr_chunks, nr_batches, nr_batch_remaining);
    printk(KERN_ALERT "[run_test_store] nr_pages_per_batch = %ld, nr_pages_remaining = %ld\n", nr_pages_per_batch, nr_pages_remaining);
    printk(KERN_ALERT "[run_test_store] Chunk size = %d, batch size = %d\n", CHUNK_SIZE, BATCH_SIZE);  
    printk(KERN_ALERT "[run_test_store] Start compress\n");

    shared_compr_buffer = kmalloc(large_window_size * CHUNK_SIZE, GFP_KERNEL); 
    shared_compr_meta_info = kmalloc(large_window_size * CHUNK_SIZE * sizeof(struct compr_meta_info) / PAGE_SIZE, GFP_KERNEL);
    nr_chunks_in_shared_buffer = 0;
    if (!shared_compr_buffer) {
        printk(KERN_ERR "Failed to allocate memory for compress data\n");
        goto close_file;
    }

    meta_info = kmalloc(nr_pages_per_batch * sizeof(struct compr_meta_info *), GFP_KERNEL);
    if (!meta_info) {
        printk(KERN_ERR "Failed to allocate memory for meta info\n");
        goto free_compr_buffers;
    }
    for (i = 0; i < nr_pages_per_batch; i++) {
        meta_info[i] = NULL;  
    }

    data_pages = kmalloc(nr_pages_per_batch * sizeof(char *), GFP_KERNEL);
    if (!data_pages) {
        printk(KERN_ERR "Failed to allocate memory for data pages\n");
        goto close_file;
    }
    for (i = 0; i < nr_pages_per_batch; i++) {
        data_pages[i] = NULL;  
    }

    for (i = 0; i <= nr_batches; i++) {
        // int lines_read = 0;
        if (i == nr_batches) {
            nr_pages_per_batch = nr_pages_remaining;
        }
        for (j = 0 ; j < nr_pages_per_batch; j++) {
            data_pages[j] = kmalloc(PAGE_SIZE, GFP_KERNEL);
            if (!data_pages[j]) {
                printk(KERN_ERR "Failed to allocate memory for data page %d\n", j);
                goto free_data_pages;
            }
        }

        for (j = 0; j < nr_pages_per_batch; j++) {
            kernel_read(data_file, data_pages[j], PAGE_SIZE, &data_file->f_pos);
        }

        for (j = 0; j < nr_pages_per_batch; j++) {
            meta_info[j] = kzalloc(sizeof(struct compr_meta_info), GFP_KERNEL);
            kernel_read(meta_file, &meta_info[j]->app_id,            8, &meta_file->f_pos);
            // printk(KERN_ALERT "app_id = %lu\n", meta_info[j]->app_id);
            kernel_read(meta_file, &meta_info[j]->pfn,               8, &meta_file->f_pos);
            // printk(KERN_ALERT "pfn = %lu\n", meta_info[j]->pfn);
            kernel_read(meta_file, &meta_info[j]->sector,            8, &meta_file->f_pos);
            // printk(KERN_ALERT "sector = %lu\n", meta_info[j]->sector);
            kernel_read(meta_file, &meta_info[j]->compression_scale, 8, &meta_file->f_pos);
            // printk(KERN_ALERT "compression_scale = %lu\n", meta_info[j]->compression_scale);
        }
        for (j = 0; j < nr_pages_per_batch; j++) {
            unsigned long element = 0;
            if (page_same_pattern(data_pages[j], &element)) {
                // printk(KERN_ALERT "Page %d has same pattern %lu\n", j, element);
                continue;
            };
            if (meta_info[j]->compression_scale == 0) { // use small scale compression
                struct zram_meta *new_meta = kzalloc(sizeof(struct zram_meta), GFP_KERNEL);
                struct zram_map_entry *temp_zram_map_entry = kzalloc(sizeof(struct zram_map_entry), GFP_KERNEL);
                new_meta->compression_scale = 0;
                new_meta->compression_window_size = window_size;
                new_meta->page_meta[0].app_id = meta_info[j]->app_id;
                new_meta->page_meta[0].sector = meta_info[j]->sector;
                new_meta->zram_slot_idx = zscnt;
                new_meta->data_offset = zram_data_file->f_pos;
                zscnt++;

                // write app_id, sector, zram_slot_idx to zram_map_file             
                temp_zram_map_entry->app_id = meta_info[j]->app_id;
                temp_zram_map_entry->sector = meta_info[j]->sector;
                temp_zram_map_entry->zram_slot_idx = new_meta->zram_slot_idx;
                kernel_write(zram_map_file, temp_zram_map_entry, sizeof(struct zram_map_entry), &zram_map_file->f_pos);
                kfree(temp_zram_map_entry);

                for (k = 0; k < PAGE_SIZE /(window_size * CHUNK_SIZE); k++) {
                    void *tmp_buffer = kmalloc(window_size * CHUNK_SIZE, GFP_KERNEL);
                    struct compr_buffer *new_cf = alloc_compr_buffer(window_size);
                    unsigned int compr_len;
                    memcpy(tmp_buffer, data_pages[j] + k * window_size * CHUNK_SIZE, window_size * CHUNK_SIZE);   
                    compr_start = ktime_get();        
                    compress_pages(alg, tmp_buffer, window_size * CHUNK_SIZE, new_cf->data, &compr_len);
                    compr_end = ktime_get();
                    small_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start));
                    small_compr_size += compr_len;
                    new_meta->compr_len[k] = compr_len;
                    // write compressed data to zram_data_file
                    kernel_write(zram_data_file, new_cf->data, compr_len, &zram_data_file->f_pos);
                    kfree(tmp_buffer);
                    free_compr_buffer(new_cf);
                }
                // write meta info to zram_meta_file
                kernel_write(zram_meta_file, new_meta, sizeof(struct zram_meta), &zram_meta_file->f_pos);
                kfree(new_meta);
            } else if(meta_info[j]->compression_scale == 1){ // use medium scale compression
                struct zram_meta *new_meta = kzalloc(sizeof(struct zram_meta), GFP_KERNEL);
                struct zram_map_entry *temp_zram_map_entry = kzalloc(sizeof(struct zram_map_entry), GFP_KERNEL);
                new_meta->compression_scale = 1;
                new_meta->compression_window_size = medium_window_size;
                new_meta->page_meta[0].app_id = meta_info[j]->app_id;
                new_meta->page_meta[0].sector = meta_info[j]->sector;
                new_meta->zram_slot_idx = zscnt;
                new_meta->data_offset = zram_data_file->f_pos;
                zscnt++;

                // write app_id, sector, zram_slot_idx to zram_map_file             
                temp_zram_map_entry->app_id = meta_info[j]->app_id;
                temp_zram_map_entry->sector = meta_info[j]->sector;
                temp_zram_map_entry->zram_slot_idx = new_meta->zram_slot_idx;
                kernel_write(zram_map_file, temp_zram_map_entry, sizeof(struct zram_map_entry), &zram_map_file->f_pos);
                kfree(temp_zram_map_entry);

                for (k = 0; k < PAGE_SIZE /(medium_window_size * CHUNK_SIZE); k++) {
                    void *tmp_buffer = kmalloc(medium_window_size * CHUNK_SIZE, GFP_KERNEL);
                    struct compr_buffer *new_cf = alloc_compr_buffer(medium_window_size);
                    unsigned int compr_len;
                    memcpy(tmp_buffer, data_pages[j] + k * medium_window_size * CHUNK_SIZE, medium_window_size * CHUNK_SIZE); 
                    compr_start = ktime_get();          
                    compress_pages(alg, tmp_buffer, medium_window_size * CHUNK_SIZE, new_cf->data, &compr_len);
                    compr_end = ktime_get();
                    medium_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start));
                    medium_compr_size += compr_len;
                    new_meta->compr_len[k] = compr_len;
                    // write compressed data to zram_data_file
                    kernel_write(zram_data_file, new_cf->data, compr_len, &zram_data_file->f_pos);
                    kfree(tmp_buffer);
                    free_compr_buffer(new_cf);
                }
                // write meta info to zram_meta_file
                kernel_write(zram_meta_file, new_meta, sizeof(struct zram_meta), &zram_meta_file->f_pos);
                kfree(new_meta);
            } else if(meta_info[j]->compression_scale == 2){ // use large scale compression
                // add data to shared_compr_buffer
                memcpy(shared_compr_buffer + nr_chunks_in_shared_buffer * CHUNK_SIZE, data_pages[j], PAGE_SIZE);
                shared_compr_meta_info[nr_chunks_in_shared_buffer * CHUNK_SIZE / PAGE_SIZE] = *meta_info[j];
                nr_chunks_in_shared_buffer += PAGE_SIZE / CHUNK_SIZE;
                if (nr_chunks_in_shared_buffer == large_window_size) {
                    struct zram_meta *new_meta = kzalloc(sizeof(struct zram_meta), GFP_KERNEL);
                    struct compr_buffer *new_cf = alloc_compr_buffer(large_window_size);
                    unsigned int compr_len;
                    new_meta->zram_slot_idx = zscnt;
                    new_meta->data_offset = zram_data_file->f_pos;
                    new_meta->compression_scale = 2;
                    new_meta->compression_window_size = large_window_size;
                    zscnt++;
                    compr_start = ktime_get();
                    compress_pages(alg, shared_compr_buffer, large_window_size * CHUNK_SIZE, new_cf->data, &compr_len);   
                    compr_end = ktime_get();
                    large_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start));
                    large_compr_size += compr_len;             
                    kernel_write(zram_data_file, new_cf->data, compr_len, &zram_data_file->f_pos);
                    new_meta->compr_len[0] = compr_len;
                    for (k = 0; k < large_window_size * CHUNK_SIZE / PAGE_SIZE; k++) {
                        struct zram_map_entry *temp_zram_map_entry = kzalloc(sizeof(struct zram_map_entry), GFP_KERNEL);                 
                        new_meta->page_meta[k].app_id = shared_compr_meta_info[k].app_id;
                        new_meta->page_meta[k].sector = shared_compr_meta_info[k].sector;
                        // write app_id, sector, zram_slot_idx to zram_map_file
                        temp_zram_map_entry->app_id = shared_compr_meta_info[k].app_id;
                        temp_zram_map_entry->sector = shared_compr_meta_info[k].sector;
                        temp_zram_map_entry->zram_slot_idx = new_meta->zram_slot_idx;
                        kernel_write(zram_map_file, temp_zram_map_entry, sizeof(struct zram_map_entry), &zram_map_file->f_pos);
                        kfree(temp_zram_map_entry);
                    }
                    // write meta info to zram_meta_file
                    kernel_write(zram_meta_file, new_meta, sizeof(struct zram_meta), &zram_meta_file->f_pos);
                    kfree(new_meta);
                    free_compr_buffer(new_cf);
                    nr_chunks_in_shared_buffer = 0;
                }
            }
        } 

        for (j = 0; j < nr_pages_per_batch; j++) {
            if (data_pages[j]) kfree(data_pages[j]);
            data_pages[j] = NULL;
            if (meta_info[j]) kfree(meta_info[j]);
            meta_info[j] = NULL;
        }
    }    

    ccnt = 0;
    total_compr_time = small_compr_time + medium_compr_time + large_compr_time;
    total_compr_size = small_compr_size + medium_compr_size + large_compr_size;
    printk(KERN_ALERT "[run_test_store] total_compr_size = %d\n", total_compr_size);
    printk(KERN_ALERT "[run_test_store] small_compr_size = %d\n", small_compr_size);
    printk(KERN_ALERT "[run_test_store] medium_compr_size = %d\n", medium_compr_size);
    printk(KERN_ALERT "[run_test_store] large_compr_size = %d\n", large_compr_size);

    printk(KERN_ALERT "[run_test_store] total compression time = %ld us\n", total_compr_time);
    printk(KERN_ALERT "[run_test_store] small compression time = %ld us\n", small_compr_time);
    printk(KERN_ALERT "[run_test_store] medium compression time = %ld us\n", medium_compr_time);
    printk(KERN_ALERT "[run_test_store] large compression time = %ld us\n", large_compr_time);
    
    // print first 10 entries of zram_meta_file
    // meta_pos = 0;
    // for (i = 0; i < 10; i++) {
    //     char *meta_buf = kmalloc(sizeof(struct zram_meta), GFP_KERNEL);
    //     kernel_read(zram_meta_file, meta_buf, sizeof(struct zram_meta), &meta_pos);
    //     temp_meta = (struct zram_meta *)meta_buf;
    //     printk(KERN_ALERT "app_id = %lu, sector = %lu, zram_slot_idx = %lu, data_offset = %lu\n", temp_meta->page_meta[0].app_id, temp_meta->page_meta[0].sector, temp_meta->zram_slot_idx, temp_meta->data_offset);
    //     kfree(meta_buf);
    // }

    kfree(shared_compr_buffer);
    kfree(shared_compr_meta_info);
free_compr_buffers:
    list_for_each_entry_safe(cf, tmp, &compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

    list_for_each_entry_safe(cf, tmp, &large_compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

free_data_pages:
    for (i = 0; i < nr_pages_per_batch; i++){
        if (data_pages[i]) kfree(data_pages[i]);
        data_pages[i] = NULL;
    }
    kfree(data_pages);
    for (i = 0; i < nr_pages_per_batch; i++){
        if (meta_info[i]) kfree(meta_info[i]);
        meta_info[i] = NULL;
    }
    kfree(meta_info);
close_file:
    filp_close(meta_file, NULL);
    filp_close(data_file, NULL);
    filp_close(zram_map_file, NULL);
    filp_close(zram_meta_file, NULL);
    filp_close(zram_data_file, NULL);
free_alg:
    free_compress_algriothm(alg);
	return count;
}

static struct kobj_attribute run_test_attribute =
	__ATTR(run_test, 0664, run_test_show, run_test_store);

static struct attribute *run_test_attrs[] = {
	&run_test_attribute.attr,
	NULL,
};

static struct attribute_group run_test_attr_group = {
	.attrs = run_test_attrs,
};

/////////////////////////////////////////////
static ssize_t replay_swapin_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "compr time = %ld us, decompr time = %ld us\n", total_compr_time, total_decompr_time);
}

static ssize_t replay_swapin_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    char real_swapin_meta_path[256] = {0};
    struct file *meta_file;
    struct file *zram_map_file;
    struct file *zram_meta_file;
    struct file *zram_data_file;
    struct compress_algorithm *alg;
    struct compr_buffer *cf, *tmp;
    struct decompr_cache *dc, *tmp_dc;
    // char *decompr_buffer;
    // struct zram_meta *zram_meta;
    struct zram_map_entry *zram_map_table = NULL;
    char *shared_compr_buffer;
    struct compr_meta_info *shared_compr_meta_info;
    struct list_head compr_buffer_list;
    struct list_head large_compr_buffer_list;
    struct list_head decompr_cache_list;
    unsigned long zram_map_size, swapin_meta_size;
    unsigned long nr_zram_map_entries, nr_swapin_meta_entries;
    ktime_t decompr_start, decompr_end;
    int i;
    int j;
    int k;
    int ccnt;
    int large_cnt = 0, medium_cnt = 0, small_cnt = 0;
    // int zscnt = 0;
    // char *meta_line, *meta_ptr;
    loff_t meta_pos = 0;
    // struct zram_meta *temp_meta;
    
    INIT_LIST_HEAD(&decompr_cache_list);
    INIT_LIST_HEAD(&compr_buffer_list);
    INIT_LIST_HEAD(&large_compr_buffer_list);
    zram_map_file = filp_open("/data/local/tmp/compr_test/zram_map.txt", O_RDONLY, 0644);
    zram_meta_file = filp_open("/data/local/tmp/compr_test/zram_meta.txt", O_RDONLY, 0644);
    zram_data_file = filp_open("/data/local/tmp/compr_test/zram_data.txt", O_RDONLY, 0644);
    printk(KERN_ALERT "[run_test_store] Run decompress test. meta_file = %s, window size = %d, large window size = %d, alg = %s\n", swapin_meta_path, window_size, large_window_size, cur_alg);

    alg = alloc_compress_algriothm(cur_alg, window_size); // 先分配一个压缩算法
    if (!alg) {
        printk(KERN_ERR "Failed to allocate compress algorithm %s\n", cur_alg);
        goto free_compr_buffers;
    }

    sprintf(real_swapin_meta_path, "/data/local/tmp/compr_test/%s", swapin_meta_path);

    meta_file = filp_open(real_swapin_meta_path, O_RDONLY, 0644);
    if (IS_ERR(meta_file)) {
        printk(KERN_ERR "Failed to open meta file %s\n", real_swapin_meta_path);
        goto free_alg;
    }

    shared_compr_buffer = kmalloc(large_window_size * CHUNK_SIZE, GFP_KERNEL); 
    shared_compr_meta_info = kmalloc(large_window_size * CHUNK_SIZE * sizeof(struct compr_meta_info) / PAGE_SIZE, GFP_KERNEL);
    if (!shared_compr_buffer) {
        printk(KERN_ERR "Failed to allocate memory for compress data\n");
        goto close_file;
    }

    ccnt = 0;
    total_compr_time = 0;
    
    
    // print first 10 entries of zram_meta_file
    meta_pos = 0;
    for (i = 0; i < 10; i++) {
        struct zram_meta *temp_meta = kmalloc(sizeof(struct zram_meta), GFP_KERNEL);
        kernel_read(zram_meta_file, temp_meta, sizeof(struct zram_meta), &meta_pos);
        printk(KERN_ALERT "Read zram_meta: app_id = %lu, sector = %lu, zram_slot_idx = %lu, data_offset = %lu\n", temp_meta->page_meta[0].app_id, temp_meta->page_meta[0].sector, temp_meta->zram_slot_idx, temp_meta->data_offset);
        kfree(temp_meta);
    }

    // get the sizes of zram_map_file
    zram_map_size = i_size_read(zram_map_file->f_mapping->host);
    nr_zram_map_entries = zram_map_size / sizeof(struct zram_map_entry);
    printk(KERN_ALERT "zram_map_size = %lu, nr_zram_map_entries = %lu\n", zram_map_size, nr_zram_map_entries);
    // allocate memory for zram_map_table
    zram_map_table = vmalloc(nr_zram_map_entries * sizeof(struct zram_map_entry));
    if (!zram_map_table) {
        printk(KERN_ERR "Failed to allocate memory for zram map table\n");
        goto close_file;
    }
    // read zram_map_file
    for (i = 0; i < nr_zram_map_entries; i++) {
        kernel_read(zram_map_file, &zram_map_table[i], sizeof(struct zram_map_entry), &zram_map_file->f_pos);
        // printk(KERN_ALERT "Read zram_map: app_id = %lu, sector = %lu, zram_slot_idx = %lu\n", zram_map_table[i].app_id, zram_map_table[i].sector, zram_map_table[i].zram_slot_idx);
    }
    
    // get the size of swapin_meta_file
    swapin_meta_size = i_size_read(meta_file->f_mapping->host);
    nr_swapin_meta_entries = swapin_meta_size / 16;
    total_decompr_time = 0;
    total_decompr_time_small = 0;
    total_decompr_time_medium = 0;
    total_decompr_time_large = 0;
    // read swapin_meta_file
    for (i = 0; i < nr_swapin_meta_entries; i++) {
        unsigned long app_id, sector;
        kernel_read(meta_file, &app_id, 8, &meta_file->f_pos);
        kernel_read(meta_file, &sector, 8, &meta_file->f_pos);
        // printk(KERN_ALERT "Read swapin_meta: app_id = %lu, sector = %lu\n", app_id, sector);
        // find the corresponding zram_slot_idx in zram_map_table
        for (j = 0; j < nr_zram_map_entries; j++) {
            unsigned long zram_slot_idx;
            if ((zram_map_table[j].app_id == app_id) && (zram_map_table[j].sector == sector)) {
                struct zram_meta *temp_meta = kmalloc(sizeof(struct zram_meta), GFP_KERNEL);
                unsigned long compr_len;
                zram_slot_idx = zram_map_table[j].zram_slot_idx;
                meta_pos = zram_slot_idx * sizeof(struct zram_meta);
                // printk(KERN_ALERT "Find zram_slot_idx = %lu\n", zram_slot_idx);
                // read zram_meta_file
                kernel_read(zram_meta_file, temp_meta, sizeof(struct zram_meta), &meta_pos);
                // printk(KERN_ALERT "Read zram_meta: app_id = %lu, sector = %lu, zram_slot_idx = %lu, data_offset = %lu\n", temp_meta->page_meta[0].app_id, temp_meta->page_meta[0].sector, temp_meta->zram_slot_idx, temp_meta->data_offset);
                if (temp_meta->compression_scale == 0) {
                    loff_t data_pos = temp_meta->data_offset;
                    struct compr_buffer *new_cf = alloc_compr_buffer(window_size);
                    unsigned int decompr_len = window_size * CHUNK_SIZE;
                    for (k = 0; k < PAGE_SIZE /(window_size * CHUNK_SIZE); k++) {
                        void *tmp_buffer = kmalloc(window_size * CHUNK_SIZE, GFP_KERNEL);
                        compr_len = temp_meta->compr_len[k];
                        kernel_read(zram_data_file, new_cf->data, compr_len, &data_pos);
                        // flush cache of new_cf->data
                        decompr_start = ktime_get();
                        decompress_pages(alg, new_cf->data, compr_len, tmp_buffer, &decompr_len);
                        decompr_end = ktime_get();
                        total_decompr_time_small += ktime_to_us(ktime_sub(decompr_end, decompr_start));
                        // printk(KERN_ALERT"decopress length = %d\n", decompr_len);
                        kfree(tmp_buffer);
                    }
                    small_cnt++;
                    // printk(KERN_ALERT "Decompress with small scale\n");
                    free_compr_buffer(new_cf);
                } else if (temp_meta->compression_scale == 1) {
                    loff_t data_pos = temp_meta->data_offset;
                    struct compr_buffer *new_cf = alloc_compr_buffer(medium_window_size);
                    unsigned int decompr_len = medium_window_size * CHUNK_SIZE;
                    for (k = 0; k < PAGE_SIZE /(medium_window_size * CHUNK_SIZE); k++) {
                        void *tmp_buffer = kmalloc(medium_window_size * CHUNK_SIZE, GFP_KERNEL);
                        compr_len = temp_meta->compr_len[k];
                        kernel_read(zram_data_file, new_cf->data, compr_len, &data_pos);
                        // flush cache of new_cf->data
                        decompr_start = ktime_get();
                        decompress_pages(alg, new_cf->data, compr_len, tmp_buffer, &decompr_len);
                        decompr_end = ktime_get();
                        total_decompr_time_medium += ktime_to_us(ktime_sub(decompr_end, decompr_start));
                        // printk(KERN_ALERT"decopress length = %d\n", decompr_len);
                        kfree(tmp_buffer);
                    }
                    medium_cnt++;
                    // printk(KERN_ALERT "Decompress with medium scale\n");
                    free_compr_buffer(new_cf);
                } else if (temp_meta->compression_scale == 2) {
                    loff_t data_pos = temp_meta->data_offset;
                    struct compr_buffer *new_cf = alloc_compr_buffer(large_window_size);
                    int skip = 0;
                    unsigned int decompr_len = large_window_size * CHUNK_SIZE;
                    // check if the data is in decompr_cache_list
                    list_for_each_entry(dc, &decompr_cache_list, list) {
                        if (dc->page_meta.app_id == app_id && dc->page_meta.sector == sector) {
                            // printk(KERN_ALERT "Find in decompr_cache_list\n");
                            skip = 1;
                            break;
                        }
                    }
                    
                    if (skip) {
                        free_compr_buffer(new_cf);
                        continue;
                    }

                    kernel_read(zram_data_file, new_cf->data, temp_meta->compr_len[0], &data_pos);
                    decompr_start = ktime_get();
                    decompress_pages(alg, new_cf->data, temp_meta->compr_len[0], shared_compr_buffer, &decompr_len);
                    decompr_end = ktime_get();
                    total_decompr_time_large += ktime_to_us(ktime_sub(decompr_end, decompr_start));
                    large_cnt++;
                    // Add each entry in zram_meta to decompr_cache_list
                    for (k = 0; k < large_window_size * CHUNK_SIZE / PAGE_SIZE; k++) {
                        struct decompr_cache *new_dc = alloc_decompr_cache();
                        new_dc->page_meta = temp_meta->page_meta[k];
                        list_add_tail(&new_dc->list, &decompr_cache_list);
                    }
                    // printk(KERN_ALERT"decopress length = %d\n", decompr_len);
                    free_compr_buffer(new_cf);
                }
                kfree(temp_meta);
                break;
            }
        }
    }

    printk(KERN_ALERT "[run_test_store] small_cnt = %d, medium_cnt = %d, large_cnt = %d\n", small_cnt, medium_cnt, large_cnt);
    printk(KERN_ALERT "[run_test_store] small total_decompr_time = %ld us", total_decompr_time_small);
    printk(KERN_ALERT "[run_test_store] medium total_decompr_time = %ld us", total_decompr_time_medium);
    printk(KERN_ALERT "[run_test_store] large total_decompr_time = %ld us", total_decompr_time_large);
    total_decompr_time = total_decompr_time_small + total_decompr_time_medium + total_decompr_time_large;
    printk(KERN_ALERT "[run_test_store] total_decompr_time = %ld us", total_decompr_time);
    
    kfree(shared_compr_buffer);
    kfree(shared_compr_meta_info);
free_compr_buffers:
    list_for_each_entry_safe(cf, tmp, &compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

    list_for_each_entry_safe(cf, tmp, &large_compr_buffer_list, list) {
        list_del(&cf->list);
        free_compr_buffer(cf);
    }

    list_for_each_entry_safe(dc, tmp_dc, &decompr_cache_list, list) {
        list_del(&dc->list);
        free_decompr_cache(dc);
    }

close_file:
    filp_close(meta_file, NULL);
    filp_close(zram_map_file, NULL);
    filp_close(zram_meta_file, NULL);
    filp_close(zram_data_file, NULL);
    if (zram_map_table) vfree(zram_map_table);
free_alg:
    free_compress_algriothm(alg);
	return count;
}


static struct kobj_attribute replay_swapout_trace_attribute =
	__ATTR(replay_swapout_trace, 0664, replay_swapout_trace_show, replay_swapout_trace_store);


static struct attribute *replay_swapout_trace_attrs[] = {
	&replay_swapout_trace_attribute.attr,
	NULL,
};

static struct attribute_group replay_swapout_trace_attr_group = {
	.attrs = replay_swapout_trace_attrs,
};

/////////////////////////////////////////////
static struct kobj_attribute replay_swapin_trace_attribute =
	__ATTR(replay_swapin_trace, 0664, replay_swapin_trace_show, replay_swapin_trace_store);


static struct attribute *replay_swapin_trace_attrs[] = {
	&replay_swapin_trace_attribute.attr,
	NULL,
};

static struct attribute_group replay_swapin_trace_attr_group = {
	.attrs = replay_swapin_trace_attrs,
};

///////////////////////////////////////////////
static ssize_t window_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", window_size);
}

static ssize_t window_size_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &window_size);
	return count;
}



static struct kobj_attribute window_size_attribute =
	__ATTR(window_size, 0664, window_size_show, window_size_store);


static struct attribute *window_size_attrs[] = {
	&window_size_attribute.attr,
	NULL,
};

static struct attribute_group window_size_attr_group = {
	.attrs = window_size_attrs,
};

static ssize_t large_window_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", large_window_size);
}

static ssize_t large_window_size_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &large_window_size);
	return count;
}

static struct kobj_attribute large_window_size_attribute =
	__ATTR(large_window_size, 0664, large_window_size_show, large_window_size_store);

static struct attribute *large_window_size_attrs[] = {
	&large_window_size_attribute.attr,
	NULL,
};

static struct attribute_group large_window_size_attr_group = {
	.attrs = large_window_size_attrs,
};

///////////////////////////////////////////////
static ssize_t medium_window_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", medium_window_size);
}

static ssize_t medium_window_size_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	sscanf(buf, "%d\n", &medium_window_size);
	return count;
}

static struct kobj_attribute medium_window_size_attribute =
	__ATTR(medium_window_size, 0664, medium_window_size_show, medium_window_size_store);

static struct attribute *medium_window_size_attrs[] = {
	&medium_window_size_attribute.attr,
	NULL,
};

static struct attribute_group medium_window_size_attr_group = {
	.attrs = medium_window_size_attrs,
};
///////////////////////////////////////////////
static ssize_t trace_file_path_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", trace_file_path);
}

static ssize_t trace_file_path_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    memset(trace_file_path, 0, sizeof(trace_file_path));
	sscanf(buf, "%s\n", trace_file_path);
    return count;
}

static struct kobj_attribute trace_file_path_attribute =
	__ATTR(trace_file_path, 0664, trace_file_path_show, trace_file_path_store);


static struct attribute *trace_file_path_attrs[] = {
	&trace_file_path_attribute.attr,
	NULL,
};

static struct attribute_group trace_file_path_attr_group = {
	.attrs = trace_file_path_attrs,
};

///////////////////////////////////////////////
static ssize_t swapin_meta_path_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", swapin_meta_path);
}

static ssize_t swapin_meta_path_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    memset(swapin_meta_path, 0, sizeof(swapin_meta_path));
	sscanf(buf, "%s\n", swapin_meta_path);
    return count;
}

static struct kobj_attribute swapin_meta_path_attribute =
	__ATTR(swapin_meta_path, 0664, swapin_meta_path_show, swapin_meta_path_store);


static struct attribute *swapin_meta_path_attrs[] = {
	&swapin_meta_path_attribute.attr,
	NULL,
};

static struct attribute_group swapin_meta_path_attr_group = {
	.attrs = swapin_meta_path_attrs,
};

///////////////////////////////////////////////
static ssize_t swapout_meta_path_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", swapout_meta_path);
}

static ssize_t swapout_meta_path_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    memset(swapout_meta_path, 0, sizeof(swapout_meta_path));
	sscanf(buf, "%s\n", swapout_meta_path);
    return count;
}

static struct kobj_attribute swapout_meta_path_attribute =
	__ATTR(swapout_meta_path, 0664, swapout_meta_path_show, swapout_meta_path_store);


static struct attribute *swapout_meta_path_attrs[] = {
	&swapout_meta_path_attribute.attr,
	NULL,
};

static struct attribute_group swapout_meta_path_attr_group = {
	.attrs = swapout_meta_path_attrs,
};


///////////////////////////////////////////////
static ssize_t swapout_data_path_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", swapout_data_path);
}

static ssize_t swapout_data_path_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    memset(swapout_data_path, 0, sizeof(swapout_data_path));
	sscanf(buf, "%s\n", swapout_data_path);
    return count;
}

static struct kobj_attribute swapout_data_path_attribute =
	__ATTR(swapout_data_path, 0664, swapout_data_path_show, swapout_data_path_store);


static struct attribute *swapout_data_path_attrs[] = {
	&swapout_data_path_attribute.attr,
	NULL,
};

static struct attribute_group swapout_data_path_attr_group = {
	.attrs = swapout_data_path_attrs,
};

///////////////////////////////////////////////
static ssize_t compr_alg_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%s\n", cur_alg);
}

static ssize_t compr_alg_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    int i;
    for (i = 0; i < NR_COMPRESS_ALG; i++) {
        if (strcmp(buf, compress_alg_type[i]) == 0) {
            cur_alg = compress_alg_type[i];
            return count;
        }
    }
    printk(KERN_ERR "Invalid compress algorithm %s\n", buf);
    return count;
}

static struct kobj_attribute compr_alg_attribute =
	__ATTR(compr_alg, 0664, compr_alg_show, compr_alg_store);


static struct attribute *compr_alg_attrs[] = {
	&compr_alg_attribute.attr,
	NULL,
};

static struct attribute_group compr_alg_attr_group = {
	.attrs = compr_alg_attrs,
};
///////////////////////////////////////////////
static int __init compress_test_init(void)
{
	int retval;

    cur_alg = compress_alg_type[COMPRESS_LZO];
    strcpy(trace_file_path, "trace.data");

    parent_dir_kobj = kobject_create_and_add("compress_test", kernel_kobj);
    if (!parent_dir_kobj)
        return -ENOMEM;
    
    retval = sysfs_create_group(parent_dir_kobj, &large_window_size_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &medium_window_size_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &window_size_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &trace_file_path_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &swapin_meta_path_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &swapout_meta_path_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &swapout_data_path_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &run_test_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &replay_swapout_trace_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &replay_swapin_trace_attr_group);
    if (retval)
        goto free_parent_dir_obj;

    retval = sysfs_create_group(parent_dir_kobj, &compr_alg_attr_group);
    if (retval) {
        goto free_parent_dir_obj;
    }

	return 0;
free_parent_dir_obj:
    kobject_put(parent_dir_kobj);
    return retval;
}

static void __exit compress_test_exit(void)
{
    kobject_put(parent_dir_kobj);
}

module_init(compress_test_init);
module_exit(compress_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Developers");

// data_pages = kmalloc(nr_page * sizeof(char *), GFP_KERNEL); // 分配一个数组保存从文件读取出来的每一个页的数据
// if (!data_pages) {
//     printk(KERN_ERR "Failed to allocate memory for data pages\n");
//     goto close_file;
// }

// for (i = 0; i < nr_page; i++, nr_alloced_page++) {
//     data_pages[i] = kmalloc(PAGE_SIZE, GFP_KERNEL);  // 分配一个4KB的页用来保存读出来的数据
//     if (!data_pages[i]) {
//         printk(KERN_ERR "Failed to allocate memory for data page %d\n", i);
//         goto free_data_pages;
//     }
// }

// printk(KERN_ALERT "[run_test_store] Read data from file\n");
// for(i = 0; i < nr_page; i++) {
//     kernel_read(trace_file, data_pages[i], CHUNK_SIZE, &trace_file->f_pos); // 读取数据
// }

// for (i = 0; i < nr_page; i++) {
//     if(ccnt < window_size) { // 存放数据到window
//         tmp_buffer = kmalloc(CHUNK_SIZE, GFP_KERNEL);
//         if (!tmp_buffer) {
//             printk(KERN_ERR "Failed to allocate memory for tmp buffer\n");
//             goto free_compr_buffers;
//         }
//         kernel_read(trace_file, tmp_buffer, CHUNK_SIZE, &trace_file->f_pos);
//         // printk("f_pos is %lld\n", trace_file->f_pos);
//         memcpy(shared_compr_buffer + ccnt * CHUNK_SIZE, trace_file, CHUNK_SIZE);
//         kfree(tmp_buffer);
//         ccnt++;
//         continue;
//     } else { // window满了就压缩
//         unsigned int compr_len;
//         struct compr_buffer *new_cf = alloc_compr_buffer(window_size); // 这里的compr_buffer的size是双倍window size去保存压缩后数据
//                                                                        // 这是为了防止压缩后的数据比原数据大
//                                                                        // 但是这种方式只能用于模拟，实际上这样做会导致大量的内存浪费
//                                                                        // 在实际场景，需要使用先用一个shared buffer去保存压缩后的数据
//                                                                        // 当确定压缩后的长度后，再分配内存保存压缩后的数据
//         if (!new_cf) {
//             printk(KERN_ERR "Failed to allocate compress buffer\n");
//             goto free_compr_buffers;
//         }

//         compr_start = ktime_get();
//         compress_pages(alg, shared_compr_buffer, window_size * CHUNK_SIZE, new_cf->data, &compr_len);
//         compr_end = ktime_get();
//         if (compr_len > window_size * CHUNK_SIZE) {
//             large_compr_num++;
//         }
//         total_compr_time += ktime_to_us(ktime_sub(compr_end, compr_start)); // 计算压缩耗时
//         new_cf->used_bytes = compr_len;
//         new_cf->nr_compr_page = window_size;
//         total_compr_size += compr_len;
//         list_add_tail(&new_cf->list, &compr_buffer_list);
//         ccnt = 0;
//         memset(shared_compr_buffer, 0, window_size * CHUNK_SIZE);
//         compr_cnt++;
//         i--;
//     }
// }




// Read a line from meta_file
// meta_buf = kmalloc(META_BUFFER_SIZE, GFP_KERNEL);
// meta_file_ret = kernel_read(meta_file, meta_buf, META_BUFFER_SIZE, &meta_pos);
// if (meta_file_ret <= 0) {
//     printk(KERN_ERR "Failed to read meta file\n");
//     goto free_data_pages;
// }
// meta_buf[meta_file_ret - 1] = '\0';
// for (k = 0; k < meta_file_ret; k++) {
//     if (meta_buf[k] == '\n') {
//         int line_size = k + 1;
//         char *meta_line = kmalloc(line_size, GFP_KERNEL);
//         memcpy(meta_line, meta_buf, line_size);
//         meta_line[line_size - 1] = '\0';
//         meta_info[j] = kmalloc(sizeof(struct compr_meta_info), GFP_KERNEL);
//         sscanf(meta_line, "%lu,%lu,%lu,%lu", &meta_info[j]->app_id, 
//                                              &meta_info[j]->pfn, 
//                                              &meta_info[j]->sector, 
//                                              &meta_info[j]->compression_scale);
//         meta_pos = meta_pos + line_size - meta_file_ret;
//         kfree(meta_line);
//         break;
//     }
// }
// kfree(meta_buf);