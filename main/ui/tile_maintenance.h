#pragma once

#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *name_labels[3];
    lv_obj_t *due_labels[3];
} tile_maintenance_t;

tile_maintenance_t tile_maintenance_create(lv_obj_t *parent);
void tile_maintenance_update(tile_maintenance_t *tile);
