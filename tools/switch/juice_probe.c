#include <switch.h>
#include <juice/juice.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PROBE_ROOT "sdmc:/switch/sm64coopdx"
#define PROBE_LOG_DIR PROBE_ROOT "/logs"
#define PROBE_LOG PROBE_LOG_DIR "/juice_probe.log"

static FILE *sLog = NULL;
static volatile bool sGatheringDone = false;
static volatile juice_state_t sState = JUICE_STATE_DISCONNECTED;
static unsigned int sCandidateCount = 0;
static unsigned int sNonLoopbackCandidateCount = 0;

static void probe_mkdir(const char *path) {
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir failed for %s: %d\n", path, errno);
    }
}

static void probe_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (sLog != NULL) {
        vfprintf(sLog, fmt, args);
        fputc('\n', sLog);
        fflush(sLog);
    }
    va_end(args);
}

static void on_state_changed(juice_agent_t *agent, juice_state_t state, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    sState = state;
    probe_log("state=%s", juice_state_to_string(state));
}

static void on_candidate(juice_agent_t *agent, const char *sdp, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    if (sdp == NULL) return;

    sCandidateCount++;
    if (strstr(sdp, " 127.0.0.1 ") == NULL && strstr(sdp, " ::1 ") == NULL) {
        sNonLoopbackCandidateCount++;
    }
    probe_log("candidate[%u]=%s", sCandidateCount, sdp);
}

static void on_gathering_done(juice_agent_t *agent, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    sGatheringDone = true;
    probe_log("gathering_done=1 candidates=%u non_loopback=%u",
              sCandidateCount, sNonLoopbackCandidateCount);
}

static void on_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr) {
    (void)agent;
    (void)data;
    (void)user_ptr;
    probe_log("recv_size=%zu", size);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    probe_mkdir("sdmc:/switch");
    probe_mkdir(PROBE_ROOT);
    probe_mkdir(PROBE_LOG_DIR);
    sLog = fopen(PROBE_LOG, "w");

    const AppletType appletType = appletGetAppletType();
    probe_log("probe=libjuice Horizon ICE");
    probe_log("hos_version=0x%08x", hosversionGet());
    probe_log("applet_type=%d", (int)appletType);

    Result socketRc = socketInitializeDefault();
    probe_log("socketInitializeDefault=0x%08x", (unsigned int)socketRc);
    if (R_FAILED(socketRc)) {
        if (sLog) fclose(sLog);
        return 2;
    }

    juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);

    juice_config_t config;
    memset(&config, 0, sizeof(config));
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
    config.stun_server_host = "stun.l.google.com";
    config.stun_server_port = 19302;
    config.cb_state_changed = on_state_changed;
    config.cb_candidate = on_candidate;
    config.cb_gathering_done = on_gathering_done;
    config.cb_recv = on_recv;

    probe_log("stun=%s:%u", config.stun_server_host, config.stun_server_port);
    juice_agent_t *agent = juice_create(&config);
    probe_log("juice_create=%p", (void *)agent);
    if (agent == NULL) {
        socketExit();
        if (sLog) fclose(sLog);
        return 3;
    }

    int gatherRc = juice_gather_candidates(agent);
    probe_log("juice_gather_candidates=%d", gatherRc);

    const u64 start = armGetSystemTick();
    const u64 frequency = armGetSystemTickFreq();
    while (appletMainLoop()) {
        const u64 elapsed = armGetSystemTick() - start;
        if (elapsed >= frequency * 10u) break;
        if (sGatheringDone &&
            (sState == JUICE_STATE_CONNECTED || sState == JUICE_STATE_COMPLETED || sState == JUICE_STATE_FAILED)) {
            break;
        }
        svcSleepThread(100000000LL);
    }

    char local[JUICE_MAX_CANDIDATE_SDP_STRING_LEN] = { 0 };
    char remote[JUICE_MAX_CANDIDATE_SDP_STRING_LEN] = { 0 };
    const int selectedRc = juice_get_selected_candidates(
        agent, local, sizeof(local), remote, sizeof(remote));
    probe_log("final_state=%s", juice_state_to_string(juice_get_state(agent)));
    probe_log("selected_candidates_rc=%d local=%s remote=%s", selectedRc, local, remote);
    probe_log("summary candidates=%u non_loopback=%u gathering_done=%d",
              sCandidateCount, sNonLoopbackCandidateCount, sGatheringDone ? 1 : 0);

    juice_destroy(agent);
    socketExit();

    const bool pass = gatherRc == JUICE_ERR_SUCCESS && sNonLoopbackCandidateCount > 0;
    probe_log("result=%s", pass ? "PASS" : "FAIL");
    if (sLog) {
        fclose(sLog);
        sLog = NULL;
    }
    return pass ? 0 : 4;
}
