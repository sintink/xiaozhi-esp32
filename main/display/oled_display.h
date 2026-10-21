#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <cstdint>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_obj_t* notification_label_ = nullptr;

    lv_obj_t* face_container_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* left_pupil_ = nullptr;   // Tambahan titik fokus
    lv_obj_t* right_pupil_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;

    int64_t boot_time_us_ = 0;

    // Posisi Dasar & Lirik
    int eye_offset_ = 30; 
    float look_x_ = 0;
    float look_y_ = 0;
    int target_look_x_ = 0;
    int target_look_y_ = 0;

    // State Animation
    int anim_state_ = 0;      
    int current_emotion_ = 0; 

    // AI Emotion Priority
    bool ai_emotion_active_ = false;
    int64_t ai_emotion_timeout_ = 0;

    // Kedipan
    bool is_blinking_ = false;
    bool is_double_blinking_ = false; // Fitur kedip 2x
    int64_t blink_start_ = 0;
    int64_t next_blink_ = 0;
    int wink_eye_ = 0; 
    int64_t wink_start_ = 0;
    int64_t next_wink_ = 0;

    // Timer Idle
    int64_t next_emote_change_ = 0;
    int64_t next_look_ = 0;

    // Sistem Lerp
    float target_w_ = 36, target_h_ = 36, target_r_ = 6;
    float current_w_ = 36, current_h_ = 36, current_r_ = 6;
    int target_angle_l_ = 0, target_angle_r_ = 0;
    float current_angle_l_ = 0, current_angle_r_ = 0;
    
    // Floating (Naik-turun halus)
    float float_y_ = 0;

    // Optimasi Redraw
    int last_drawn_w_ = 0;
    int last_drawn_lh_ = 0;
    int last_drawn_rh_ = 0;
    int last_drawn_ox_ = 0;
    int last_drawn_oy_ = 0;
    int last_drawn_angle_l_ = 0;
    int last_drawn_angle_r_ = 0;

    void InitEyes();
    void UpdateFace();
    void UpdateTargetShape(int emote);
    void SetupUI_128x64();

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override ;
    virtual void ClearChatMessages() override ;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
    
    virtual void SetFaceState(int state) override; 
};

#endif  // OLED_DISPLAY_H
