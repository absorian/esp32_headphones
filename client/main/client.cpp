#include <cstdlib>
#include <iostream>

#include "impl.h"

#include "protocol_examples_common.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include <raw_stream.h>
#include <i2s_stream.h>
#include <flac_decoder.h>

const char *TAG_GLOB = "Headphones";

//#define CONFIG_EXAMPLE_WIFI_SSID "Wireless@Pepe"
//#define CONFIG_EXAMPLE_WIFI_PASSWORD "01010100"
//#define CONFIG_EXAMPLE_PORT 65001

//#define FRAMES_PER_BUFFER 256
#define NUM_CHANNELS 2
#define BITS_PER_SAMPLE 16

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


#define NUM_CHANNELS 2
#define SAMPLE_RATE 44100

const ip_address_t HOST_ADDR = ip_addr_from_string("192.168.1.101");

#include "controller.h"
#include "sender.h"
#include "receiver.h"

#define PIPE_WIDTH 1024 - 4 + controller_t::md_size()

#define DMA_BUF_COUNT 6
#define DMA_BUF_SIZE 1024
static audio_element_handle_t raw_write, flac_decoder, i2s_stream_writer;
static audio_pipeline_handle_t pipeline_d, pipeline_e;

int64_t get_time_ms() {
    timeval tv_now{};
    gettimeofday(&tv_now, nullptr);
    return (int64_t) tv_now.tv_sec * 1000L + (int64_t) tv_now.tv_usec / 1000L;
}

class remote_speaker_t { // TODO: connect with the actual mic
    const char *TAG = "Remote speaker";
public:
    explicit remote_speaker_t(sender_t &sender)
            : sender(sender) {
        ESP_LOGI(TAG, "mic_t initialized");
    }

    void start(uint32_t sample_rate) {

    }

    void stop() {

    }

private:

    bool onProcessSamples(const int16_t *samples, size_t sampleCount) {
        sender.send((uint8_t *) samples, sampleCount * 2);
        return true;
    }

    sender_t &sender;
};

class remote_mic_t {
    const char *TAG = "Remote microphone";
public:
    explicit remote_mic_t(receiver_t &receiver)
            : recv(receiver) {
        recv.set_receive_callback(on_receive_data, this);
        ESP_LOGI(TAG, "speaker_t initialized");
    }

    void set_channel_count(uint8_t count) {
        num_channels = count;
    }

    void start(uint32_t sample_rate) { // TODO: initialize raw_stream here, actually in its own derived class
        time_stamp = get_time_ms();

//        initialize(num_channels, sample_rate);
//        play();
    }

    void stop() {

    }

private:
    static void on_receive_data(const uint8_t *data, size_t bytes, void *client_data) {
        auto body = (remote_mic_t *) client_data;

        int16_t past = get_time_ms() - body->time_stamp;
        if (past > 30) {
            ESP_LOGI(body->TAG, "Huge delay: %d ms", past);
        }
//        float frames_must = past / 1000.0 * 44100; // body->sample_rate
//        int32_t frames = bytes / body->num_channels;
//        if (frames < frames_must) {
//            ESP_LOGI(body->TAG, "Time: %d ms, came: %d, must: %f, debt is %f",
//                     past, frames, frames_must, frames_must - frames);
//        }
        body->time_stamp = get_time_ms();

        raw_stream_write(raw_write, (char *) data, bytes);
    }

    uint8_t num_channels = 2;

    int64_t time_stamp{};

    receiver_t &recv;
};

class headphones_t : public controller_t {
    const char *TAG = "Headphones";

public:
    headphones_t() : controller_t(),
                     snd(this, PIPE_WIDTH), recv(this, PIPE_WIDTH), mic(snd), spk(recv) {
        // useless for now
        spk.set_channel_count(NUM_CHANNELS);

        ESP_LOGI(TAG, "headphones_t initialized");
    }

