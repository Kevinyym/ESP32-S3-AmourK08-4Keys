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

private:
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    void ApplyCompactLayoutLocked();
    void CompactLatestMessageLocked();
    void UpdateIdleRobotLocked();
#endif
};
