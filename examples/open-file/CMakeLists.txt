add_executable(ex_text main.c)
parrotcart_screen(ex_text)
cvsd_use(ex_text)
cvcart_plugin(ex_text ID parrotcart.ex-text NAME "Text Viewer" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXTEXT.CVP
              OPENS .txt FILE_ICON file_icon.txt)
