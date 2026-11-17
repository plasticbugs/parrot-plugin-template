add_executable(ex_midi main.c usbmidi_dev.c)
parrotcart_screen(ex_midi)
# TinyUSB as a device, with this directory's tusb_config.h (first on the path)
target_include_directories(ex_midi BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ex_midi tinyusb_device)
cvcart_plugin(ex_midi ID parrotcart.ex-midi NAME "MIDI Out Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXMIDI.CVP
              USES usb-device)
