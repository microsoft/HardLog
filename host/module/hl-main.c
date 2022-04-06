/* This file creates a background HardLog daemon, which intercepts data in the 
   circular buffer (HostBuf) and sends the data to the USB audit device. */

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
#include <linux/sched.h>
#include <linux/sched/types.h>

#include "hl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adil Ahmad");
MODULE_DESCRIPTION("Enable and disable hardlog during execution.");
MODULE_VERSION("0.01");

/* Functions to start and terminate hardlog-based
   logging. They are all defined in kernel/hardlog.c */
func_type* setup_hardlog_ptr;
func_type* teardown_hardlog_ptr;

/* Audit queues and Circular buffers. */
struct sk_buff_head* audit_queue_ptr = NULL;
struct audit_circ_buffer* audit_cbuff_ptr;

/* Task struct for the background hardlog thread. */
static struct task_struct *hardlog_auditd_task = NULL;

/* Wait queues. */
wait_queue_head_t* kauditd_wait_ptr = NULL;
wait_queue_head_t* hardlog_auditd_wait_ptr = NULL;

/* Parameters that should be provided by the user.
   device = "/dev/sdX"
   biof   = 0 or 1 (triggers block IO)
   debug  = 0 or 1
   microlog = 0 or 1 (should not be used)
   vmallocf = 0 or 1 (vmalloc or kmalloc; did not make much difference)
   flushtimef = 0 or 1 (measuring flush time?)  */
char* device;
bool biof = false;
bool debug = false;
bool microlog = false; // added microlog for testing purposes but it should not be used.
bool vmallocf = false;
bool flushtimef = false;
module_param(device, charp, S_IRUSR);
module_param(biof, bool, S_IRUSR);
module_param(debug, bool, S_IRUSR);
module_param(microlog, bool, S_IRUSR); // added microlog for testing purposes but it should not be used.
module_param(vmallocf, bool, S_IRUSR);
module_param(flushtimef, bool, S_IRUSR);

/* Defined in hl-logger.c */
extern struct mutex hardlog_audit_producer;
extern uint64_t audit_cbuff_last_pos;
extern struct audit_circ_buffer audit_cbuff;

/* Debugging and testing purposes */
char* hardlog_welcome;

struct sched_param {
  int sched_priority;
};

/* Communication between the logging thread and IOCTL thread. The logging
   thread sets this flag and the IOCTL thread reads it. The IOCTL thread 
   reads it every 512 bytes to avoid priority inversion. */
bool hardlog_logging = false;

