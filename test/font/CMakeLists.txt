# test/font: a plugin for test/run.sh and sim/test.sh (main.c says what it
# does), with the SDK's font and editor. Its cvcart_features() is what
# test/host/plugin.cmake reads to build it for this computer; it builds
# for the cart too, from a CMakeLists.txt that adds this folder.
add_executable(fonttest main.c)
parrotcart_screen(fonttest)
cvedit_use(fonttest)
cvcart_features(fonttest KEYBOARD CARD)
cvcart_plugin(fonttest ID parrotcart.fonttest NAME "Font Test" VERSION 1.0.0 BY "Parrot Cart"
              FILE FONTTEST.CVP)
