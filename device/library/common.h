// SPDX-License-Identifier: MIT
/* Copyright (c) 2021, Microsoft Corporation. */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <string>
using namespace std;

// defined in main.cpp
extern string log_filename;
extern string comm_filename;
extern string resp_filename;
extern string tmp_resp_filename;

extern uint64_t log_file_start_offset;
extern uint64_t log_file_end_offset;

extern bool compression;

// defined in response.cpp
void dump_log(void);
void filter_and_dump_log(string key);

void commit(void);
void commit_compress(void);
size_t commit_response (void);
void commit_response_message (size_t bytes);

// defined in decode.cpp
void decode_command(string command);
void handle_command(void);

// defined in device.cpp
void start_device_communication (void);

#endif