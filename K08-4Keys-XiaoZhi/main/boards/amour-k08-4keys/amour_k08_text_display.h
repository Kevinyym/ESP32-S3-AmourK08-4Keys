#pragma once

#include "display/lcd_display.h"

class AmourK08TextDisplay : public SpiLcdDisplay {
public:
    AmourK08TextDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                        int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                        bool swap_xy);

    void SetupUI() override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetEmotion(const char* emotion) override;
    void SetTheme(Theme* theme) override;
    void ShowRadioPlayer(const char* station, const char* category, bool connecting, int station_number,
                         int station_count, bool favorite);
    void HideRadioPlayer();
    void ShowNasMusicPlayer(const char* title, bool buffering);
    void HideNasMusicPlayer();

private:
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    void ApplyCompactLayoutLocked();
    void CompactLatestMessageLocked();
    void UpdateIdleRobotLocked();
    void CreateRadioPlayerLocked();
    void UpdateRadioPlayerThemeLocked();
    void CreateNasMusicPlayerLocked();
    void UpdateNasMusicPlayerThemeLocked();

    lv_obj_t* radio_view_ = nullptr;
    lv_obj_t* radio_state_label_ = nullptr;
    lv_obj_t* radio_title_label_ = nullptr;
    lv_obj_t* radio_category_label_ = nullptr;
    lv_obj_t* radio_meta_label_ = nullptr;
    lv_obj_t* radio_live_label_ = nullptr;
    lv_obj_t* radio_hint_label_ = nullptr;
    lv_obj_t* radio_divider_ = nullptr;
    lv_obj_t* radio_title_card_ = nullptr;

    lv_obj_t* nas_music_view_ = nullptr;
    lv_obj_t* nas_music_state_label_ = nullptr;
    lv_obj_t* nas_music_source_label_ = nullptr;
    lv_obj_t* nas_music_title_label_ = nullptr;
    lv_obj_t* nas_music_library_label_ = nullptr;
    lv_obj_t* nas_music_format_label_ = nullptr;
    lv_obj_t* nas_music_hint_label_ = nullptr;
    lv_obj_t* nas_music_divider_ = nullptr;
    lv_obj_t* nas_music_title_card_ = nullptr;
#endif
};
