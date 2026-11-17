// TinyUSB's configuration for this plugin: a USB device on the cart's own
// port, with the MIDI class. TinyUSB includes "tusb_config.h" from wherever
// the include path finds it first, which CMakeLists.txt makes this directory.
#pragma once
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_PICO
#endif
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_MIDI                1
#define CFG_TUD_MIDI_RX_BUFSIZE     64
#define CFG_TUD_MIDI_TX_BUFSIZE     1024
