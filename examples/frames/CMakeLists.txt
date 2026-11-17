add_executable(ex_frames main.c)
parrotcart_frames(ex_frames)             # the frame kernel, not the screen kernel
cvcart_plugin(ex_frames ID parrotcart.ex-frames NAME "Frames Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXFRAMES.CVP)
