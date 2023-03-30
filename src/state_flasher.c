#pragma bank 255

#include <gbdk/platform.h>
#include <gbdk/metasprites.h>
#include <stdint.h>
#include <stdio.h>

#include "compat.h"
#include "musicmanager.h"
#include "joy.h"
#include "screen.h"
#include "states.h"
#include "bankdata.h"
#include "fade_manager.h"
#include "gbcamera.h"
#include "load_save.h"
#include "vector.h"

#include "state_flasher.h"

#include "misc_assets.h"

// menus
#include "menus.h"
#include "menu_codes.h"
#include "menu_main.h"
#include "menu_yesno.h"

// audio assets
#include "sound_ok.h"
#include "sound_error.h"
#include "sound_transmit.h"
#include "sound_menu_alter.h"

// assets
#include "flash.h"
#include "flasher_folder_full.h"
#include "flasher_folder_empty.h"

BANKREF(state_flasher)

static enum brows_mode_e browse_mode = browse_mode_folders;
static uint8_t thumbnails_num_pages = 0, thumbnails_page_no = 0, current_slot = 0, cursor_anim = 0, cx = 0, cy = 0;

const metasprite_t flasher_cursor0[] = {
    METASPR_ITEM(16,  8, 0, 0), METASPR_ITEM(0,  24, 1, 0),
    METASPR_ITEM(24,  0, 3, 0), METASPR_ITEM(0, -24, 2, 0),
    METASPR_TERM
};
const metasprite_t flasher_cursor1[] = {
    METASPR_ITEM(17,  9, 0, 0), METASPR_ITEM(0,  22, 1, 0),
    METASPR_ITEM(22,  0, 3, 0), METASPR_ITEM(0, -22, 2, 0),
    METASPR_TERM
};
const metasprite_t flasher_cursor2[] = {
    METASPR_ITEM(18, 10, 0, 0), METASPR_ITEM(0,  20, 1, 0),
    METASPR_ITEM(20,  0, 3, 0), METASPR_ITEM(0, -20, 2, 0),
    METASPR_TERM
};
const metasprite_t * const flasher_cursor[] = {flasher_cursor0, flasher_cursor1, flasher_cursor2, flasher_cursor1};

const metasprite_t flasher[] = {
    METASPR_ITEM(16,  8,  0,  0), METASPR_ITEM(0,  8,  1,  0), METASPR_ITEM(0,  8,  2,  0), METASPR_ITEM(0,  8,  3,  0),
    METASPR_ITEM( 8,-24,  4,  0), METASPR_ITEM(0,  8,  5,  0), METASPR_ITEM(0,  8,  6,  0), METASPR_ITEM(0,  8,  7,  0),
    METASPR_ITEM( 8,-24,  8,  0), METASPR_ITEM(0,  8,  9,  0), METASPR_ITEM(0,  8, 10,  0), METASPR_ITEM(0,  8, 11,  0),
    METASPR_ITEM( 8,-24, 12,  0), METASPR_ITEM(0,  8, 13,  0), METASPR_ITEM(0,  8, 14,  0), METASPR_ITEM(0,  8, 15,  0),
    METASPR_TERM
};


static cam_game_data_t AT(0x4000 + (0xB1B2 - 0xA000)) game_data;

static const item_coord_t folder_coords[] = {{2, 1}, {6, 1}, {10, 1}, {14, 1}, {4, 5}, {8, 5}, {12, 5}};

static const item_coord_t thumbnail_coords[MAX_FLASHER_THUMBNAILS] = {
    {0,   9}, {4,   9}, {8,   9}, {12,   9}, {16,  9},
    {0,  13}, {4,  13}, {8,  13}, {12,  13}, {16, 13}
};

static const uint8_t * const thumbnail_addr[] = {0x6E00, 0x7E00, 0x4E00, 0x5E00};

