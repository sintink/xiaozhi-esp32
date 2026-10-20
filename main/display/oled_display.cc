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

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

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
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);

    status_label_ = lv_label_create(screen);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    
    notification_label_ = lv_label_create(screen);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    face_container_ = lv_obj_create(container_);
    lv_obj_set_size(face_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_border_width(face_container_, 0, 0);
    lv_obj_set_style_bg_opa(face_container_, LV_OPA_TRANSP, 0);
    lv_obj_center(face_container_);

    /* === MATA KIRI === */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 4, 0); 
    lv_obj_set_size(left_eye_, 24, 24);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -4);
    lv_obj_set_style_transform_pivot_x(left_eye_, 12, 0);
    lv_obj_set_style_transform_pivot_y(left_eye_, 12, 0);

    /* === MATA KANAN === */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 4, 0);
    lv_obj_set_size(right_eye_, 24, 24);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -4);
    lv_obj_set_style_transform_pivot_x(right_eye_, 12, 0);
    lv_obj_set_style_transform_pivot_y(right_eye_, 12, 0);

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

    if (strcmp(emotion, "happy") == 0) current_emotion_ = 1;
    else if (strcmp(emotion, "sleepy") == 0) current_emotion_ = 3;
    else if (strcmp(emotion, "surprised") == 0) current_emotion_ = 4;
    else if (strcmp(emotion, "angry") == 0) current_emotion_ = 2;
    else if (strcmp(emotion, "sad") == 0) current_emotion_ = 5;
    else current_emotion_ = 0;
}

void OledDisplay::SetTheme(Theme* theme) {}

void OledDisplay::SetPowerSaveMode(bool on) {
    if (panel_) {
        Settings settings("wifi", false);
        if (settings.GetBool("power_save_display_off", false)) {
            esp_lcd_panel_disp_on_off(panel_, !on);
        }
    }
    LvglDisplay::SetPowerSaveMode(on);
}

// TANGKAP STATE DARI SISTEM
void OledDisplay::SetFaceState(int state) {
    DisplayLockGuard lock(this);
    anim_state_ = state;
}

// Fungsi pembantu bentuk mata
void OledDisplay::ApplyExpression(int emote, int breath, int bounce_y) {
    int w = 24, h = 24, r = 4;
    int angle_l = 0, angle_r = 0;

    switch (emote) {
        case 0: w = 24; h = 24; r = 4; angle_l = 0; angle_r = 0; break; // Neutral
        case 1: w = 28; h = 14; r = LV_RADIUS_CIRCLE; angle_l = 0; angle_r = 0; break; // Happy
        case 2: w = 26; h = 10; r = 0; angle_l = 200; angle_r = -200; break; // Angry
        case 3: w = 24; h = 4; r = 2; angle_l = 0; angle_r = 0; break; // Sleepy
        case 4: w = 32; h = 32; r = 6; angle_l = 0; angle_r = 0; break; // Surprised
        case 5: w = 26; h = 10; r = 0; angle_l = -200; angle_r = 200; break; // Sad
    }

    w += breath;
    h += breath;

    if (w != last_w_ || h != last_lh_ || r != last_radius_ || angle_l != last_angle_l_) {
        lv_obj_set_size(left_eye_, w, h);
        lv_obj_set_size(right_eye_, w, h);
        lv_obj_set_style_radius(left_eye_, r, 0);
        lv_obj_set_style_radius(right_eye_, r, 0);
        
        lv_obj_set_style_transform_pivot_x(left_eye_, w/2, 0);
        lv_obj_set_style_transform_pivot_y(left_eye_, h/2, 0);
        lv_obj_set_style_transform_pivot_x(right_eye_, w/2, 0);
        lv_obj_set_style_transform_pivot_y(right_eye_, h/2, 0);

        lv_obj_set_style_transform_angle(left_eye_, angle_l, 0);
        lv_obj_set_style_transform_angle(right_eye_, angle_r, 0);

        last_w_ = w;
        last_lh_ = h;
        last_rh_ = h;
        last_radius_ = r;
        last_angle_l_ = angle_l;
        last_angle_r_ = angle_r;
    }
}