/* Open the USB audit device. */
static bool usb_open(void)
{
    bool ret = false;

    /* Allocating this buffer to print a welcome message in the storage file. */
    hardlog_welcome = (char*) vmalloc(4096);
    if (IS_ERR(hardlog_welcome)) {
        HARDLOG_MODULE_PRINT("error: couldn't allocate buffer\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("detail: pointer for hardlog_welcome is at %px\n", hardlog_welcome);
    snprintf(hardlog_welcome, 512, "Welcome to HARDLOG!\n");

    /* Use BIO or VFS depending on the flag. BIO is much faster and should be default. */
    if (biof)
        ret = bio_open();
    else
        ret = fs_open();

    vfree(hardlog_welcome);
    return ret;
}

static void usb_close(void) 
{
    /* Close the USB audit device. */
    if (biof)
        bio_close();
    else
        fs_close();
}

static int usb_write(uint64_t end, uint64_t start) 
{
    /* Write the HostBuf to the device using either BIO or VFS. */
    if (biof) 
        return bio_write(end, start);
    else 
        return fs_write (end, start);
}

/* START: L3 cache isolation using Intel CAT. */

#define PQR_ASSOC 0xC8F
#define PQOS_MSR_ASSOC_QECOS_SHIFT 32
#define COS_MASK  0xffffffff00000000ULL

#define IA32_L3_MASK_0 0xC90
#define IA32_L3_MASK_1 0xC91
#define IA32_L3_MASK_2 0xC92
#define IA32_L3_MASK_3 0xC93

/* Just a custom WRMSR wrapper. In practice, it is better
   to use the kernel's own wrappers. */
static inline void custom_wrmsr(uint64_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile (
    "wrmsr"
    :
    : "c"(msr), "a"(low), "d"(high)
    );
}

static void flush_all_vaddrs(void)
{
    asm volatile("wbinvd");
}

static void set_hardlog_l3_cos(void* a)
{
    /* Set the Hardlog core to have a disjoint slice of the L3 cache. Each time
       the slice is updated, the L3 must be flushed (entirely). */
    HARDLOG_MODULE_PRINT("[core = %d] setting hardlog l3 cat\n", current->cpu);
    custom_wrmsr(PQR_ASSOC, 0x0);
    flush_all_vaddrs();
}

static void set_else_l3_cos(void* a)
{
    /* Set non-Hardlog cores to have the rest of the L3 cache. Each time
       the slice is updated, the L3 must be flushed (entirely). */
    HARDLOG_MODULE_PRINT("[core = %d] setting else l3 cat\n", current->cpu);
    custom_wrmsr(PQR_ASSOC, 0x1);
    flush_all_vaddrs();
}

static void setup_l3_cat(void)
{
    /* The current setup involves giving Hardlog 1/8*LLC (2-ways) and the rest
       (14-ways) to all remaining cores. This setup was for an i7-8700 CPU. */
    custom_wrmsr(IA32_L3_MASK_0, 0x8000);
    custom_wrmsr(IA32_L3_MASK_1, 0x8FFF);
    custom_wrmsr(IA32_L3_MASK_2, 0x8FFF);
    custom_wrmsr(IA32_L3_MASK_3, 0x8FFF);

    /* Not sure this is needed tbh. It has negligible impact anyways (one-time cost). */
    flush_all_vaddrs();
}

static void reset_l3_cat(void* a)
{
    /* Reset all L3 mask values to use entire L3 cache. */
    custom_wrmsr(IA32_L3_MASK_0, 0xFFFF);
    custom_wrmsr(IA32_L3_MASK_1, 0xFFFF);
    custom_wrmsr(IA32_L3_MASK_2, 0xFFFF);
    custom_wrmsr(IA32_L3_MASK_3, 0xFFFF);

    /* Set the first L3 mask to be used by the CPU core. */
    custom_wrmsr(PQR_ASSOC, 0x0);

    /* Flush vaddrs to enforce changes. */
    flush_all_vaddrs();
}

/* END: L3 cache isolation using Intel CAT. */

extern bool reset_log_time;

/* Created this mask for setting CPU affinity. */
struct cpumask mask;

/* Background daemon process that reads from the
   circular buffer and writes to the USB device */
static int hardlog_auditd_thread(void *dummy) {
    uint64_t start, end, end_one, size = 0;
    uint64_t elapsed_us, start_us = 0;
    uint64_t cbuff_size = HARDLOG_AUDIT_CBUFFSIZE;
    bool special = false;

    /* Get the circular buffer pointer. */
    audit_cbuff_ptr = &audit_cbuff;

    /* Get the wait queue(s). */
    kauditd_wait_ptr = (wait_queue_head_t*) kallsyms_lookup_name("kauditd_wait");
    hardlog_auditd_wait_ptr = (wait_queue_head_t*) kallsyms_lookup_name("hardlog_auditd_wait");
    if (kauditd_wait_ptr == 0 || hardlog_auditd_wait_ptr == 0) {
        HARDLOG_MODULE_PRINT("error: failed to get the wait queue pointers\n");
        return -1;
    }
    HARDLOG_MODULE_PRINT("success: obtained wait queues\n");

    /* Set CAT registers (and partitions) on each processor core to avoid attacks
       through L3 contention. */
    setup_l3_cat();
    on_each_cpu(&set_else_l3_cos, NULL, 1);
    set_hardlog_l3_cos(NULL);

    while (!kthread_should_stop()) {

        /* Disable this thread from being preempted here. */
        // preempt_disable();

        /* Get the current statistics (head and tail indices) of the circular buffer (HostBuf). */
        end = audit_cbuff_ptr->head;
        start = audit_cbuff_ptr->tail;
        size = end - start;

        /* (Measurement) */
        save_log_appended();

        /* (Debugging) */
        if (debug) {
            if (!special) {
                HARDLOG_MODULE_PRINT("(cbuff) writing: start = %lld, end = %lld\n", start, end);
            } else {
                HARDLOG_MODULE_PRINT("(cbuff) writing: start_one = %lld, end_one = %lld, start_two = 0, end_two = %lld\n", 
                    start, end_one, end);
            }
        }

#if 0
        /* Sync testing purposes, let the buffer fill then write. */
        if (size > (HARDLOG_AUDIT_CBUFFSIZE - 1024)) {
            /* notify the IOCTL thread to wait. */
            hardlog_logging = true;

            /* Write to the audit device. */
            usb_write(end, start);

            /* Update the tail, letting the kernel know we have written to usb. */
            audit_cbuff_ptr->tail = end;
        }
#endif

        /* notify the IOCTL thread to wait (if it was reading). */
        hardlog_logging = true;

        /* Write to the audit device (uninterrupted!). */
        usb_write(end, start);

        /* Update the tail, letting the kernel know we have written to usb. */
        audit_cbuff_ptr->tail = end;

        /* Notify the IOCTL thread to read stuff now (if it needs to). */
        hardlog_logging = false;

        /* Re-enable preemption (allowing this thread to be interrupted). */
        // preempt_enable();

        /* Sleep for a small time, waiting for next log data.
           Note: If we do not sleep here, the whole system sometimes gets unresponsive. 
           Reference: https://www.falsig.org/simon/blog/2013/07/10/real-time-linux-kernel-drivers-part-3-the-better-implementation/
        */
        elapsed_us = ktime_us_delta(ktime_get(), start_us);
        if(elapsed_us < 100) {
            /* (Debugging) */
            if (debug)
                HARDLOG_MODULE_PRINT("detail: sleeping for 100us.\n");
            usleep_range(90 - elapsed_us, 110 - elapsed_us);
        }
        start_us = ktime_get();
    }

    /* Write whatever is left to USB device too. This is to handle some
       corner cases on the HostBuf. */
    if (audit_cbuff_ptr->head != audit_cbuff_ptr->tail) {
        end = audit_cbuff_ptr->head % cbuff_size;
        start = audit_cbuff_ptr->tail % cbuff_size;
        if (end < start) {
            start = 0;
        }
        size = end - start;

        if (size > 0) {
            usb_write(end, start);
            audit_cbuff_ptr->tail = end;
        }
    }

    return 1;
}

/* Init and Exit functions */
static int __init hardlog_enable_init(void) {

    struct sched_param param;

    HARDLOG_MODULE_PRINT("enabling hardlog-based logging!\n");

    /* Setup device (if specified) */
    if (!usb_open()) {
        HARDLOG_MODULE_PRINT("error: couldn't open usb device.\n");
        return -1;
    }
    HARDLOG_MODULE_PRINT("success: opened the device (%s).\n", device);

    /* Start background kernel thread */
    hardlog_auditd_task = kthread_create(hardlog_auditd_thread, NULL, "hardlog_auditd");
    if (IS_ERR(hardlog_auditd_task)) {
        int err = PTR_ERR(hardlog_auditd_task);
        HARDLOG_MODULE_PRINT("error: failed to start the hardlog thread (%d)\n", err);
        return -1;
    }

    /* Bind the audit thread to CPU #2 and run it */
    kthread_bind(hardlog_auditd_task, 2);
    wake_up_process(hardlog_auditd_task);

    HARDLOG_MODULE_PRINT("success: started the hardlog kernel thread (and bound it!)\n");

    /* Setting nice value of Hardlog thread. This thread is not nice at all!! ;) */
    set_user_nice(hardlog_auditd_task, -20);

    /* Setting scheduling priority. This did not help much. Left here for future analysis. */
    param.sched_priority = 99;
    sched_setscheduler(hardlog_auditd_task, SCHED_FIFO, &param);

    /* Initialize the device driver to accept IOCTL commands. */
    if (!hardlog_ioctl_init()) {
        HARDLOG_MODULE_PRINT("error: failed to start IOCTL.\n");
        return -1;
    }

    /* Let the kernel know that the hardlog thread is ready! */
    if (!hardlog_logger_setup()) {
        HARDLOG_MODULE_PRINT("error: failed to start the logger.\n");
        return -1;
    }

    return 0;
}

static void __exit hardlog_enable_exit(void) {
    /* Stop the running kernel thread. */
    kthread_stop(hardlog_auditd_task);

    /* Close the usb device. */
    usb_close();

    /* Stop accepting IOCTL commands. */
    hardlog_ioctl_teardown();

    /* Let the kauditd thread know that hardlog is stopped. */
    hardlog_logger_teardown();

    /* Also send a wake-up request, if kauditd is sleeping. The remaining
       logs should be handled by kauditd. */
    if(hardlog_auditd_wait_ptr != 0) {
        wake_up(hardlog_auditd_wait_ptr);
        wake_up(kauditd_wait_ptr);
        HARDLOG_MODULE_PRINT("Woken up the kauditd thread!\n");
    }

    /* (Benchmarking) Print measurements. */
    print_measurement();

    /* Reset L3 CAT. */
    on_each_cpu(&reset_l3_cat, NULL, 1);
}

module_init(hardlog_enable_init);
module_exit(hardlog_enable_exit);