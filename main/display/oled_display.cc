#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <algorithm>
#include <string>
#include <cstring>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>
#include <esp_random.h>
#include <esp_timer.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144; // Tetap 6KB biar aman
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }
}

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    if (content == nullptr || subtitle_label_ == nullptr) return;
    DisplayLockGuard lock(this);

    std::string prefix;
    if (strcmp(role, "user") == 0) prefix = ">> ";
    else if (strcmp(role, "assistant") == 0) prefix = "<< ";

    std::string text = prefix + std::string(content);
    lv_label_set_text(subtitle_label_, text.c_str());
}

void OledDisplay::SetupUI() {
    if (setup_ui_called_) return;
    Display::SetupUI();
    SetupUI_128x64();
}

OledDisplay::~OledDisplay() {
    // Hapus timer DULU sebelum widget
    if (face_timer_ != nullptr) {
        lv_timer_del(face_timer_);
        face_timer_ = nullptr;
    }

    if (content_ != nullptr) lv_obj_del(content_);

    if (status_bar_ != nullptr) lv_obj_del(status_bar_);
    if (top_bar_ != nullptr) lv_obj_del(top_bar_);
    if (side_bar_ != nullptr) lv_obj_del(side_bar_);
    if (container_ != nullptr) lv_obj_del(container_);

    if (panel_ != nullptr) esp_lcd_panel_del(panel_);
    if (panel_io_ != nullptr) esp_lcd_panel_io_del(panel_io_);
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container utama */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Top bar */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 12);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content (Tempat Mata) */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_size(content_, LV_HOR_RES, 48);
    lv_obj_set_flex_grow(content_, 1);

    /* Face container */
    face_container_ = lv_obj_create(content_);
    lv_obj_set_size(face_container_, LV_HOR_RES, 48);
    lv_obj_set_style_border_width(face_container_, 0, 0);
    lv_obj_set_style_bg_opa(face_container_, LV_OPA_TRANSP, 0);
    lv_obj_center(face_container_);

    /* Mata Kiri */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 10, 0); // Radius fixed, gak diubah-ubah
    lv_obj_set_size(left_eye_, eye_size_, eye_size_);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -4);

    /* Mata Kanan */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 10, 0);
    lv_obj_set_size(right_eye_, eye_size_, eye_size_);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -4);

    /* Mulut (opsional) */
    mouth_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(mouth_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_style_radius(mouth_, 2, 0);
    lv_obj_set_size(mouth_, 12, 3);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 14);

    /* Subtitle */
    subtitle_label_ = lv_label_create(content_);
    lv_obj_set_width(subtitle_label_, LV_HOR_RES - 4);
    lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(subtitle_label_, "");
    lv_obj_align(subtitle_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);

    /* Low Battery */
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // Mulai Timer
    InitEyes();
}

void OledDisplay::InitEyes() {
    if (face_timer_ != nullptr) return;
    face_timer_ = lv_timer_create([](lv_timer_t* t) {
        OledDisplay* self = (OledDisplay*)lv_timer_get_user_data(t);
        if (self) self->UpdateFace();
    }, 50, this);
}

void OledDisplay::ApplyEyeShape(int emotion) {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;
    
    int w = eye_size_;
    int h = eye_size_;

    switch (emotion) {
        case 1: // Happy (Mata bulat lebar)
            w = eye_size_ + 2; h = eye_size_ - 4; break;
        case 2: // Sleepy (Mata sipit)
            w = eye_size_; h = eye_size_ / 2; break;
        case 3: // Surprised (Mata besar)
            w = eye_size_ + 4; h = eye_size_ + 4; break;
        case 4: // Angry (Mata tajam)
            w = eye_size_ + 2; h = eye_size_ - 6; break;
        case 0: default: // Normal
            w = eye_size_; h = eye_size_; break;
    }
    
    lv_obj_set_size(left_eye_, w, h);
    lv_obj_set_size(right_eye_, w, h);
}

void OledDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) return;
    DisplayLockGuard lock(this);

    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0) {
        current_emotion_ = 1;
    } else if (strcmp(emotion, "sleepy") == 0) {
        current_emotion_ = 2;
    } else if (strcmp(emotion, "surprised") == 0) {
        current_emotion_ = 3;
    } else if (strcmp(emotion, "angry") == 0) {
        current_emotion_ = 4;
    } else {
        current_emotion_ = 0;
    }
    ApplyEyeShape(current_emotion_);
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();
    lv_obj_set_style_text_font(lv_screen_active(), text_font, 0);
}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}

void OledDisplay::ClearChatMessages() {
    if (subtitle_label_ == nullptr) return;
    DisplayLockGuard lock(this);
    lv_label_set_text(subtitle_label_, "");
    lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN); // Mata selalu muncul
}

void OledDisplay::SetFaceState(int state) {
    DisplayLockGuard lock(this);
    
    // Saat komunikasi (Listening/Speaking), tampilkan subtitle, sembunyikan mata? 
    // User minta "full mata semua aja". Jadi mata TETAP MUNCUL.
    
    if (state == 0) {
        // Idle
        lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(subtitle_label_, "");
        lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN); 
    } else {
        // Komunikasi
        lv_obj_clear_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
        // face_container_ JANGAN disembunyikan
        lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
        
        // Geser mata sedikit ke atas biar gak nabrak subtitle
        lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -10);
        lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -10);
        lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 6);
    }
}

void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;

    unsigned long now = esp_timer_get_time() / 1000;

    // --- LOGIKA BLINKING ---
    if (!is_blinking_ && now - next_blink_ > 0) {
        is_blinking_ = true;
        blink_start_ = now;
        next_blink_ = now + 2500 + (esp_random() % 2500);
    }

    if (is_blinking_) {
        unsigned long elapsed = now - blink_start_;
        int h = eye_size_;
        if (elapsed < 80) {
            h = eye_size_ - (elapsed * (eye_size_ - 2) / 80);
        } else if (elapsed < 160) {
            h = 2 + ((elapsed - 80) * (eye_size_ - 2) / 80);
        } else {
            is_blinking_ = false;
            h = eye_size_;
            ApplyEyeShape(current_emotion_); // Balikin ke bentuk asli
        }
        lv_obj_set_height(left_eye_, h);
        lv_obj_set_height(right_eye_, h);
    }

    // --- LOGIKA LIRIK ---
    if (now - next_look_ > 0) {
        next_look_ = now + 2000 + (esp_random() % 3000);
        target_look_x_ = (int)(esp_random() % 9) - 4;
        target_look_y_ = (int)(esp_random() % 5) - 2;
    }

    look_x_ += (target_look_x_ - look_x_) * 0.15f;
    look_y_ += (target_look_y_ - look_y_) * 0.15f;
    
    int ox = std::clamp((int)look_x_, -5, 5);
int oy = std::clamp((int)look_y_, -3, 3);
  
    // Cek apakah sedang komunikasi (mata agak ke atas)
    int y_offset = -4; // Default idle
    if (subtitle_label_ && !lv_obj_has_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN)) {
        y_offset = -10; // Mode komunikasi
    }

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_ + ox, y_offset + oy);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_ + ox, y_offset + oy);
}
