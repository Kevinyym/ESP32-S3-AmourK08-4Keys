#include "amour_k08_text_display.h"

#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <material_symbols.h>

#include <cstring>
#include <string>

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
    CreateRadioPlayerLocked();
    CreateNasMusicPlayerLocked();
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
    UpdateRadioPlayerThemeLocked();
    UpdateNasMusicPlayerThemeLocked();
    UpdateIdleRobotLocked();
#endif
}

void AmourK08TextDisplay::ShowRadioPlayer(const char* station, const char* category, bool connecting,
                                          int station_number, int station_count, bool favorite) {
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    CreateRadioPlayerLocked();
    if (radio_view_ == nullptr) {
        return;
    }
    lv_label_set_text(radio_state_label_, connecting ? "正在连接" : "正在播放");
    lv_label_set_text(radio_title_label_, station != nullptr ? station : "网络电台");
    std::string category_text = "分类：";
    category_text += category != nullptr ? category : "音乐";
    lv_label_set_text(radio_category_label_, category_text.c_str());
    std::string meta_text = "预置 " + std::to_string(station_number) + " / " +
                            std::to_string(station_count);
    if (favorite) {
        meta_text += " · 已收藏";
    }
    lv_label_set_text(radio_meta_label_, meta_text.c_str());
    if (nas_music_view_ != nullptr) {
        lv_obj_add_flag(nas_music_view_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(radio_view_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(radio_view_);
#else
    (void)station;
    (void)category;
    (void)connecting;
    (void)station_number;
    (void)station_count;
    (void)favorite;
#endif
}

void AmourK08TextDisplay::ShowNasMusicPlayer(const char* title, bool buffering) {
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    CreateNasMusicPlayerLocked();
    if (nas_music_view_ == nullptr) {
        return;
    }
    lv_label_set_text(nas_music_state_label_, buffering ? "正在缓冲" : "正在播放");
    lv_label_set_text(nas_music_title_label_, title != nullptr ? title : "NAS 本地音乐");
    if (radio_view_ != nullptr) {
        lv_obj_add_flag(radio_view_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(nas_music_view_, LV_OBJ_FLAG_HIDDEN);
    // Subtitle updates can create or refresh chat widgets while a song is
    // opening. Keep the dedicated player above those widgets for its whole
    // playback lifecycle.
    lv_obj_move_foreground(nas_music_view_);
#else
    (void)title;
    (void)buffering;
#endif
}

void AmourK08TextDisplay::HideNasMusicPlayer() {
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (nas_music_view_ != nullptr) {
        lv_obj_add_flag(nas_music_view_, LV_OBJ_FLAG_HIDDEN);
    }
    UpdateIdleRobotLocked();
#endif
}

void AmourK08TextDisplay::HideRadioPlayer() {
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
    if (!setup_ui_called_) {
        return;
    }
    DisplayLockGuard lock(this);
    if (radio_view_ != nullptr) {
        lv_obj_add_flag(radio_view_, LV_OBJ_FLAG_HIDDEN);
    }
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
    auto emoji_collection = lvgl_theme->emoji_collection();
    auto image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage("neutral") : nullptr;
    if (image != nullptr && emoji_image_ != nullptr) {
        // K08 has a 240x240 display. A 128px bitmap is prominent in standby
        // without taking space away from the text-first conversation layout.
        lv_image_set_src(emoji_image_, image->image_dsc());
        lv_obj_center(emoji_image_);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Keep the built-in robot as a fallback if a custom asset package does
    // not include the neutral bitmap.
    lv_obj_set_style_text_font(emoji_label_, lvgl_theme->large_icon_font()->font(), 0);
    lv_obj_set_style_text_color(emoji_label_, lvgl_theme->text_color(), 0);
    lv_label_set_text(emoji_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emoji_label_);
    lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
}

void AmourK08TextDisplay::CreateRadioPlayerLocked() {
    if (radio_view_ != nullptr) {
        return;
    }

    auto* screen = lv_screen_active();
    radio_view_ = lv_obj_create(screen);
    lv_obj_set_size(radio_view_, LV_HOR_RES, LV_VER_RES - kTopBarHeight);
    lv_obj_align(radio_view_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(radio_view_, 0, 0);
    lv_obj_set_style_border_width(radio_view_, 0, 0);
    lv_obj_set_style_pad_all(radio_view_, 0, 0);
    lv_obj_set_scrollbar_mode(radio_view_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(radio_view_, LV_DIR_NONE);

    radio_state_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_state_label_, LV_HOR_RES - 24);
    lv_obj_set_style_text_align(radio_state_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(radio_state_label_, LV_ALIGN_TOP_MID, 0, 10);

    radio_live_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_live_label_, LV_HOR_RES - 24);
    lv_obj_set_style_text_align(radio_live_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(radio_live_label_, "网络直播 · 电台");
    lv_obj_align(radio_live_label_, LV_ALIGN_TOP_MID, 0, 36);

    radio_title_card_ = lv_obj_create(radio_view_);
    lv_obj_set_size(radio_title_card_, LV_HOR_RES - 28, 46);
    lv_obj_set_style_radius(radio_title_card_, 8, 0);
    lv_obj_set_style_pad_all(radio_title_card_, 0, 0);
    lv_obj_set_scrollbar_mode(radio_title_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(radio_title_card_, LV_ALIGN_TOP_MID, 0, 66);

    radio_title_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_title_label_, LV_HOR_RES - 28);
    lv_obj_set_style_text_align(radio_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    // All preloaded station names fit in this title card. Avoid a continuously
    // scrolling label, which is distracting during long listening sessions.
    lv_label_set_long_mode(radio_title_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(radio_title_label_, "网络电台");
    lv_obj_set_height(radio_title_label_, 24);
    lv_obj_set_style_transform_scale(radio_title_label_, 288, 0);
    lv_obj_align(radio_title_label_, LV_ALIGN_TOP_MID, 0, 78);

    radio_category_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_category_label_, LV_HOR_RES - 32);
    lv_obj_set_style_text_align(radio_category_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(radio_category_label_, "分类：音乐");
    lv_obj_align(radio_category_label_, LV_ALIGN_TOP_MID, 0, 122);

    radio_meta_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_meta_label_, LV_HOR_RES - 32);
    lv_obj_set_style_text_align(radio_meta_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(radio_meta_label_, "预置 1 / 31");
    lv_obj_align(radio_meta_label_, LV_ALIGN_TOP_MID, 0, 145);

    radio_divider_ = lv_obj_create(radio_view_);
    lv_obj_set_size(radio_divider_, LV_HOR_RES - 48, 1);
    lv_obj_set_style_radius(radio_divider_, 0, 0);
    lv_obj_set_style_border_width(radio_divider_, 0, 0);
    lv_obj_set_style_pad_all(radio_divider_, 0, 0);
    lv_obj_align(radio_divider_, LV_ALIGN_TOP_MID, 0, 170);

    radio_hint_label_ = lv_label_create(radio_view_);
    lv_obj_set_width(radio_hint_label_, LV_HOR_RES - 28);
    lv_obj_set_style_text_align(radio_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(radio_hint_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(radio_hint_label_, "长按 + / - 切台 · 模式键停止");
    lv_obj_align(radio_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -12);

    UpdateRadioPlayerThemeLocked();
    lv_obj_add_flag(radio_view_, LV_OBJ_FLAG_HIDDEN);
}

void AmourK08TextDisplay::UpdateRadioPlayerThemeLocked() {
    if (radio_view_ == nullptr || current_theme_ == nullptr) {
        return;
    }
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const auto text_font = lvgl_theme->text_font()->font();
    const auto text_color = lvgl_theme->text_color();
    lv_obj_set_style_bg_color(radio_view_, lvgl_theme->chat_background_color(), 0);
    lv_obj_set_style_bg_opa(radio_view_, LV_OPA_COVER, 0);
    for (auto* label : {radio_state_label_, radio_live_label_, radio_title_label_,
                        radio_category_label_, radio_meta_label_, radio_hint_label_}) {
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
    }
    // The theme accent can be a bright green, which is uncomfortable and has poor
    // contrast on the K08 radio page. Keep this secondary label deliberately muted.
    lv_obj_set_style_text_color(radio_live_label_, text_color, 0);
    lv_obj_set_style_text_opa(radio_live_label_, LV_OPA_60, 0);
    lv_obj_set_style_text_opa(radio_category_label_, LV_OPA_80, 0);
    lv_obj_set_style_text_opa(radio_meta_label_, LV_OPA_60, 0);
    lv_obj_set_style_text_opa(radio_hint_label_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(radio_title_card_, lvgl_theme->chat_background_color(), 0);
    lv_obj_set_style_bg_opa(radio_title_card_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(radio_title_card_, lvgl_theme->border_color(), 0);
    lv_obj_set_style_border_opa(radio_title_card_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(radio_title_card_, 1, 0);
    lv_obj_set_style_bg_color(radio_divider_, lvgl_theme->border_color(), 0);
    lv_obj_set_style_bg_opa(radio_divider_, LV_OPA_COVER, 0);
}

void AmourK08TextDisplay::CreateNasMusicPlayerLocked() {
    if (nas_music_view_ != nullptr) {
        return;
    }

    auto* screen = lv_screen_active();
    nas_music_view_ = lv_obj_create(screen);
    lv_obj_set_size(nas_music_view_, LV_HOR_RES, LV_VER_RES - kTopBarHeight);
    lv_obj_align(nas_music_view_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(nas_music_view_, 0, 0);
    lv_obj_set_style_border_width(nas_music_view_, 0, 0);
    lv_obj_set_style_pad_all(nas_music_view_, 0, 0);
    lv_obj_set_scrollbar_mode(nas_music_view_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(nas_music_view_, LV_DIR_NONE);

    nas_music_state_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_state_label_, LV_HOR_RES - 24);
    lv_obj_set_style_text_align(nas_music_state_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nas_music_state_label_, LV_ALIGN_TOP_MID, 0, 10);

    nas_music_source_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_source_label_, LV_HOR_RES - 24);
    lv_obj_set_style_text_align(nas_music_source_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(nas_music_source_label_, "NAS 本地音乐");
    lv_obj_align(nas_music_source_label_, LV_ALIGN_TOP_MID, 0, 36);

    nas_music_title_card_ = lv_obj_create(nas_music_view_);
    lv_obj_set_size(nas_music_title_card_, LV_HOR_RES - 28, 46);
    lv_obj_set_style_radius(nas_music_title_card_, 8, 0);
    lv_obj_set_style_pad_all(nas_music_title_card_, 0, 0);
    lv_obj_set_scrollbar_mode(nas_music_title_card_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(nas_music_title_card_, LV_ALIGN_TOP_MID, 0, 66);

    nas_music_title_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_title_label_, LV_HOR_RES - 28);
    lv_obj_set_height(nas_music_title_label_, 24);
    lv_obj_set_style_text_align(nas_music_title_label_, LV_TEXT_ALIGN_CENTER, 0);
    // Keep long filenames static and clipped: a continuously rolling title is
    // distracting while listening and gives no stable reading point.
    lv_label_set_long_mode(nas_music_title_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(nas_music_title_label_, "NAS 本地音乐");
    lv_obj_set_style_transform_scale(nas_music_title_label_, 288, 0);
    lv_obj_align(nas_music_title_label_, LV_ALIGN_TOP_MID, 0, 78);

    nas_music_library_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_library_label_, LV_HOR_RES - 32);
    lv_obj_set_style_text_align(nas_music_library_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(nas_music_library_label_, "DS923+ · 本地曲库");
    lv_obj_align(nas_music_library_label_, LV_ALIGN_TOP_MID, 0, 122);

    nas_music_format_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_format_label_, LV_HOR_RES - 32);
    lv_obj_set_style_text_align(nas_music_format_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(nas_music_format_label_, "Ogg Opus · 局域网播放");
    lv_obj_align(nas_music_format_label_, LV_ALIGN_TOP_MID, 0, 145);

    nas_music_divider_ = lv_obj_create(nas_music_view_);
    lv_obj_set_size(nas_music_divider_, LV_HOR_RES - 48, 1);
    lv_obj_set_style_radius(nas_music_divider_, 0, 0);
    lv_obj_set_style_border_width(nas_music_divider_, 0, 0);
    lv_obj_set_style_pad_all(nas_music_divider_, 0, 0);
    lv_obj_align(nas_music_divider_, LV_ALIGN_TOP_MID, 0, 170);

    nas_music_hint_label_ = lv_label_create(nas_music_view_);
    lv_obj_set_width(nas_music_hint_label_, LV_HOR_RES - 28);
    lv_obj_set_style_text_align(nas_music_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(nas_music_hint_label_, LV_LABEL_LONG_CLIP);
    lv_label_set_text(nas_music_hint_label_, "长按 + / - 切歌 · 模式键停止");
    lv_obj_align(nas_music_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -12);

    UpdateNasMusicPlayerThemeLocked();
    lv_obj_add_flag(nas_music_view_, LV_OBJ_FLAG_HIDDEN);
}

void AmourK08TextDisplay::UpdateNasMusicPlayerThemeLocked() {
    if (nas_music_view_ == nullptr || current_theme_ == nullptr) {
        return;
    }
    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const auto text_font = lvgl_theme->text_font()->font();
    const auto text_color = lvgl_theme->text_color();
    lv_obj_set_style_bg_color(nas_music_view_, lvgl_theme->chat_background_color(), 0);
    lv_obj_set_style_bg_opa(nas_music_view_, LV_OPA_COVER, 0);
    for (auto* label : {nas_music_state_label_, nas_music_source_label_, nas_music_title_label_,
                        nas_music_library_label_, nas_music_format_label_, nas_music_hint_label_}) {
        lv_obj_set_style_text_font(label, text_font, 0);
        lv_obj_set_style_text_color(label, text_color, 0);
    }
    lv_obj_set_style_text_opa(nas_music_source_label_, LV_OPA_60, 0);
    lv_obj_set_style_text_opa(nas_music_library_label_, LV_OPA_80, 0);
    lv_obj_set_style_text_opa(nas_music_format_label_, LV_OPA_60, 0);
    lv_obj_set_style_text_opa(nas_music_hint_label_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(nas_music_title_card_, lvgl_theme->chat_background_color(), 0);
    lv_obj_set_style_bg_opa(nas_music_title_card_, LV_OPA_30, 0);
    lv_obj_set_style_border_color(nas_music_title_card_, lvgl_theme->border_color(), 0);
    lv_obj_set_style_border_opa(nas_music_title_card_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(nas_music_title_card_, 1, 0);
    lv_obj_set_style_bg_color(nas_music_divider_, lvgl_theme->border_color(), 0);
    lv_obj_set_style_bg_opa(nas_music_divider_, LV_OPA_COVER, 0);
}
#endif
