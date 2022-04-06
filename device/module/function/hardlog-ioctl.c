/* This file contains the major code for hardlog's device-side
   implementation, responsible for storing incoming USB messages
   in a fast manner to an in-memory data structure and dumping it
   into the actual backend file. */

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

#include "configfs.h"

#include "f_mass_storage.h"
#include "hardlog.h"

dev_t dev = 0;
static struct class* hardlog_class;
static struct cdev   hardlog_cdev;

struct hardlog_ioctl_request {
    bool server_request;
    uint64_t log_file_start_offset;
    uint64_t log_file_end_offset;
};

#define REQUEST       _IOW('a', 'a', struct hardlog_ioctl_request)
#define DELETELOG     _IOR('a', 'c', struct hardlog_ioctl_request)

bool server_request = false;

/* Writing an IOCTL for hardlog */
static long hardlog_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    struct hardlog_ioctl_request hreq;

    switch (cmd)
    {
    case DELETELOG:
        HARDLOG_MODULE_PRINT("IOCTL: delete log data.\n");
        /* TODO: This is not currently implemented. */
        break;

    case REQUEST:
        /* Note: I've implemented it such that each request should arrive 
           sequentially but potentially we should be able to serve multiple 
           requests. This might not be needed, however. */
        hreq.server_request = server_request;
        hreq.log_file_start_offset = file_start_offset;
        hreq.log_file_end_offset = file_end_offset;
        if (copy_to_user((void*) arg, &hreq, sizeof(struct hardlog_ioctl_request))) {
            HARDLOG_MODULE_PRINT("error: couldn't respond to DATA ioctl request.\n");
            return -1;
        }

        /* Now that we have sent the request to the library, we can assume it will
           be completed by the library. Importantly, the library will simply let the
           host know when the data is ready. */
        if (server_request == true)
            server_request = false;
        
        break;

    default:
        HARDLOG_MODULE_PRINT("error: wrong ioctl request.\n");
        break;
    }

    return 0;
}

/* Writing an open function for hardlog */
static int hardlog_open(struct inode* inode, struct file* file) {
    HARDLOG_MODULE_PRINT("ioctl: open executed.\n");
    return 0;
}

/* Writing a release function for hardlog */
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

    /* Allocate a character device */
    if (alloc_chrdev_region(&dev, 0, 1, "hardlog_dev") < 0) {
        HARDLOG_MODULE_PRINT("error: couldn't allocate chardev region.\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("success: allocated chardev region.\n");

    /* Initialize the chardev with my fops */
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

    if ((device_create(hardlog_class, NULL, dev, NULL, "hardlog_device")) == NULL) {
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
    if (hardlog_class) {
        device_destroy(hardlog_class, dev);
        class_destroy(hardlog_class);
    }
    cdev_del(&hardlog_cdev);
    unregister_chrdev_region(dev,1);
    HARDLOG_MODULE_PRINT("Removed hardlog device driver.\n");
}