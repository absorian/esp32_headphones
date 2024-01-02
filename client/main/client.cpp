#include <cstdlib>
#include <iostream>

#define LOG_DEBUG

#include "impl.h"

#include "protocol_examples_common.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include <raw_stream.h>
#include <i2s_stream.h>

const char *TAG_GLOB = "Headphones";

const i2s_pin_config_t pin_config_spk = {
        .bck_io_num = 14,
        .ws_io_num = 27,
        .data_out_num = 22,
        .data_in_num = I2S_PIN_NO_CHANGE
};
const i2s_pin_config_t pin_config_mic = {
        .bck_io_num = 16,
        .ws_io_num = 15,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 4
};

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1

#define USE_FLAC_SPK 0
#define SAMPLE_RATE 44100
const ip_address_t HOST_ADDR = ip_address_t::from_string("192.168.1.4");
#define PORT 533

#include "controller.h"
#include "sender.h"
#include "receiver.h"

#define PIPE_WIDTH 1024 - (controller_t::md_size() / 4 + (controller_t::md_size() % 4 != 0)) * 4

#if NUM_CHANNELS_SPK == 1
#define CHANNEL_FMT_SPK I2S_CHANNEL_FMT_ONLY_RIGHT
#else
#define CHANNEL_FMT_SPK I2S_CHANNEL_FMT_RIGHT_LEFT
#endif

#if NUM_CHANNELS_MIC == 1
#define CHANNEL_FMT_MIC I2S_CHANNEL_FMT_ONLY_LEFT
#else
#define CHANNEL_FMT_MIC I2S_CHANNEL_FMT_RIGHT_LEFT
#endif

#if USE_FLAC_SPK

#include <flac_decoder.h>

#endif

#define DMA_BUF_COUNT 4
#define DMA_BUF_SIZE 1024

int64_t get_time_ms() {
    timeval tv_now{};
    gettimeofday(&tv_now, nullptr);
    return (int64_t) tv_now.tv_sec * 1000L + (int64_t) tv_now.tv_usec / 1000L;
}

class remote_sink_t { // TODO: connect with the actual mic
    const char *TAG = "Remote sink";
public:
    explicit remote_sink_t(sender_t &sender)
            : sender(sender) {
        logi(TAG, "initializing...");

        audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
        pipeline = audio_pipeline_init(&pipeline_cfg);
        logi(TAG, "created pipeline");

        i2s_stream_cfg_t i2s_mic_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_mic_cfg.i2s_port = I2S_NUM_1;
        i2s_mic_cfg.type = AUDIO_STREAM_READER;
        i2s_mic_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX);
        i2s_mic_cfg.i2s_config.sample_rate = SAMPLE_RATE;
        i2s_mic_cfg.i2s_config.bits_per_sample = i2s_bits_per_sample_t(16);
        i2s_mic_cfg.i2s_config.channel_format = CHANNEL_FMT_MIC;
        i2s_mic_cfg.i2s_config.dma_buf_count = DMA_BUF_COUNT;
        i2s_mic_cfg.i2s_config.dma_buf_len = DMA_BUF_SIZE;
        i2s_mic_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;

        i2s_stream_reader = i2s_stream_init(&i2s_mic_cfg);
        i2s_set_pin(I2S_NUM_1, &pin_config_mic);
        logi(TAG, "created i2s element");

        audio_pipeline_register(pipeline, i2s_stream_reader, "i2s_mic");
        logi(TAG, "registered elements");
        audio_element_set_write_cb(i2s_stream_reader, on_raw_read, this);

        const char *link[] = {"i2s_mic"};
        audio_pipeline_link(pipeline, &link[0], 1);

        logi(TAG, "initialized!");
    }

    void start() {
        logd(TAG, "starting");
        audio_pipeline_run(pipeline);
        audio_pipeline_resume(pipeline);
    }

    void stop() {
        logd(TAG, "stopping");
        audio_pipeline_pause(pipeline);
    }

private:
    static audio_element_err_t
    on_raw_read(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context) {
        auto *body = (remote_sink_t *) context;

        body->sender.send((uint8_t *) buffer, len);
        return static_cast<audio_element_err_t>(len); // esp-adf - stuff happens
    }


    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_handle_t pipeline;
    sender_t &sender;
};

