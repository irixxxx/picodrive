#!/bin/bash
#
export TCBASE=/opt/gcw0-toolchain/usr/
export TC=$TCBASE/mipsel-gcw0-linux-uclibc/sysroot
export CROSS_LIB_PATH=$TCBASE/lib
export PATH=$TCBASE/bin:$PATH
export PKG_CONFIG_PATH=$TCBASE/bin
#
#
#
export CROSS_COMPILE=mipsel-linux-
export CFLAGS="-I$TC/usr/include -I$TC/usr/include/SDL"
export LDFLAGS="--sysroot $TC -L$TC/lib"
./configure --platform=opendingux && make clean && make opk
