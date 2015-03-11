#!/bin/bash
###############################################################################
#
#                           Kernel Build Script 
#
###############################################################################
# 2011-10-24 effectivesky : modified
# 2010-12-29 allydrop     : created
# 2015-03-08 Le Hoang   : modified for custom source
###############################################################################
##############################################################################
# set toolchain
##############################################################################
# Don't edit ARCH
export ARCH=arm
#Patch to toolchain
#export PATH=$(pwd)/../../../../arm-eabi-4.6/bin:$PATH
export CROSS_COMPILE=~/gcc_linaro/bin/arm-eabi-
#export CROSS_COMPILE=~/android-ndk-r9d/toolchains/arm-linux-androideabi-4.8/prebuilt/linux-x86/bin/arm-linux-androideabi-
#Export Host name and user build
export KBUILD_BUILD_USER=lehoang
export KBUILD_BUILD_HOST=HLF-LeHoang96-Ubuntu32
##############################################################################
# make zImage
##############################################################################
mkdir -p ./obj/KERNEL_OBJ/
#make O=./obj/KERNEL_OBJ/ 
make ARCH=arm O=./obj/KERNEL_OBJ/ cm_ef52k_defconfig #Use arch/arm/configs/cm_ef52k_defconfig for CyanogenMod, PAC, Omini rom
make ARCH=arm O=./obj/KERNEL_OBJ/ 2>&1 | tee kernel_log.txt
# Use make -j#

##############################################################################
# Copy Kernel Image
##############################################################################
cp -f ./obj/KERNEL_OBJ/arch/arm/boot/zImage .
