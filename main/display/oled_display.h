#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;

    // Wajah
    lv_obj_t* face_container_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* z_label_ = nullptr;
    lv_obj_t* subtitle_label_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;

    // Ukuran mata (mochi)
    static constexpr int kEyeSize = 20;
    static constexpr int kEyeOffset = 28;

    // Posisi lirik
    float look_x_ = 0.f;
    float look_y_ = 0.f;
    int target_look_x_ = 0;
    int target_look_y_ = 0;

    // State
    int face_state_ = 0;   // 0=idle, 1=listening, 2=speaking
    int sleep_state_ = 0;  // 0=normal, 1=ngantuk, 2=tidur, 3=kaget

    // Timer (pakai int64_t agar tidak overflow)
    int64_t last_activity_us_ = 0;
    int64_t next_blink_us_ = 0;
    int64_t blink_start_us_ = 0;
    int64_t next_look_us_ = 0;
    bool is_blinking_ = false;

    void InitEyes();
    void UpdateFace();
    void ApplyEyeExpression(int expression);  // 0=normal,1=happy,2=sleepy,3=sleep,4=surprised

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
    void SetFaceState(int state) override;
};

#endif  // OLED_DISPLAY_H
