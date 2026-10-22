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
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
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

    /* === MATA KIRI (Bersih tanpa pupil) === */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 14, 0); 
    lv_obj_set_size(left_eye_, 32, 36);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -4);

    /* === MATA KANAN (Bersih tanpa pupil) === */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 14, 0);
    lv_obj_set_size(right_eye_, 32, 36);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -4);

    /* === OBJEK MULUT DINAMIS (Di bawah mata) === */
    mouth_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(mouth_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mouth_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mouth_, 0, 0);
    lv_obj_set_style_radius(mouth_, 2, 0);
    lv_obj_set_size(mouth_, 8, 3);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, 18);

    InitEyes();
}

void OledDisplay::InitEyes() {
    if (face_timer_ != nullptr) return;
    face_timer_ = lv_timer_create([](lv_timer_t* t) {
        OledDisplay* self = (OledDisplay*)lv_timer_get_user_data(t);
        if (self) self->UpdateFace();
    }, 40, this);
}

void OledDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) return;
    DisplayLockGuard lock(this);

    if (strcmp(emotion, "happy") == 0) current_emotion_ = 1;
    else if (strcmp(emotion, "sleepy") == 0) current_emotion_ = 3;
    else if (strcmp(emotion, "surprised") == 0) current_emotion_ = 4;
    else if (strcmp(emotion, "angry") == 0) current_emotion_ = 2;
    else if (strcmp(emotion, "sad") == 0) current_emotion_ = 5;
    else if (strcmp(emotion, "confused") == 0) current_emotion_ = 6;
    else if (strcmp(emotion, "shocked") == 0) current_emotion_ = 7;
    else if (strcmp(emotion, "love") == 0) current_emotion_ = 8;
    else if (strcmp(emotion, "suspicious") == 0) current_emotion_ = 9;
    else if (strcmp(emotion, "dizzy") == 0) current_emotion_ = 10;
    else current_emotion_ = 0;

    ai_emotion_active_ = true;
    ai_emotion_timeout_ = (esp_timer_get_time() / 1000) + 4000; 
}

void OledDisplay::SetChatMessage(const char* role, const char* content) {
    if (content == nullptr) return;
    
    std::string text(content);
    for (char &c : text) c = tolower(c);

    DisplayLockGuard lock(this);
    ai_emotion_active_ = true;
    ai_emotion_timeout_ = (esp_timer_get_time() / 1000) + 5000;

    if (text.find("sedih") != std::string::npos || text.find("maaf") != std::string::npos) {
        current_emotion_ = 5; 
    } else if (text.find("senang") != std::string::npos || text.find("haha") != std::string::npos || text.find("keren") != std::string::npos) {
        current_emotion_ = 1; 
    } else if (text.find("sayang") != std::string::npos || text.find("cinta") != std::string::npos) {
        current_emotion_ = 8; // Love
    } else if (text.find("kenapa") != std::string::npos || text.find("?") != std::string::npos) {
        current_emotion_ = 6; 
    } else {
        current_emotion_ = 0; 
    }
}

void OledDisplay::ClearChatMessages() {}
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

void OledDisplay::SetFaceState(int state) {
    DisplayLockGuard lock(this);
    if (anim_state_ != state) {
        anim_state_ = state;
        is_blinking_ = false;
        wink_eye_ = 0;
        next_look_ = 0; 
        ai_emotion_active_ = false;
    }
}

