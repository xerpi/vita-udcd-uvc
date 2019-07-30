#!/usr/bin/env bash

rm -rf builds
mkdir -p builds

make
cp udcd_uvc.skprx builds/udcd_uvc.skprx

make clean
make DISPLAY_OFF_OLED=1
cp udcd_uvc.skprx builds/udcd_uvc_oled_off.skprx

make clean
make DISPLAY_OFF_LCD=1
cp udcd_uvc.skprx builds/udcd_uvc_lcd_off.skprx

make clean
