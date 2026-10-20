#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "settings.h"

#include <algorithm>
#include <string>
#include <cstring>
#include <cmath>

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
    
    // Simpan waktu boot untuk Grace Period
    boot_time_us_ = esp_timer_get_time();
    
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

void OledDisplay::SetupUI() {
    if (setup_ui_called_) return;
    Display::SetupUI();
    SetupUI_128x64();
}

OledDisplay::~OledDisplay() {
    if (face_timer_ != nullptr) {
        lv_timer_del(face_timer_);
        face_timer_ = nullptr;
    }

    if (container_ != nullptr) lv_obj_del(container_);

    if (panel_ != nullptr) esp_lcd_panel_del(panel_);
    if (panel_io_ != nullptr) esp_lcd_panel_io_del(panel_io_);
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    
    // Background putih agar mata hitam (OLED) terlihat jelas
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    /* Container utama (Full layar tanpa top/status bar) */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    /* Face container */
    face_container_ = lv_obj_create(container_);
    lv_obj_set_size(face_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_border_width(face_container_, 0, 0);
    lv_obj_set_style_bg_opa(face_container_, LV_OPA_TRANSP, 0);
    lv_obj_center(face_container_);

    /* Mata Kiri - pakai LV_RADIUS_CIRCLE agar otomatis membulat tanpa ubah style terus-menerus */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(left_eye_, eye_size_, eye_size_);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -4);

    /* Mata Kanan */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(right_eye_, eye_size_, eye_size_);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -4);

    /* Mulut */
    mouth_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(mouth_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_style_radius(mouth_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(mouth_, 10, 2);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 16);

    // Mulai Timer Animasi
    InitEyes();
}

void OledDisplay::InitEyes() {
    if (face_timer_ != nullptr) return;
    face_timer_ = lv_timer_create([](lv_timer_t* t) {
        OledDisplay* self = (OledDisplay*)lv_timer_get_user_data(t);
        if (self) self->UpdateFace();
    }, 50, this);
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
}

void OledDisplay::SetTheme(Theme* theme) {
    // Tidak ada text, jadi kosongkan saja
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

void OledDisplay::SetFaceState(int state) {
    DisplayLockGuard lock(this);
    anim_state_ = state; // Menyimpan status sistem (Idle, Listening, Speaking)
}

void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;

    // --- GRACE PERIOD 8 DETIK (Mencegah Bootloop saat OTA) ---
    if (esp_timer_get_time() - boot_time_us_ < 8000000LL) {
        return; 
    }

    int64_t now = esp_timer_get_time() / 1000;

    // 1. Tentukan Bentuk Dasar dari Emosi
    int base_w = 20;
    int base_h = 20;
    int base_y = -4;
    
    switch (current_emotion_) {
        case 1: // Happy (Mata lebar ceper ala mochi)
            base_w = 26; base_h = 10; break;
        case 2: // Sleepy (Mata sipit ngantuk)
            base_w = 22; base_h = 6; break;
        case 3: // Surprised (Mata bulat besar kaget)
            base_w = 28; base_h = 28; break;
        case 4: // Angry (Mata tajam marah)
            base_w = 26; base_h = 6; base_y = -2; break;
        default: // Normal
            base_w = 20; base_h = 20; break;
    }

    // 2. Modifikasi dari State (Listening / Speaking)
    if (anim_state_ == 1) { 
        // Listening: Mata agak membesar dan fokus
        base_w += 2;
        base_h += 2;
    } else if (anim_state_ == 2) { 
        // Speaking: Mata memantul / berdenyut santai
        int bounce = (now / 150) % 3;
        if (bounce == 0) base_h += 4;
        else if (bounce == 1) base_h += 2;
    }

    // 3. Logika Kedip (Hanya saat Idle)
    if (anim_state_ == 0) {
        if (!is_blinking_ && now - next_blink_ > 0) {
            is_blinking_ = true;
            blink_start_ = now;
            next_blink_ = now + 2000 + (esp_random() % 3000);
        }

        if (is_blinking_) {
            int64_t elapsed = now - blink_start_;
            if (elapsed < 80) {
                base_h = base_h - (elapsed * (base_h - 2) / 80);
            } else if (elapsed < 160) {
                base_h = 2 + ((elapsed - 80) * (base_h - 2) / 80);
            } else {
                is_blinking_ = false;
            }
        }
    }

    // Terapkan ukuran mata baru
    lv_obj_set_size(left_eye_, base_w, base_h);
    lv_obj_set_size(right_eye_, base_w, base_h);

    // 4. Logika Lirik Mata (Hanya saat Idle)
    if (anim_state_ == 1) {
        target_look_x_ = 0;
        target_look_y_ = 0;
    } else if (now - next_look_ > 0) {
        next_look_ = now + 2000 + (esp_random() % 3000);
        target_look_x_ = (int)(esp_random() % 9) - 4;
        target_look_y_ = (int)(esp_random() % 5) - 2;
    }

    look_x_ += (target_look_x_ - look_x_) * 0.15f;
    look_y_ += (target_look_y_ - look_y_) * 0.15f;
    
    int ox = std::clamp((int)look_x_, -5, 5);
    int oy = std::clamp((int)look_y_, -3, 3);

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_ + ox, base_y + oy);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_ + ox, base_y + oy);

    // 5. Logika Mulut (Buka tutup saat Speaking)
    if (mouth_ != nullptr) {
        if (anim_state_ == 2) {
            // Mulut komat-kamit buka tutup halus
            int mouth_h = 4 + (int)(4.0 + 4.0 * sin(now / 80.0));
            lv_obj_set_size(mouth_, 12, mouth_h);
        } else {
            // Diam
            lv_obj_set_size(mouth_, 10, 2);
        }
    }
}
