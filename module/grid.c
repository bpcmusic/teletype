#include "font.h"
#include "grid.h"
#include "globals.h"
#include "state.h"
#include "teletype.h"
#include "util.h"

/*
static const u8 font[][] = {
    {
    0b000000,
    0b011110,
    0b000000,
    0b000000,
    0b000000,
    0b000000
    },
    {
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000
    },
    {
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000
    }
};
*/

static u16 size_x = 16, size_y = 8;
static u8 screen[GRID_MAX_DIMENSION][GRID_MAX_DIMENSION];
static u8 layout[GRID_MAX_DIMENSION][GRID_MAX_DIMENSION];

static void grid_screen_refresh_ctrl(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static void grid_screen_refresh_led(scene_state_t *ss, u8 full_grid, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static void grid_screen_refresh_info(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static bool grid_within_area(u8 x, u8 y, grid_common_t *gc);
static void grid_fill_area(u8 x, u8 y, u8 w, u8 h, u8 level);
static void grid_fill_area_scr(u8 x, u8 y, u8 w, u8 h, u8 level, u8 page);

void grid_refresh(scene_state_t *ss) {
    size_x = monome_size_x();
    size_y = monome_size_y();

    grid_fill_area(0, 0, size_x, size_y, 0);

    u16 x, y;
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (GXYC.enabled && SG.group[GXYC.group].enabled) {
            if (GXY.value_x || GXY.value_y) {
                x = GXYC.x + GXY.value_x;
                y = GXYC.y + GXY.value_y;
                grid_fill_area(GXYC.x, y, GXYC.w, 1, GXYC.background);
                grid_fill_area(x, GXYC.y, 1, GXYC.h, GXYC.background);
                grid_fill_area(x, y, 1, 1, 15);
            }
        }
    }

    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled) {
            if (GF.dir) {
                grid_fill_area(GFC.x, GFC.y, GFC.w, GFC.h - GF.value - 1, GFC.background);
                grid_fill_area(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, GF.value + 1, 15);
            } else { 
                grid_fill_area(GFC.x, GFC.y, GF.value + 1, GFC.h, 15);
                grid_fill_area(GFC.x + GF.value + 1, GFC.y, GFC.w - GF.value - 1, GFC.h, GFC.background);
            }
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++)
        if (GBC.enabled && SG.group[GBC.group].enabled)
            grid_fill_area(GBC.x, GBC.y, GBC.w, GBC.h, GB.state ? 15 : GBC.background);
    
    u16 led;
    for (u16 i = 0; i < size_x; i++)
        for (u16 j = 0; j < size_y; j++) {
            led = j * size_x + i;
            if (led >= MONOME_MAX_LED_BYTES) continue;
            
            if (SG.leds[i][j] >= 0)
                monomeLedBuffer[led] = SG.leds[i][j];
            else if (SG.leds[i][j] == LED_DIM)
                monomeLedBuffer[led] >>= 1;
            else if (SG.leds[i][j] == LED_BRI) {
                monomeLedBuffer[led] <<= 1;
                if (monomeLedBuffer[led] > 15) monomeLedBuffer[led] = 15;
                else if (monomeLedBuffer[led] < 1) monomeLedBuffer[led] = 1;
            }
            
            if (monomeLedBuffer[led] < SG.dim)
                monomeLedBuffer[led] = 0;
            else
                monomeLedBuffer[led] -= SG.dim;
        }
    
    if (SG.rotate) {
        u16 total = size_x * size_y;
        if (total > MONOME_MAX_LED_BYTES) total = MONOME_MAX_LED_BYTES;
        u8 temp;
        for (u16 i = 0; i < (total >> 1); i++) {
            temp = monomeLedBuffer[i];
            monomeLedBuffer[i] = monomeLedBuffer[total - i - 1];
            monomeLedBuffer[total - i - 1] = temp;
        }
    }
    
    SG.grid_dirty = 0;
}

void grid_process_key(scene_state_t *ss, u8 _x, u8 _y, u8 z) {
    u8 x = SG.rotate ? monome_size_x() - _x - 1 : _x;
    u8 y = SG.rotate ? monome_size_y() - _y - 1 : _y;
    u8 refresh = 0;
    u8 scripts[SCRIPT_COUNT];
    for (u8 i = 0; i < SCRIPT_COUNT; i++) scripts[i] = 0;
    
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (z && GXYC.enabled && SG.group[GXYC.group].enabled && grid_within_area(x, y, &GXYC)) {
            GXY.value_x = x - GXYC.x;
            GXY.value_y = y - GXYC.y;
            if (GXYC.script != -1) scripts[GXYC.script] = 1;
            SG.latest_group = GXYC.group;
            if (SG.group[GXYC.group].script != -1) scripts[SG.group[GXYC.group].script] = 1;
            refresh = 1;
        }
    }

    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled && grid_within_area(x, y, &GFC)) {
            GF.value = GF.dir ? GFC.h + GFC.y - y - 1 : x - GFC.x;
            if (GFC.script != -1) scripts[GFC.script] = 1;
            SG.latest_fader = i;
            SG.latest_group = GFC.group;
            if (SG.group[GFC.group].script != -1) scripts[SG.group[GFC.group].script] = 1;
            refresh = 1;
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++) {
        if (GBC.enabled && SG.group[GBC.group].enabled && grid_within_area(x, y, &GBC)) {
            if (GB.latch) {
                if (z) {
                    GB.state = !GB.state;
                    if (GBC.script != -1) scripts[GBC.script] = 1;
                }
            } else {
                GB.state = z;
                if (GBC.script != -1) scripts[GBC.script] = 1;
            }
            SG.latest_button = i;
            SG.latest_group = GBC.group;
            if (SG.group[GBC.group].script != -1) scripts[SG.group[GBC.group].script] = 1;
            refresh = 1;
        }
    }
    
    for (u8 i = 0; i < SCRIPT_COUNT; i++)
        if (scripts[i]) run_script(ss, i);

    SG.grid_dirty = SG.scr_dirty = refresh;
}

