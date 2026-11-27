// test/vendor/more/twice.c: more of the plugin's own code, outside its
// folder (--src): built with warnings and UBSan, as its main.c is.
#include "twice.h"

int twice(int x) { return 2 * x; }
