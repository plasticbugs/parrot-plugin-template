add_executable(ex_sd main.c)
parrotcart_screen(ex_sd)
cvsd_use(ex_sd)                             # the card and its file system
cvcart_plugin(ex_sd ID parrotcart.ex-sd NAME "SD Card Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXSD.CVP)
