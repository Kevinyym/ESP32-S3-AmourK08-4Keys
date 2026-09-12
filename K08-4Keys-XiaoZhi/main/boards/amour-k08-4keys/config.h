#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <esp_lcd_types.h>

// Audio: the microphone and speaker use separate ESP32-S3 I2S controllers.
#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_39
#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_41
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_40

#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_5
#define AUDIO_CODEC_PA_PIN GPIO_NUM_4

// Buttons are active-low. The fourth physical key controls the IP5306 directly.
#define MODE_BUTTON_GPIO GPIO_NUM_14
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_9
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_21

// Indicators.
#define RGB_LED_GPIO GPIO_NUM_48
#define RGB_LED_COUNT 4
#define RED_LED_GPIO GPIO_NUM_47

// ST7789 240x240 display on SPI2, without a chip-select signal.
#define DISPLAY_SPI_HOST SPI2_HOST
#define DISPLAY_CLK_PIN GPIO_NUM_8
#define DISPLAY_MOSI_PIN GPIO_NUM_18
#define DISPLAY_DC_PIN GPIO_NUM_16
#define DISPLAY_RST_PIN GPIO_NUM_17
#define DISPLAY_CS_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_15

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR true
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X 80
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Reserved expansion headers. They are intentionally not initialized yet.
#define EXPANSION_I2C_SDA_PIN GPIO_NUM_1
#define EXPANSION_I2C_SCL_PIN GPIO_NUM_2
#define TF_CARD_SCK_PIN GPIO_NUM_12
#define TF_CARD_MOSI_PIN GPIO_NUM_11
#define TF_CARD_MISO_PIN GPIO_NUM_13
#define TF_CARD_CS_PIN GPIO_NUM_10

#endif  // _BOARD_CONFIG_H_