bool flash_slots[MAX_FLASH_SLOTS];
VECTOR_DECLARE(flash_image_slots, uint8_t, CAMERA_MAX_IMAGE_SLOTS);

static const uint8_t MAGIC_SAVE_VALUE[] = {'M', 'a', 'g', 'i', 'c'};

extern uint8_t erase_flash() OLDCALL;                   // erases FLASH sector: 64K or 4 banks
extern uint8_t save_sram_banks(uint8_t count) OLDCALL;  // copies up to count SRAM banks to FLASH

static uint8_t flash_save_gallery_to_slot(uint8_t slot) {
    // erase the sector and save first 8 SRAM banks
    save_sram_bank_offset = FIRST_HALF_OFS;
    save_rom_bank = slot_to_sector(slot, 0);
    if (!erase_flash()) return FALSE;
    if (!save_sram_banks(FIRST_HALF_LEN)) return FALSE;
    // erase the next sector and save the next 8 sram banks
    save_sram_bank_offset = SECOND_HALF_OFS;
    save_rom_bank = slot_to_sector(slot, 1);
    if (!erase_flash()) return FALSE;
    return save_sram_banks(SECOND_HALF_LEN);            // update offset
}

static uint8_t flash_erase_slot(uint8_t slot) {
    save_rom_bank = slot_to_sector(slot, 0);
    if (!erase_flash()) return FALSE;
    save_rom_bank = slot_to_sector(slot, 1);
    return erase_flash();
}

static void screen_load_picture(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t * map, const uint8_t * tiles, uint8_t bank) {
    static uint8_t **addr, i, j;
    static const uint8_t *data;
    data = map;
    addr = (uint8_t **)(screen_tile_addresses + y);
    for (i = 0; i != h; i++, addr++) {
        for (j = 0; j != w; j++, data++) {
            uint8_t tile = read_banked_ubyte(data, bank);
            set_banked_data(*addr + ((x + j) << 4), tiles + (tile << 4), 16, bank);
        }
    }
}

typedef enum {
    idFlasherSave = 0,
    idFlasherLoad,
    idFlasherErase,
    idFlasherPrintSlot,
    idFlasherTransferSlot,
} settings_menu_e;

uint8_t onHelpFlasherMenu(const struct menu_t * menu, const struct menu_item_t * selection);
uint8_t onTranslateSubResultFlasherMenu(const struct menu_t * menu, const struct menu_item_t * self, uint8_t value);
uint8_t onFlasherMenuItemProps(const struct menu_t * menu, const struct menu_item_t * self);
const menu_item_t FlasherMenuItems[] = {
    {
        .sub = &YesNoMenu, .sub_params = "Save camera roll?",
        .ofs_x = 1, .ofs_y = 1, .width = 10,
        .id = idFlasherSave,
        .caption = " Save camera roll",
        .helpcontext = " Save camera roll to slot",
        .onPaint = NULL,
        .onGetProps = onFlasherMenuItemProps,
        .result = ACTION_FLASH_SAVE
    }, {
        .sub = &YesNoMenu, .sub_params = "Load camera roll?",
        .ofs_x = 1, .ofs_y = 2, .width = 10,
        .id = idFlasherLoad,
        .caption = " Load camera roll",
        .helpcontext = " Load images to camera roll",
        .onPaint = NULL,
        .onGetProps = onFlasherMenuItemProps,
        .result = ACTION_FLASH_LOAD
    }, {
        .sub = &YesNoMenu, .sub_params = "Erase slot?",
        .ofs_x = 1, .ofs_y = 3, .width = 10,
        .id = idFlasherErase,
        .caption = " Erase slot",
        .helpcontext = " Erase all images in slot",
        .onPaint = NULL,
        .onGetProps = onFlasherMenuItemProps,
        .result = ACTION_FLASH_ERASE
    }, {
        .sub = NULL, .sub_params = NULL,
        .ofs_x = 1, .ofs_y = 4, .width = 10,
        .id = idFlasherPrintSlot,
        .caption = " Print slot",
        .helpcontext = " Print all images from slot",
        .onPaint = NULL,
        .onGetProps = onFlasherMenuItemProps,
        .result = ACTION_PRINT_SLOT
    }, {
        .sub = NULL, .sub_params = NULL,
        .ofs_x = 1, .ofs_y = 5, .width = 10,
        .id = idFlasherTransferSlot,
        .caption = " Transfer slot",
        .helpcontext = " Transfer all images from slot",
        .onPaint = NULL,
        .onGetProps = onFlasherMenuItemProps,
        .result = ACTION_TRANSFER_SLOT
    }
};
const menu_t FlasherMenu = {
    .x = 1, .y = 4, .width = 12, .height = 7,
    .cancel_mask = J_B, .cancel_result = ACTION_NONE,
    .items = FlasherMenuItems, .last_item = LAST_ITEM(FlasherMenuItems),
    .onShow = NULL, .onHelpContext = onHelpFlasherMenu,
    .onTranslateKey = NULL, .onTranslateSubResult = onTranslateSubResultFlasherMenu
};

