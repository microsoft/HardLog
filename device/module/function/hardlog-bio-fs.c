/* This file contains the major code for hardlog's device-side
   flusher, which dumps the in-memory data into the actual backend
   file. */

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

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include <linux/nospec.h>

#include "configfs.h"

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

/* Other headers */
#include "f_mass_storage.h"
#include "hardlog.h"

/* File and Blockdev for the usb storage */
struct file* device_file;
struct fsg_lun* device_lun;

/* Timing purposes only */
ktime_t start_us;
s64 end_us = 0;

/* Keeping track of how much of the file is used */
uint64_t file_start_offset = 0;
uint64_t file_end_offset = 0;

/* Note: we do not need fs_open() because the USB gadget is already opening
   the file for us, thus, we can simply reuse the file pointer provided to us. 
*/
uint64_t remove_null_chars(uint64_t end, uint64_t start) {
    int i = 0;
    uint64_t final_size = 0;
    char* cbuff_ptr = &(data_cbuff.buffer[start]);
    char* trim_cbuff_ptr = &(data_cbuff_trim.buffer[0]);
    for (i = 0 ; i < end-start; i++) {
        if (*cbuff_ptr != '\0') {
            *trim_cbuff_ptr = *cbuff_ptr;
            trim_cbuff_ptr++;
            final_size++;
        }
        cbuff_ptr++;
    }
    return final_size;
}

int fs_write(uint64_t end, uint64_t start)
{
    int len = 0;
    uint64_t final_size = 0;

    /* Perform sanity checks for size and device file */
    uint64_t remaining = end - start;
    if (remaining <= 0) {
        return 0;
    }
    if (IS_ERR(device_file) || !device_file) {
        HARDLOG_MODULE_PRINT("error: cannot write to null file.\n");
        return -1;
    }

    /* Note: With O_SYNC, it seems stupidly slow. Therefore, I disabled
       O_SYNC. This should be checked again later. */
	device_lun->filp->f_flags &= ~O_SYNC;

    if (debug)
        HARDLOG_MODULE_PRINT("fs_write: writing %lld bytes (%lld - %lld) at %lld\n", 
            end-start, start, end, device_lun->filp->f_pos);

    /* Just write the raw buffer (with null characters). Trimming should be done by 
       the device library when the logs are processed. */
    len = vfs_write(device_lun->filp, &(data_cbuff.buffer[start]), end-start, &device_lun->filp->f_pos);
    if (len <= 0) {
        HARDLOG_MODULE_PRINT("error: kernel write command failed (ret = %d, write-size: %lld, pos = %lld).\n", 
                                                len, end-start, device_lun->filp->f_pos);
        return len;
    }
    
    /* Update the file end offset to inform the device library */
    file_end_offset = device_lun->filp->f_pos;

    /* Note: Currently, I assume that the file is large enough to never
       need overwriting and the file writes will fail (when they overflow).
       This is correct based on our semantics---only the sysadmin should be
       able to overwrite the file. It can be made more elegant, however. */

    return len;
}