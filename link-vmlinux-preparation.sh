#!/bin/bash
# Created by Deokgyu Yang <secugyu@gmail.com>

# Create a soft link file named ld for use lld to fix the build error on modern clang
# Remove "/clang" from $HOSTCC string
CLANG_BIN="${HOSTCC:0:(-6)}"
if [ ! -f "${CLANG_BIN}/ld" ]; then
    ln -s "${CLANG_BIN}/lld" "${CLANG_BIN}/ld"
fi