void OledDisplay::UpdateTargetShape(int emote) {
    switch (emote) {
        case 0: target_w_ = 32; target_h_ = 36; target_r_ = 14; target_angle_l_ = 0; target_angle_r_ = 0; break; // Netral
        case 1: target_w_ = 36; target_h_ = 18; target_r_ = LV_RADIUS_CIRCLE; target_angle_l_ = 0; target_angle_r_ = 0; break; // Happy
        case 2: target_w_ = 34; target_h_ = 16; target_r_ = 6; target_angle_l_ = 250; target_angle_r_ = -250; break; // Angry
        case 3: target_w_ = 30; target_h_ = 3;  target_r_ = 1; target_angle_l_ = 0; target_angle_r_ = 0; break; // Sleepy (Merem bersih)
        case 4: target_w_ = 40; target_h_ = 40; target_r_ = 20; target_angle_l_ = 0; target_angle_r_ = 0; break; // Surprised
        case 5: target_w_ = 34; target_h_ = 16; target_r_ = 6; target_angle_l_ = -250; target_angle_r_ = 250; break; // Sad
        case 6: target_w_ = 32; target_h_ = 24; target_r_ = 10; target_angle_l_ = -100; target_angle_r_ = 150; break; // Confused
        case 7: target_w_ = 44; target_h_ = 20; target_r_ = 8; target_angle_l_ = 0; target_angle_r_ = 0; break; // Shocked
        case 8: target_w_ = 36; target_h_ = 20; target_r_ = LV_RADIUS_CIRCLE; target_angle_l_ = 0; target_angle_r_ = 0; break; // Love
        case 9: target_w_ = 34; target_h_ = 12; target_r_ = 4; target_angle_l_ = 150; target_angle_r_ = 150; break; // Suspicious
        case 10: target_w_ = 30; target_h_ = 30; target_r_ = 8; target_angle_l_ = 180; target_angle_r_ = -180; break; // Dizzy
    }
}

