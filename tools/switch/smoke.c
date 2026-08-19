#include <switch.h>
#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    puts("SM64CoopDX Switch toolchain smoke test");
    puts("libnx initialized successfully.");
    consoleExit(NULL);

    return 0;
}
