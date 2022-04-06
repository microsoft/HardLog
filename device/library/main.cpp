/* This file will wait for some signal from the kernel module
   which basically wakes up the thread, if there is some command
   from the server. */

#include <iostream>
#include <fstream>
#include <string>

#include "common.h"
using namespace std;

/* the log file */
string log_filename = "/mnt/nvme/host-data.bin";
uint64_t log_file_start_offset = 0;
uint64_t log_file_end_offset = 0;

/* the server commands file */
string comm_filename = "/mnt/nvme/server-commands.bin";

/* the server response file */
string resp_filename = "/mnt/nvme/server-data.bin";

/* the response to the server will be dumped here first */
string tmp_resp_filename = "response";

bool compression = true;

/* for stress testing purposes */
bool stress = true;

int main (void) {
   cout << "Hello from device library!" << endl;

   /* this is just for testing purposes, where writes are 
      made continuously to see if it affects flushing latency. */
   if (stress) {
      cout << "STRESS TESTING DEVICE NVME ACCESS.\n";
      log_file_start_offset = 0;
      log_file_end_offset = 10*1024*1024;
      while (true) dump_log();
   }

   /* start monitoring the device for commands from the server. */
   start_device_communication();

   cout << "Goodbye from the device library!\n";
   return 1;
}