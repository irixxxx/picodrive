#!/bin/bash
#
export TCBASE=/mingw64
export TC=$TCBASE
export CROSS_LIB_PATH=$TCBASE/lib
#export PATH=$TCBASE/bin:$PATH
export PKG_CONFIG_PATH=$TCBASE/bin
#
#
#
#export CROSS_COMPILE=mipsel-linux-
export CFLAGS="-I$TC/include"
export LDFLAGS="--sysroot $TC -L$TC/lib"
export PKG_CONFIG=$TCBASE/bin/pkg-config.exe
./configure --platform=mingw && make clean && make $1
