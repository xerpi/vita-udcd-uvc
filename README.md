# PSVita UDCD USB Video Class plugin

## What's this?

This is a kernel plugin that uses lets you stream your PSVita screen to your computer via USB.

## How does it work?

The plugin uses the [SceUdcd](https://wiki.henkaku.xyz/vita/SceUdcd) module of the PSVita OS to setup
the necessary USB descriptors to simulate and behave as an [USB Video Class](https://en.wikipedia.org/wiki/USB_video_device_class) device (like a webcam or an USB video capture card).

The [hardware color space converter](https://wiki.henkaku.xyz/vita/IFTU_Registers) of the PSVita's SoC is used to perform the conversion to the destination pixel format; then the USB
controller directly performs a DMA transfer from the physical address of the resulting converted framebuffer, and therefore, saving CPU usage and power consumption.

## Supported formats and resolutions

* 960x544 @ 30 FPS and (less than) 60 FPS
* 896x504 @ 30 FPS and (almost) 60 FPS
* 864x488 @ 30 FPS and 60 FPS
* 480x272 @ 30 FPS and 60 FPS

## Download and installation

**Download**:

* [udcd\_uvc.skprx](https://github.com/xerpi/vita-udcd-uvc/releases)

**Compilation**

* [vitasdk](https://vitasdk.org/) is needed.

**Installation**:

1. Copy `udcd_uvc.skprx` to your PSVita
2. Add `udcd_uvc.skprx` to taiHEN's config (`ur0:/tai/config.txt` or `ux0:/tai/config.txt`):
```
*KERNEL
ur0:tai/udcd_uvc.skprx
```
3. Reboot your PSVita.

## Troubleshooting

If the video looks glitched, try to change the video player configuration to use the *NV12* format or switch to another player (like PotPlayer or OBS).

If you use Windows 10 you might have to change the Camera access permissions on the Privacy Settings.

On Linux I recommend using *mplayer* (`mplayer tv:// -tv driver=v4l2:device=/dev/videoX:width=960:height=544`).

**Audio noise fix:**

* Disable USB power supply (Settings > System)

Note: Remember that if anything goes wrong (like PSVita not booting) you can always press L at boot to skip plugin loading.

Note 2: No, it *doesn't* stream audio. For that use a 3.5mm jack to jack adapter (a ferrite bead might help reduce the electromagnetic noise).
