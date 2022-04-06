/* Communicate with the device driver using IOCTL. */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <bits/stdc++.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "common.h"
using namespace std;

/* The device is the mass storage device, which has
   a background kernel thread to answer IOCTL requests. */
string devname = "/dev/hardlog_device";

struct hardlog_ioctl_request {
    bool server_request = false;
    uint64_t log_file_start_offset = 0;
    uint64_t log_file_end_offset = 0;
};

#define REQUEST       _IOW('a', 'a', struct hardlog_ioctl_request)
#define RESPONSE      _IOW('a', 'b', struct hardlog_ioctl_request)
#define DELETELOG     _IOR('a', 'c', struct hardlog_ioctl_request)

void start_device_communication (void) {
    /* open device */
    int devfd = open(devname.c_str(), O_RDWR);
    if (devfd < 0) {
        cout << "error: couldn't open " << devname << " device.\n";
        return;
    }

    /* keep pinging the device to see if a request was received */
    struct hardlog_ioctl_request hreq;
    while (true) {
        ioctl(devfd, REQUEST, &hreq);

        /* check if a request was received */
        if (hreq.server_request == true) {

            /* update the start and end log file offsets */
            log_file_start_offset = hreq.log_file_start_offset;
            log_file_end_offset = hreq.log_file_end_offset;

            /* handle the request */
            handle_command();

        }

        /* sleep for 1 second */
        sleep(1);
    }

    /* close device */
    close(devfd);
}