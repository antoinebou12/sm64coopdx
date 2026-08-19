#include "lib/src/libultra_internal.h"
#include "lib/src/osContInternal.h"
#include "macros.h"

#include "../configfile.h"

#include "controller_keyboard.h"
#include "controller_sdl.h"
#ifdef __SWITCH__
#include "controller_switch.h"
#include "game/local_multiplayer.h"
#include "pc/network/network.h"
#endif

// Analog camera movement by Pathétique (github.com/vrmiguel), y0shin and Mors
// Contribute or communicate bugs at github.com/vrmiguel/sm64-analog-camera

#ifdef __SWITCH__
static struct ControllerAPI *controller_implementations[] = {
    &controller_switch,
    &controller_keyboard,
};
#else
static struct ControllerAPI *controller_implementations[] = {
    &controller_sdl,
    &controller_keyboard,
};
#endif

static void clear_pad(OSContPad *pad) {
    pad->button = 0;
    pad->stick_x = 0;
    pad->stick_y = 0;
    pad->ext_stick_x = 0;
    pad->ext_stick_y = 0;
    pad->errnum = 0;
}

s32 osContInit(UNUSED OSMesgQueue *mq, u8 *controllerBits, UNUSED OSContStatus *status) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++)
        controller_implementations[i]->init();
#ifdef __SWITCH__
    *controllerBits = 0x0F;
#else
    *controllerBits = 1;
#endif
    return 0;
}

s32 osMotorStart(UNUSED void *pfs) {
    // Since rumble stops by osMotorStop, its duration is not nessecary.
    // Set it to 5 seconds and hope osMotorStop() is called in time.
    if (configRumbleStrength)
        controller_rumble_play(configRumbleStrength / 100.0f, 5.0f);
    return 0;
}

s32 osMotorStop(UNUSED void *pfs) {
    if (configRumbleStrength)
        controller_rumble_stop();
    return 0;
}

u32 osMotorInit(UNUSED OSMesgQueue *mq, UNUSED void *pfs, UNUSED s32 port) {
    return 0; // rumble is initialized in the specific backend's init function
}

s32 osContStartReadData(UNUSED OSMesgQueue *mesg) {
    return 0;
}

void osContGetReadData(OSContPad *pad) {
    clear_pad(pad);

    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        controller_implementations[i]->read(pad);
    }
}

void controller_read_local_pads(OSContPad *pads, u8 maxPads) {
    if (pads == NULL || maxPads == 0) return;

    for (u8 i = 0; i < maxPads; i++) {
        clear_pad(&pads[i]);
    }

#ifdef __SWITCH__
    controller_switch_poll();
    const u8 count = maxPads < 4 ? maxPads : 4;
    for (u8 i = 0; i < count; i++) {
        controller_switch_read_slot(i, &pads[i]);
    }

    // Physical/USB keyboard input remains a player-1-only overlay.
    controller_keyboard.read(&pads[0]);

    /*
     * CoopDX currently assigns one NetworkPlayer slot per remote peer. Until
     * the network protocol advertises multiple local identities per client,
     * only offline/local-wireless gameplay may expose several local Mario
     * slots. This keeps split-screen input from colliding with remote indexes.
     */
    const bool allow_multiple_local_players = (gNetworkType == NT_NONE);
    local_multiplayer_sync_controller_count(
        controller_switch_connected_count(), allow_multiple_local_players
    );
#else
    osContGetReadData(&pads[0]);
#endif
}

u8 controller_local_connected_count(void) {
#ifdef __SWITCH__
    return controller_switch_connected_count();
#else
    return 1;
#endif
}

void controller_rumble_local(u8 slot, float str, float time) {
#ifdef __SWITCH__
    (void)time;
    controller_switch_rumble_slot(slot, str);
#else
    (void)slot;
    controller_rumble_play(str, time);
#endif
}

void controller_rumble_local_stop(u8 slot) {
#ifdef __SWITCH__
    controller_switch_rumble_stop_slot(slot);
#else
    (void)slot;
    controller_rumble_stop();
#endif
}

u32 controller_get_raw_key(void) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        u32 vk = controller_implementations[i]->rawkey();
        if (vk != VK_INVALID) return vk + controller_implementations[i]->vkbase;
    }
    return VK_INVALID;
}

void controller_shutdown(void) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        if (controller_implementations[i]->shutdown)
            controller_implementations[i]->shutdown();
    }
}

void controller_reconfigure(void) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        if (controller_implementations[i]->reconfig)
            controller_implementations[i]->reconfig();
    }
}

void controller_rumble_play(float str, float time) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        if (controller_implementations[i]->rumble_play)
            controller_implementations[i]->rumble_play(str, time);
    }
}

void controller_rumble_stop(void) {
    for (size_t i = 0; i < sizeof(controller_implementations) / sizeof(struct ControllerAPI *); i++) {
        if (controller_implementations[i]->rumble_stop)
            controller_implementations[i]->rumble_stop();
    }
}
