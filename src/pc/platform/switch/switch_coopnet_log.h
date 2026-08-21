#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__SWITCH__) && defined(COOPNET)

#ifdef __cplusplus
extern "C" {
#endif

void switch_coopnet_log_init(void);
void switch_coopnet_log_printf(const char *fmt, ...);
void switch_coopnet_log_checkpoint(const char *component, const char *operation, const char *phase);
void switch_coopnet_log_flush(bool force);
void switch_coopnet_log_tx(uint64_t bytes, int rc);
void switch_coopnet_log_rx(uint64_t bytes);
void switch_coopnet_log_shutdown_summary(void);

#ifdef __cplusplus
}
#endif

#else

static inline void switch_coopnet_log_init(void) {}
static inline void switch_coopnet_log_printf(const char *fmt, ...) { (void)fmt; }
static inline void switch_coopnet_log_checkpoint(const char *component, const char *operation, const char *phase) {
    (void)component;
    (void)operation;
    (void)phase;
}
static inline void switch_coopnet_log_flush(bool force) { (void)force; }
static inline void switch_coopnet_log_tx(uint64_t bytes, int rc) { (void)bytes; (void)rc; }
static inline void switch_coopnet_log_rx(uint64_t bytes) { (void)bytes; }
static inline void switch_coopnet_log_shutdown_summary(void) {}

#endif
