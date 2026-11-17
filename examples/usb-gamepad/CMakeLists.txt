add_executable(ex_pad main.c)
parrotcart_screen(ex_pad)
cvpad_use(ex_pad)                           # the SDK's USB pads; the port is a host
cvcart_plugin(ex_pad ID parrotcart.ex-pad NAME "Game Pad Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXPAD.CVP
              USES usb-host)
