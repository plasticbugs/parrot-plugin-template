add_executable(ex_samples main.c)
parrotcart_frames(ex_samples BACK_TO_BACK)   # the frame kernel, its lists back to back (main.c)
cvcart_plugin(ex_samples ID parrotcart.ex-samples NAME "Samples Example" VERSION 1.0.0 BY "Parrot Cart"
              ICON icon.txt README README.md FILE EXSAMPLE.CVP)
