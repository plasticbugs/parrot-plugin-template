# Your plugin. Build it with
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/HELLO.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(hello C CXX ASM)
parrotcart_init()

add_executable(hello src/main.c)
parrotcart_screen(hello)

# Change all of these (docs/plugin-rules.md):
#   ID       developer.pluginname, yours for good: the menu and the registry go by it
#   NAME     as the menu shows it
#   VERSION  major.minor.patch; the release's tag is v + this
#   BY       you, as the menu shows it
#   ICON     8x8, src/icon.txt (docs/icons.md)
#   README   the plugin's details page on the cart, from README.md (docs/readme-spec.md)
# and when the plugin wants them:
#   HOME /FOLDER        where its files live on the card
#   OPENS .ext ...      the files the menu opens with it (FILE_ICON for their icon)
#   USES usb-host usb-device psram
#   USES standalone     it has OPENS but can also start with no file
cvcart_plugin(hello ID yourname.hello NAME "Hello" VERSION 0.1.0 BY "Your Name"
              ICON src/icon.txt README README.md)
