#ifndef __HL_H__
#define __HL_H__

/* -------------------------------------------------------------- */
/* Adil. Got the below definitions from audit.h in kernel folder */

#include <linux/fs.h>
#include <linux/audit.h>
#include <linux/skbuff.h>
#include <uapi/linux/mqueue.h>
#include <linux/tty.h>

#define AUDIT_NAMES 5

/* At task start time, the audit_state is set in the audit_context using
   a per-task filter.  At syscall entry, the audit_state is augmented by
   the syscall filter. */
enum audit_state {
  AUDIT_DISABLED,   /* Do not create per-task audit_context.
         * No syscall-specific audit records can
         * be generated. */
  AUDIT_BUILD_CONTEXT,  /* Create the per-task audit_context,
         * and fill it in at syscall
         * entry time.  This makes a full
         * syscall record available if some
         * other part of the kernel decides it
         * should be recorded. */
  AUDIT_RECORD_CONTEXT  /* Create the per-task audit_context,
         * always fill it in at syscall entry
         * time, and always write out the audit
         * record at syscall exit time.  */
};

struct audit_entry {
  struct list_head  list;
  struct rcu_head   rcu;
  struct audit_krule  rule;
};

struct audit_cap_data {
  kernel_cap_t    permitted;
  kernel_cap_t    inheritable;
  union {
    unsigned int  fE;   /* effective bit of file cap */
    kernel_cap_t  effective;  /* effective set of process */
  };
  kernel_cap_t    ambient;
  kuid_t      rootid;
};

struct audit_names {
  struct list_head  list;   /* audit_context->names_list */

  struct filename   *name;
  int     name_len; /* number of chars to log */
  bool      hidden;   /* don't log this record */

  unsigned long   ino;
  dev_t     dev;
  umode_t     mode;
  kuid_t      uid;
  kgid_t      gid;
  dev_t     rdev;
  u32     osid;
  struct audit_cap_data fcap;
  unsigned int    fcap_ver;
  unsigned char   type;   /* record type */
  /*
   * This was an allocated audit_names and not from the array of
   * names allocated in the task audit context.  Thus this name
   * should be freed on syscall exit.
   */
  bool      should_free;
};

struct audit_proctitle {
  int len;  /* length of the cmdline field. */
  char  *value; /* the cmdline field */
};

/* The per-task audit context. */
struct audit_context {
  int       dummy;  /* must be the first element */
  int       in_syscall; /* 1 if task is in a syscall */
  enum audit_state    state, current_state;
  unsigned int      serial;     /* serial number for record */
  int       major;      /* syscall number */
  struct timespec64   ctime;      /* time of syscall entry */
  unsigned long     argv[4];    /* syscall arguments */
  long        return_code;/* syscall return code */
  u64       prio;
  int       return_valid; /* return code is valid */
  /*
   * The names_list is the list of all audit_names collected during this
   * syscall.  The first AUDIT_NAMES entries in the names_list will
   * actually be from the preallocated_names array for performance
   * reasons.  Except during allocation they should never be referenced
   * through the preallocated_names array and should only be found/used
   * by running the names_list.
   */
  struct audit_names  preallocated_names[AUDIT_NAMES];
  int       name_count; /* total records in names_list */
  struct list_head    names_list; /* struct audit_names->list anchor */
  char        *filterkey; /* key for rule that triggered record */
  struct path     pwd;
  struct audit_aux_data *aux;
  struct audit_aux_data *aux_pids;
  struct sockaddr_storage *sockaddr;
  size_t sockaddr_len;
  /* Save things to print about task_struct */
  pid_t       pid, ppid;
  kuid_t        uid, euid, suid, fsuid;
  kgid_t        gid, egid, sgid, fsgid;
  unsigned long     personality;
  int       arch;

  pid_t       target_pid;
  kuid_t        target_auid;
  kuid_t        target_uid;
  unsigned int      target_sessionid;
  u32       target_sid;
  char        target_comm[TASK_COMM_LEN];

  struct audit_tree_refs *trees, *first_trees;
  struct list_head killed_trees;
  int tree_count;

