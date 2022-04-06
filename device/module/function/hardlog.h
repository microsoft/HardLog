#ifndef __HARDLOG_H__
#define __HARDLOG_H__

/* Just a fancy print statement */
#define HARDLOG_MODULE_PRINT(args...) printk("hardlog-device: " args)

/* Setting the size of the circular buffer to 256 MB for now */
#define HARDLOG_DATA_CBUFFSIZE 512*1024*1024

/* Circular buffer for storing data received from the host */
struct circ_buffer {
    char* buffer;
    uint64_t head;
    uint64_t tail;
};

/* Defined in hardlog-main.c */
extern struct circ_buffer data_cbuff;
extern struct circ_buffer data_cbuff_trim;
extern bool faststorage;
extern bool debug;
bool hardlog_setup(const char* name, struct fsg_lun* lunp, struct file* filp);
void hardlog_teardown(void);
int do_hardlog_write(struct fsg_common *common);

/* Defined in hardlog-flush.c */
bool hardlog_flush_setup(void);
void hardlog_flush_teardown(void);

/* Defined in hardlog-bio-fs.c */
extern struct file* device_file;
extern struct fsg_lun* device_lun;
extern uint64_t file_start_offset;
extern uint64_t file_end_offset;
bool fs_open(const char* device);
void fs_close(void);
int fs_write(uint64_t end, uint64_t start);

/* Defined in hardlog-ioctl.c */
extern bool server_request;
bool hardlog_ioctl_init(void);
void hardlog_ioctl_teardown(void);

#endif