uint8_t onTranslateSubResultFlasherMenu(const struct menu_t * menu, const struct menu_item_t * self, uint8_t value) {
    menu;
    if (self->sub == &YesNoMenu) {
        return (value == MENU_RESULT_YES) ? self->result : ACTION_NONE;
    }
    return value;
}
uint8_t onHelpFlasherMenu(const struct menu_t * menu, const struct menu_item_t * selection) {
    menu;
    menu_text_out(0, 17, HELP_CONTEXT_WIDTH, HELP_CONTEXT_COLOR, selection->helpcontext);
    return 0;
}
uint8_t onFlasherMenuItemProps(const struct menu_t * menu, const struct menu_item_t * self) {
    menu;
    switch ((settings_menu_e)self->id) {
        case idFlasherSave:
            return (!flash_slots[current_slot]) ? ITEM_DEFAULT : ITEM_DISABLED;
        case idFlasherErase:
            return (flash_slots[current_slot]) ? ITEM_DEFAULT : ITEM_DISABLED;
        case idFlasherLoad:
        case idFlasherPrintSlot:
        case idFlasherTransferSlot:
            return ITEM_DISABLED;
        default:
            return ITEM_DEFAULT;
    }
}


void flasher_read_slots(void) {
    for (uint8_t i = 0; i != MAX_FLASH_SLOTS; i++) {
        banked_memcpy(text_buffer, game_data.magic, sizeof(game_data.magic), slot_to_sector(i, 0));
        flash_slots[i] = (memcmp(text_buffer, MAGIC_SAVE_VALUE, sizeof(game_data.magic)) == 0);
    }
}

void flasher_refresh_folders(void) {
    vsync();
    screen_clear_rect(FLASHER_FOLDER_DISPLAY_X1, FLASHER_FOLDER_DISPLAY_Y1, FLASHER_FOLDER_DISPLAY_ROW1, 4 * 2, WHITE_ON_BLACK);
    for (uint8_t i = 0; i != MAX_FLASH_SLOTS; i++) {
        if (flash_slots[i]) {
            screen_load_picture(folder_coords[i].x, folder_coords[i].y, 4, 4, flasher_folder_full_map, flasher_folder_full_tiles, BANK(flasher_folder_full));
        } else {
            screen_load_picture(folder_coords[i].x, folder_coords[i].y, 4, 4, flasher_folder_empty_map, flasher_folder_empty_tiles, BANK(flasher_folder_empty));
        }
    }
    screen_restore_rect(FLASHER_FOLDER_DISPLAY_X1, FLASHER_FOLDER_DISPLAY_Y1, FLASHER_FOLDER_DISPLAY_ROW1, 4);
    screen_restore_rect(FLASHER_FOLDER_DISPLAY_X2, FLASHER_FOLDER_DISPLAY_Y2, FLASHER_FOLDER_DISPLAY_ROW2, 4);
}

