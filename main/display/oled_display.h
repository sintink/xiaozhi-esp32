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
    lv_timer_t* face_timer_ = nullptr;

    int64_t boot_time_us_ = 0;

    // Variabel Animasi (Diperbesar)
    int eye_offset_ = 32; 
    
    float look_x_ = 0;
    float look_y_ = 0;
    int target_look_x_ = 0;
    int target_look_y_ = 0;

    bool is_blinking_ = false;
    int64_t blink_start_ = 0;
    int64_t next_blink_ = 0;
    int64_t next_look_ = 0;

    int current_emotion_ = 0; 
    int64_t next_emote_change_ = 0;
    
    int wink_eye_ = 0; 
    int64_t wink_start_ = 0;
    int64_t next_wink_ = 0;

    // STATE ANIMASI (0=Idle, 1=Listening, 2=Speaking)
    int anim_state_ = 0; 

    // Optimasi Redraw
    int last_w_ = 0;
    int last_lh_ = 0;
    int last_rh_ = 0;
    int last_ox_ = 0;
    int last_oy_ = 0;
    int last_angle_l_ = 0;
    int last_angle_r_ = 0;
    int last_radius_ = 0;

    void InitEyes();
    void UpdateFace();
    void ApplyExpression(int emote, int breath, int bounce_y);
    void SetupUI_128x64();

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override {}
    virtual void ClearChatMessages() override {}
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
    
    virtual void SetFaceState(int state) override; 
};

#endif  // OLED_DISPLAY_H
