#include <switch.h>
#include <SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <curl/curl.h>
#include <zlib.h>

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    consoleInit(NULL);

    puts("SM64CoopDX Switch portlibs probe");
    printf("SDL compile version: %u.%u.%u\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    printf("libcurl: %s\n", curl_version());
    printf("zlib: %s\n", zlibVersion());

    if (SDL_Init(SDL_INIT_TIMER) == 0) {
        SDL_Quit();
    }

    CURL *curl = curl_easy_init();
    if (curl != NULL) {
        curl_easy_cleanup(curl);
    }

    /* Reference the graphics entry points so CI proves EGL/GLES2 linkage. */
    (void)eglGetDisplay(EGL_DEFAULT_DISPLAY);
    (void)glGetString(GL_VERSION);

    consoleExit(NULL);
    return 0;
}