void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr || mouth_ == nullptr) return;
    if (esp_timer_get_time() - boot_time_us_ < 8000000LL) return; 

    int64_t now = esp_timer_get_time() / 1000;
    int bounce_y = 0;

    static int64_t last_activity_time = now;
    static bool is_sleeping = false;
    static int64_t wake_shock_timeout = 0;

    // Cek aktivitas untuk membangunkan tidur (25 Detik threshold)
    if (ai_emotion_active_ || anim_state_ != 0) {
        if (is_sleeping) {
            wake_shock_timeout = now + 1200; 
            current_emotion_ = 7; // Kaget pas bangun
        }
        last_activity_time = now;
        is_sleeping = false;
    }

    if (wake_shock_timeout > 0 && now < wake_shock_timeout) {
        current_emotion_ = 7;
    } else {
        if (wake_shock_timeout > 0) wake_shock_timeout = 0;
        
        if (ai_emotion_active_ && now > ai_emotion_timeout_) {
            ai_emotion_active_ = false; 
            if (anim_state_ == 0) current_emotion_ = 0; 
        }
    }

    // 1. LOGIKA IDLE & TAMAGOTCHI TIDUR (25 Detik)
    if (anim_state_ == 0 && wake_shock_timeout == 0) {
        if (!ai_emotion_active_ && (now - last_activity_time > 25000)) {
            is_sleeping = true;
            current_emotion_ = 3; // Tidur bersih
            target_look_x_ = 0;
            target_look_y_ = 5;   
        } 
        else if (!is_sleeping) {
            if (!ai_emotion_active_ && now > next_emote_change_) {
                int chance = esp_random() % 18;
                if (chance < 8) current_emotion_ = 0;
                else if (chance == 8) current_emotion_ = 1; 
                else if (chance == 9) current_emotion_ = 6; 
                else if (chance == 10) current_emotion_ = 9; 
                else current_emotion_ = 0;
                
                next_emote_change_ = now + 2500 + (esp_random() % 6000);
            }
            
            if (now > next_look_) {
                int look_style = esp_random() % 6;
                if (look_style == 0) {
                    target_look_x_ = (esp_random() % 2 == 0) ? 12 : -12;
                    target_look_y_ = (esp_random() % 5) - 2;
                } else if (look_style == 1) {
                    target_look_x_ = 0;
                    target_look_y_ = -6; 
                } else {
                    target_look_x_ = (esp_random() % 9) - 4;
                    target_look_y_ = (esp_random() % 5) - 2;
                }
                next_look_ = now + 1200 + (esp_random() % 4500);
            }

            if (!is_blinking_ && now > next_blink_) {
                is_blinking_ = true; 
                blink_start_ = now;
                next_blink_ = now + 3000 + (esp_random() % 4000); 
            }
        }
    } 
    else if (anim_state_ == 2) {
        // SPEAKING (Bounce Mochi)
        target_look_x_ = 0; target_look_y_ = 0;
        float wave1 = sin(now / 30.0);
        float wave2 = sin(now / 10.0);
        bounce_y = (int)((wave1 * 0.7 + wave2 * 0.3) * 4.0);
    }

    // 2. LERP TRANSISI
    UpdateTargetShape(current_emotion_);
    
    current_w_ += (target_w_ - current_w_) * 0.25f;
    current_h_ += (target_h_ - current_h_) * 0.25f;
    current_r_ += (target_r_ - current_r_) * 0.25f;
    current_angle_l_ += (target_angle_l_ - current_angle_l_) * 0.25f;
    current_angle_r_ += (target_angle_r_ - current_angle_r_) * 0.25f;

    // 3. KEDIP
    float blink_mult = 1.0f;
    if (is_blinking_ && !is_sleeping) {
        int64_t elapsed = now - blink_start_;
        if (elapsed < 50) blink_mult = 1.0f - ((float)elapsed / 50.0f);
        else if (elapsed < 120) blink_mult = (float)(elapsed - 50) / 70.0f;
        else is_blinking_ = false;
    }

    int final_lh = (int)(current_h_ * blink_mult);
    int final_rh = (int)(current_h_ * blink_mult);

    // 4. LIRIK & BREATHING
    look_x_ += (target_look_x_ - look_x_) * 0.5f;
    look_y_ += (target_look_y_ - look_y_) * 0.5f;
    
    if (is_sleeping) {
        float_y_ = sin(now / 800.0) * 1.2f; // Napas tidur pelan
    } else if (anim_state_ == 0 && !is_blinking_) {
        float_y_ = sin(now / 600.0) * 1.5f; 
    } else {
        float_y_ *= 0.9f; 
    }
    
    int ox = std::clamp((int)look_x_, -8, 8);
    int oy = std::clamp((int)look_y_, -4, 4);
    int final_oy = oy + bounce_y + (int)float_y_;

    // 5. KELOLA BENTUK MULUT DINAMIS
    int mouth_w = 8;
    int mouth_h = 3;
    int mouth_r = 2;
    int mouth_y_offset = 18;

    if (is_sleeping) {
        mouth_w = 4; mouth_h = 2; // Mulut mingkem kecil pas tidur
    } else if (current_emotion_ == 7 || wake_shock_timeout > 0) {
        mouth_w = 8; mouth_h = 8; mouth_r = LV_RADIUS_CIRCLE; // Mangap bulat 'o' pas kaget
    } else if (current_emotion_ == 1 || current_emotion_ == 8) {
        mouth_w = 12; mouth_h = 4; mouth_r = 3; // Senyum lebar
    } else if (anim_state_ == 2) {
        float mouth_wave = sin(now / 20.0);
        mouth_w = 6 + (int)(mouth_wave * 4.0);
        mouth_h = 4 + (int)(std::abs(mouth_wave) * 4.0);
        mouth_r = LV_RADIUS_CIRCLE;
    }

    // 6. RENDER KE LVGL (Tanpa Pupil)
    lv_obj_set_size(left_eye_, (int)current_w_, final_lh);
    lv_obj_set_size(right_eye_, (int)current_w_, final_rh);
    lv_obj_set_style_radius(left_eye_, (int)current_r_, 0);
    lv_obj_set_style_radius(right_eye_, (int)current_r_, 0);
    
    lv_obj_set_style_transform_angle(left_eye_, (int)current_angle_l_, 0);
    lv_obj_set_style_transform_angle(right_eye_, (int)current_angle_r_, 0);

    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_ + ox, -4 + final_oy);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_ + ox, -4 + final_oy);

    // Render Mulut
    lv_obj_set_size(mouth_, mouth_w, mouth_h);
    lv_obj_set_style_radius(mouth_, mouth_r, 0);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, ox / 2, mouth_y_offset + final_oy);
}
