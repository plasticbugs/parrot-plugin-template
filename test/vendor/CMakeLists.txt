# test/vendor: a plugin for test/run.sh and sim/test.sh (src/main.c says
# what it does), built from sources outside its folder. It builds for the
# cart too, from a CMakeLists.txt that adds this folder.
add_executable(vendortest src/main.c more/twice.c lib/answer.c)
target_include_directories(vendortest PRIVATE lib/include more)
target_compile_definitions(vendortest PRIVATE VENDOR_ANSWER=42)
set_source_files_properties(lib/answer.c PROPERTIES COMPILE_OPTIONS "-w")
parrotcart_screen(vendortest)
cvcart_plugin(vendortest ID parrotcart.vendortest NAME "Vendor Test" VERSION 1.0.0 BY "Parrot Cart"
              FILE VENDTEST.CVP)
