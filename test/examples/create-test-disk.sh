#!/bin/sh

# Create a disk file with a GDT

image=$1

EXIT_SKIP=69

if which sgdisk; then
  sgdisk=sgdisk
elif [ -x /sbin/sgdisk ]; then
  sgdisk=/sbin/sgdisk
else
  echo "Cannot find sgdisk tool for creating GPT."
  exit $EXIT_SKIP
fi

seq 400000 | dd bs=1024 count=2048 of=$image
$sgdisk -o -N 1 -u 1:88E59675-4DC8-469A-98E4-B7B021DC7FBE $image
