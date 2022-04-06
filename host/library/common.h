#ifndef __COMMON_H__
#define __COMMON_H__

#include <string>
using namespace std;

extern string devname;

/* defined in device.cpp */
void start_device_communication (void);
void stress_device_communication (void);

#endif