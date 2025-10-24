#!/usr/bin/env bash

if pgrep -f "wpu-darkscore-select" ; then
    echo "Process is running"
else
    echo "Process is not running, starting..."
    wpu-darkscore-select -i "/media/SSD/media/bg/wpu-darkscore_output.csv" -e plasma-apply-wallpaperimage -l -d
fi
