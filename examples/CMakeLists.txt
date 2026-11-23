# Every example, each its own plugin:
#
#   cmake -S examples -B build-examples -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build-examples            -> build-examples/<example>/EX....CVP
#
# To start a plugin from one, copy its files into src/ and its lines from
# its CMakeLists.txt into the one at the top of the repository.
cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/parrotcart.cmake)
project(parrotcart_examples C CXX ASM)
parrotcart_init()

add_subdirectory(screen)
add_subdirectory(sound)
add_subdirectory(usb-gamepad)
add_subdirectory(usb-keyboard-mouse)
add_subdirectory(usb-midi-out)
add_subdirectory(sd-card)
add_subdirectory(open-file)
add_subdirectory(frames)
add_subdirectory(samples)
add_subdirectory(two-controllers)
add_subdirectory(psram)
