/* This file logs all system call log entries into the circular buffer (HostBuf), 
   rather than the audit queue (which seems to be rather expensive). 

   Note: More tests are needed on certain parts of this file. They are all marked.
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

/* File IO-related headers */
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

/* Sleep and timer headers */
#include <linux/hrtimer.h>
#include <linux/delay.h>


#include <linux/file.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/pid.h>

#include <net/sock.h>
#include <net/netlink.h>
#include <linux/skbuff.h>
#ifdef CONFIG_SECURITY
#include <linux/security.h>
#endif
#include <linux/freezer.h>
#include <linux/pid_namespace.h>
#include <net/netns/generic.h>

#include <linux/kallsyms.h>

#include "hl.h"

/* Mutex for producer (kauditf); not sure if we need this tbh. */
DEFINE_MUTEX(hardlog_audit_producer);
DEFINE_MUTEX(hardlog_audit_buffer);
DEFINE_MUTEX(hardlog_audit_consumer);
EXPORT_SYMBOL_GPL(hardlog_audit_producer);
EXPORT_SYMBOL_GPL(hardlog_audit_consumer);

/* Circular buffer (HostBuf). */
struct audit_circ_buffer audit_cbuff;
EXPORT_SYMBOL_GPL(audit_cbuff);

/* Pointers to kernel backend. */
func_typem* setup_hardlog_pointers_ptr;
bool* hardlog_active_ptr;
bool* hardlog_microlog_active_ptr;
uint64_t audit_cbuff_last_pos = 0;
extern bool biof;
extern bool microlog;
extern bool vmallocf;
extern bool flushtimef;

/* Prioritize the flushing of certain events.
   Current set: "clone, execveat, execve, fork, ptrace, chmod, setgid, setreuid, setuid"

   Note: Additional system calls can be added here (if needed).
 */
int prio_syscalls [PRIO_SYSCALL_NUM] = {56, 322, 59, 57, 101, 90, 10000, 106, 113, 105};

unsigned long make_space_in_cbuff(void) {
    /* An event is at most 512 bytes (set in the kernel header files). 
       Our audit log (HostBuf) is already 4KB more than HARDLOG_AUDIT_BUFSIZE. 
       Thus, there will be no overflow. */

    /* Currently, we are just waiting for the buffer to get empty again. I'm not
       sure if this is the best way to do this. */
    while ((audit_cbuff.head - audit_cbuff.tail) >= HARDLOG_AUDIT_CBUFFSIZE) {
        usleep_range(1900, 2100);
    }

    return audit_cbuff.head;
}

bool is_prio_syscall(int syscall) {
    int i;
    for (i = 0; i < PRIO_SYSCALL_NUM; i++) {
        if (prio_syscalls[i] == syscall) return true;
    }
    return false;
}

/* Debugging purposes only. */
void print_event(const char* fmt, va_list args) {
    char print_buffer[1000];
    vsnprintf(print_buffer, 1000, fmt, args);
    printk("%s\n", print_buffer);
}

void audit_log_start_hardlog (struct audit_buffer* ab) {

    /* Create a Hardlog buffer inside the ab struct using either vmalloc or kmalloc. If I
       correctly recall, it did not make much difference. */
    if (!vmallocf)
        ab->hardlog_event_buffer = (char*) kmalloc(HARDLOG_AUDIT_EVENTSIZE, GFP_KERNEL);
    else
        ab->hardlog_event_buffer = (char*) vmalloc(HARDLOG_AUDIT_EVENTSIZE);

    /* Perform sanity check. */
    if (!ab->hardlog_event_buffer) {
        HARDLOG_PRINT("error: could not create ab's event buffer!\n");
        return;
    }
    ab->hardlog_event_consumed = 0;
}
EXPORT_SYMBOL(audit_log_start_hardlog);

void audit_log_format_hardlog(struct audit_buffer* ab, const char* fmt, va_list args) {
    int len = 0;

    /* Perform various sanity checks */
    if (!ab) return;
    if (!ab->hardlog_event_buffer) {
        HARDLOG_PRINT("error: ab's event buffer does not exist!\n");
        return;
    }
    if ((HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed) <= 0) {
        /* We should start consuming the next entry if the current entry 
           is completely used but not currently implemented because this
           case has never happened with system calls. */
        HARDLOG_PRINT("Warning: no more space in this event entry; dropping.\n");
        return;
    }

#if 0
    /* reallocate the buffer to 2 KB (max size allowed). */
    if ((HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed) <= 100) {
        krealloc(ab->hardlog_event_buffer, 2*1024, GFP_KERNEL);
        if (!ab->hardlog_event_buffer) {
            HARDLOG_PRINT("error: could not extend ab's event buffer!\n");
            return;
        }
    }
#endif

    /* Copy into the event's own buffer. */
    len = vsnprintf(&(ab->hardlog_event_buffer[ab->hardlog_event_consumed]),
            HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed, fmt, args);
    if (len < HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed) {
        /* Event has been fully logged. */
        ab->hardlog_event_consumed += len;
    } else {
        /* Event has not been fully logged. We are currently not doing anything 
           about it because this case has never happened. If it does happen, it
           should be fixed by increasing overall buffer size. */
        ab->hardlog_event_consumed += len - 10;
        HARDLOG_PRINT("Warning: event was bigger than allocated block size (%d)!\n",
                    HARDLOG_AUDIT_BLOCKSIZE);
    }
}
EXPORT_SYMBOL(audit_log_format_hardlog);

