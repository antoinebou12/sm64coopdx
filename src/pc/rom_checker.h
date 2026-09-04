#include "types.h"

extern bool gRomIsValid;
extern char gRomFilename[];

#if defined(__SWITCH__) || defined(__3DS__)
const char *rom_get_setup_error(void);
#endif

void legacy_folder_handler(void);

bool main_rom_handler(void);
void rom_on_drop_file(const char *path);
