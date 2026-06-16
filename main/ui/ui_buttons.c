#include "ui_buttons.h"

#include "ui_buttons.h"

void ui_style_flat_button(lv_obj_t *btn, uint32_t bg_hex, uint32_t pressed_bg_hex)
{
    if (btn == NULL) {
        return;
    }

    lv_obj_remove_style_all(btn);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(pressed_bg_hex), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_hor(btn, 20, 0);
    lv_obj_set_style_pad_ver(btn, 8, 0);
    lv_obj_set_style_anim_duration(btn, 0, 0);
    lv_obj_set_style_anim_duration(btn, 0, LV_STATE_PRESSED);
}

void ui_button_clear_pressed(lv_obj_t *btn)
{
    if (btn != NULL) {
        lv_obj_remove_state(btn, LV_STATE_PRESSED);
    }
}
