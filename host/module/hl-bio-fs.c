
/* This file creates a kernel thread that sends logs to the audit device
   using Linux's Block IO (BIO) or Virtual FileSystem (VFS) interfaces. */

/* General headers */
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/hardlog.h>
#include <linux/mutex.h>

/* File IO-related headers */
#include <linux/fs.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

/* Sleep and timer headers */
#include <linux/hrtimer.h>
#include <linux/delay.h>

#include "hl.h"

/* Defined in hl-main.c. */
extern char* device;
extern bool biof;
extern bool debug;
extern bool vmallocf;
extern char* hardlog_welcome;
extern struct audit_circ_buffer* audit_cbuff_ptr;

/* File and Blockdev for the USB storage. */
static struct file* device_file;
static struct block_device* bdevice = NULL;
static uint64_t bdevice_sector = 0;
static char* tmpbuf;

/* Timing purposes. */
uint64_t start_us, end_us = 0;

/* Block IO (BIO) request holders. */
#define SECTORS_PER_PAGE (PAGE_SIZE/512)
static struct bio* usb_bio;

bool bio_open(void) 
{
    int len = 0;

    HARDLOG_MODULE_PRINT("detail: trying to open %s\n", device);

    /* Use Linux BIO to open the audit device. */
    bdevice = blkdev_get_by_path(device, FMODE_READ | FMODE_WRITE, NULL);

    /* Perform various sanity checks. */
    if (IS_ERR(bdevice) || !bdevice) {
        HARDLOG_MODULE_PRINT("error: failed to open the device (%s) using lookup_bdev().\n", device);
        return false;
    }
    if (!bdevice->bd_disk) {
        HARDLOG_MODULE_PRINT("error: bdevice not correct.\n");
        return false;
    }
    if (strcmp(bdevice->bd_disk->disk_name, "sdb") != 0) {
        HARDLOG_MODULE_PRINT("error: bdevice: %s, device: %s.\n", bdevice->bd_disk->disk_name, device);
        return false;
    }
    HARDLOG_MODULE_PRINT("success: opened %s as a block device.\n", bdevice->bd_disk->disk_name);

    /* Allocate the BIO struct for USB communication. */
    usb_bio = bio_alloc(GFP_NOIO, 256);
    if (IS_ERR(usb_bio) || !usb_bio) {
        HARDLOG_MODULE_PRINT("error: failed to allocate a bio structure.\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("success: opened usb_bio.\n");

    HARDLOG_MODULE_PRINT("detail: hardlog welcome buffer is at %px.\n", hardlog_welcome);
    if (!vmalloc_to_page((void*) hardlog_welcome)) {
        HARDLOG_MODULE_PRINT("error: cannot translate page to struct.\n");
        return false;
    }

    /* (Testing) Send a welcome message. */
    bio_set_dev(usb_bio, bdevice);
    usb_bio->bi_iter.bi_sector = bdevice_sector;
    usb_bio->bi_opf = REQ_OP_WRITE;
    bio_add_page(usb_bio, vmalloc_to_page((void*) hardlog_welcome), 512, 0);
    bdevice_sector++;

    /* Submit BIO and wait. */
    len = submit_bio_wait(usb_bio);
    if (len < 0) {
        HARDLOG_MODULE_PRINT("error: bio communication failed (ret = %d)\n", len);
        return false;
    }
    bio_reset(usb_bio);

    tmpbuf = (char*) vmalloc(4096);
    if (!tmpbuf) return false;

    return true;
}

void bio_close(void)
{
    /* Close the audit device communication interface. */
    if (bdevice)
        blkdev_put(bdevice, FMODE_READ | FMODE_WRITE);
    return;
}

int bio_write(uint64_t end, uint64_t start)
{
    int len = 0;
    uint64_t remaining, consumed = 0;
    unsigned long addr, added_pages = 0;
    uint64_t cbuff_size = HARDLOG_AUDIT_CBUFFSIZE;
    uint64_t cbuff_index = 0;

    /* Perform various sanity checks. */
    if (IS_ERR(usb_bio) || !usb_bio || IS_ERR(tmpbuf)) {
        HARDLOG_MODULE_PRINT("error: failed to allocate a bio structure or buffer.\n");
        return -1;
    }

    remaining = end - start;
    if (remaining <= 0) {
        return -1;
    }
    if (IS_ERR(bdevice) || !bdevice) {
        HARDLOG_MODULE_PRINT("error: cannot write to null device.\n");
        return -1;
    }
    if (IS_ERR(usb_bio) || !usb_bio) {
        HARDLOG_MODULE_PRINT("error: usb bio structure is null.\n");
        return -1;
    }

    /* (Benchmarking) Write times.*/
    start_us = ktime_get();
    
    if (debug)
        HARDLOG_MODULE_PRINT("bio_write: writing %lld bytes (%lld - %lld)\n", remaining, start, end);

    /* Create bio requests to send. */
    bio_set_dev(usb_bio, bdevice);
    usb_bio->bi_iter.bi_sector = bdevice_sector;
    usb_bio->bi_opf = REQ_OP_WRITE;

    while (true) {

        cbuff_index = (start+consumed) % cbuff_size;
        addr = ((unsigned long) &(audit_cbuff_ptr->buffer[cbuff_index]));
        
        // addr = ((unsigned long) &(audit_cbuff_ptr->buffer[start+consumed]));

        /* Copying in 4KB chunks (essentially 8 512 byte sectors). */
        if (remaining >= 4096) {
            bdevice_sector += SECTORS_PER_PAGE;
            bio_add_page(usb_bio, vmalloc_to_page((void*) addr), 4096, 0);
        } else {
            memcpy(tmpbuf, (char*) addr, remaining);
            bdevice_sector += SECTORS_PER_PAGE;
            bio_add_page(usb_bio, vmalloc_to_page((void*) tmpbuf), 4096, 0);
        }

        added_pages++;

        /* Each BIO structure can only handle 1 MB of data. I did not test if
           we could send more data using a different means or perhaps updating
           the backed BIO. (Potential optimization) */
        if (added_pages == 256) {

            /* Submit BIO request and wait. This can definitely be optimized by
               sending all requests first and then waiting once. We are wasting
               some CPU cycles here. (Potential optimization) */
            len = submit_bio_wait(usb_bio);
            if (len < 0) {
                HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
                bio_reset(usb_bio);
                return -1;
            }

            /* Start tracking pages from start again. */
            added_pages = 0;

            /* Reset the bio request to send out. This could also be optimized if we can
               simply change the actual fields. */
            bio_reset(usb_bio);

            bio_set_dev(usb_bio, bdevice);
            usb_bio->bi_iter.bi_sector = bdevice_sector;
            usb_bio->bi_opf = REQ_OP_WRITE;
        }

        /* Add/reduce by a page (4KB) each time. */
        consumed += 4096;
        if (remaining <= 4096) break;
        remaining -= 4096;
    }

    /* This is added to keep track of that last write that the thread
       could have potentially missed when it was being signalled to stop. */
    if (added_pages > 0) {
        if (debug)
            HARDLOG_MODULE_PRINT("detail: writing rest (%ld pages) to the usb device.\n", added_pages);

        len = submit_bio_wait(usb_bio);
        if (len < 0) {
            HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
            bio_reset(usb_bio);
            return -1;
        }
        bio_reset(usb_bio);
    }

    /* (Benchmarking) Measuring and updating elapsed time for writes. */
    end_us = ktime_us_delta(ktime_get(), start_us);
    update_write_measurement(end_us, end-start);

    /* (Debugging only) */
    if (debug) {
        HARDLOG_MODULE_PRINT("success: written %lld bytes (%lld - %lld) to the usb device (time = %lld us).\n", 
                        end-start, start, end, end_us);
    }

    return len;
}

/* VFS-related functions. For reference on reading and writing files within the kernel
   https://stackoverflow.com/questions/1184274/read-write-files-within-a-linux-kernel-module 
   
   This is an older implementation that I updated with the faster BIO one. I'm keeping this
   here for reference purposes (and potential benchmarking). 
*/
bool fs_open(void) 
{
    int len = 0;

    /* Use Linux VFS to open the device file. */
    device_file = filp_open(device, O_CREAT | O_RDWR | O_SYNC, 0644);
    if (IS_ERR(device_file)) {
        HARDLOG_MODULE_PRINT("error: failed to open the device (%s).\n", device);
        return false;
    }

    HARDLOG_MODULE_PRINT("success: device (%s) successfully opened.\n", device);

    /* First write is slow for some reason (setup I guess?). To avoid that, I'm
       performing the write here. */
    len = kernel_write(device_file, hardlog_welcome, 512, &device_file->f_pos);
    if (len <= 0) {
        HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
        return false;
    }

    HARDLOG_MODULE_PRINT("success: opened the device using VFS!\n");
    return true;
}

void fs_close(void) 
{
    /* Close the audit device file. */
    if (IS_ERR(device_file) || !device_file) {
        HARDLOG_MODULE_PRINT("error: cannot close null file.\n");
        return;
    }
    filp_close(device_file, NULL);
}

int fs_write(uint64_t end, uint64_t start)
{
    int len = 0;
    uint64_t cbuff_index = 0;
    // uint64_t cbuff_size  = HARDLOG_AUDIT_CBUFFSIZE;

    /* Perform sanity checks for size and device file */
    uint64_t remaining = end - start;
    if (remaining <= 0) {
        return 0;
    }
    if (IS_ERR(device_file) || !device_file) {
        HARDLOG_MODULE_PRINT("error: cannot write to null file.\n");
        return -1;
    }

    /* (Debugging) */
    if (debug)
        HARDLOG_MODULE_PRINT("fs_write: writing %lld bytes (%lld - %lld)\n", end-start, start, end);

    /* (Benchmarking) */
    start_us = ktime_get();

    /* Perform the actual write. */
    cbuff_index = 0;
    len = kernel_write(device_file, &(audit_cbuff_ptr->buffer[cbuff_index]), end-start, &device_file->f_pos);
    if (len <= 0) {
        HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
        return len;
    }

    /* (Benchmarking) */
    end_us = ktime_us_delta(ktime_get(), start_us);
    update_write_measurement(end_us, end-start);

    /* (Debugging) */
    if (debug)    
        HARDLOG_MODULE_PRINT("success: written %lld bytes (%lld - %lld) to the usb device (time = %lld us).\n", 
                        end-start, start, end, end_us);

    return len;
}