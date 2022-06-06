// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Microsoft Corporation. */

/* This file contains the IOCTL implementation for Hardlog's host
   module. This IOCTL is used to send administrator requests to the
   audit device, and receive system logs.

   Note: The system logs are not currently encrypted or signed.
*/

#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <linux/ioctl.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include <linux/cdev.h>
#include <linux/nospec.h>

#include "hl.h"

static dev_t dev = 0;
static struct class* hardlog_class;
static struct cdev   hardlog_cdev;
static char* request_buffer;
static char* response_buffer;

struct block_device* reqdevice;
struct block_device* respdevice;

/* Just making a 4 KB buffer for requests. This should be enough since
   most requests are very simple (e.g., GET XYZ). If more complex requests 
   are needed, this should be updated. */
struct hardlog_ioctl_request {
    char data[4096];
};

/* Response sizes can be very different, and data should be vmalloced based on
   response size. */
struct hardlog_ioctl_response {
    char* data;
    uint64_t size;
};

/* This was merely for some benchmarking experiments. */
struct hardlog_ioctl_reset {
    uint64_t prev_time;
};

/* IOCTL-related definitions and structs. */
#define SENDREQUEST             _IOW('a', 'a', struct hardlog_ioctl_request)
#define CHECKRESPONSEREADY      _IOR('a', 'b', struct hardlog_ioctl_request)
#define READRESPONSE            _IOW('a', 'c', struct hardlog_ioctl_response)
#define SENDRESETFLUSHTIME      _IOW('a', 'd', struct hardlog_ioctl_reset)
struct hardlog_ioctl_request  hreq;
struct hardlog_ioctl_response hrsp;
struct hardlog_ioctl_reset    hres;

/* Writing an IOCTL for hardlog */
static long hardlog_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    switch (cmd)
    {
    case SENDREQUEST:
        /* Send request to the provided request file (e.g., /dev/sdc). */
        if (copy_from_user((void*) &hreq, (void*) arg, sizeof(struct hardlog_ioctl_request))) {
            HARDLOG_MODULE_PRINT("error: user didnt send correct message.\n");
            return -1;
        }
        snprintf(request_buffer, 4096, hreq.data);
        bio_write_request_file(request_buffer);
        break;

    case CHECKRESPONSEREADY:
        /* Read the command file to check if the response is ready. The command file
           is updated by the audit device. */
        bio_read_request_file(request_buffer);
        if (copy_to_user((void*) arg, request_buffer, sizeof(struct hardlog_ioctl_request))) {
            HARDLOG_MODULE_PRINT("error: couldn't copy data to user.\n");
            return -1;
        }
        break;

    case READRESPONSE:
        /* Read response from the provided response file. (e.g., /dev/sdd). */
        if (copy_from_user((void*) &hrsp, (void*) arg, sizeof(struct hardlog_ioctl_response))) {
            HARDLOG_MODULE_PRINT("error: user didnt send correct message.\n");
            return -1;
        }
        
        /* Create an intermediatte response buffer. I keep a buffer of 
           atleast 512 bytes at the end (hacky!). */
        response_buffer = (char*) vmalloc(hrsp.size + 4096);
        if (!response_buffer) {
            HARDLOG_MODULE_PRINT("error: couldn't allocate buffer for response.\n");
            return -1;
        }
        
        /* Read the response file. */
        bio_read_response_file(response_buffer, hrsp.size);
        
        /* Copy back to the user. */
        if (copy_to_user((void*) hrsp.data, response_buffer, hrsp.size)) {
            HARDLOG_MODULE_PRINT("error: couldn't copy data to user.\n");
            return -1;
        }

        break;

    case SENDRESETFLUSHTIME:
        /* Send the response time to the user and reset it. */
        hres.prev_time = get_reset_flush_time_measurement();
        if (copy_to_user((void*) arg, &hres, sizeof(struct hardlog_ioctl_reset))) {
            HARDLOG_MODULE_PRINT("error: couldn't copy data to user.\n");
            return -1;
        }
        break;

    default:
        HARDLOG_MODULE_PRINT("error: wrong ioctl request.\n");
        break;
    }

    return 0;
}

