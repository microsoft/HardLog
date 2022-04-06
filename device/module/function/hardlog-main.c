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

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include <linux/nospec.h>

#include "configfs.h"

#include "f_mass_storage.h"
#include "hardlog.h"

/* Data structures and user commands */
struct circ_buffer data_cbuff;
struct circ_buffer data_cbuff_trim;
bool faststorage = true;
bool debug = false;
bool ioctl = true;
module_param(faststorage, bool, S_IRUSR);
module_param(debug, bool, S_IRUSR);
module_param(ioctl, bool, S_IRUSR);

static uint64_t make_space_in_cbuff(void) {

    /* Currently, we are just waiting for the buffer to get empty again */
    while ((data_cbuff.head - data_cbuff.tail) >= HARDLOG_DATA_CBUFFSIZE) {
		HARDLOG_MODULE_PRINT("detail: waiting for the data buffer to be flushed.\n");
		HARDLOG_MODULE_PRINT("detail: head = %lld, tail = %lld.\n", data_cbuff.head, data_cbuff.tail);
        usleep_range(1900, 2100);
    }

	/* Once there is space, simply send the head of the buffer */
    return data_cbuff.head;
}

int do_hardlog_write(struct fsg_common *common) {
	struct fsg_lun		*curlun = common->curlun;
	u32			lba;
	struct fsg_buffhd	*bh;
	int			get_some_more;
	u32			amount_left_to_req, amount_left_to_write;
	loff_t			usb_offset, file_offset, file_offset_tmp;
	unsigned int		amount;
	ssize_t			nwritten;
	int			rc;

	/* Keeping statistics for the hardlog data buffer */
	uint64_t data_cbuff_offset = 0;
	uint64_t cbuff_size = HARDLOG_DATA_CBUFFSIZE;

	/* Perform sanity checks */
	if (!data_cbuff.buffer) {
		HARDLOG_MODULE_PRINT("error: hardlog data buffer is not allocated.\n");
		return -EINVAL;
	}
	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		return -EINVAL;
	}

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big
	 */
	if (common->cmnd[0] == WRITE_6)
		lba = get_unaligned_be24(&common->cmnd[1]);
	else {
		lba = get_unaligned_be32(&common->cmnd[2]);

		/*
		 * We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output.
		 */
		if (common->cmnd[1] & ~0x18) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}

		#if 0
		/* I explicitly removed this FUA */
		if (!curlun->nofua && (common->cmnd[1] & 0x08)) { /* FUA */
			spin_lock(&curlun->filp->f_lock);
			curlun->filp->f_flags |= O_SYNC;
			spin_unlock(&curlun->filp->f_lock);
		}
		#endif
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* Carry out the file writes */
	get_some_more = 1;
	file_offset = usb_offset = ((loff_t) lba) << curlun->blkbits;
	amount_left_to_req = common->data_size_from_cmnd;
	amount_left_to_write = common->data_size_from_cmnd;

	/* Debugging purposes only */
	if (debug)
		HARDLOG_MODULE_PRINT("usb-write: size = %d, offset = %lld, modulo = %lld, file len: %lld\n", 
			amount_left_to_write, file_offset, cbuff_size, curlun->file_length);

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = common->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/*
			 * Figure out how much we want to get:
			 * Try to get the remaining amount,
			 * but not more than the buffer size.
			 */
			amount = min(amount_left_to_req, FSG_BUFLEN);

			/* Beyond the end of the backing file? */
			if (usb_offset >= curlun->file_length) {
				get_some_more = 0;
				curlun->sense_data =
					SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
				curlun->sense_data_info =
					usb_offset >> curlun->blkbits;
				curlun->info_valid = 1;
				continue;
			}

			/* Get the next buffer */
			usb_offset += amount;
			common->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/*
			 * Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(common, bh, amount);
			if (!start_out_transfer(common, bh))
				/* Dunno what to do if common->fsg is NULL */
				return -EIO;
			common->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = common->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */
		if (bh->state == BUF_STATE_FULL) {
			smp_rmb();
			common->next_buffhd_to_drain = bh->next;
			bh->state = BUF_STATE_EMPTY;

			/* Did something go wrong with the transfer? */
			if (bh->outreq->status != 0) {
				curlun->sense_data = SS_COMMUNICATION_FAILURE;
				curlun->sense_data_info =
					file_offset >> curlun->blkbits;
				curlun->info_valid = 1;
				break;
			}

			amount = bh->outreq->actual;
			if (curlun->file_length - file_offset < amount) {
				LERROR(curlun,
				       "write %u @ %llu beyond end %llu\n",
				       amount, (unsigned long long)file_offset,
				       (unsigned long long)curlun->file_length);
				amount = curlun->file_length - file_offset;
			}

			/* Don't accept excess data.  The spec doesn't say
			 * what to do in this case.  We'll ignore the error.
			 */
			amount = min(amount, bh->bulk_out_intended_length);

			/* Don't write a partial block */
			amount = round_down(amount, curlun->blksize);
			if (amount == 0)
				goto empty_write;

			/* Perform the write */
			file_offset_tmp = file_offset;

			/* Simply copy it into the circular buffer's start for now.*/
			data_cbuff_offset = make_space_in_cbuff();
			data_cbuff_offset %= cbuff_size;
			memcpy(&data_cbuff.buffer[data_cbuff_offset], bh->buf, amount);
			data_cbuff.head += amount;
			nwritten = amount;

			if (signal_pending(current))
				return -EINTR;		/* Interrupted! */

			file_offset += nwritten;
			amount_left_to_write -= nwritten;
			common->residue -= nwritten;

			/* If an error occurred, report it and its position */
			if (nwritten < amount) {
				curlun->sense_data = SS_WRITE_ERROR;
				curlun->sense_data_info =
					file_offset >> curlun->blkbits;
				curlun->info_valid = 1;
				break;
			}

 empty_write:
			/* Did the host decide to stop early? */
			if (bh->outreq->actual < bh->bulk_out_intended_length) {
				common->short_packet_received = 1;
				break;
			}
			continue;
		}

		/* Wait for something to happen */
		rc = sleep_thread(common, true);
		if (rc)
			return rc;
	}

	return -EIO;		/* No default reply */
}

