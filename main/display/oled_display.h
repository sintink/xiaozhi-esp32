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
    lv_obj_t* subtitle_label_ = nullptr;
    lv_timer_t* face_timer_ = nullptr;

    // Variabel Animasi
    int eye_size_ = 18;
    int eye_offset_ = 28;
    
    float look_x_ = 0;
    float look_y_ = 0;
    int target_look_x_ = 0;
    int target_look_y_ = 0;

    bool is_blinking_ = false;
    unsigned long blink_start_ = 0;
    unsigned long next_blink_ = 3000;
    unsigned long next_look_ = 4000;

    int current_emotion_ = 0; // 0=Normal, 1=Happy, 2=Sleepy, 3=Surprised, 4=Angry

    void InitEyes();
    void UpdateFace();
    void ApplyEyeShape(int emotion);

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
