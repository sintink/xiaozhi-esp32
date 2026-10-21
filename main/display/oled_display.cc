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
    DisplayGuard lock(this);

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

    /* === MATA KIRI === */
    left_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 14, 0); // Bentuk lebih melengkung ala mochi
    lv_obj_set_size(left_eye_, 32, 36);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_, -2);
    lv_obj_set_style_transform_pivot_x(left_eye_, 16, 0);
    lv_obj_set_style_transform_pivot_y(left_eye_, 18, 0);

    // TITIK FOKUS (PUPIL)
    left_pupil_ = lv_obj_create(left_eye_);
    lv_obj_set_style_bg_color(left_pupil_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(left_pupil_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left_pupil_, 0, 0);
    lv_obj_set_style_radius(left_pupil_, 3, 0);
    lv_obj_set_size(left_pupil_, 7, 7);
    lv_obj_align(left_pupil_, LV_ALIGN_CENTER, 0, 0);

    /* === MATA KANAN === */
    right_eye_ = lv_obj_create(face_container_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 14, 0);
    lv_obj_set_size(right_eye_, 32, 36);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_, -2);
    lv_obj_set_style_transform_pivot_x(right_eye_, 16, 0);
    lv_obj_set_style_transform_pivot_y(right_eye_, 18, 0);

    // TITIK FOKUS (PUPIL)
    right_pupil_ = lv_obj_create(right_eye_);
    lv_obj_set_style_bg_color(right_pupil_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(right_pupil_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right_pupil_, 0, 0);
    lv_obj_set_style_radius(right_pupil_, 3, 0);
    lv_obj_set_size(right_pupil_, 7, 7);
    lv_obj_align(right_pupil_, LV_ALIGN_CENTER, 0, 0);

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
    DisplayGuard lock(this);

    if (strcmp(emotion, "happy") == 0) current_emotion_ = 1;
    else if (strcmp(emotion, "sleepy") == 0) current_emotion_ = 3;
    else if (strcmp(emotion, "surprised") == 0) current_emotion_ = 4;
    else if (strcmp(emotion, "angry") == 0) current_emotion_ = 2;
    else if (strcmp(emotion, "sad") == 0) current_emotion_ = 5;
    else if (strcmp(emotion, "confused") == 0) current_emotion_ = 6; // Emosi baru: Bingung
    else if (strcmp(emotion, "shocked") == 0) current_emotion_ = 7;  // Emosi baru: Terperanjat
    else current_emotion_ = 0;

    ai_emotion_active_ = true;
    ai_emotion_timeout_ = (esp_timer_get_time() / 1000) + 3500; 
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

void OledDisplay::SetFaceState(int state) {
    DisplayGuard lock(this);
    if (anim_state_ != state) {
        anim_state_ = state;
        is_blinking_ = false;
        wink_eye_ = 0;
        next_look_ = 0; 
    }
}

void OledDisplay::UpdateTargetShape(int emote) {
    switch (emote) {
        case 0: // Netral Mochi (Lonjong lembut)
            target_w_ = 32; target_h_ = 36; target_r_ = 14; target_angle_l_ = 0; target_angle_r_ = 0; break;
        case 1: // Happy / Senyum (Menyepit melengkung)
            target_w_ = 36; target_h_ = 18; target_r_ = LV_RADIUS_CIRCLE; target_angle_l_ = 0; target_angle_r_ = 0; break; 
        case 2: // Angry (Mata tajam menyerong ke bawah)
            target_w_ = 34; target_h_ = 16; target_r_ = 4; target_angle_l_ = 250; target_angle_r_ = -250; break; 
        case 3: // Sleepy (Mata mengantuk/setengah terpejam)
            target_w_ = 34; target_h_ = 8; target_r_ = 4; target_angle_l_ = 0; target_angle_r_ = 0; break; 
        case 4: // Surprised (Mata membulat besar)
            target_w_ = 40; target_h_ = 40; target_r_ = 20; target_angle_l_ = 0; target_angle_r_ = 0; break; 
        case 5: // Sad (Mata sayu miring ke atas)
            target_w_ = 34; target_h_ = 16; target_r_ = 4; target_angle_l_ = -250; target_angle_r_ = 250; break; 
        case 6: // Confused (Bingung: Kiri besar, kanan sipit miring)
            target_w_ = 32; target_h_ = 24; target_r_ = 10; target_angle_l_ = -100; target_angle_r_ = 150; break;
        case 7: // Shocked (Terperanjat: Kotak melebar tajam)
            target_w_ = 44; target_h_ = 20; target_r_ = 2; target_angle_l_ = 0; target_angle_r_ = 0; break;
    }
}

void OledDisplay::UpdateFace() {
    if (left_eye_ == nullptr || right_eye_ == nullptr) return;
    if (esp_timer_get_time() - boot_time_us_ < 8000000LL) return; 

    int64_t now = esp_timer_get_time() / 1000;
    int bounce_y = 0;

    // 1. LOGIKA STATE & EMOSI (PRIORITAS)
    if (ai_emotion_active_ && now > ai_emotion_timeout_) {
        ai_emotion_active_ = false; 
        if (anim_state_ == 0) current_emotion_ = 0; 
    }

    if (anim_state_ == 0) {
        // === IDLE: SIFAT LEBIH HIDUP, UNPREDICTABLE & ACAK ===
        if (!ai_emotion_active_ && now > next_emote_change_) {
            int chance = esp_random() % 12;
            if (chance < 5) {
                current_emotion_ = 0; // Sering balik ke netral mochi
            } else if (chance == 5 || chance == 6) {
                current_emotion_ = 1; // Happy
            } else if (chance == 7) {
                current_emotion_ = 3; // Sleepy / Mager sebentar
            } else if (chance == 8) {
                current_emotion_ = 6; // Confused (bingung random)
            } else if (chance == 9) {
                current_emotion_ = 4; // Kaget kecil
            } else {
                current_emotion_ = (esp_random() % 2 == 0) ? 2 : 5; // Marah/Sedih sithik
            }
            // Durasi emosi acak supaya tidak monoton (1 hingga 6 detik)
            next_emote_change_ = now + 1200 + (esp_random() % 5000);
        }
        
        // Pola Lirikan Dinamis (Saccade Beragam)
        if (now > next_look_) {
            int look_style = esp_random() % 6;
            if (look_style == 0) {
                // Melengos jauh ke samping
                target_look_x_ = (esp_random() % 2 == 0) ? 12 : -12;
                target_look_y_ = (esp_random() % 5) - 2;
            } else if (look_style == 1) {
                // Nengok ke atas (penasaran)
                target_look_x_ = (esp_random() % 7) - 3;
                target_look_y_ = -6;
            } else if (look_style == 2) {
                // Tiba-tiba balik lurus
                target_look_x_ = 0;
                target_look_y_ = 0;
            } else {
                // Lirikan kecil acak
                target_look_x_ = (esp_random() % 11) - 5;
                target_look_y_ = (esp_random() % 7) - 3;
            }
            
            // Jeda antar lirik acak (bisa cepat 600ms atau santai 3.5 detik)
            next_look_ = now + 600 + (esp_random() % 3000);
        }

        // Pola Kedip & Wink Spontan
        if (!is_blinking_ && now > next_blink_) {
            is_blinking_ = true; 
            blink_start_ = now;
            
            int blink_mode = esp_random() % 10;
            if (blink_mode < 3) {
                is_double_blinking_ = true;
                next_blink_ = now + 280; // Kedip kedua kilat
            } else if (blink_mode == 4) {
                wink_eye_ = (esp_random() % 2) + 1; // Wink sebelah mata
                wink_start_ = now;
                next_blink_ = now + 5000;
            } else {
                is_double_blinking_ = false;
                next_blink_ = now + 1800 + (esp_random() % 4000); 
            }
        }
    } 
    else if (anim_state_ == 1) {
        // === LISTENING: SIAP MENDENGARKAN ===
        target_look_x_ = 0; target_look_y_ = 0;
        if (!ai_emotion_active_) current_emotion_ = 0; 
        if (!is_blinking_ && now > next_blink_) {
            is_blinking_ = true; blink_start_ = now;
            next_blink_ = now + 3500 + (esp_random() % 3000); 
        }
    } 
    else if (anim_state_ == 2) {
        // === SPEAKING: BICARA DENGAN GELOMBANG MOCHI (BOUNCE) ===
        target_look_x_ = 0; target_look_y_ = 0;
        
        float wave1 = sin(now / 35.0);
        float wave2 = sin(now / 11.0);
        bounce_y = (int)((wave1 * 0.7 + wave2 * 0.3) * 5.0);

        if (!ai_emotion_active_ && now > next_emote_change_) {
            int r = esp_random() % 4;
            current_emotion_ = (r == 0) ? 1 : ((r == 1) ? 0 : ((r == 2) ? 4 : 6)); 
            next_emote_change_ = now + 800 + (esp_random() % 1200);
        }
    }

    // 2. LERP (TRANSISI KENYAL / ELASTIS ALA MOCHI)
    UpdateTargetShape(current_emotion_);
    
    // Kecepatan lerp dibuat sedikit lebih kenyal (0.25f)
    current_w_ += (target_w_ - current_w_) * 0.25f;
    current_h_ += (target_h_ - current_h_) * 0.25f;
    current_r_ += (target_r_ - current_r_) * 0.25f;
    current_angle_l_ += (target_angle_l_ - current_angle_l_) * 0.25f;
    current_angle_r_ += (target_angle_r_ - current_angle_r_) * 0.25f;

    // 3. KEDIP & WINK
    float blink_mult = 1.0f;
    float wink_l_mult = 1.0f;
    float wink_r_mult = 1.0f;

    if (is_blinking_) {
        int64_t elapsed = now - blink_start_;
        if (elapsed < 50) blink_mult = 1.0f - ((float)elapsed / 50.0f);
        else if (elapsed < 120) blink_mult = (float)(elapsed - 50) / 70.0f;
        else is_blinking_ = false;
    }

    if (wink_eye_ != 0) {
        int64_t elapsed = now - wink_start_;
        if (elapsed < 180) {
            if (wink_eye_ == 1) wink_l_mult = 0.15f;
            if (wink_eye_ == 2) wink_r_mult = 0.15f;
        } else {
            wink_eye_ = 0;
        }
    }

    int final_lh = (int)(current_h_ * blink_mult * wink_l_mult);
    int final_rh = (int)(current_h_ * blink_mult * wink_r_mult);

    // 4. LIRIK & FLOATING HALUS
    look_x_ += (target_look_x_ - look_x_) * 0.55f;
    look_y_ += (target_look_y_ - look_y_) * 0.55f;
    
    if (anim_state_ == 0 && !is_blinking_) {
        float_y_ = sin(now / 700.0) * 1.8f; // Efek nafas / melayang mochi
    } else {
        float_y_ *= 0.85f; 
    }
    
    int ox = std::clamp((int)look_x_, -9, 9);
    int oy = std::clamp((int)look_y_, -5, 5);
    int final_oy = oy + bounce_y + (int)float_y_;

    // 5. RENDER KE LVGL
    int draw_w = (int)current_w_;
    int draw_r = (int)current_r_;
    int draw_al = (int)current_angle_l_;
    int draw_ar = (int)current_angle_r_;

    if (std::abs(draw_w - last_drawn_w_) > 0 || 
        std::abs(final_lh - last_drawn_lh_) > 0 || 
        std::abs(final_rh - last_drawn_rh_) > 0) {
        
        lv_obj_set_size(left_eye_, draw_w, final_lh);
        lv_obj_set_size(right_eye_, draw_w, final_rh);
        lv_obj_set_style_radius(left_eye_, draw_r, 0);
        lv_obj_set_style_radius(right_eye_, draw_r, 0);
        
        lv_obj_set_style_transform_pivot_x(left_eye_, draw_w/2, 0);
        lv_obj_set_style_transform_pivot_y(left_eye_, final_lh/2, 0);
        lv_obj_set_style_transform_pivot_x(right_eye_, draw_w/2, 0);
        lv_obj_set_style_transform_pivot_y(right_eye_, final_rh/2, 0);

        last_drawn_w_ = draw_w;
        last_drawn_lh_ = final_lh;
        last_drawn_rh_ = final_rh;
    }

    if (std::abs(draw_al - last_drawn_angle_l_) > 0 || 
        std::abs(draw_ar - last_drawn_angle_r_) > 0) {
        
        lv_obj_set_style_transform_angle(left_eye_, draw_al, 0);
        lv_obj_set_style_transform_angle(right_eye_, draw_ar, 0);
        
        last_drawn_angle_l_ = draw_al;
        last_drawn_angle_r_ = draw_ar;
    }

    if (ox != last_drawn_ox_ || final_oy != last_drawn_oy_) {
        lv_obj_align(left_eye_, LV_ALIGN_CENTER, -eye_offset_ + ox, -2 + final_oy);
        lv_obj_align(right_eye_, LV_ALIGN_CENTER, eye_offset_ + ox, -2 + final_oy);
        
        last_drawn_ox_ = ox;
        last_drawn_oy_ = final_oy;

        // PARALLAX PUPIL
        int pupil_ox = std::clamp((int)(ox * -0.3f), -2, 2);
        int pupil_oy = std::clamp((int)(final_oy * -0.3f), -2, 2);
        lv_obj_align(left_pupil_, LV_ALIGN_CENTER, pupil_ox, pupil_oy);
        lv_obj_align(right_pupil_, LV_ALIGN_CENTER, pupil_ox, pupil_oy);
    }
}
