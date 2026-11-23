add_executable(ex_two_controllers main.c)
parrotcart_frames(ex_two_controllers CONTROLLER2)     # the frame kernel, reading controller 2 too
cvcart_features(ex_two_controllers PADS)               # USB pad 1, for its two sticks
cvcart_plugin(ex_two_controllers ID parrotcart.ex-two-controllers NAME "Two Controllers Example" VERSION 1.0.0
              BY "Parrot Cart" ICON icon.txt README README.md FILE EXTWOCTL.CVP)