void flasher_toss_images() {
    banked_memcpy(text_buffer, game_data.imageslots, CAMERA_MAX_IMAGE_SLOTS, slot_to_sector(current_slot, 0));
    memset(flash_image_slots, CAMERA_IMAGE_DELETED, sizeof(flash_image_slots));
    uint8_t elem;
    for (uint8_t i = 0; i != CAMERA_MAX_IMAGE_SLOTS; i++) {
        uint8_t order = text_buffer[i];
        if (order < CAMERA_MAX_IMAGE_SLOTS) {
            elem = VECTOR_GET(flash_image_slots, order);
            if (elem == CAMERA_IMAGE_DELETED) VECTOR_SET_DIRECT(flash_image_slots, order, i);
        }
    }
    uint8_t j = 0;
    for (uint8_t i = 0; i != CAMERA_MAX_IMAGE_SLOTS; i++) {
        elem = VECTOR_GET(flash_image_slots, i);
        if (elem < CAMERA_MAX_IMAGE_SLOTS) VECTOR_SET_DIRECT(flash_image_slots, j++, elem);
    }
    VECTOR_LEN(flash_image_slots) = j;
}

uint8_t flasher_thumbnails_diaplay(uint8_t start) {
    vsync();
    uint8_t slot_bank = slot_to_sector(current_slot, 0);
    screen_clear_rect(FLASHER_THUMBS_DISPLAY_X, FLASHER_THUMBS_DISPLAY_Y, FLASHER_THUMBS_DISPLAY_WIDTH, FLASHER_THUMBS_DISPLAY_HEIGHT, WHITE_ON_BLACK);
    for (uint8_t i = start, j = 0; (i < VECTOR_LEN(flash_image_slots)) && (j != MAX_FLASHER_THUMBNAILS); i++, j++) {
        uint8_t slot = VECTOR_GET(flash_image_slots, i);
        screen_load_thumbnail_banked(thumbnail_coords[j].x, thumbnail_coords[j].y,
                                     (uint8_t *)thumbnail_addr[slot & 0x03],
                                     0xFF,
                                     slot_bank + (((slot >> 1) + 1) >> 1));
        screen_restore_rect(thumbnail_coords[j].x, thumbnail_coords[j].y, CAMERA_THUMB_TILE_WIDTH, CAMERA_THUMB_TILE_HEIGHT);
    }
    return TRUE;
}

void flasher_refresh_thumbnails(void) {
    if (flash_slots[current_slot]) {
        flasher_toss_images();
        flasher_thumbnails_diaplay(thumbnails_page_no * MAX_FLASHER_THUMBNAILS);
    } else {
        vsync();
        screen_clear_rect(FLASHER_THUMBS_DISPLAY_X, FLASHER_THUMBS_DISPLAY_Y, FLASHER_THUMBS_DISPLAY_WIDTH, FLASHER_THUMBS_DISPLAY_HEIGHT, WHITE_ON_BLACK);
        VECTOR_CLEAR(flash_image_slots);
    }
    sprintf(text_buffer, " Images: %hd", VECTOR_LEN(flash_image_slots));
    menu_text_out(0, 17, 20, WHITE_ON_BLACK, text_buffer);
    return;
}

static void refresh_screen(void) {
    vsync();
    vwf_set_colors(DMG_WHITE, DMG_BLACK);
    screen_clear_rect(DEVICE_SCREEN_X_OFFSET, DEVICE_SCREEN_Y_OFFSET, DEVICE_SCREEN_WIDTH, DEVICE_SCREEN_HEIGHT, WHITE_ON_BLACK);
    menu_text_out(0,  0, 20, WHITE_ON_BLACK, " Flash directory");

    // folders
    flasher_refresh_folders();

    // thumbnails
    flasher_refresh_thumbnails();
}

