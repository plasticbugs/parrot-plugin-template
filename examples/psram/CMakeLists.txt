add_executable(ex_psram main.c)
parrotcart_screen(ex_psram)
cvpsram_use(ex_psram)
cvcart_plugin(ex_psram ID parrotcart.ex-psram NAME "PSRAM Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXPSRAM.CVP
              USES psram)
