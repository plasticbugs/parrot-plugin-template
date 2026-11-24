# test/leave: a plugin for test/run.sh and sim/test.sh (main.c says what
# it does), and leave.lua, the console's side of a late way back in MAME.
# It builds for the cart too, from a CMakeLists.txt that adds this folder.
add_executable(leavetest main.c)
parrotcart_screen(leavetest)
cvcart_plugin(leavetest ID parrotcart.leavetest NAME "Exit Test" VERSION 1.0.0 BY "Parrot Cart"
              FILE EXITTEST.CVP)