void flasher_show_icon(void) {
    fade_out_modal();
    hide_sprites_range(0, MAX_HARDWARE_SPRITES);
    screen_clear_rect(DEVICE_SCREEN_X_OFFSET, DEVICE_SCREEN_Y_OFFSET, DEVICE_SCREEN_WIDTH, DEVICE_SCREEN_HEIGHT, WHITE_ON_BLACK);
    banked_vmemcpy(_VRAM, flash_tiles, sizeof(flash_tiles), BANK(flash));
    hide_sprites_range(move_metasprite(flasher, 0, 0, (DEVICE_SCREEN_PX_WIDTH - 32) >> 1, (DEVICE_SCREEN_PX_HEIGHT - 32) >> 1), MAX_HARDWARE_SPRITES);
    fade_in_modal();
}
void flasher_hide_icon(void) {
    fade_out_modal();
    hide_sprites_range(0, MAX_HARDWARE_SPRITES);
    refresh_screen();
    fade_in_modal();
}


uint8_t INIT_state_flasher(void) BANKED {
    return 0;
}

uint8_t ENTER_state_flasher(void) BANKED {
    browse_mode = browse_mode_folders;
    flasher_read_slots();
    refresh_screen();
    fade_in_modal();
    JOYPAD_RESET();
    return 0;
}

inline void update_mode_folders_cursor(void) {
    hide_sprites_range(move_metasprite(flasher_cursor[cursor_anim], CORNER_UL, 0, folder_coords[current_slot].x << 3, folder_coords[current_slot].y << 3), MAX_HARDWARE_SPRITES);
}

void update_mode_folders(void) {
    if (KEY_PRESSED(J_UP)) {
        PLAY_SFX(sound_error);
    } else if (KEY_PRESSED(J_DOWN)) {
        PLAY_SFX(sound_error);
    } else if (KEY_PRESSED(J_LEFT)) {
        if (current_slot-- == 0) current_slot = LENGTH(folder_coords) - 1;
        update_mode_folders_cursor();
        PLAY_SFX(sound_menu_alter);
        thumbnails_page_no = cx = cy = 0;
        flasher_refresh_thumbnails();
    } else if (KEY_PRESSED(J_RIGHT)) {
        if (++current_slot == LENGTH(folder_coords)) current_slot = 0;
        update_mode_folders_cursor();
        PLAY_SFX(sound_menu_alter);
        thumbnails_page_no = cx = cy = 0;
        flasher_refresh_thumbnails();
    } else if (KEY_PRESSED(J_A)) {
        if (flash_slots[current_slot]) {
            browse_mode = browse_mode_thumbnails;
            thumbnails_num_pages = VECTOR_LEN(flash_image_slots) / MAX_FLASHER_THUMBNAILS;
            if (VECTOR_LEN(flash_image_slots) % MAX_FLASHER_THUMBNAILS) thumbnails_num_pages++;
            PLAY_SFX(sound_menu_alter);
        } else PLAY_SFX(sound_error);
    } else if (KEY_PRESSED(J_B)) {
        PLAY_SFX(sound_error);
    }
    update_mode_folders_cursor();
}

