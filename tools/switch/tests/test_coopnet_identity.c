#include "src/pc/platform/switch/switch_coopnet_identity.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void switch_coopnet_log_printf(const char *fmt, ...) { (void)fmt; }

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 2;
    }
    uint64_t bytes = 0;
    const size_t hash = switch_coopnet_identity_hash_path(argv[1], &bytes);
    if (hash == 0) {
        printf("0 0\n");
        return 1;
    }
    printf("%016" PRIx64 " %" PRIu64 "\n", (uint64_t)hash, bytes);
    return 0;
}
