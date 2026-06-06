#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLUVAL_MODE_UNKNOWN = 0,
    FLUVAL_MODE_MANUAL,
    FLUVAL_MODE_AUTO,
} fluval_mode_t;

typedef struct {
    bool valid;
    fluval_mode_t mode;
    uint8_t pink;
    uint8_t blue;
    uint8_t cold_white;
    uint8_t white;
    uint8_t warm_white;
    uint8_t avg_output;
} fluval_pkt_status_t;

#define FLUVAL_STATUS_STALE_MS 180000

size_t fluval_build_status_query(uint8_t *out, size_t out_len);
size_t fluval_build_set_manual(uint8_t *out, size_t out_len);
size_t fluval_build_set_auto(uint8_t *out, size_t out_len);
size_t fluval_build_set_channels(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white,
                                 uint8_t *out, size_t out_len);
size_t fluval_build_set_all(uint8_t percent, uint8_t *out, size_t out_len);

bool fluval_decode_status_packet(const uint8_t *pkt, size_t len, fluval_pkt_status_t *out);
bool fluval_decode_ack_packet(const uint8_t *pkt, size_t len);

uint8_t fluval_avg_output(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white);

bool fluval_protocol_run_selftests(void);

#ifdef __cplusplus
}
#endif
