#include "cstdio"
#include "../DllFoo/foo.h"
#include "../DllBar/bar.h"

int main() {
    printf("myproj inside main\n");
    foo();
    bar();
    return 0;
}
