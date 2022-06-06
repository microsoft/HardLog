#!/bin/bash -e

diff -urN desktop-original desktop-hardlog \
     --exclude="signing_key.pem" --exclude="signing_key.x509"\
     --exclude="x509.genkey" --exclude="inat-tables.c" \
     --exclude="fixdep" --exclude="objtool" > linux-5.4.92-host.diff