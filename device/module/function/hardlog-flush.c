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

#include "f_mass_storage.h"
#include "hardlog.h"

/* Task struct for the background thread to flush data */
static struct task_struct *hardlog_flushd_task = NULL;

static int hardlog_flushd_thread(void *dummy) {
    ktime_t start_us;
    s64 elapsed_us = 0;
    uint64_t cbuff_size = HARDLOG_DATA_CBUFFSIZE;

    uint64_t start, end, real_end, end_one, size = 0;
    bool special = false;

    while (!kthread_should_stop()) {
		start_us = ktime_get();

        /* Get the current statistics of the circular buffer */
        real_end = data_cbuff.head;
        end = real_end % cbuff_size;
        start = data_cbuff.tail % cbuff_size;
        if (end < start) {
            special = true;
            end_one = HARDLOG_DATA_CBUFFSIZE;
        }
        size = end - start;

        /* Debugging purposes only */
        if (debug) {
            HARDLOG_MODULE_PRINT("head: %lld, tail: %lld.\n", data_cbuff.head, data_cbuff.tail);
            HARDLOG_MODULE_PRINT("start: %lld, end: %lld.\n", start, end);
        }

        /* Write to the file, if there is anything to write */
        if (size > 0 && !special) {
            /* Normal case, going forward in the circular buffer. */
            fs_write(end, start);

            /* Update the tail, letting the kernel know we have written to usb */
            data_cbuff.tail = real_end;
        } 
        else if (special) {
            /* Special case, going forward and backwards in the circular buffer.
               there are two usb writes in this case. */
            fs_write(end_one, start);
            fs_write(end, 0);

            /* Update the tail, letting the kernel know we have written to usb */
            data_cbuff.tail = real_end;

            /* Make special false, next writes are normal. */
            special = false;
        }

        /* Sleep for 1ms. This is kinda arbitrary, could be shortened. */
        elapsed_us = ktime_us_delta(ktime_get(), start_us);
		if(elapsed_us < 900) {
			usleep_range(900 - elapsed_us, 1100 - elapsed_us);
		}
    }

    HARDLOG_MODULE_PRINT("detail: stopping flusher thread.\n");
    return 1;
}

bool hardlog_flush_setup(void) {

    struct sched_param param;

    /* Create the background thread */
    hardlog_flushd_task = kthread_run(hardlog_flushd_thread, NULL, "hardlog_flushd");
	if (IS_ERR(hardlog_flushd_task)) {
		int err = PTR_ERR(hardlog_flushd_task);
		HARDLOG_MODULE_PRINT("error: failed to start the hardlog thread (%d)\n", err);
        return false;
	}
    HARDLOG_MODULE_PRINT("success: started the hardlog flush thread.\n");

#if 0
    /* Note: I make the same thread priority requests when the thread
       is being created. Not sure if we need to do it here again, to check. */

    /* hardlog_flushd_thread is not nice at all!! ;) */
    set_user_nice(hardlog_flushd_task, -20);

    /* this did not help much but setting nevertheless */
    param.sched_priority = 99;
    sched_setscheduler(hardlog_flushd_task, SCHED_FIFO, &param);
#endif

    return true;
}

void hardlog_flush_teardown(void) {
    /* Stop the running kernel thread. */
    kthread_stop(hardlog_flushd_task);
}

