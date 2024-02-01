#include "stream_bridge.h"
#include "event_bridge.h"
#include "common_util.h"

#include <driver/i2s_std.h>
#include <cstring>
#include <atomic>
#include <impl.h>

static const char *TAG = "STREAM_BRIDGE";

static i2s_std_config_t sink_cfg = {
        .clk_cfg = {
                .sample_rate_hz = 44100,
                .clk_src = I2S_CLK_SRC_APLL,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
                .mclk = GPIO_NUM_NC,
                .bclk = GPIO_NUM_13,
                .ws = GPIO_NUM_14,
                .dout = GPIO_NUM_12,
                .din = GPIO_NUM_NC,
                .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                }
        },
};
static i2s_std_config_t source_cfg = {
        .clk_cfg = {
                .sample_rate_hz = 44100,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256, // for 16bit data
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
                .mclk = GPIO_NUM_NC,
                .bclk = GPIO_NUM_16,
                .ws = GPIO_NUM_15,
                .dout = GPIO_NUM_NC,
                .din = GPIO_NUM_4,
                .invert_flags = {
                        .mclk_inv = false,
                        .bclk_inv = false,
                        .ws_inv = false,
                }
        },
};

// Volume thresholds
static int volume_convert_alc_sink(int vol) {
    return vol ? map(vol, 0, 127, -40, 0) : -64;
}

static int volume_convert_alc_source(int vol) {
    return vol ? map(vol, 0, 127, -30, 0) : -64;
}

static int source_vol = 127, sink_vol = 127;

static i2s_chan_handle_t tx_handle, rx_handle;

static std::atomic<size_t> available_read;
static std::atomic<size_t> available_write;

static IRAM_ATTR bool i2s_rx_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
    available_read = event->size;
    return false;
}

static IRAM_ATTR bool i2s_tx_callback(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
    available_write = event->size;
    return false;
}

void stream_bridge::init() {
    if (tx_handle && rx_handle) return;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = DMA_BUF_SIZE;
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, nullptr));
    chan_cfg.id = I2S_NUM_1;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle));

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &sink_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &source_cfg));

    i2s_event_callbacks_t cbs = {
            .on_recv = i2s_rx_callback,
            .on_recv_q_ovf = nullptr,
            .on_sent = i2s_tx_callback,
            .on_send_q_ovf = nullptr,
    };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(rx_handle, &cbs, nullptr));
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx_handle, &cbs, nullptr));

    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

int stream_bridge::write(const void *buffer, int len, uint32_t wait_time) {
    size_t b;
    i2s_channel_write(tx_handle, buffer, len, &b, wait_time);
    if (b < len) {
        loge(TAG, "i2s write underrun: %d/%d", b, len);
        available_write = 0;
    } else {
        available_write -= len;
        if (available_write < 0) available_write = 0;
    }
    return b;
}

int stream_bridge::read(void *buffer, int len, uint32_t wait_time) {
    size_t b;
    i2s_channel_read(rx_handle, buffer, len, &b, wait_time);
    if (b < len) {
        loge(TAG, "i2s read underrun: %d/%d", b, len);
        available_read = 0;
    } else {
        available_read -= len;
        if (available_read < 0) available_read = 0;
    }
    return b;
}

int stream_bridge::bytes_ready_to_read() {
    return available_read;
}

int stream_bridge::bytes_can_write() {
    return available_write;
}

void stream_bridge::configure_sink(int sample_rates, int channels, int bits) {
    i2s_channel_disable(tx_handle);
    sink_cfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(static_cast<i2s_data_bit_width_t>(bits),
                                                        static_cast<i2s_slot_mode_t>(channels));
    sink_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH; // in i2s_slot_mode mono, pcm5102 plays only on left channel, fix
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(tx_handle, &sink_cfg.slot_cfg));
    sink_cfg.clk_cfg.sample_rate_hz = sample_rates;
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_handle, &sink_cfg.clk_cfg));
    i2s_channel_enable(tx_handle);
    logi(TAG, "sink reconfigured to sr: %d, ch: %d, bt: %d", sample_rates, channels, bits);
}

void stream_bridge::configure_source(int sample_rates, int channels, int bits) {
    i2s_channel_disable(rx_handle);
    source_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(static_cast<i2s_data_bit_width_t>(bits),
                                                              static_cast<i2s_slot_mode_t>(channels));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(rx_handle, &source_cfg.slot_cfg));
    source_cfg.clk_cfg.sample_rate_hz = sample_rates;
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(rx_handle, &source_cfg.clk_cfg));
    i2s_channel_enable(rx_handle);
    logi(TAG, "source reconfigured to sr: %d, ch: %d, bt: %d", sample_rates, channels, bits);
}

void stream_bridge::set_source_volume(int vol) {

}

void stream_bridge::set_sink_volume(int vol) {

}

int stream_bridge::get_source_volume() {
    return 127;
}

int stream_bridge::get_sink_volume() {
    return 127;
}