// UPDATE FACE (Jalan di Task LVGL, tanpa Lock)
void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;

    if (esp_timer_get_time() - boot_time_us_ < 8000000LL) return; // Grace Period 8s

    int64_t now = esp_timer_get_time() / 1000;
    int breath = (int)(sin(now / 300.0) * 1.5);
    int bounce_y = 0;

    // ==========================================
    // LOGIKA BERDASARKAN STATE (IDLE, LISTEN, SPEAK)
    // ==========================================
    
    if (anim_state_ == 0) {
        // === IDLE (BENGONG / SANTAI) ===
        if (now > next_emote_change_) {
            current_emotion_ = esp_random() % 6; 
            next_emote_change_ = now + 3000 + (esp_random() % 4000);
        }
        if (now > next_look_) {
            target_look_x_ = (esp_random() % 9) - 4; 
            target_look_y_ = (esp_random() % 5) - 2;  
            next_look_ = now + 1500 + (esp_random() % 2000);
        }
        
        // Kedip normal
        if (!is_blinking_ && now > next_blink_) {
            is_blinking_ = true;
            blink_start_ = now;
            next_blink_ = now + 3000 + (esp_random() % 3000); 
        }

        // Wink (Kedip sebelah)
        if (wink_eye_ == 0 && now > next_wink_) {
            wink_eye_ = (esp_random() % 2) + 1; 
            wink_start_ = now;
            next_wink_ = now + 6000 + (esp_random() % 5000);
        }
    } 
    else if (anim_state_ == 1) {
        // === LISTENING (FOKUS MENDENGARKAN) ===
        // Mata fokus ke tengah, tidak melirik
        target_look_x_ = 0;
        target_look_y_ = 0;
        
        // Ekspresi selalu fokus (jangan berubah-ubah)
        current_emotion_ = 0; 
        
        // Jarang kedip
        if (!is_blinking_ && now > next_blink_) {
            is_blinking_ = true;
            blink_start_ = now;
            next_blink_ = now + 4000 + (esp_random() % 3000); 
        }
    } 
    else if (anim_state_ == 2) {
        // === SPEAKING (SINKRONISASI BICARA) ===
        // Mata selalu fokus ke tengah
        target_look_x_ = 0;
        target_look_y_ = 0;
        
        // Ilusi sinkronisasi suara: Mata memantul cepat (seperti irama ngomong)
        // Oscillator cepat setiap ~120ms
        bounce_y = (int)(sin(now / 40.0) * 3.0); 
        
        // Biarkan server yang menentukan emosi saat bicara, 
        // TAPI kalau tidak ada emosi, kadang berubah-ubah agar hidup
        if (now > next_emote_change_) {
            int r = esp_random() % 3;
            if (r == 0) current_emotion_ = 1; // Happy
            else if (r == 1) current_emotion_ = 0; // Normal
            else current_emotion_ = 4; // Surprised (antusias)
            
            next_emote_change_ = now + 1000 + (esp_random() % 1500);
        }
    }

    // ==========================================
    // RENDER BENTUK
    // ==========================================
    ApplyExpression(current_emotion_, breath, bounce_y);

    // ==========================================
    // HANDLE BLINKING & WINKING
    // ==========================================
    int left_h = last_lh_;
    int right_h = last_rh_;

    if (is_blinking_) {
        int64_t elapsed = now - blink_start_;
        if (elapsed < 80) {
            left_h = right_h = last_lh_ - (elapsed * (last_lh_ - 2) / 80);
        } else if (elapsed < 160) {
            left_h = right_h = 2 + ((elapsed - 80) * (last_lh_ - 2) / 80);
        } else {
            is_blinking_ = false;
        }
    }

    if (wink_eye_ != 0) {
        int64_t elapsed = now - wink_start_;
        if (elapsed < 200) {
            if (wink_eye_ == 1) left_h = 2;
            if (wink_eye_ == 2) right_h = 2;
        } else {
            wink_eye_ = 0;
        }
    }

    // Terapkan tinggi mata (jika berubah)
    if (left_h != last_lh_ || right_h != last_rh_) {
        lv_obj_set_height(left_eye_, left_h);
        lv_obj_set_height(right_eye_, right_h);
    }

    // ==========================================
    // HANDLE POSISI (LIRIK & BOUNCE)
    // ==========================================
    look_x_ += (target_look_x_ - look_x_) * 0.2f;
    look_y_ += (target_look_y_ - look_y_) * 0.2f;
    
    int ox = std::clamp((int)look_x_, -6, 6);
    int oy = std::clamp((int)look_y_, -3, 3);

    // Gabungkan posisi lirik dengan efek pantulan suara (bounce_y)
    int final_oy = oy + bounce_y;

    if (ox != last_ox_ || final_oy != last_oy_) {
        lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_ + ox, -4 + final_oy);
        lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_ + ox, -4 + final_oy);
        last_ox_ = ox;
        last_oy_ = final_oy;
    }
}
