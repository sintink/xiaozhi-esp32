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
    port_cfg.task_stack = 6144;
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
        .rotation = {.swap_xy = false, .mirror_x = mirror_x, .mirror_y = mirror_y},
        .flags = {.buff_dma = 1, .buff_spiram = 0, .sw_rotate = 0,
                  .full_refresh = 0, .direct_mode = 0},
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Inisialisasi waktu
    last_activity_us_ = esp_timer_get_time();
    next_blink_us_ = last_activity_us_ + 3000000LL;
    next_look_us_ = last_activity_us_ + 2000000LL;
}

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    if (content == nullptr || subtitle_label_ == nullptr) return;
    DisplayLockGuard lock(this);

    std::string prefix;
    if (strcmp(role, "user") == 0) {
        prefix = ">> ";
    } else if (strcmp(role, "assistant") == 0) {
        prefix = "<< ";
    }
    std::string text = prefix + std::string(content);
    lv_label_set_text(subtitle_label_, text.c_str());
}

void OledDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping");
        return;
    }
    Display::SetupUI();
    SetupUI_128x64();
}

OledDisplay::~OledDisplay() {
    // Hapus timer DULU sebelum widget, supaya callback tidak akses widget yang sudah dibebaskan
    if (face_timer_ != nullptr) {
        lv_timer_del(face_timer_);
        face_timer_ = nullptr;
    }

    if (content_ != nullptr) {
        lv_obj_del(content_);
        content_ = nullptr;
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
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
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Status bar overlay */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
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
    lv_obj_set_style_pad_all(face_container_, 0, 0);
    lv_obj_center(face_container_);

    /* Mata kiri - mochi, bulat besar, radius di-clamp otomatis LVGL */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 20, 0);   // clamp otomatis ke min(w,h)/2
    lv_obj_set_size(left_eye_, kEyeSize, kEyeSize);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -kEyeOffset, -4);

    /* Mata kanan */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 20, 0);
    lv_obj_set_size(right_eye_, kEyeSize, kEyeSize);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, kEyeOffset, -4);

    /* Mulut kecil */
    mouth_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(mouth_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_style_radius(mouth_, 2, 0);
    lv_obj_set_size(mouth_, 10, 3);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 14);

    /* Z label (tidur) - diletakkan setelah semua widget siap */
    z_label_ = lv_label_create(content_);
    lv_label_set_text(z_label_, "Z");
    lv_obj_set_style_text_font(z_label_, text_font, 0);
    lv_obj_align(z_label_, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_flag(z_label_, LV_OBJ_FLAG_HIDDEN);

    /* Subtitle label */
    subtitle_label_ = lv_label_create(content_);
    lv_obj_set_width(subtitle_label_, LV_HOR_RES - 4);
    lv_label_set_long_mode(subtitle_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(subtitle_label_, "");
    lv_obj_align(subtitle_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);

    /* Low battery popup */
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);

    // Waktu aktivitas — reset setelah semua widget siap
    last_activity_us_ = esp_timer_get_time();
    next_blink_us_ = last_activity_us_ + 3000000LL;
    next_look_us_ = last_activity_us_ + 2000000LL;

    // Timer DIMULAI PALING AKHIR
    InitEyes();
}

void OledDisplay::InitEyes() {
    if (face_timer_ != nullptr) return;
    face_timer_ = lv_timer_create([](lv_timer_t* t) {
        OledDisplay* self = (OledDisplay*)lv_timer_get_user_data(t);
        if (self != nullptr) self->UpdateFace();
    }, 50, this);
}

// HANYA ubah ukuran (tidak ubah radius). Radius 20 di setup akan otomatis di-clamp LVGL.
void OledDisplay::ApplyEyeExpression(int expression) {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;

    int w = kEyeSize;
    int h = kEyeSize;

    switch (expression) {
        case 1:  // happy ^_^
            w = kEyeSize + 4;      // 24
            h = kEyeSize - 10;     // 10
            break;
        case 2:  // ngantuk -_-
            w = kEyeSize;
            h = kEyeSize / 2;      // 10
            break;
        case 3:  // tidur --
            w = kEyeSize;
            h = 2;
            break;
        case 4:  // kaget O_O
            w = kEyeSize + 4;      // 24
            h = kEyeSize + 4;      // 24
            break;
        case 0:
        default:
            w = kEyeSize;
            h = kEyeSize;
            break;
    }

    lv_obj_set_size(left_eye_, w, h);
    lv_obj_set_size(right_eye_, w, h);
}

void OledDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr || left_eye_ == nullptr) return;

    DisplayLockGuard lock(this);
    if (strcmp(emotion, "happy") == 0 || strcmp(emotion, "laughing") == 0) {
        ApplyEyeExpression(1);
    } else if (strcmp(emotion, "sleepy") == 0) {
        ApplyEyeExpression(2);
    } else if (strcmp(emotion, "surprised") == 0) {
        ApplyEyeExpression(4);
    } else {
        ApplyEyeExpression(0);
    }
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
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
}

void OledDisplay::SetFaceState(int state) {
    DisplayLockGuard lock(this);

    last_activity_us_ = esp_timer_get_time();

    if (sleep_state_ == 1 || sleep_state_ == 2) {
        sleep_state_ = 3;  // kaget saat dibangunkan
    } else if (sleep_state_ != 3) {
        sleep_state_ = 0;
    }

    if (z_label_ != nullptr) lv_obj_add_flag(z_label_, LV_OBJ_FLAG_HIDDEN);

    face_state_ = state;

    if (state == 0) {
        if (face_container_) lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
        if (subtitle_label_) {
            lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(subtitle_label_, "");
        }
    } else {
        if (sleep_state_ != 3) {
            if (face_container_) lv_obj_add_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
            if (subtitle_label_) lv_obj_clear_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// === TIMER: hanya ubah ukuran + align + toggle hidden (semua proven-safe) ===
void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;

    int64_t now = esp_timer_get_time();

    /* --- State tidur/ngantuk (hanya saat idle) --- */
    if (face_state_ == 0) {
        if (face_container_) lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
        if (subtitle_label_) lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);

        int64_t idle_ms = (now - last_activity_us_) / 1000;

        if (sleep_state_ == 3) {                       // kaget
            ApplyEyeExpression(4);
            if (idle_ms > 500) sleep_state_ = 0;
        } else if (idle_ms > 60000) {                  // tidur
            if (sleep_state_ != 2) {
                sleep_state_ = 2;
                ApplyEyeExpression(3);
                if (z_label_) lv_obj_clear_flag(z_label_, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (idle_ms > 30000) {                  // ngantuk
            if (sleep_state_ != 1) {
                sleep_state_ = 1;
                ApplyEyeExpression(2);
                if (z_label_) lv_obj_add_flag(z_label_, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            if (sleep_state_ != 0) {
                sleep_state_ = 0;
                ApplyEyeExpression(0);
                if (z_label_) lv_obj_add_flag(z_label_, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        /* Listening/Speaking */
        if (sleep_state_ == 3) {
            ApplyEyeExpression(4);
            if (face_container_) lv_obj_clear_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
            if (subtitle_label_) lv_obj_add_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
            if ((now - last_activity_us_) / 1000 > 500) {
                sleep_state_ = 0;
                if (face_container_) lv_obj_add_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
                if (subtitle_label_) lv_obj_clear_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            last_activity_us_ = now;
            if (face_container_) lv_obj_add_flag(face_container_, LV_OBJ_FLAG_HIDDEN);
            if (subtitle_label_) lv_obj_clear_flag(subtitle_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* --- Blink & lirik: hanya saat normal dan idle --- */
    if (face_state_ == 0 && sleep_state_ == 0) {
        if (!is_blinking_ && now >= next_blink_us_) {
            is_blinking_ = true;
            blink_start_us_ = now;
            next_blink_us_ = now + (2500 + (esp_random() % 2500)) * 1000LL;
        }

        if (is_blinking_) {
            int64_t elapsed_ms = (now - blink_start_us_) / 1000;
            int h = kEyeSize;
            if (elapsed_ms < 80) {
                h = kEyeSize - (int)(elapsed_ms * (kEyeSize - 2) / 80);
            } else if (elapsed_ms < 160) {
                h = 2 + (int)((elapsed_ms - 80) * (kEyeSize - 2) / 80);
            } else {
                is_blinking_ = false;
            }
            lv_obj_set_height(left_eye_, h);
            lv_obj_set_height(right_eye_, h);
        }

        if (now >= next_look_us_) {
            next_look_us_ = now + (2000 + (esp_random() % 3000)) * 1000LL;
            target_look_x_ = (int)(esp_random() % 9) - 4;
            target_look_y_ = (int)(esp_random() % 5) - 2;
        }

        look_x_ += (target_look_x_ - look_x_) * 0.15f;
        look_y_ += (target_look_y_ - look_y_) * 0.15f;
        int ox = (int)look_x_;
        int oy = (int)look_y_;
        if (ox < -5) ox = -5;
        if (ox > 5) ox = 5;
        if (oy < -3) oy = -3;
        if (oy > 3) oy = 3;
        lv_obj_align(left_eye_, LV_ALIGN_CENTER, -kEyeOffset + ox, -4 + oy);
        lv_obj_align(right_eye_, LV_ALIGN_CENTER, kEyeOffset + ox, -4 + oy);
    }
}
