#include "fluval_light_protocol.h"

#include <string.h>

static uint8_t clamp_pct(uint8_t v)
{
    return v > 100 ? 100 : v;
}

uint8_t fluval_avg_output(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white)
{
    const unsigned sum = (unsigned)pink + (unsigned)blue + (unsigned)cold_white + (unsigned)white + (unsigned)warm_white;
    return (uint8_t)((sum + 2U) / 5U);
}

size_t fluval_build_status_query(uint8_t *out, size_t out_len)
{
    if (out == NULL || out_len < 2) {
        return 0;
    }
    out[0] = 0xd0;
    out[1] = 0xff;
    return 2;
}

size_t fluval_build_set_manual(uint8_t *out, size_t out_len)
{
    static const uint8_t cmd[] = {0xd1, 0xa1, 0x01, 0x00};
    if (out == NULL || out_len < sizeof(cmd)) {
        return 0;
    }
    memcpy(out, cmd, sizeof(cmd));
    return sizeof(cmd);
}

size_t fluval_build_set_auto(uint8_t *out, size_t out_len)
{
    static const uint8_t cmd[] = {0xd1, 0xa1, 0x01, 0x01};
    if (out == NULL || out_len < sizeof(cmd)) {
        return 0;
    }
    memcpy(out, cmd, sizeof(cmd));
    return sizeof(cmd);
}

size_t fluval_build_set_channels(uint8_t pink, uint8_t blue, uint8_t cold_white, uint8_t white, uint8_t warm_white,
                                 uint8_t *out, size_t out_len)
{
    if (out == NULL || out_len < 16) {
        return 0;
    }

    pink = clamp_pct(pink);
    blue = clamp_pct(blue);
    cold_white = clamp_pct(cold_white);
    white = clamp_pct(white);
    warm_white = clamp_pct(warm_white);

    size_t i = 0;
    out[i++] = 0xd1;
    out[i++] = 0xa6;
    out[i++] = 0x03;
    out[i++] = 0x18;
    out[i++] = pink;
    out[i++] = 0x04;
    out[i++] = 0x18;
    out[i++] = blue;
    out[i++] = 0x05;
    out[i++] = 0x18;
    out[i++] = cold_white;
    out[i++] = 0x06;
    out[i++] = 0x18;
    out[i++] = white;
    out[i++] = 0x07;
    out[i++] = 0x18;
    out[i++] = warm_white;
    out[i++] = 0x0e;
    out[i++] = 0x00;
    return i;
}

size_t fluval_build_set_all(uint8_t percent, uint8_t *out, size_t out_len)
{
    percent = clamp_pct(percent);
    return fluval_build_set_channels(percent, percent, percent, percent, percent, out, out_len);
}

static bool find_marker(const uint8_t *pkt, size_t len, size_t *out_start)
{
    if (pkt == NULL || len < 2) {
        return false;
    }

    for (size_t i = 0; i + 1 < len; i++) {
        if (pkt[i] == 0x02 && pkt[i + 1] == 0xf5) {
            if (out_start != NULL) {
                *out_start = i + 2;
            }
            return true;
        }
    }
    return false;
}

static uint8_t parse_channel(const uint8_t *pkt, size_t len, size_t start, uint8_t field_id)
{
    for (size_t i = start; i + 1 < len; i++) {
        if (pkt[i] != field_id) {
            continue;
        }
        if (pkt[i + 1] == 0x18 && i + 2 < len) {
            return clamp_pct(pkt[i + 2]);
        }
        if (pkt[i + 1] == 0x00) {
            return 0;
        }
        break;
    }
    return 0;
}

bool fluval_decode_status_packet(const uint8_t *pkt, size_t len, fluval_pkt_status_t *out)
{
    if (pkt == NULL || out == NULL || len < 8) {
        return false;
    }
    if (pkt[0] != 0xd2 || pkt[1] != 0xb0) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    if (pkt[5] == 0x00) {
        out->mode = FLUVAL_MODE_MANUAL;
    } else if (pkt[5] == 0x01) {
        out->mode = FLUVAL_MODE_AUTO;
    } else {
        out->mode = FLUVAL_MODE_UNKNOWN;
    }

    size_t ch_start = 0;
    if (!find_marker(pkt, len, &ch_start)) {
        return false;
    }

    out->pink = parse_channel(pkt, len, ch_start, 0x03);
    out->blue = parse_channel(pkt, len, ch_start, 0x04);
    out->cold_white = parse_channel(pkt, len, ch_start, 0x05);
    out->white = parse_channel(pkt, len, ch_start, 0x06);
    out->warm_white = parse_channel(pkt, len, ch_start, 0x07);
    out->avg_output = fluval_avg_output(out->pink, out->blue, out->cold_white, out->white, out->warm_white);
    out->valid = true;
    return true;
}

bool fluval_decode_ack_packet(const uint8_t *pkt, size_t len)
{
    if (pkt == NULL || len < 3) {
        return false;
    }
    return pkt[0] == 0xd2 && (pkt[1] == 0xa1 || pkt[1] == 0xa6);
}

static bool selftest_status_manual(void)
{
    static const uint8_t pkt[] = {
        0xd2, 0xb0, 0x00, 0x0e, 0x01, 0x00, 0x02, 0xf5, 0x03, 0x18, 0x28, 0x04, 0x18, 0x14, 0x05, 0x18, 0x3c,
        0x06, 0x18, 0x46, 0x07, 0x18, 0x32,
    };

    fluval_pkt_status_t out = {0};
    if (!fluval_decode_status_packet(pkt, sizeof(pkt), &out) || !out.valid) {
        return false;
    }
    return out.mode == FLUVAL_MODE_MANUAL && out.pink == 40 && out.blue == 20 && out.cold_white == 60 &&
           out.white == 70 && out.warm_white == 50 && out.avg_output == 48;
}

static bool selftest_status_auto(void)
{
    static const uint8_t pkt[] = {
        0xd2, 0xb0, 0x00, 0x0e, 0x01, 0x01, 0x02, 0xf5, 0x03, 0x18, 0x28, 0x04, 0x18, 0x14, 0x05, 0x18, 0x3c,
        0x06, 0x18, 0x46, 0x07, 0x18, 0x32,
    };

    fluval_pkt_status_t out = {0};
    if (!fluval_decode_status_packet(pkt, sizeof(pkt), &out) || !out.valid) {
        return false;
    }
    return out.mode == FLUVAL_MODE_AUTO && out.avg_output == 48;
}

static bool selftest_set_all_cmd(void)
{
    uint8_t cmd[20] = {0};
    if (fluval_build_set_all(25, cmd, sizeof(cmd)) != 19) {
        return false;
    }
    static const uint8_t expected[] = {0xd1, 0xa6, 0x03, 0x18, 0x19, 0x04, 0x18, 0x19, 0x05, 0x18,
                                       0x19, 0x06, 0x18, 0x19, 0x07, 0x18, 0x19, 0x0e, 0x00};
    return memcmp(cmd, expected, sizeof(expected)) == 0;
}

bool fluval_protocol_run_selftests(void)
{
    return selftest_status_manual() && selftest_status_auto() && selftest_set_all_cmd();
}
