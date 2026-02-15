#include "foo.h"
#include "cstdio"
#include "../DllBar/bar.h"

void foo() {
    printf("DllFoo inside foo\n");
    bar();
}