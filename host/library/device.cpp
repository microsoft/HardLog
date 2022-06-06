// SPDX-License-Identifier: MIT
/* Copyright (c) 2021, Microsoft Corporation. */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <bits/stdc++.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <time.h>

#include "common.h"
using namespace std;

/* Just making a 4 KB buffer, should be enough */
struct hardlog_ioctl_request {
    char data[4096];
};

struct hardlog_ioctl_response {
    char* data;
    uint64_t size;
};

#define SENDREQUEST             _IOW('a', 'a', struct hardlog_ioctl_request)
#define CHECKRESPONSEREADY      _IOR('a', 'b', struct hardlog_ioctl_request)
#define READRESPONSE            _IOW('a', 'c', struct hardlog_ioctl_response)

bool is_response_ready(char* data) {
    istringstream response(data);
    string word;
    response >> word;
    if (word == "READY") {
        return true;
    }
    return false;
}

uint64_t get_response_size(char* data) {
    istringstream response(data);
    string word;
    response >> word;
    response >> word;
    return stoul(word);
}

void write_response_to_file(char* data, size_t size) {
    FILE* fp = fopen("response.gz", "w");
    if (!fp) {
        cout << "error: couldn't open response file.\n";
        return;
    }
    fwrite(data, size, 1, fp);
    fclose(fp);
}

void start_device_communication (void) {
    string command = "";
    uint64_t response_size = 0;
    struct hardlog_ioctl_request  hreq;
    struct hardlog_ioctl_response hrsp;

    time_t start,end;
    double dif;

    /* open device */
    int devfd = open(devname.c_str(), O_RDWR);
    if (devfd < 0) {
        cout << "error: couldn't open " << devname << " device.\n";
        return;
    }

    while (true) {

    start:

        /* ask for command */
        cout << "Your wish is my command: ";

        /* accept commands from stdin */
        cin >> command;

        if (command == "exit") {
            cout << "exiting the while loop!\n";
            break;
        }

        /* start timer */
        clock_t start_time = clock();

        time (&start);

        /* else, send message to device */
        snprintf((char*) &(hreq.data), 4096, "%s\n", command.c_str());
        ioctl(devfd, SENDREQUEST, &hreq);

        while (true) {
            /* wait for response */
            sleep(2);

            /* see if response is ready */
            ioctl(devfd, CHECKRESPONSEREADY, &hreq);

            if (is_response_ready(hreq.data)) {

                response_size = get_response_size(hreq.data);
                cout << "RESPONSE READY (SIZE = " << response_size << ")\n";

                hrsp.size = response_size;
                hrsp.data = (char*) malloc(response_size + 512);
                if (!hrsp.data) {
                    cout << "error: couldn't allocate memory for response.\n";
                    goto exit;
                }

                /* get response from device */
                ioctl(devfd, READRESPONSE, &hrsp);

                /* record the response */
                write_response_to_file(hrsp.data, hrsp.size);

                break;
            }

        }

        /* stop timer and print time */
        clock_t end_time = clock() - start_time;
        time(&end);

        double time_taken = ((double)end_time)/CLOCKS_PER_SEC; // in seconds
        dif = difftime(end,start);

        printf("Time taken to retrieve logs = %f\n", dif);

    }

exit:
    close (devfd);
}

void stress_device_communication (void) {
    string command = "";
    uint64_t response_size = 0;
    struct hardlog_ioctl_request  hreq;
    struct hardlog_ioctl_response hrsp;

    /* open device */
    int devfd = open(devname.c_str(), O_RDWR);
    if (devfd < 0) {
        cout << "error: couldn't open " << devname << " device.\n";
        return;
    }

    cout << "STRESS TESTING DEVICE COMMUNICATION\n";


    while (true) {

    start:

        command = "ALL";

        /* else, send message to device */
        snprintf((char*) &(hreq.data), 4096, "%s\n", command.c_str());
        ioctl(devfd, SENDREQUEST, &hreq);

        while (true) {
            /* wait for response */
            sleep(1);

            /* see if response is ready */
            ioctl(devfd, CHECKRESPONSEREADY, &hreq);

            if (is_response_ready(hreq.data)) {

                response_size = get_response_size(hreq.data);
                cout << "RESPONSE READY (SIZE = " << response_size << ")\n";

                hrsp.size = response_size;
                hrsp.data = (char*) malloc(response_size + 512);
                if (!hrsp.data) {
                    cout << "error: couldn't allocate memory for response.\n";
                    goto exit;
                }

                /* get response from device */
                ioctl(devfd, READRESPONSE, &hrsp);

                /* record the response */
                write_response_to_file(hrsp.data, hrsp.size);

                goto start;
            }
        }

    }

exit:
    close (devfd);
}