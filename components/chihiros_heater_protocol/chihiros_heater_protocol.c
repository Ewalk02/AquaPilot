#include "chihiros_heater_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint8_t SETPOINT_SEQ = 0x05;
static const uint8_t SETPOINT_CMD = 0x2b;
static const uint8_t SETPOINT_CONST_1 = 0x01;
static const uint8_t SETPOINT_CONST_2 = 0x32;

static const float FINE_AT_BLOCK_START = 6.0f;
static const float FINE_COUNTS_PER_F = (float)(0x2B - 0x06) / 0.7f;

static float clampf(float x, float lo, float hi)
{
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

uint8_t chihiros_checksum_xor(const uint8_t *pkt, size_t len_without_checksum)
{
    if (pkt == NULL || len_without_checksum < 2) {
        return 0;
    }

    uint8_t x = 0;
    for (size_t i = 1; i < len_without_checksum; i++) {
        x ^= pkt[i];
    }
    return x;
}

bool chihiros_setpoint_f_allowed(float target_f)
{
    return target_f >= CHIHIROS_SETPOINT_ABS_MIN_F && target_f <= CHIHIROS_SETPOINT_ABS_MAX_F;
}

static void protocol_temp_from_f(float target_f, int *whole_c_out, uint8_t *fine_out)
{
    float target_c = (target_f - 32.0f) * (5.0f / 9.0f);

    int whole_c = (int)floorf(target_c);
    float block_start_f = (float)whole_c * (9.0f / 5.0f) + 32.0f;
    float delta_f = target_f - block_start_f;

    if (delta_f < -0.0001f) {
        whole_c -= 1;
        block_start_f = (float)whole_c * (9.0f / 5.0f) + 32.0f;
        delta_f = target_f - block_start_f;
    }

    int fine = (int)lroundf(FINE_AT_BLOCK_START + delta_f * FINE_COUNTS_PER_F);

    if (fine > 0x63) {
        whole_c += 1;
        block_start_f = (float)whole_c * (9.0f / 5.0f) + 32.0f;
        delta_f = target_f - block_start_f;
        fine = (int)lroundf(FINE_AT_BLOCK_START + delta_f * FINE_COUNTS_PER_F);
    }

    if (fine < 0) {
        fine = 0;
    }
    if (fine > 0xFF) {
        fine = 0xFF;
    }

    *whole_c_out = whole_c;
    *fine_out = (uint8_t)fine;
}

bool chihiros_make_setpoint_packet_f(float target_f, float min_f, float max_f, uint8_t out[11])
{
    if (out == NULL || !chihiros_setpoint_f_allowed(target_f)) {
        return false;
    }

    if (!(min_f < max_f)) {
        min_f = CHIHIROS_SETPOINT_MIN_F;
        max_f = CHIHIROS_SETPOINT_MAX_F;
    }

    float clamped_f = clampf(target_f, min_f, max_f);

    int whole_c = 0;
    uint8_t fine = 0;
    protocol_temp_from_f(clamped_f, &whole_c, &fine);

    out[0] = 0x5a;
    out[1] = 0x01;
    out[2] = 0x09;
    out[3] = 0x00;
    out[4] = SETPOINT_SEQ;
    out[5] = SETPOINT_CMD;
    out[6] = SETPOINT_CONST_1;
    out[7] = (uint8_t)whole_c;
    out[8] = fine;
    out[9] = SETPOINT_CONST_2;
    out[10] = chihiros_checksum_xor(out, 10);

    return true;
}

static bool decode_temp_sane(float temp_c)
{
    return temp_c >= 5.0f && temp_c <= 45.0f;
}

static int bcd_byte_to_int(uint8_t b)
{
    return ((b >> 4) * 10) + (b & 0x0f);
}

bool chihiros_decode_status_packet(const uint8_t *pkt, size_t len, chihiros_pkt_status_t *out)
{
    if (pkt == NULL || out == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Main live status: 5b 03 0e ... (13 bytes minimum). */
    if (len < 13 || pkt[0] != 0x5b || pkt[1] != 0x03 || pkt[2] != 0x0e) {
        return false;
    }

    out->status_a = pkt[3];
    out->status_b = pkt[4];
    out->whole_c_byte = pkt[5];

    uint16_t watts = (uint16_t)((pkt[6] << 8) | pkt[7]);
    out->watts = watts;
    out->heating = watts > 0;

    uint16_t raw_temp = (uint16_t)((pkt[10] << 8) | pkt[11]);
    if (raw_temp == 0xffff || raw_temp == 0xfffe || raw_temp > 600) {
        return false;
    }

    float temp_c = (float)raw_temp / 10.0f;
    if (!decode_temp_sane(temp_c)) {
        return false;
    }

    out->current_temp_c = temp_c;
    out->current_temp_f = temp_c * (9.0f / 5.0f) + 32.0f;
    out->valid = true;
    (void)bcd_byte_to_int;
    return true;
}

static bool packet_equals(const uint8_t *got, const uint8_t *expected, size_t len)
{
    return memcmp(got, expected, len) == 0;
}

bool chihiros_protocol_run_selftests(void)
{
    static const struct {
        float target_f;
        uint8_t expected[11];
    } setpoint_vectors[] = {
        {77.0f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x19, 0x06, 0x32, 0x0a}},
        {77.5f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x19, 0x20, 0x32, 0x2c}},
        {77.8f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x19, 0x30, 0x32, 0x3c}},
        {78.0f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x19, 0x3b, 0x32, 0x37}},
        {78.5f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x19, 0x55, 0x32, 0x59}},
        {78.8f, {0x5a, 0x01, 0x09, 0x00, 0x05, 0x2b, 0x01, 0x1a, 0x06, 0x32, 0x09}},
    };

    for (size_t i = 0; i < sizeof(setpoint_vectors) / sizeof(setpoint_vectors[0]); i++) {
        uint8_t pkt[11] = {0};
        if (!chihiros_make_setpoint_packet_f(setpoint_vectors[i].target_f, CHIHIROS_SETPOINT_MIN_F,
                                             CHIHIROS_SETPOINT_MAX_F, pkt)) {
            return false;
        }
        if (!packet_equals(pkt, setpoint_vectors[i].expected, sizeof(pkt))) {
            return false;
        }
    }

    uint8_t reject_pkt[11] = {0};
    if (chihiros_make_setpoint_packet_f(40.0f, CHIHIROS_SETPOINT_MIN_F, CHIHIROS_SETPOINT_MAX_F, reject_pkt)) {
        return false;
    }

    static const uint8_t status_idle[] = {0x5b, 0x03, 0x0e, 0x00, 0x25, 0x25, 0x00, 0x00, 0x0b, 0xff, 0x00, 0xfe, 0x00};
    static const uint8_t status_heat[] = {0x5b, 0x03, 0x0e, 0x00, 0x25, 0x25, 0x02, 0x72, 0x0b, 0xff, 0x00, 0xfe, 0x00};
    static const uint8_t status_bad[] = {0x5b, 0x03, 0x0e, 0x00, 0x01, 0x0a, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0x22};

    chihiros_pkt_status_t st = {0};
    if (!chihiros_decode_status_packet(status_idle, sizeof(status_idle), &st)) {
        return false;
    }
    if (st.watts != 0 || st.heating || st.current_temp_c < 25.3f || st.current_temp_c > 25.5f) {
        return false;
    }

    memset(&st, 0, sizeof(st));
    if (!chihiros_decode_status_packet(status_heat, sizeof(status_heat), &st)) {
        return false;
    }
    if (st.watts != 626 || !st.heating) {
        return false;
    }

    if (chihiros_decode_status_packet(status_bad, sizeof(status_bad), &st)) {
        return false;
    }

    return true;
}
