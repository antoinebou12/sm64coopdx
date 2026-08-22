#include <switch.h>
#include <juice/juice.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
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
static unsigned int sHostCandidateCount = 0;
static unsigned int sSrflxCandidateCount = 0;
static unsigned int sRelayCandidateCount = 0;
static unsigned int sNonNumericCandidateCount = 0;

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

static bool candidate_address_is_numeric(const char *sdp, char *typeOut, size_t typeOutSize) {
    char foundation[64] = { 0 };
    char transport[32] = { 0 };
    char host[257] = { 0 };
    char type[32] = { 0 };
    unsigned int component = 0;
    unsigned int priority = 0;
    unsigned int port = 0;

    const int fields = sscanf(sdp,
                              "a=candidate:%63s %u %31s %u %256s %u typ %31s",
                              foundation, &component, transport, &priority, host, &port, type);
    if (fields != 7) {
        probe_log("candidate_parse_failed=%s", sdp);
        return false;
    }

    if (typeOut != NULL && typeOutSize > 0) {
        snprintf(typeOut, typeOutSize, "%s", type);
    }

    struct in_addr ipv4;
    struct in6_addr ipv6;
    const bool numeric = inet_pton(AF_INET, host, &ipv4) == 1 ||
                         inet_pton(AF_INET6, host, &ipv6) == 1;
    probe_log("candidate_address host=%s port=%u type=%s numeric=%d",
              host, port, type, numeric ? 1 : 0);
    return numeric;
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

    char type[32] = { 0 };
    if (!candidate_address_is_numeric(sdp, type, sizeof(type))) {
        sNonNumericCandidateCount++;
    }
    if (strcmp(type, "host") == 0) {
        sHostCandidateCount++;
    } else if (strcmp(type, "srflx") == 0) {
        sSrflxCandidateCount++;
    } else if (strcmp(type, "relay") == 0) {
        sRelayCandidateCount++;
    }

    probe_log("candidate[%u]=%s", sCandidateCount, sdp);
}

static void on_gathering_done(juice_agent_t *agent, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    sGatheringDone = true;
    probe_log("gathering_done=1 candidates=%u non_loopback=%u host=%u srflx=%u relay=%u non_numeric=%u",
              sCandidateCount, sNonLoopbackCandidateCount, sHostCandidateCount,
              sSrflxCandidateCount, sRelayCandidateCount, sNonNumericCandidateCount);
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

    Result nifmRc = nifmInitialize(NifmServiceType_User);
    probe_log("nifmInitialize=0x%08x", (unsigned int)nifmRc);
    if (R_FAILED(nifmRc)) {
        socketExit();
        if (sLog) fclose(sLog);
        return 5;
    }

    u32 currentIp = 0;
    Result ipRc = nifmGetCurrentIpAddress(&currentIp);
    char currentIpString[INET_ADDRSTRLEN] = { 0 };
    if (R_SUCCEEDED(ipRc)) {
        struct in_addr addr = { .s_addr = currentIp };
        inet_ntop(AF_INET, &addr, currentIpString, sizeof(currentIpString));
    }
    probe_log("nifmGetCurrentIpAddress=0x%08x raw=0x%08x ip=%s",
              (unsigned int)ipRc, (unsigned int)currentIp,
              currentIpString[0] != '\0' ? currentIpString : "(unavailable)");

    juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);

    juice_config_t config;
    memset(&config, 0, sizeof(config));
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;
    config.bind_address = "0.0.0.0";
    config.stun_server_host = "stun.l.google.com";
    config.stun_server_port = 19302;
    config.cb_state_changed = on_state_changed;
    config.cb_candidate = on_candidate;
    config.cb_gathering_done = on_gathering_done;
    config.cb_recv = on_recv;

    probe_log("ice_mode=thread bind=%s", config.bind_address);
    probe_log("stun=%s:%u", config.stun_server_host, config.stun_server_port);
    juice_agent_t *agent = juice_create(&config);
    probe_log("juice_create=%p", (void *)agent);
    if (agent == NULL) {
        nifmExit();
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
    probe_log("summary candidates=%u non_loopback=%u host=%u srflx=%u relay=%u non_numeric=%u gathering_done=%d",
              sCandidateCount, sNonLoopbackCandidateCount, sHostCandidateCount,
              sSrflxCandidateCount, sRelayCandidateCount, sNonNumericCandidateCount,
              sGatheringDone ? 1 : 0);

    juice_destroy(agent);
    nifmExit();
    socketExit();

    const bool pass = gatherRc == JUICE_ERR_SUCCESS &&
                      sHostCandidateCount > 0 &&
                      sSrflxCandidateCount > 0 &&
                      sNonLoopbackCandidateCount > 0 &&
                      sNonNumericCandidateCount == 0;
    probe_log("result=%s", pass ? "PASS" : "FAIL");
    if (sLog) {
        fclose(sLog);
        sLog = NULL;
    }
    return pass ? 0 : 4;
}