void audit_log_n_string_hardlog(struct audit_buffer* ab, const char* string, size_t slen) {
	char* ptr = &(ab->hardlog_event_buffer[ab->hardlog_event_consumed]);
    
    /* Perform sanity checks. */
    if (!ab) return;
    if (!ab->hardlog_event_buffer) {
        HARDLOG_PRINT("error: ab's event buffer does not exist!\n");
        return;
    }

    /* Make sure we have slen+2 space; slen for string, 2 for quotes, 1 for terminator. */
    if ((HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed) < slen+3) {
        HARDLOG_PRINT("error: no more space in this event entry; dropping\n");
        return;
    }

    /* Copy to the buffer. */
    *ptr++ = '"';
	memcpy(ptr, string, slen);
	ptr += slen;
	*ptr++ = '"';
    *ptr = 0;

    /* Increase the consumed space. For some reason, Kaudit does not include '0' in
       the original function so I am ignoring too. (TODO: this should be checked) */
    ab->hardlog_event_consumed += slen + 2;
}
EXPORT_SYMBOL(audit_log_n_string_hardlog);

void audit_log_n_hex_hardlog(struct audit_buffer *ab, const unsigned char *buf, size_t len) {
    unsigned char* ptr;
    int i;  

    /* Perform sanity checks. */
    if (!ab) return;
    if (!ab->hardlog_event_buffer) {
        HARDLOG_PRINT("error: ab's event buffer does not exist!\n");
        return;
    }

    ptr = &(ab->hardlog_event_buffer[ab->hardlog_event_consumed]);

    /* Make sure we have len << 1 (2*len) space. */
    if ((HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed) < (len << 1)) {
        HARDLOG_PRINT("error: no more space in this event entry; dropping\n");
        return;
    }

    /* Add to the buffer. */
	for (i = 0; i < len; i++)
		ptr = hex_byte_pack_upper(ptr, buf[i]);
	*ptr = 0;

    /* Increase the consumed space by 2*len. */
    ab->hardlog_event_consumed += (len << 1);
}
EXPORT_SYMBOL(audit_log_n_hex_hardlog);

void audit_log_end_hardlog(struct audit_buffer* ab) {
    uint64_t cbuff_head, len = 0;
    uint64_t cbuff_size = HARDLOG_AUDIT_CBUFFSIZE;

    /* Perform various sanity checks */
    if (!ab) return;
    if (!ab->hardlog_event_buffer) {
        HARDLOG_PRINT("error: ab's event buffer does not exist!\n");
        return;
    }

    /* I think the reason that these messages are sent is to tell the
       userspace auditd that the systemcall event is complete (Not sure).
       I tested that not sending these empty messages did not change the logs 
       stored in the audit device. (TODO: more testing is needed.) */
    if (strlen(ab->hardlog_event_buffer) < 35) {
        update_empty_logs_measurement();

        /* Use vmalloc or kmalloc (depending on configuration). Did not
           make much difference (if I correctly recall). */
        if (!vmallocf)
            kfree(ab->hardlog_event_buffer);
        else 
            vfree(ab->hardlog_event_buffer);

        return;
    }

#if 0
    /* append an endline to the end of the event */
    snprintf(&(ab->hardlog_event_buffer[HARDLOG_AUDIT_BLOCKSIZE]), 1, "\n");

    /* append to the log in an orderly fashion */
    mutex_lock(&hardlog_audit_producer);
    memcpy(&(audit_cbuff.buffer[audit_cbuff.head]), ab->hardlog_event_buffer, 
                HARDLOG_AUDIT_EVENTSIZE);
    audit_cbuff.head += HARDLOG_AUDIT_EVENTSIZE;
    mutex_unlock(&hardlog_audit_producer);

#endif

    /* Append an endline to the end of the event. */
    len = snprintf(&(ab->hardlog_event_buffer[ab->hardlog_event_consumed]),
        HARDLOG_AUDIT_BLOCKSIZE - ab->hardlog_event_consumed, "\n");
    ab->hardlog_event_consumed += len;

    /* need to update head in an orderly fashion only */
    mutex_lock(&hardlog_audit_producer);

    /* Make space in the circular buffer. */
    cbuff_head = make_space_in_cbuff();
    cbuff_head %= cbuff_size;

    /* Actually copy the log entry into HostBuf. */
    memcpy(&(audit_cbuff.buffer[cbuff_head]), ab->hardlog_event_buffer, 
             ab->hardlog_event_consumed);

    /* Update the head of HostBuf. */
    audit_cbuff.head += ab->hardlog_event_consumed;

    /* Keep track of cbuff_head to see if a priority system call is now flushed */
    cbuff_head = audit_cbuff.head;

    /* (Benchmarking) For log data measurements. */
    update_logs_created_measurement(ab->hardlog_event_consumed);
    
    /* (Benchmarking) For log flushing time measurements. */
    if (flushtimef)
        mark_log_appended();

    mutex_unlock(&hardlog_audit_producer);

    /* If the system call is priority (critical event), the thread should wait until
       the logs have been flushed. */
    if (ab->ctx) {
        if (is_prio_syscall(ab->ctx->major)) {
            /* Sleep until the system call's log (and all prior logs) were flushed. */
            while (audit_cbuff.tail < cbuff_head) {
                usleep_range(200, 300);
            }

            /* Keep track that a priority system call was flushed. */
            update_prio_syscall_measurement(ab->ctx->major);
        }
    }

    /* Free the hardlog event buffer here instead of its dedicated function 
       in audit.c. The reason is that otherwise, the previous (non-hardlog) ab's
       might also be freed (which results in a segmentation fault). There might be
       a better way of avoiding this problem. */
    if(!vmallocf)
	    kfree(ab->hardlog_event_buffer);
    else
        vfree(ab->hardlog_event_buffer);
}
EXPORT_SYMBOL(audit_log_end_hardlog);