class remote_source_t {
    const char *TAG = "Remote source";
public:
    explicit remote_source_t(receiver_t &receiver)
            : recv(receiver) {
        recv.set_receive_callback(on_receive_data, this);

        audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
        pipeline = audio_pipeline_init(&pipeline_cfg);

        i2s_stream_cfg_t i2s_spk_cfg = I2S_STREAM_CFG_DEFAULT();
        i2s_spk_cfg.i2s_port = I2S_NUM_0;
        i2s_spk_cfg.type = AUDIO_STREAM_WRITER;
        i2s_spk_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
        i2s_spk_cfg.i2s_config.sample_rate = SAMPLE_RATE;
        i2s_spk_cfg.i2s_config.channel_format = CHANNEL_FMT_SPK;
        i2s_spk_cfg.i2s_config.dma_buf_count = DMA_BUF_COUNT;
        i2s_spk_cfg.i2s_config.dma_buf_len = DMA_BUF_SIZE;
        i2s_spk_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;

        i2s_stream_writer = i2s_stream_init(&i2s_spk_cfg);
        i2s_set_pin(I2S_NUM_0, &pin_config_spk);

#if USE_FLAC_SPK
        flac_decoder_cfg_t decoder_cfg = DEFAULT_FLAC_DECODER_CONFIG();
        flac_decoder = flac_decoder_init(&decoder_cfg);
#endif

        raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
        raw_cfg.type = AUDIO_STREAM_WRITER;
        raw_write = raw_stream_init(&raw_cfg);

        audio_pipeline_register(pipeline, raw_write, "raw_spk");
#if USE_FLAC_SPK
        audio_pipeline_register(pipeline, flac_decoder, "flac_dec");
#endif
        audio_pipeline_register(pipeline, i2s_stream_writer, "i2s_spk");

#if USE_FLAC_SPK
        const char *link[] = {"raw_spk", "flac_dec", "i2s_spk"};
        audio_pipeline_link(pipeline, &link[0], 3);
#else
        const char *link[] = {"raw_spk", "i2s_spk"};
        audio_pipeline_link(pipeline, &link[0], 2);
#endif

        logi(TAG, "initialized");
    }

    void start() { // TODO: initialize raw_stream here, actually in its own derived class
        logi(TAG, "starting");
        time_stamp = get_time_ms();
        audio_pipeline_run(pipeline);
        audio_pipeline_resume(pipeline);
    }

    void stop() {
        logi(TAG, "stopping");
        audio_pipeline_pause(pipeline);
    }

private:
    static void on_receive_data(const uint8_t *data, size_t bytes, void *client_data) {
        auto body = (remote_source_t *) client_data;

        if (audio_element_get_state(body->raw_write) == AEL_STATE_RUNNING)
            raw_stream_write(body->raw_write, (char *) data, bytes);
    }

    audio_element_handle_t raw_write, __unused flac_decoder{}, i2s_stream_writer;
    audio_pipeline_handle_t pipeline;

    receiver_t &recv;
};

class headphones_t : public controller_t {
    const char *TAG = "Headphones";

public:
    headphones_t() : controller_t(),
                     snd(this, PIPE_WIDTH), recv(this, PIPE_WIDTH), mic(snd), spk(recv) {

        logi(TAG, "headphones_t initialized");
    }

    void start(const asio::ip::address_v4 &remote_addr_, uint16_t remote_port_) {
        remote_port = remote_port_;
        remote_addr = remote_addr_;
        snd.set_endpoint(udp_endpoint_t(remote_addr, remote_port));
        recv.start(remote_port);
    }

    void stop() {
        logi(TAG, "Stopping");
        mic_enabled(false);
        spk_enabled(false);
        recv.stop();
        spk.stop();
    }

    void send_state(state_t state) {
        set_state(state);
        // TODO: if spk enabled -> return;
        for (int i = 0; i < 3; ++i) {
            snd.send_immediate(nullptr, 0);
        }
    }

    bool is_connected() {
        bool c;
        mutex.lock();
        c = connected;
        mutex.unlock();
        return c;
    }

    void spk_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            spk.start();
        } else {
            spk.stop();
        }
    }

    void mic_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            mic.start();
        } else {
            mic.stop();
        }
    }

protected:
    void on_remote_state_change(state_t state) override {
        if (state >= STALL && !connected) {
            logi(TAG, "Successfully connected from %s:%d", remote_addr.to_string().c_str(), remote_port);
            connected = true;

        } else if (state == DISCONNECT && connected) {
            logi(TAG, "Got disconnected, stopping enabled devices");
            connected = false;
            spk_enabled(false);
            mic_enabled(false);
            return;
        }

        switch (state) {
            case STALL:
                logi(TAG, "The state is STALL");
                spk_enabled(false);
                mic_enabled(false);
                break;
            case SPK_ONLY:
                logi(TAG, "The state is SPK_ONLY");
                spk_enabled(true);
                mic_enabled(false);
                break;
            case FULL:
                logi(TAG, "The state is FULL");
                mic_enabled(true);
                spk_enabled(true);
                break;
            default:
                break;
        }
    }

    void on_remote_cmd_receive(cmd_t cmd) override {
        logi(TAG, "Received a command: %d", cmd);
    }

private:
    std::atomic<bool> connected{};

    sender_t snd;
    receiver_t recv;
    uint16_t remote_port{};
    ip_address_t remote_addr;

    remote_sink_t mic;
    remote_source_t spk;
};

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG_GLOB, ESP_LOG_DEBUG);

    // TODO: code own connection func/class
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* This helper function configures blocking UART I/O */
//    ESP_ERROR_CHECK(example_configure_stdin_stdout());

    headphones_t hf;
    hf.start(HOST_ADDR, PORT);

    while (1) {
        thread_t::sleep(500);
        if (!hf.is_connected())
            hf.send_state(headphones_t::FULL);
    }
}
