#include "wifi_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "led/circular_strip.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

#include <algorithm>
#include <mutex>
#include <string>

#define TAG "AmourK08"

class PaControlledAudioCodec : public NoAudioCodecSimplex {
public:
    PaControlledAudioCodec()
        : NoAudioCodecSimplex(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                              AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                              AUDIO_I2S_SPK_GPIO_DOUT, I2S_STD_SLOT_LEFT, AUDIO_I2S_MIC_GPIO_SCK,
                              AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN, I2S_STD_SLOT_LEFT) {
        gpio_config_t io_config = {};
        io_config.pin_bit_mask = 1ULL << AUDIO_CODEC_PA_PIN;
        io_config.mode = GPIO_MODE_OUTPUT;
        io_config.pull_up_en = GPIO_PULLUP_DISABLE;
        io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_config));
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
    }

    void EnableOutput(bool enable) override {
        // This mutex serializes the complete I2S/PA transition. The base codec
        // uses its own data mutex, so do not reuse it here.
        std::lock_guard<std::mutex> lock(output_state_mutex_);
        if (enable == output_enabled_) {
            return;
        }

        if (enable) {
            NoAudioCodecSimplex::EnableOutput(true);
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 1));
        } else {
            ESP_ERROR_CHECK(gpio_set_level(AUDIO_CODEC_PA_PIN, 0));
            NoAudioCodecSimplex::EnableOutput(false);
        }
    }

private:
    std::mutex output_state_mutex_;
};

class AmourK08FourKeysBoard : public WifiBoard {
public:
    AmourK08FourKeysBoard()
        : mode_button_(MODE_BUTTON_GPIO, false, 2000),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeRedLed();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static PaControlledAudioCodec audio_codec;
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    Led* GetLed() override {
        static CircularStrip led(RGB_LED_GPIO, RGB_LED_COUNT);
        return &led;
    }

private:
    Button mode_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* display_ = nullptr;

    void InitializeRedLed() {
        gpio_config_t io_config = {};
        io_config.pin_bit_mask = 1ULL << RED_LED_GPIO;
        io_config.mode = GPIO_MODE_OUTPUT;
        io_config.pull_up_en = GPIO_PULLUP_DISABLE;
        io_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_config.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_config));
        ESP_ERROR_CHECK(gpio_set_level(RED_LED_GPIO, 0));
    }

    void InitializeSpi() {
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = DISPLAY_MOSI_PIN;
        bus_config.miso_io_num = GPIO_NUM_NC;
        bus_config.sclk_io_num = DISPLAY_CLK_PIN;
        bus_config.quadwp_io_num = GPIO_NUM_NC;
        bus_config.quadhd_io_num = GPIO_NUM_NC;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                     DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        mode_button_.OnClick([]() {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() { app.ToggleChatState(); });
        });
        mode_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            app.Schedule([this]() { EnterWifiConfigMode(); });
        });

        volume_up_button_.OnClick([this]() { ScheduleVolumeChange(10); });
        volume_up_button_.OnLongPress([this]() { ScheduleVolumeSet(100); });
        volume_down_button_.OnClick([this]() { ScheduleVolumeChange(-10); });
        volume_down_button_.OnLongPress([this]() { ScheduleVolumeSet(0); });
    }

    void ScheduleVolumeChange(int delta) {
        auto& app = Application::GetInstance();
        app.Schedule([this, delta]() {
            auto* codec = GetAudioCodec();
            const int volume = std::clamp(codec->output_volume() + delta, 0, 100);
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
    }

    void ScheduleVolumeSet(int volume) {
        auto& app = Application::GetInstance();
        app.Schedule([this, volume]() {
            GetAudioCodec()->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(volume == 0 ? Lang::Strings::MUTED
                                                       : Lang::Strings::MAX_VOLUME);
        });
    }
};

DECLARE_BOARD(AmourK08FourKeysBoard);
