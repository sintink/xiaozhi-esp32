#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

lv_obj_t* face_container_ = nullptr;
lv_obj_t* left_eye_ = nullptr;
lv_obj_t* right_eye_ = nullptr;
lv_obj_t* mouth_ = nullptr;
lv_timer_t* face_timer_ = nullptr;

int eye_size = 18;
int eye_offset = 30;
int current_look_x = 0;
int current_look_y = 0;
int target_look_x = 0;
int target_look_y = 0;
bool is_blinking = false;
unsigned long blink_start = 0;
unsigned long next_blink = 3000;
unsigned long next_look = 4000;

void UpdateFace();

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
};

#endif  // OLED_DISPLAY_H
