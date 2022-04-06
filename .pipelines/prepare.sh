#!/bin/bash -e
set -x

sudo apt update
sudo apt install -y \
	wget \
	build-essential \
	libncurses-dev \
	bison \
	flex \
	libssl-dev \
	libelf-dev \
	gcc-aarch64-linux-gnu \
	binutils-aarch64-linux-gnu \
	u-boot-tools