bool hardlog_logger_setup(void) {

    /* Make sure all pointers are fixed with the backend. I did this only
       to ensure easy setup/teardown of Hardlog for testing purposes. In practice, 
       Hardlog should be implemented in the kernel backend completely. */
    setup_hardlog_pointers_ptr = (func_typem*) kallsyms_lookup_name("setup_hardlog_pointers");
    if(setup_hardlog_pointers_ptr != 0) {
        if (setup_hardlog_pointers_ptr() == true){
            HARDLOG_MODULE_PRINT("success: found all hardlog pointers.\n");
        } else {
            HARDLOG_MODULE_PRINT("error: could not find all pointers.\n");
            return false;
        }
    } else {
        HARDLOG_MODULE_PRINT("error: could not find 'setup_hardlog_pointers'.\n");
        return false;
    }

    /* Initialize the circular buffer (HostBuf). */
    audit_cbuff.buffer = (char*) vmalloc(HARDLOG_AUDIT_CBUFFSIZE + 4096);
    if (!audit_cbuff.buffer) {
        HARDLOG_PRINT("error: could not create circular buffer!\n");
        return false;
    }
    HARDLOG_MODULE_PRINT("detail: location of audit_cbuff = %px\n", (void*) audit_cbuff.buffer);

    /* Initialize the internal variables for HostBuf. */
    audit_cbuff.head = 0;
    audit_cbuff.tail = 0;

	/* Mark as active so that the kauditd thread stops executing. The Linux kernel
       code was updated so that kauditd thread checks this variable. */
    hardlog_active_ptr = (bool*) kallsyms_lookup_name("hardlog_active");
    if(hardlog_active_ptr != 0) {
        HARDLOG_MODULE_PRINT("success: found 'hardlog_active'.\n");
        *hardlog_active_ptr = true;
    } else {
        HARDLOG_MODULE_PRINT("error: could not find 'hardlog_active'.\n");
        return false;
    }

    /* Mark as active so that the kauditd thread stops executing. This should not be
       needed (only for microlog scenarios). */
    hardlog_microlog_active_ptr = (bool*) kallsyms_lookup_name("hardlog_microlog_active");
    if(hardlog_active_ptr != 0) {
        HARDLOG_MODULE_PRINT("success: found 'hardlog_microlog_active'.\n");
        *hardlog_microlog_active_ptr = microlog;
    } else {
        HARDLOG_MODULE_PRINT("error: could not find 'hardlog_microlog_active'.\n");
        return false;
    }

#if 0
    /* Create 12 buffers where log data will be stored. This was
       12 because the evaluation CPU had 12 (virtual) threads. 

       (Potential future optimization possible here.)   
    */
    for (i = 0; i < NUM_CPUS; i++) { 
        logbufs[i] = kmalloc(HARDLOG_AUDIT_EVENTSIZE, GFP_KERNEL);
        if (!logbufs[i]) {
            HARDLOG_MODULE_PRINT("error: coulnd't allocate memory for log buffers.\n");
            return false;
        }
        logbufs_used = false;
    }
#endif

	HARDLOG_MODULE_PRINT("Activated hardlog and created circular buffer!\n");
    return true;
}
EXPORT_SYMBOL(hardlog_logger_setup);

void hardlog_logger_teardown(void) {
    if (hardlog_active_ptr != 0) *hardlog_active_ptr = false;
    if (hardlog_microlog_active_ptr != 0) *hardlog_microlog_active_ptr = false;
    mutex_unlock(&hardlog_audit_producer);
    if (audit_cbuff.buffer) vfree(audit_cbuff.buffer);
    HARDLOG_MODULE_PRINT("Terminated hardlog!\n");
}
EXPORT_SYMBOL(hardlog_logger_teardown);