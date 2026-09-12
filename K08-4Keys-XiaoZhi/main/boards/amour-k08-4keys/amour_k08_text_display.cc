#include "amour_k08_text_display.h"

#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <material_symbols.h>

#include <cstring>

#define TAG "AmourK08Display"

namespace {
constexpr lv_coord_t kTopBarHeight = 24;
constexpr lv_coord_t kContentPadding = 4;
constexpr lv_coord_t kMessageGap = 4;
constexpr lv_coord_t kBubbleHorizontalPadding = 6;
constexpr lv_coord_t kBubbleVerticalPadding = 4;
constexpr lv_coord_t kBubbleRadius = 6;
constexpr lv_coord_t kBaseLongTextWidth = 188;
constexpr lv_coord_t kCompactLongTextWidth = 204;
}  // namespace

AmourK08TextDisplay::AmourK08TextDisplay(esp_lcd_panel_io_handle_t panel_io,
                                         esp_lcd_panel_handle_t panel, int width, int height,
                                         int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                         bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

void AmourK08TextDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    DisplayLockGuard lock(this);
    ApplyCompactLayoutLocked();
    UpdateIdleRobotLocked();
#endif
}

void AmourK08TextDisplay::SetChatMessage(const char* role, const char* content) {
    SpiLcdDisplay::SetChatMessage(role, content);

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    DisplayLockGuard lock(this);
    CompactLatestMessageLocked();
    UpdateIdleRobotLocked();

    if (content_ != nullptr && lv_obj_get_child_cnt(content_) > 0) {
        lv_obj_update_layout(lv_screen_active());
        auto* latest_message = lv_obj_get_child(content_, lv_obj_get_child_cnt(content_) - 1);
        if (latest_message != nullptr && lv_obj_is_valid(latest_message)) {
            lv_obj_scroll_to_view_recursive(latest_message, LV_ANIM_OFF);
        }
    }
#endif
}

void AmourK08TextDisplay::ClearChatMessages() {
    SpiLcdDisplay::ClearChatMessages();

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    DisplayLockGuard lock(this);
    UpdateIdleRobotLocked();
#endif
}

void AmourK08TextDisplay::SetEmotion(const char* emotion) {
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (!setup_ui_called_) {
        ESP_LOGW(TAG, "SetEmotion('%s') called before SetupUI() - emotion will not be displayed!",
                 emotion);
        return;
    }

    DisplayLockGuard lock(this);
    UpdateIdleRobotLocked();
#else
    SpiLcdDisplay::SetEmotion(emotion);
#endif
}

void AmourK08TextDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    DisplayLockGuard lock(this);
    ApplyCompactLayoutLocked();
    UpdateIdleRobotLocked();
#endif
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void AmourK08TextDisplay::ApplyCompactLayoutLocked() {
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);

    if (top_bar_ != nullptr) {
        lv_obj_set_size(top_bar_, LV_HOR_RES, kTopBarHeight);
        lv_obj_set_style_pad_top(top_bar_, 2, 0);
        lv_obj_set_style_pad_bottom(top_bar_, 2, 0);
        lv_obj_set_style_pad_left(top_bar_, 6, 0);
        lv_obj_set_style_pad_right(top_bar_, 6, 0);
    }
    if (status_bar_ != nullptr) {
        lv_obj_set_size(status_bar_, LV_HOR_RES, kTopBarHeight);
        lv_obj_set_style_pad_top(status_bar_, 2, 0);
        lv_obj_set_style_pad_bottom(status_bar_, 2, 0);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    }
    if (content_ != nullptr) {
        lv_obj_set_style_pad_all(content_, kContentPadding, 0);
        lv_obj_set_style_pad_row(content_, kMessageGap, 0);
        lv_obj_set_style_bg_color(content_, lvgl_theme->chat_background_color(), 0);
        lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    }
}

void AmourK08TextDisplay::CompactLatestMessageLocked() {
    if (chat_message_label_ == nullptr || !lv_obj_is_valid(chat_message_label_)) {
        return;
    }

    auto* bubble = lv_obj_get_parent(chat_message_label_);
    if (bubble == nullptr || !lv_obj_is_valid(bubble)) {
        return;
    }

    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    if (lv_obj_get_width(chat_message_label_) >= kBaseLongTextWidth) {
        lv_obj_set_width(chat_message_label_, kCompactLongTextWidth);
    }

    lv_obj_set_style_pad_left(bubble, kBubbleHorizontalPadding, 0);
    lv_obj_set_style_pad_right(bubble, kBubbleHorizontalPadding, 0);
    lv_obj_set_style_pad_top(bubble, kBubbleVerticalPadding, 0);
    lv_obj_set_style_pad_bottom(bubble, kBubbleVerticalPadding, 0);
    lv_obj_set_style_radius(bubble, kBubbleRadius, 0);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);

    const auto* role = static_cast<const char*>(lv_obj_get_user_data(bubble));
    auto* message_container = lv_obj_get_parent(bubble);
    if (role != nullptr && message_container != nullptr && message_container != content_) {
        lv_obj_set_width(message_container, lv_pct(100));
        if (strcmp(role, "user") == 0) {
            lv_obj_align(bubble, LV_ALIGN_RIGHT_MID, 0, 0);
        } else if (strcmp(role, "system") == 0) {
            lv_obj_align(bubble, LV_ALIGN_CENTER, 0, 0);
        }
    } else {
        lv_obj_align(bubble, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

void AmourK08TextDisplay::UpdateIdleRobotLocked() {
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    if (emoji_image_ != nullptr) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_label_ == nullptr) {
        return;
    }

    const bool has_messages = content_ != nullptr && lv_obj_get_child_cnt(content_) > 0;
    if (has_messages) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    lv_obj_set_style_text_font(emoji_label_, lvgl_theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emoji_label_);
    lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
}
#endif
