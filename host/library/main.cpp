/*
*  Copyright (c) 2021-2022 Microsoft Corporation.
*  Licensed under the MIT License.
*/
#include <iostream>
#include <string>

#include "common.h"
using namespace std;

string devname = "/dev/hardlog_host";

/* for stress testing purposes only */
bool stress = false;

int main() {
    cout << "Hello from the Hardlog host library!\n";

    if (stress) {
        stress_device_communication();
        return 1;
    }

    start_device_communication();

    return 1;
}