bool grid_within_area(u8 x, u8 y, grid_common_t *gc) {
    return x >= gc->x && x < (gc->x + gc->w) && y >= gc->y && y < (gc->y + gc->h);
}

void grid_fill_area(u8 x, u8 y, u8 w, u8 h, u8 level) {
    if (level == LED_OFF) return;
    
    u16 index;
    u16 x_end = min(size_x, x + w);
    u16 y_end = min(size_y, y + h);
    
    if (level == LED_DIM) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES) monomeLedBuffer[index] >>= 1;
            }

    } else if (level == LED_BRI) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES) {
                    monomeLedBuffer[index] <<= 1; 
                    if (monomeLedBuffer[index] > 15) monomeLedBuffer[index] = 15;
                }
            }
        
    } else {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES) monomeLedBuffer[index] = level;
            }
    }
}

///////////////////////////////////////// screen functions

void grid_screen_refresh(scene_state_t *ss, screen_grid_mode mode, u8 page, u8 x1, u8 y1, u8 x2, u8 y2) {
    switch (mode) {
        case GRID_MODE_LED:
            grid_screen_refresh_led(ss, 0, page, x1, y1, x2, y2);
            grid_screen_refresh_info(ss, page, x1, y1, x2, y2);
            break;
        //case GRID_MODE_CTRL:
        //    grid_screen_refresh_ctrl(ss, page);
        //    grid_screen_refresh_info(ss, page, x1, y1, x2, y2);
        //    break;
        case GRID_MODE_FULL:
            grid_screen_refresh_led(ss, 1, page, x1, y1, x2, y2);
            break;
        case GRID_MODE_OFF:
        case GRID_MODE_LAST:
            break;
    }
    SG.scr_dirty = 0;
}

void grid_screen_refresh_ctrl(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2) {
    grid_fill_area_scr(0, 0, GRID_MAX_DIMENSION, GRID_MAX_DIMENSION, 0, 0);
    
    /*
    u8 level, last_x, last_y;
    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++) {
        if (!SG.group[GBC.group].enabled) continue;
        level = (GBC.enabled ? (GB.state ? 15 : 8) : 1) << 4;
        last_x = GBC.x + GBC.w - 1;
        last_y = GBC.y + GBC.h - 1;
        if (GBC.w == 1 && GBC.h == 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, level + 1, page);
        } else if (GBC.w == 1 && GBC.h > 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, level + 2, page);
            grid_fill_area_scr(GBC.x, GBC.y + 1, 1, GBC.h - 2, level + 3, page);
            grid_fill_area_scr(GBC.x, last_y, 1, 1, level + 4, page);
        } else if (GBC.w > 1 && GBC.h == 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, level + 5, page);
            grid_fill_area_scr(GBC.x + 1, GBC.y, GBC.w - 2, 1, level + 6, page);
            grid_fill_area_scr(last_x, GBC.y, 1, 1, level + 7, page);
        } else {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, level + 8, page);
            grid_fill_area_scr(GBC.x + 1, GBC.y, GBC.w - 2, 1, level + 9, page);
            grid_fill_area_scr(last_x, GBC.y, 1, 1, level + 10, page);
            grid_fill_area_scr(GBC.x, GBC.y + 1, 1, GBC.h - 2, level + 11, page);
            grid_fill_area_scr(last_x, GBC.y + 1, 1, GBC.h - 2, level + 12, page);
            grid_fill_area_scr(GBC.x, last_y, 1, 1, level + 13, page);
            grid_fill_area_scr(GBC.x + 1, last_y, GBC.w - 2, 1, level + 14, page);
            grid_fill_area_scr(last_x, last_y, 1, 1, level + 15, page);
        }
    }
    
    u8 _line;
    u16 _data;
    u8 type;
    for (u16 x = 0; x < GRID_MAX_DIMENSION; x++)
        for (u16 y = 0; y < GRID_MAX_DIMENSION; y++)
            type = screen[x][y] && ;
        
            for (u16 j = 0; j < 6; j++) {
                _y = y * 6 + j + 1;
                if (page) {
                    if (_y < cell << 3) continue;
                     _y -= cell << 3;
                }
                _line = _y >> 3;
                for (u16 i = 0; i < size; i++) {
                    _data = left + x * cell + i + ((_y & 7) << 7) + 1;
                    if (_line > 7 || _data > 1023) continue;
                    if (screen[x][y] == 0) {
                        if (i == 0 || i == size - 1 || j == 0 || j == size - 1)
                            line[_line].data[_data] = 1;
                    } else
                        line[_line].data[_data] = screen[x][y];
                }
            }

    */
}

