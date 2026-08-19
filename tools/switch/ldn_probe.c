#ifdef __SWITCH__

#include <switch.h>
#include <stdint.h>
#include <stdio.h>

#include "pc/network/socket/socket_ldn_backend.h"

void network_ldn_receive_bridge(uint8_t local_index, void *addr, uint8_t *data, uint16_t data_length) {
    (void)local_index;
    (void)addr;
    (void)data;
    (void)data_length;
}

const char *network_ldn_player_name_bridge(void) {
    return "Probe";
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);
    printf("SM64CoopDX Switch LDN probe\n");
    printf("backend connected: %s\n", ldn_backend_connected() ? "yes" : "no");
    consoleUpdate(NULL);
    consoleExit(NULL);
    return 0;
}

#endif