/* An open function for host module. */
static int hardlog_open(struct inode* inode, struct file* file) {
    HARDLOG_MODULE_PRINT("ioctl: open executed.\n");
    return 0;
}

/* A release function for the host module. */
static int hardlog_release(struct inode* inode, struct file* file) {
    HARDLOG_MODULE_PRINT("ioctl: close executed.\n");
    return 0;
}

static struct file_operations fops = 
{
    .owner = THIS_MODULE,
    .open = hardlog_open,
    .release = hardlog_release,
    .unlocked_ioctl = hardlog_ioctl,
};

/* Initialize the module for IOCTL commands */
bool hardlog_ioctl_init(void) {

    /* open request file */
    if (!bio_open_request_file("sdc")) {
        HARDLOG_MODULE_PRINT("error: couldn't open request file.\n");
        return false;
    };
    HARDLOG_MODULE_PRINT("success: allocated block device for request file.\n");

    /* need a buffer to keep all data for requests */
    request_buffer = (char*) vmalloc(4096);
    if (!request_buffer) {
        HARDLOG_MODULE_PRINT("error: couldn't allocate request_buffer.\n");
        return -1;
    }

    /* open response file */
    if (!bio_open_response_file("sdd")) {
        HARDLOG_MODULE_PRINT("error: couldn't open response file.\n");
        return false;
    };
    HARDLOG_MODULE_PRINT("success: allocated block device for response file.\n");

    /* only have this for debugging purposes */
    char* testmsg = (char*) vmalloc(4096);
    if (!testmsg) {
        HARDLOG_MODULE_PRINT("error: couldn't allocate test message buffer.\n");
        return false;
    }
    snprintf(testmsg, 20, "HELLO WORLD\n");
    HARDLOG_MODULE_PRINT("detail: message sent is %s", testmsg);
    bio_write_reqfile(testmsg);

    /* Allocate a character device. */
    if (alloc_chrdev_region(&dev, 0, 1, "hardlog_dev") < 0) {
        HARDLOG_MODULE_PRINT("error: couldn't allocate chardev region.\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("success: allocated chardev region.\n");

    /* Initialize the chardev with my fops. */
    cdev_init(&hardlog_cdev, &fops);

    if (cdev_add(&hardlog_cdev, dev, 1) < 0) {
        HARDLOG_MODULE_PRINT("error: couldn't add hardlog_cdev.\n");
        goto cdevfailed;
    }
    HARDLOG_MODULE_PRINT("success: added hardlog_cdev.\n");

    if ((hardlog_class = class_create(THIS_MODULE, "hardlog_class")) == NULL) {
        HARDLOG_MODULE_PRINT("error: couldn't create class.\n");
        goto cdevfailed;
    }
    HARDLOG_MODULE_PRINT("success: created hardlog_class.\n");

    if ((device_create(hardlog_class, NULL, dev, NULL, "hardlog_host")) == NULL) {
        HARDLOG_MODULE_PRINT("error: couldn't create device.\n");
        goto classfailed;
    }
    HARDLOG_MODULE_PRINT("success: hardlog device driver inserted.\n");

    return true;

classfailed:
    class_destroy(hardlog_class);
cdevfailed:
    unregister_chrdev_region(dev, 1);
    return false;
}

void hardlog_ioctl_teardown(void) {
    /* Close the request and response files. */
    bio_close_request_file();
    bio_close_response_file();

    /* Destroy the classes too (IOCTL-specific). */
    if (hardlog_class) {
        device_destroy(hardlog_class, dev);
        class_destroy(hardlog_class);
    }
    cdev_del(&hardlog_cdev);
    unregister_chrdev_region(dev,1);

    HARDLOG_MODULE_PRINT("Removed hardlog device driver from host.\n");
}