void grid_screen_refresh_led(scene_state_t *ss, u8 full_grid, u8 page, u8 x1, u8 y1, u8 x2, u8 y2) {
    grid_fill_area_scr(0, 0, GRID_MAX_DIMENSION, GRID_MAX_DIMENSION, 0, 0);
    
    u16 x, y;
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (GXYC.enabled && SG.group[GXYC.group].enabled) {
            if (GXY.value_x || GXY.value_y) {
                x = GXYC.x + GXY.value_x;
                y = GXYC.y + GXY.value_y;
                grid_fill_area_scr(GXYC.x, y, GXYC.w, 1, GXYC.background, page);
                grid_fill_area_scr(x, GXYC.y, 1, GXYC.h, GXYC.background, page);
                grid_fill_area_scr(x, y, 1, 1, 15, page);
            }
        }
    }

    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled) {
            if (GF.dir) {
                grid_fill_area_scr(GFC.x, GFC.y, GFC.w, GFC.h - GF.value - 1, GFC.background, page);
                grid_fill_area_scr(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, GF.value + 1, 15, page);
            } else { 
                grid_fill_area_scr(GFC.x, GFC.y, GF.value + 1, GFC.h, 15, page);
                grid_fill_area_scr(GFC.x + GF.value + 1, GFC.y, GFC.w - GF.value - 1, GFC.h, GFC.background, page);
            }
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++)
        if (GBC.enabled && SG.group[GBC.group].enabled)
            grid_fill_area_scr(GBC.x, GBC.y, GBC.w, GBC.h, GB.state ? 15 : GBC.background, page);
    
    for (u16 i = 0; i < GRID_MAX_DIMENSION; i++)
        for (u16 j = 0; j < GRID_MAX_DIMENSION; j++) {
            if (SG.leds[i][j] >= 0)
                screen[i][j] = SG.leds[i][j];
            else if (SG.leds[i][j] == LED_DIM)
                screen[i][j] >>= 1;
            else if (SG.leds[i][j] == LED_BRI) {
                screen[i][j] <<= 1;
                if (screen[i][j] > 15) screen[i][j] = 15;
                else if (screen[i][j] < 1) screen[i][j] = 1;
            }
        }
    
    u16 _y, cell, size, left;
    if (full_grid) {
        cell = 8;
        size = 5;
        left = 0;
        for (int i = 0; i < 8; i++) region_fill(&line[i], 0);
    } else {
        cell = 6;
        size = 4;
        left = 10;
        for (int i = 0; i < 6; i++) region_fill(&line[i], 0);
    }
    
    u8 _line;
    u16 _data;
    for (u16 x = 0; x < GRID_MAX_DIMENSION; x++)
        for (u16 y = 0; y < GRID_MAX_DIMENSION; y++)
            for (u16 j = 0; j < size; j++) {
                _y = y * cell + j + 1;
                if (page) {
                    if (_y < cell << 3) continue;
                     _y -= cell << 3;
                }
                _line = _y >> 3;
                for (u16 i = 0; i < size; i++) {
                    _data = left + x * cell + i + ((_y & 7) << 7) + 1;
                    if (_line > 7 || _data > 1023) continue;
                    if (screen[x][y] == 0) {
                        if (i == 0 || i == size - 1 || j == 0 || j == size - 1)
                            line[_line].data[_data] = 1;
                    } else
                        line[_line].data[_data] = screen[x][y];
                }
            }

    u16 area_x, area_y, area_w, area_h;
    if (x1 < x2) {
        area_x = x1 * cell;
        area_w = (x2 + 1 - x1) * cell;
    } else {
        area_x = x2 * cell;
        area_w = (x1 + 1 - x2) * cell;
    }
    if (y1 < y2) {
        area_y = y1 * cell;
        area_h = (y2 + 1 - y1) * cell;
    } else {
        area_y = y2 * cell;
        area_h = (y1 + 1 - y2) * cell;
    }
    if (full_grid) {
        area_w--;
        area_h--;
    }
    
    for (u16 j = 0; j < area_h; j++) {
        _y = area_y + j;
        if (page) {
            if (_y < (cell << 3)) continue;
             _y -= cell << 3 ;
        }
        _line = _y >> 3;
        for (u16 i = 0; i < area_w; i++) {
            if (i == 0 || i == area_w - 1 || j == 0 || j == area_h - 1) {
                _data = left + i + area_x + ((_y & 7) << 7);
                if (_line > 7 || _data > 1023) continue;
                line[_line].data[_data] = 8;
            }
        }
    }
}                