bool hardlog_setup(const char* filename, struct fsg_lun* lunp, struct file* filp) {

    struct sched_param param;
	struct kstat device_file_kstat;

	/* Print a welcome message */
	HARDLOG_MODULE_PRINT("Welcome to Hardlog's USB device gadget!\n");
    
	/* Perform sanity checks */
	if (strcmp(filename, "") == 0) {
		HARDLOG_MODULE_PRINT("error: filename is empty.\n");
		return false;
	}
	if (!filp) {
		HARDLOG_MODULE_PRINT("error: file pointer is NULL.\n");
		return false;
	}

    /* Printing parameters for testing */
    if (faststorage)
        HARDLOG_MODULE_PRINT("parameter: faststorage is ENABLED.\n");
    else 
        HARDLOG_MODULE_PRINT("parameter: faststorage is DISABLED.\n");
	
	device_file = filp;
	device_lun = lunp;
	HARDLOG_MODULE_PRINT("parameter: storage file is %s\n", filename);
	HARDLOG_MODULE_PRINT("parameter: lun name is %s\n", device_lun->name);

	if (!vfs_stat(device_lun->name, &device_file_kstat)) {
		HARDLOG_MODULE_PRINT("error: couldn't get the stats for device file.\n");
		return false;
	}
	HARDLOG_MODULE_PRINT("parameter: lun size is %lld\n", device_file_kstat.size);

    /* Creating data buffer (keep an extra buffer of 2MB) */
	HARDLOG_MODULE_PRINT("detail: creating hardlog data circular buffer.\n");
	data_cbuff.buffer = (char*) vmalloc(HARDLOG_DATA_CBUFFSIZE + 2*1024*1024);
	if (!data_cbuff.buffer) {
		HARDLOG_MODULE_PRINT("error: couldn't allocate memory for the cicular buffer.\n");
		return false;
	}
	HARDLOG_MODULE_PRINT("success: allocated memory for the cicular buffer.\n");

	data_cbuff.head = 0;
	data_cbuff.tail = 0;

    /* This thread is not nice at all!! ;) */
    set_user_nice(current, -20);

	/* Assigning highest priority to the thread! */
    param.sched_priority = 99;
    sched_setscheduler(current, SCHED_FIFO, &param);

    /* Creating a background thread to flush data to disk */
	if (!hardlog_flush_setup()) {
		HARDLOG_MODULE_PRINT("error: failed to setup hardlog flusher.\n");
		hardlog_teardown();
		return false;
	}

	/* Initialize IOCTL if needed */
	if (ioctl)
		hardlog_ioctl_init();

    return true;
}

void hardlog_teardown(void) {
	/* Close the background thread */
	hardlog_flush_teardown();

	/* Free space allocated for the buffer */
    if (data_cbuff.buffer) {
        vfree(data_cbuff.buffer);
	}

	if (data_cbuff_trim.buffer) {
		vfree(data_cbuff_trim.buffer);
	}

	if (ioctl)
		hardlog_ioctl_teardown();
	
	HARDLOG_MODULE_PRINT("Goodbye from Hardlog!\n");
}