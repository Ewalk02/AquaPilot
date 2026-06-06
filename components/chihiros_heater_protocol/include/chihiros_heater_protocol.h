#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Decoded fields from a live status notification (5b 03 0e ...). */
typedef struct {
    bool valid;
    uint8_t status_a; /**< Unknown — do not use for heater state. */
    uint8_t status_b; /**< Unknown — do not use for heater state. */
    uint8_t whole_c_byte; /**< BCD-ish whole °C block (informational). */

    float current_temp_c;
    float current_temp_f;

    uint16_t watts;
    bool heating; /**< Verified: watts > 0 */
} chihiros_pkt_status_t;

/** Software clamp range (aquarium-safe defaults). */
#define CHIHIROS_SETPOINT_MIN_F 60.0f
#define CHIHIROS_SETPOINT_MAX_F 86.0f

/** Hard reject outside this range. */
#define CHIHIROS_SETPOINT_ABS_MIN_F 50.0f
#define CHIHIROS_SETPOINT_ABS_MAX_F 95.0f

/** Status considered stale after this many ms without a valid packet. */
#define CHIHIROS_STATUS_STALE_MS 10000

uint8_t chihiros_checksum_xor(const uint8_t *pkt, size_t len_without_checksum);

bool chihiros_setpoint_f_allowed(float target_f);

bool chihiros_make_setpoint_packet_f(float target_f, float min_f, float max_f, uint8_t out[11]);

bool chihiros_decode_status_packet(const uint8_t *pkt, size_t len, chihiros_pkt_status_t *out);

bool chihiros_protocol_run_selftests(void);

#ifdef __cplusplus
}
#endif
