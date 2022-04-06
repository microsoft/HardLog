/* This file performs benchmarking of different system logging
   events. */

#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include "hl.h"

int write_count = 0;
uint64_t write_bytes = 0;
uint64_t write_time_us = 0;
uint64_t write_time_highest_us = 0;
uint64_t write_time_highest_bytes = 0;

uint64_t flush_time_highest_us = 0;
extern bool flushtimef;

uint64_t logs_created, logs_bytes, empty_logs = 0;
uint64_t prio_syscall_logs[PRIO_SYSCALL_NUM] = {0,0,0,0,0,0,0,0,0,0};

/* The code below is to check flushing latency. */
#define MAXLOGS 500000
uint64_t logs_appended = 0;
uint64_t logs_appended_start = 0;
uint64_t logs_appended_end = 0;
uint64_t logs_append_times[MAXLOGS];

uint64_t prev_log_time1 = 0;
uint64_t prev_log_time2 = 0;
bool reset_log_time = true;

void mark_log_appended(void) {
    if (logs_appended >= MAXLOGS) {
        logs_appended = 0;
    }
    logs_append_times[logs_appended] = ktime_get();
    logs_appended++;
}

void save_log_appended(void) {
    logs_appended_start = logs_appended_end;
    logs_appended_end = logs_appended;
}

void update_flush_time_measurement(void) {
    uint64_t curtime = ktime_get();
    int i;
    uint64_t prevtime = 0;

    if (logs_appended_end >= logs_appended_start) {
        /* Only forward case, this is simple. */
        for (i = logs_appended_start; i < logs_appended_end; i++) {
            if (logs_append_times[i] == 0) continue;
            prevtime = ktime_us_delta(curtime, logs_append_times[i]);
            if (prevtime > flush_time_highest_us)
                flush_time_highest_us = prevtime;
        }
    } else {
        /* Like every circular buffer, we should check both forward 
           and backward cases. */
        for (i = logs_appended_start; i < MAXLOGS; i++) {
            if (logs_append_times[i] == 0) continue;
            prevtime = ktime_us_delta(curtime, logs_append_times[i]);
            if (prevtime > flush_time_highest_us)
                flush_time_highest_us = prevtime;
        }
        for (i = 0; i < logs_appended_end; i++) {
            if (logs_append_times[i] == 0) continue;
            prevtime = ktime_us_delta(curtime, logs_append_times[i]);
            if (prevtime > flush_time_highest_us)
                flush_time_highest_us = prevtime;
        }
    }
}

uint64_t get_reset_flush_time_measurement(void) {
    uint64_t prev_flush_time = flush_time_highest_us;
    flush_time_highest_us = 0;
    return prev_flush_time;
}
/* Ends code for establishing flushing latency. */

void update_write_measurement(uint64_t cur_time_us, uint64_t size) {
    if (write_count == 0) {
        write_time_us = cur_time_us;
    } else {
        write_time_us = ((write_time_us*write_count)/(write_count+1)) + (cur_time_us/write_count+1);
    }

    if (cur_time_us > write_time_highest_us) {
        write_time_highest_us = cur_time_us;
        write_time_highest_bytes = size;
    }

    write_count++;
    write_bytes += size;
}

void update_write_time_measurement(uint64_t cur_time_us) {
    if (write_count == 0) {
        write_time_us = cur_time_us;
    } else {
         write_time_us = ((write_time_us*write_count)/(write_count+1)) + (cur_time_us/write_count+1);
    }

    if (cur_time_us > write_time_highest_us) 
        write_time_highest_us = cur_time_us;

    write_count++;
}

void update_write_bytes_measurement(uint64_t bytes) {
    write_bytes += bytes;
}

void update_logs_created_measurement(uint64_t bytes) {
    logs_created++;
    logs_bytes += bytes;
}

void update_empty_logs_measurement(void) {
    empty_logs++;
}

void update_prio_syscall_measurement(int syscall) {
    int i = 0;
    for (i = 0 ; i < PRIO_SYSCALL_NUM; i++) {
        if (prio_syscalls[i] == syscall) {
            prio_syscall_logs[i]++;
        }
    }
}

void print_measurement(void) {
    int i = 0;

    /* Print full statistics for logging and flushing. Here, "logger" means
       the threads that create logs and stores them into the HostBuf. The "flusher"
       is the background thread that sends logs to the audit device as quickly as
       it possibly can (best-effort). */
    HARDLOG_MODULE_PRINT("----------------------------------\n");
    HARDLOG_MODULE_PRINT("Statistics:\n");
    HARDLOG_MODULE_PRINT("[Logger] Logs: %lld, Bytes: %lld\n", logs_created, logs_bytes);
    HARDLOG_MODULE_PRINT("[Logger] Priority Logs\n");

    /* Break down statistics for each "critical event" or prioritized system call. */
    for (i = 0; i < PRIO_SYSCALL_NUM; i++)
        HARDLOG_MODULE_PRINT("[Logger] %d => %lld\n", prio_syscalls[i], prio_syscall_logs[i]);

    HARDLOG_MODULE_PRINT("[Flusher] Writes: %d, Bytes: %lld\n", write_count, write_bytes);
    HARDLOG_MODULE_PRINT("[Flusher] Average write time: %lld us, Max: %lld us, Max bytes: %lld\n", 
        write_time_us, write_time_highest_us, write_time_highest_bytes);

    /* Also print flushing time for certain experiments. */
    if (flushtimef) {
        HARDLOG_MODULE_PRINT("[Flusher] Maximum flush time: %lld us\n", flush_time_highest_us);
    }
    HARDLOG_MODULE_PRINT("----------------------------------\n");
}