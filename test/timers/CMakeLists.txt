# test/timers: a plugin for test/run.sh and sim/test.sh (main.c says what
# it does). It builds for the cart too, from a CMakeLists.txt that adds
# this folder.
add_executable(timertest main.c)
parrotcart_screen(timertest)
cvcart_plugin(timertest ID parrotcart.timertest NAME "Timer Test" VERSION 1.0.0 BY "Parrot Cart"
              FILE TIMERTST.CVP)
