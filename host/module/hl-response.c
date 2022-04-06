
/* This file reads system logs (in response to admin requests) 
   from the audit device using Linux's Block IO (BIO) or 
   Virtual FileSystem (VFS) interface. */

/* General headers. */
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/skbuff.h>
#include <linux/freezer.h>
#include <linux/hardlog.h>
#include <linux/mutex.h>

/* File IO-related headers. */
#include <linux/fs.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

/* Sleep and timer headers. */
#include <linux/hrtimer.h>
#include <linux/delay.h>

#include "hl.h"

/* Defined in hl-main.c. */
extern char* device;
extern bool biof;
extern bool debug;
extern bool vmallocf;
extern char* hardlog_welcome;

struct block_device* brspdevice;

/* Block IO request holders. */
#define SECTORS_PER_PAGE (PAGE_SIZE/512)

bool bio_open_response_file(char* device) {
    char fulldevice[20];

    /* Create full device name. */
    snprintf(fulldevice, 20, "/dev/%s", device);

    /* Use Linux BIO to open file. */
    brspdevice = blkdev_get_by_path(fulldevice, FMODE_READ, NULL);

    /* Perform sanity checks. */
    if (IS_ERR(brspdevice) || !brspdevice) {
        HARDLOG_MODULE_PRINT("error: failed to open the device (%s) using lookup_bdev().\n", fulldevice);
        return false;
    }
    if (!brspdevice->bd_disk) {
        HARDLOG_MODULE_PRINT("error: brspdevice not correct.\n");
        return false;
    }
    if (strcmp(brspdevice->bd_disk->disk_name, device) != 0) {
        HARDLOG_MODULE_PRINT("error: brspdevice: %s, device: %s.\n", brspdevice->bd_disk->disk_name, device);
        return false;
    }

    HARDLOG_MODULE_PRINT("success: opened %s as a block device.\n", brspdevice->bd_disk->disk_name);
    return true;
}

void bio_close_response_file(void)
{
    if (brspdevice)
        blkdev_put(brspdevice, FMODE_READ | FMODE_WRITE);
    return;
}

int bio_read_response_file(char* data, uint64_t size) {
    int len = 0;
    uint64_t addr = 0;
    int offset = 0;
    uint64_t read = 0;
    // uint64_t remaining = 0;
    uint64_t brspdevice_sector = 0;
    uint64_t starttime, endtime = 0;

    /* Allocate the BIO structure for usb communication. */
    struct bio* usb_bio = bio_alloc(GFP_NOIO, 256);
    if (IS_ERR(usb_bio) || !usb_bio) {
        HARDLOG_MODULE_PRINT("error: failed to allocate a bio structure.\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("success: opened usb_bio.\n");

    /* (Benchmarking) Start timer. This was implemented for
       benchmarking the request handling latency. */
    starttime = ktime_get();

    /* All reads are in 512 byte chunks. This prevents read from interfering
       with writes (atleast significantly). */
    while (true) {

        addr = ((unsigned long) data) + read;
        offset = read % 4096;

        bio_set_dev(usb_bio, brspdevice);
        usb_bio->bi_iter.bi_sector = brspdevice_sector;
        usb_bio->bi_opf = REQ_OP_READ;

        bio_add_page(usb_bio, vmalloc_to_page((void*) addr), 512, offset);

        /* If hardlog is currently writing logs, we should allow those to
           proceed first. (Avoiding priority inversion!) */
        while (hardlog_logging) {
            /* Simply go to sleep for 3 ms. (Kinda arbitrary, can be changed.) */
            usleep_range(2900, 3100);
        }

        /* Submit the BIO read request and wait. */
        len = submit_bio_wait(usb_bio);
        if (len < 0) {
            HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
            bio_reset(usb_bio);
            return -1;
        }
        bio_reset(usb_bio);

        /* Increase the sector being read. This always starts from zero, and the device
           prevents overflows. Therefore, no additional checks are needed. */
        brspdevice_sector++;

        /* Check if we have read all there is to read. If yes, simply stop. */
        read += 512;
        if (size <= 512) break;
        size -= 512;
    }
    HARDLOG_MODULE_PRINT("success: file read (%lld bytes) complete.\n", read);

    /* (Benchmarking) Measure time taken to obtain the logs. */
    endtime = ktime_get();
    HARDLOG_MODULE_PRINT("detail: time taken = %lld\n", ktime_us_delta(endtime, starttime));

    return len;
}