    void start(const asio::ip::address_v4 &remote_addr_, uint16_t remote_port_) {
        remote_port = remote_port_;
        remote_addr = remote_addr_;
        snd.set_endpoint(udp_endpoint_t(remote_addr, remote_port));
        recv.start(remote_port);
    }

    void stop() {
        ESP_LOGI(TAG, "Stopping");
        mic_enabled(false);
        spk_enabled(false);
        recv.stop();
        spk.stop();
    }

    void send_state(state_t state) {
        set_state(state);
        // TODO: if spk enabled -> return;
        for (int i = 0; i < 3; ++i) {
            snd.send(nullptr, 0);
        }
    }

    bool is_connected() {
        bool c = false;
        mutex.lock();
        c = connected;
        mutex.unlock();
        return c;
    }

    void spk_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            spk.start(SAMPLE_RATE);
        } else {
            spk.stop();
        }
    }

    void mic_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            mic.start(SAMPLE_RATE);
        } else {
            mic.stop();
        }
    }

protected:
    void on_remote_state_change(state_t state) override {
        if (state >= STALL && !connected) {
            ESP_LOGI(TAG, "Successfully connected from %s:%d", remote_addr.to_string().c_str(), remote_port);
            connected = true;

        } else if (state == DISCONNECT && connected) {
            ESP_LOGI(TAG, "Got disconnected, stopping enabled devices");
            connected = false;
            spk_enabled(false);
            mic_enabled(false);
            return;
        }

        switch (state) {
            case STALL:
                ESP_LOGI(TAG, "The state is STALL");
                spk_enabled(false);
                mic_enabled(false);
                break;
            case SPK_ONLY:
                ESP_LOGI(TAG, "The state is SPK_ONLY");
                spk_enabled(true);
                mic_enabled(false);
                break;
            case FULL:
                ESP_LOGI(TAG, "The state is FULL");
                mic_enabled(true);
                spk_enabled(true);
                break;
            default:
                break;
        }
    }

    void on_remote_cmd_receive(cmd_t cmd) override {
        ESP_LOGI(TAG, "Received a command: %d", cmd);
    }

private:
    std::atomic<bool> connected{};

    sender_t snd;
    receiver_t recv;
    uint16_t remote_port{};
    asio::ip::address_v4 remote_addr;

    remote_speaker_t mic;
    remote_mic_t spk;
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

    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* This helper function configures blocking UART I/O */
//    ESP_ERROR_CHECK(example_configure_stdin_stdout());

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_d = audio_pipeline_init(&pipeline_cfg);

    i2s_stream_cfg_t i2s_cfg1 = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg1.type = AUDIO_STREAM_WRITER;
    i2s_cfg1.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_cfg1.i2s_config.dma_buf_count = DMA_BUF_COUNT;
    i2s_cfg1.i2s_config.dma_buf_len = DMA_BUF_SIZE;
    i2s_cfg1.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg1);
    i2s_set_pin(I2S_NUM_0, &pin_config_spk);

    flac_decoder_cfg_t decoder_cfg = DEFAULT_FLAC_DECODER_CONFIG();
    flac_decoder = flac_decoder_init(&decoder_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_write = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline_d, raw_write, "raw");
//    audio_pipeline_register(pipeline_d, flac_decoder, "dec");
    audio_pipeline_register(pipeline_d, i2s_stream_writer, "i2s_w");

    const char *link_d[] = {"raw", "i2s_w"};
    audio_pipeline_link(pipeline_d, &link_d[0], 2);
//    const char *link_d[] = {"raw", "dec", "i2s_w"};
//    audio_pipeline_link(pipeline_d, &link_d[0], 3);

//    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
//    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
//    audio_pipeline_set_listener(pipeline_d, evt);

    audio_pipeline_run(pipeline_d);

    headphones_t hf;
    hf.start(HOST_ADDR, 65001);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!hf.is_connected())
            hf.send_state(headphones_t::SPK_ONLY);
    }
//    io_context.run();
}
