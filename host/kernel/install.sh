#!/bin/bash -e

pushd desktop-hardlog
    make -j`nproc`
    sudo make modules_install install
popd