# parrotcart.cmake -- what every Parrot Cart plugin's CMakeLists.txt starts with.
#
#   cmake_minimum_required(VERSION 3.13)
#   include(cmake/parrotcart.cmake)       # the board, the pico-sdk, TinyUSB
#   project(myplugin C CXX ASM)
#   parrotcart_init()                     # pico_sdk_init() and the cart's SDK
#
#   add_executable(myplugin src/main.c)
#   parrotcart_screen(myplugin)           # text and pictures: cvkernel + cvscreen
#   cvcart_plugin(myplugin ID you.myplugin NAME "My Plugin" VERSION 0.1.0 ...)
#
# The cart's SDK is the copy in sdk/ (tools/update-sdk.sh refreshes it); its
# own CMake functions are in sdk/firmware/revc/sdk/cvplugin.cmake:
# cvkernel_use, cvframes_use, cvsd_use, cvpsram_use, cvpad_use, cvcart_plugin.
#
# The pico-sdk: -DPICO_SDK_PATH=dir, or the environment's PICO_SDK_PATH, or
# -DPICO_SDK_FETCH_FROM_GIT=ON (version 2.1.1).
get_filename_component(PARROTCART_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(PARROTCART_SDK ${PARROTCART_DIR}/sdk/firmware/revc)

# the cart: an RP2350B with 16 MB of flash and 8 MB of PSRAM
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if (NOT PICO_SDK_FETCH_FROM_GIT_TAG)
    set(PICO_SDK_FETCH_FROM_GIT_TAG 2.1.1)
endif()

# USB from the SDK's TinyUSB (0.21 and fixes), not the pico-sdk's 0.18, whose
# host stops the plugin for half a second whenever something is plugged in
# (sdk/firmware/revc/sdk/tinyusb/VENDORED.md). Before pico_sdk_import.
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

macro(parrotcart_init)
    pico_sdk_init()
    if (NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.1.1")
        message(WARNING "The cart's SDK is built and tested with pico-sdk 2.1.1; this is ${PICO_SDK_VERSION_STRING}")
    endif()
    include(${PARROTCART_SDK}/sdk/cvplugin.cmake)
endmacro()

# A plugin that draws its screen on the cart (text, shapes, 256x192 in 15
# colours): the console runs the SDK's screen kernel, which also plays 4
# PSG voices and reports the wired controller. Most plugins want this.
function(parrotcart_screen TARGET)
    cvkernel_use(${TARGET})
    target_include_directories(${TARGET} PRIVATE ${PARROTCART_DIR}/lib ${PARROTCART_SDK}/midihello)
    _parrotcart_common(${TARGET})
endfunction()

# A plugin that makes a whole picture every TV frame (an emulator, a video
# player): the console runs the SDK's frame kernel, one command list a frame.
function(parrotcart_frames TARGET)
    cvframes_use(${TARGET})
    target_include_directories(${TARGET} PRIVATE ${PARROTCART_DIR}/lib)
    _parrotcart_common(${TARGET})
endfunction()

function(_parrotcart_common TARGET)
    # no serial console: the USB port is the plugin's to use, and printf
    # would bring in 19K of newlib that a plugin in SRAM can't spare
    pico_enable_stdio_usb(${TARGET} 0)
    pico_enable_stdio_uart(${TARGET} 0)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wno-stringop-truncation)
endfunction()
