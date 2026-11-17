add_executable(ex_keys main.c usbinput.c)
parrotcart_screen(ex_keys)
# TinyUSB as a host, with this directory's tusb_config.h (first on the path)
target_include_directories(ex_keys BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ex_keys tinyusb_host)
cvcart_plugin(ex_keys ID parrotcart.ex-keys NAME "Keyboard Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXKEYS.CVP
              USES usb-host)
