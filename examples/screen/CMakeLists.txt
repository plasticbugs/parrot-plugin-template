add_executable(ex_screen main.c)
parrotcart_screen(ex_screen)
cvcart_plugin(ex_screen ID parrotcart.ex-screen NAME "Screen Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXSCREEN.CVP)
