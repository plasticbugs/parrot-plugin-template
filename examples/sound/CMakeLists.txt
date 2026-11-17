add_executable(ex_sound main.c)
parrotcart_screen(ex_sound)
cvcart_plugin(ex_sound ID parrotcart.ex-sound NAME "Sound Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXSOUND.CVP)
