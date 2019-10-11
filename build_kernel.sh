
#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=~/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-

rm -rf output
mkdir output

#make -C $(pwd) O=output lineage_gts3llte_defconfig
make -C $(pwd) O=output lineage_gts3lwifi_defconfig
make -C $(pwd) O=output -j16
