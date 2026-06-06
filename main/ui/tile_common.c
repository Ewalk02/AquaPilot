#include "tile_common.h"

#define TILE_BG_COLOR       0x161B22
#define TILE_BORDER_COLOR   0x30363D

void tile_apply_panel_style(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(TILE_BG_COLOR), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(TILE_BORDER_COLOR), 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
}

void tile_show_alarm_message(lv_obj_t *label, const char *text, uint32_t color_hex)
{
    if (label == NULL || text == NULL || text[0] == '\0') {
        if (label != NULL) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color_hex), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
}

void tile_restore_value_label(lv_obj_t *label, uint32_t color_hex)
{
    if (label == NULL) {
        return;
    }

    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_AUTO, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color_hex), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
}

lv_obj_t *tile_empty_create(lv_obj_t *parent)
{
    lv_obj_t *cell = lv_obj_create(parent);
    tile_apply_panel_style(cell);
    lv_obj_set_size(cell, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cell, LV_OPA_30, 0);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    return cell;
}