static void grid_screen_refresh_info(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2) {
    char s[32];
    u8 area_x, area_y, area_w, area_h;

    if (x1 < x2) {
        area_x = x1;
        area_w = x2 + 1 - x1;
    } else {
        area_x = x2;
        area_w = x1 + 1 - x2;
    }
    if (y1 < y2) {
        area_y = y1;
        area_h = y2 + 1 - y1;
    } else {
        area_y = y2;
        area_h = y1 + 1 - y2;
    }

    s[1] = 0;
    s[0] = 'G';
    font_string_region_clip_right(&line[0], s, 127, 0, 1, 0);
    s[0] = 'X';
    font_string_region_clip_right(&line[2], s, 127, 0, 1, 0);
    s[0] = 'Y';
    font_string_region_clip_right(&line[3], s, 127, 0, 1, 0);
    s[0] = 'W';
    font_string_region_clip_right(&line[4], s, 128, 0, 1, 0);
    s[0] = 'H';
    font_string_region_clip_right(&line[5], s, 127, 0, 1, 0);

    itoa(ss->grid.current_group, s, 10);
    font_string_region_clip_right(&line[0], s, 117, 0, 8, 0);
    itoa(area_x, s, 10);
    font_string_region_clip_right(&line[2], s, 117, 0, 8, 0);
    itoa(area_y, s, 10);
    font_string_region_clip_right(&line[3], s, 117, 0, 8, 0);
    itoa(area_w, s, 10);
    font_string_region_clip_right(&line[4], s, 117, 0, 8, 0);
    itoa(area_h, s, 10);
    font_string_region_clip_right(&line[5], s, 117, 0, 8, 0);
    
    for (u16 j = 0; j < 9; j += 2) line[j >> 3].data[119 + ((j & 7) << 7)] = 1;
    for (u16 j = 17; j < 48; j += 2) line[j >> 3].data[119 + ((j & 7) << 7)] = 1;

    u8 l;
    l = page == 0 ? 10 : 2;
    for (u16 i = 0; i < 6; i++) line[0].data[i] = l;
    line[0].data[0 + 128] = l;
    line[0].data[5 + 128] = l;
    line[0].data[0 + 256] = l;
    line[0].data[5 + 256] = l;
    
    l = page == 1 ? 10 : 2;
    line[0].data[0 + 384] = l;
    line[0].data[5 + 384] = l;
    line[0].data[0 + 512] = l;
    line[0].data[5 + 512] = l;
    for (u16 i = 0; i < 6; i++) line[0].data[i + 640] = l;

    l = ss->grid.rotate ? 10 : 2;
    line[1].data[0 + 1] = l;
    line[1].data[0 + 2] = l;
    line[1].data[0 + 3] = l;
    line[1].data[128 + 0] = l;
    line[1].data[128 + 4] = l;
    line[1].data[256 + 0] = l;
    line[1].data[256 + 4] = l;
    line[1].data[384 + 3] = l;
    line[1].data[384 + 4] = l;
    line[1].data[384 + 5] = l;
    line[1].data[512 + 4] = l;
}

void grid_fill_area_scr(u8 x, u8 y, u8 w, u8 h, u8 level, u8 page) {
    if (level == LED_OFF) return;

    u16 x_end = min(GRID_MAX_DIMENSION, x + w);
    u16 y_end = min(GRID_MAX_DIMENSION, y + h);
    
    if (level == LED_DIM) {
        for (u16 _x = x; _x < x_end; _x++) {
            for (u16 _y = y; _y < y_end; _y++)
                screen[_x][_y] >>= 1;
        }

    } else if (level == LED_BRI) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                screen[_x][_y] <<= 1; 
                if (screen[_x][_y] > 15) screen[_x][_y] = 15;
            }
        
    } else {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++)
                screen[_x][_y] = level;
    }
}
