// TinyUSB's configuration for this plugin: a USB host on the cart's own
// port, through hubs, with the HID driver (keyboards and mice). TinyUSB
// includes "tusb_config.h" from wherever the include path finds it first,
// which CMakeLists.txt makes this directory.
#pragma once
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif
#define CFG_TUH_ENABLED             1
#define CFG_TUH_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 512
// A "hub" box often holds two hub chips, and a device that can't get an
// address stops all enumeration: leave room to spare.
#define CFG_TUH_HUB                 3
#define CFG_TUH_DEVICE_MAX          8
#define CFG_TUH_ENDPOINT_MAX        8
#define CFG_TUH_INTERFACE_MAX       8
#define CFG_TUH_HID                 6       // HID interfaces, all devices together (a keyboard is often two)
#define CFG_TUH_HID_EPIN_BUFSIZE    64
#define CFG_TUH_HID_EPOUT_BUFSIZE   64
