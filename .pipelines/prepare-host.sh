#!/bin/bash -e
# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

sudo apt update
sudo apt install -y \
	wget \
	build-essential \
	libncurses-dev \
	bison \
	flex \
	libssl-dev \
	libelf-dev
