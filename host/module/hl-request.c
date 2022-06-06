// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Microsoft Corporation. */

/* This file contains the code that sends administrator requests 
   to the usb device using Linux's block io or virtual filesystem
   interface. 
*/

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

/* Defined in hl-main.c */
extern char* device;
extern bool biof;
extern bool debug;
extern bool vmallocf;
extern char* hardlog_welcome;

struct block_device* breqdevice;

/* Block IO request holders */
#define SECTORS_PER_PAGE (PAGE_SIZE/512)

bool bio_open_request_file(char* device) 
{
    char fulldevice[20];

    /* Create full device name. */
    snprintf(fulldevice, 20, "/dev/%s", device);

    /* Use linux's block IO (bio) to open device communication. */
    breqdevice = blkdev_get_by_path(fulldevice, FMODE_READ | FMODE_WRITE, NULL);

    /* Perform sanity checks to determine if device was correctly opened. */
    if (IS_ERR(breqdevice) || !breqdevice) {
        HARDLOG_MODULE_PRINT("error: failed to open the device (%s) using lookup_bdev().\n", fulldevice);
        return false;
    }
    if (!breqdevice->bd_disk) {
        HARDLOG_MODULE_PRINT("error: breqdevice not correct.\n");
        return false;
    }
    if (strcmp(breqdevice->bd_disk->disk_name, device) != 0) {
        HARDLOG_MODULE_PRINT("error: breqdevice: %s, device: %s.\n", breqdevice->bd_disk->disk_name, device);
        return false;
    }

    /* Sanity checks have passed. */
    HARDLOG_MODULE_PRINT("success: opened %s as a block device.\n", breqdevice->bd_disk->disk_name);
    return true;
}

void bio_close_request_file(void)
{
    if (breqdevice)
        blkdev_put(breqdevice, FMODE_READ | FMODE_WRITE);
    return;
}

int bio_write_request_file(char* request) {
    int len = 0;
    uint64_t addr = 0;

    /* Allocate the bio struct for usb communication. */
    struct bio* usb_bio = bio_alloc(GFP_NOIO, 256);
    if (IS_ERR(usb_bio) || !usb_bio) {
        HARDLOG_MODULE_PRINT("error: failed to allocate a bio structure.\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("success: opened usb_bio.\n");

    /* Perform sanity checks on BIO structure. */
    if (IS_ERR(breqdevice) || !breqdevice) {
        HARDLOG_MODULE_PRINT("error: cannot write to null device.\n");
        return -1;
    }
    if (!request) {
        HARDLOG_MODULE_PRINT("error: data is null.\n");
        return -1;
    }

    /* Create bio requests to send to the device. */
    bio_set_dev(usb_bio, breqdevice);
    usb_bio->bi_iter.bi_sector = 0;
    usb_bio->bi_opf = REQ_OP_WRITE;

    /* Submit request to device and wait for acknowledgement. */
    addr = ((unsigned long) request);
    bio_add_page(usb_bio, vmalloc_to_page((void*) addr), 512, 0);
    len = submit_bio_wait(usb_bio);
    if (len < 0) {
        HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
        bio_reset(usb_bio);
        return -1;
    }

    HARDLOG_MODULE_PRINT("success: request sent.\n");
    return len;
}

int bio_read_request_file(char* data) {
    int len = 0;
    uint64_t addr = 0;

    /* Allocate the bio struct for usb communication. */
    struct bio* usb_bio = bio_alloc(GFP_NOIO, 256);
    if (IS_ERR(usb_bio) || !usb_bio) {
        HARDLOG_MODULE_PRINT("error: failed to allocate a bio structure.\n");
        return false;
    }

    /* Create bio read requests. */
    bio_set_dev(usb_bio, breqdevice);
    usb_bio->bi_iter.bi_sector = 0;
    usb_bio->bi_opf = REQ_OP_READ;

    /* Submit the read request and wait for response. */
    addr = ((unsigned long) data);
    bio_add_page(usb_bio, vmalloc_to_page((void*) addr), 512, 0);
    len = submit_bio_wait(usb_bio);
    if (len < 0) {
        HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d).\n", len);
        bio_reset(usb_bio);
        return -1;
    }

    HARDLOG_MODULE_PRINT("success: file read complete.\n");
    return len;
}