void update_mode_thumbnails(void) {
    if (KEY_PRESSED(J_UP)) {
        if (cy) --cy, PLAY_SFX(sound_menu_alter);
    } else if (KEY_PRESSED(J_DOWN)) {
        if (cy < (FLASHER_THUMBS_COUNT_Y - 1)) ++cy, PLAY_SFX(sound_menu_alter);
    } else if (KEY_PRESSED(J_LEFT)) {
        if (!cx) {
            uint8_t old_page = thumbnails_page_no;
            if (thumbnails_page_no) --thumbnails_page_no; else thumbnails_page_no = thumbnails_num_pages - 1;
            cx = FLASHER_THUMBS_COUNT_X - 1;
            if (old_page != thumbnails_page_no) {
                hide_sprites_range(0, MAX_HARDWARE_SPRITES);
                flasher_thumbnails_diaplay(thumbnails_page_no * MAX_FLASHER_THUMBNAILS);
            }
        } else --cx;
        PLAY_SFX(sound_menu_alter);
    } else if (KEY_PRESSED(J_RIGHT)) {
        if (++cx == FLASHER_THUMBS_COUNT_X) {
            uint8_t old_page = thumbnails_page_no;
            if (thumbnails_page_no < thumbnails_num_pages - 1) ++thumbnails_page_no; else thumbnails_page_no = 0;
            cx = 0;
            if (old_page != thumbnails_page_no) {
                hide_sprites_range(0, MAX_HARDWARE_SPRITES);
                flasher_thumbnails_diaplay(thumbnails_page_no * MAX_FLASHER_THUMBNAILS);
            }
        };
        PLAY_SFX(sound_menu_alter);
    } else if (KEY_PRESSED(J_A)) {
        PLAY_SFX(sound_error);
    } else if (KEY_PRESSED(J_B)) {
        browse_mode = browse_mode_folders;
        PLAY_SFX(sound_menu_alter);
    }
    hide_sprites_range(move_metasprite(flasher_cursor[cursor_anim], CORNER_UL, 0, ((cx << 2) + FLASHER_THUMBS_DISPLAY_X) << 3, ((cy << 2) + FLASHER_THUMBS_DISPLAY_Y) << 3), MAX_HARDWARE_SPRITES);
}

uint8_t UPDATE_state_flasher(void) BANKED {
    static uint8_t menu_result;
    PROCESS_INPUT();

    if (browse_mode == browse_mode_folders) update_mode_folders(); else update_mode_thumbnails();

    if (KEY_PRESSED(J_SELECT)) {
        if (browse_mode == browse_mode_folders) {
            switch (menu_result = menu_execute(&FlasherMenu, NULL, NULL)) {
                case ACTION_FLASH_SAVE:
                    flasher_show_icon();
                    if (flash_save_gallery_to_slot(current_slot)) {
                        flasher_read_slots();
                        PLAY_SFX(sound_ok);
                    } else PLAY_SFX(sound_error);
                    flasher_hide_icon();
                    JOYPAD_RESET();
                    break;
                case ACTION_FLASH_LOAD:
                    music_play_sfx(BANK(sound_ok), sound_ok, SFX_MUTE_MASK(sound_ok), MUSIC_SFX_PRIORITY_MINIMAL);
                    refresh_screen();
                    break;
                case ACTION_FLASH_ERASE:
                    flasher_show_icon();
                    if (flash_erase_slot(current_slot)) {
                        flasher_read_slots();
                        PLAY_SFX(sound_ok);
                    } else PLAY_SFX(sound_error);
                    flasher_hide_icon();
                    JOYPAD_RESET();
                    break;
                case ACTION_PRINT_SLOT:
                case ACTION_TRANSFER_SLOT: {
                    // print or transfer selected images
                    refresh_screen();
                    break;
                }
                default:
                    // unknown command or cancel
                    music_play_sfx(BANK(sound_ok), sound_ok, SFX_MUTE_MASK(sound_ok), MUSIC_SFX_PRIORITY_MINIMAL);
                    refresh_screen();
                    break;
            }
        }
    } else if (KEY_PRESSED(J_START)) {
        // run Main Menu
        hide_sprites_range(0, MAX_HARDWARE_SPRITES);
        if (!menu_main_execute()) refresh_screen();
    }

    if ((sys_time & 0x07) == 0) cursor_anim = ++cursor_anim & 0x03;
    return TRUE;
}

uint8_t LEAVE_state_flasher(void) BANKED {
    fade_out_modal();
    hide_sprites_range(0, MAX_HARDWARE_SPRITES);
    return 0;
}