#pragma once

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct New3dsInputState {
    u32 held;
    u32 down;
    u32 up;
    circlePosition circle;
    circlePosition cstick;
    touchPosition touch;
    bool touching;
} New3dsInputState;

typedef struct New3dsRuntimeState {
    bool initialized;
    bool is_new_3ds;
    bool speedup_enabled;
    bool soc_initialized;
    bool exit_requested;
    void *soc_buffer;
    uint64_t start_ms;
    uint64_t frame_index;
    New3dsInputState input;
} New3dsRuntimeState;

bool new3ds_runtime_init(New3dsRuntimeState *state);
void new3ds_runtime_shutdown(New3dsRuntimeState *state);
bool new3ds_runtime_poll(New3dsRuntimeState *state);
void new3ds_runtime_request_exit(New3dsRuntimeState *state);
New3dsRuntimeState *new3ds_runtime_active(void);
bool new3ds_runtime_network_available(void);
bool new3ds_runtime_get_ipv4_string(char *buf, size_t len);
uint64_t new3ds_runtime_time_ms(void);
double new3ds_runtime_time_seconds(void);
void new3ds_runtime_sleep_ms(uint32_t ms);
float new3ds_runtime_axis_normalized(s16 value);