  int type;
  union {
    struct {
      int nargs;
      long args[6];
    } socketcall;
    struct {
      kuid_t      uid;
      kgid_t      gid;
      umode_t     mode;
      u32     osid;
      int     has_perm;
      uid_t     perm_uid;
      gid_t     perm_gid;
      umode_t     perm_mode;
      unsigned long   qbytes;
    } ipc;
    struct {
      mqd_t     mqdes;
      struct mq_attr    mqstat;
    } mq_getsetattr;
    struct {
      mqd_t     mqdes;
      int     sigev_signo;
    } mq_notify;
    struct {
      mqd_t     mqdes;
      size_t      msg_len;
      unsigned int    msg_prio;
      struct timespec64 abs_timeout;
    } mq_sendrecv;
    struct {
      int     oflag;
      umode_t     mode;
      struct mq_attr    attr;
    } mq_open;
    struct {
      pid_t     pid;
      struct audit_cap_data cap;
    } capset;
    struct {
      int     fd;
      int     flags;
    } mmap;
    struct {
      int     argc;
    } execve;
    struct {
      char      *name;
    } module;
  };
  int fds[2];
  struct audit_proctitle proctitle;
};

/* -------------------------------------------------------------- */


/* Adil: The definitions below are custom ones. */

/* circular buffer for audit logs */
struct audit_circ_buffer {
    char* buffer;
    char* backup_buffer;
    uint64_t head;
    uint64_t tail;
};

/* just a fancy print statement */
#define HARDLOG_MODULE_PRINT(args...) printk("hardlog-module: " args)

/* 1 MB hardlog audit buffer */
#define HARDLOG_AUDIT_CBUFFSIZE 1*1024*1024

/* Adil: I used these other sizes for testing and evaluation purposes. */
// #define HARDLOG_AUDIT_CBUFFSIZE 3*1024*1024
// #define HARDLOG_AUDIT_CBUFFSIZE 5*1024*1024
// #define HARDLOG_AUDIT_CBUFFSIZE 10*1024*1024

#define PRIO_SYSCALL_NUM 10

/* defined in hl-logger.c */
extern int prio_syscalls[PRIO_SYSCALL_NUM];

/* Some messages have a bigger size, so we might need bigger buffers, it can be set here. 

   Adil: This might need further testing. My recollection is that most audit messages were
   less than 512 bytes. But, I did not rigorously test this. (TODO)
*/
// #define HARDLOG_AUDIT_BLOCKSIZE 2048

/* type-definitions for some backend kernel functions that the module calls */
typedef void func_type(void);
typedef bool func_typem(void);

/* defined in hl-main.c */
extern bool hardlog_logging;

/* functions defined in hl-logger.c */
void hardlog_logger_teardown(void);
bool hardlog_logger_setup(void);

/* functions defined in hl-measure.c */
void update_write_measurement(uint64_t cur_time_us, uint64_t size);
void update_write_time_measurement(uint64_t cur_time_us);
void update_write_bytes_measurement(uint64_t bytes);
void update_logs_created_measurement(uint64_t bytes);
void update_empty_logs_measurement(void);
void update_prio_syscall_measurement(int syscall);
uint64_t get_reset_flush_time_measurement(void);
void print_measurement(void);

void mark_log_appended(void);
void save_log_appended(void);
void update_flush_time_measurement(void);

/* functions defined in hl-bio-fs.c */
bool bio_open(void);
void bio_close(void);
int  bio_write(uint64_t end, uint64_t start);

bool fs_open(void);
void fs_close(void);
int  fs_write(uint64_t end, uint64_t start);

/* functions defined in hl-ioctl.c */
bool hardlog_ioctl_init(void);
void hardlog_ioctl_teardown(void);

/* functions defined in hl-request.c */
bool bio_open_request_file(char* device);
void bio_close_request_file(void);
int bio_write_request_file(char* request);
int bio_read_request_file(char* data);

/* functions defined in hl-response.c */
bool bio_open_response_file(char* device);
void bio_close_response_file(void);
int bio_read_response_file(char* data, uint64_t size);

#endif