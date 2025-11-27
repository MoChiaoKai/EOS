#!/bin/sh

set -x #prints each command and its arguments to the terminal before executing it
# set -e #Exit immediately if a command exits with a non-zero status

rmmod -f driver.ko
insmod driver.ko

./writer daniel & #run in subshell
./reader 192.168.222.100 8888 /dev/seg_dev
