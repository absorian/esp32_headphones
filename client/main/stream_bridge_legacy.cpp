#include "stream_bridge.h"
#include "event_bridge.h"
#include "common_util.h"

#include <raw_stream.h>
#include <audio_element.h>
#include <audio_pipeline.h>
#include <i2s_stream.h>
#include <ringbuf.h>
#include <cstring>
#include <impl.h>

static const char* TAG = "STREAM_BRIDGE";

constexpr i2s_pin_config_t pin_config_sink = {
        .bck_io_num = 13,
        .ws_io_num = 14,
        .data_out_num = 12,
        .data_in_num = I2S_PIN_NO_CHANGE
};
constexpr i2s_pin_config_t pin_config_source = {
        .bck_io_num = 16,
        .ws_io_num = 15,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 4
};

// Volume thresholds
static int volume_convert_alc_sink(int vol) {
    return vol ? map(vol, 0, 127, -40, 0) : -64;
}

static int volume_convert_alc_source(int vol) {
    return vol ? map(vol, 0, 127, -30, 0) : -64;
}

static int source_vol = 127, sink_vol = 127;

#define DMA_BUF_COUNT 4
#define DMA_BUF_SIZE 960

static audio_element_handle_t ael_source = nullptr;
static audio_element_handle_t ael_sink = nullptr;

static audio_element_info_t ael_sink_info;
static audio_element_info_t ael_source_info;

static audio_element_handle_t i2s_stream_writer, i2s_stream_reader;
static audio_pipeline_handle_t pipeline_sink, pipeline_source;

static bool spread_mono_sink;
static bool spread_mono_source;

static mutex_t mutex;

void stream_bridge::init() {
    mutex.lock();
    if (ael_source && ael_sink) {
        mutex.unlock();
        return;
    }
    raw_stream_cfg_t r_cfg = RAW_STREAM_CFG_DEFAULT();
    r_cfg.type = AUDIO_STREAM_WRITER;
    ael_sink = raw_stream_init(&r_cfg);
    r_cfg.type = AUDIO_STREAM_READER;
    ael_source = raw_stream_init(&r_cfg);
    mutex.unlock();

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size = 4 * 1024;
    pipeline_sink = audio_pipeline_init(&pipeline_cfg);
    pipeline_source = audio_pipeline_init(&pipeline_cfg);

    i2s_stream_cfg_t i2s_sink_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_sink_cfg.i2s_port = I2S_NUM_0;
    i2s_sink_cfg.type = AUDIO_STREAM_WRITER;
    i2s_sink_cfg.use_alc = true;
    i2s_sink_cfg.volume = volume_convert_alc_sink(sink_vol);
    i2s_sink_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_sink_cfg.i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_sink_cfg.i2s_config.dma_desc_num = DMA_BUF_COUNT;
    i2s_sink_cfg.i2s_config.dma_frame_num = DMA_BUF_SIZE;
    i2s_sink_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_MSB;

    i2s_stream_writer = i2s_stream_init(&i2s_sink_cfg);
    i2s_set_pin(i2s_sink_cfg.i2s_port, &pin_config_sink);


    i2s_stream_cfg_t i2s_source_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_source_cfg.i2s_port = I2S_NUM_1;
    i2s_source_cfg.type = AUDIO_STREAM_READER;
    i2s_source_cfg.use_alc = true;
    i2s_source_cfg.volume = volume_convert_alc_source(source_vol);
    i2s_source_cfg.i2s_config.mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_source_cfg.i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_source_cfg.i2s_config.dma_desc_num = DMA_BUF_COUNT;
    i2s_source_cfg.i2s_config.dma_frame_num = DMA_BUF_SIZE;
    i2s_source_cfg.i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_source_cfg.i2s_config.use_apll = false;

    i2s_stream_reader = i2s_stream_init(&i2s_source_cfg);
    i2s_set_pin(i2s_source_cfg.i2s_port, &pin_config_source);

    audio_pipeline_register(pipeline_sink, ael_sink, "raw_w");
    audio_pipeline_register(pipeline_sink, i2s_stream_writer, "i2s_w");

    audio_pipeline_register(pipeline_source, i2s_stream_reader, "i2s_r");
    audio_pipeline_register(pipeline_source, ael_source, "raw_r");


    const char *link_sink[] = {"raw_w", "i2s_w"};
    audio_pipeline_link(pipeline_sink, &link_sink[0], 2);

    const char *link_source[] = {"i2s_r", "raw_r"};
    audio_pipeline_link(pipeline_source, &link_source[0], 2);

    audio_pipeline_run(pipeline_sink);
    audio_pipeline_run(pipeline_source);
}

int stream_bridge::write(char *buffer, int len) {
    mutex.lock();
    if (!spread_mono_sink) {
        int cnt = raw_stream_write(ael_sink, buffer, len);
        mutex.unlock();
        return cnt;
    }

    uint8_t byte_size = ael_sink_info.bits / 8;
    char buf[len * 2];
    char *ptr = buf;
    for (int i = 0; i < len; i += byte_size) {
        memcpy(ptr, buffer + i, byte_size);
        ptr += byte_size;
        memcpy(ptr, buffer + i, byte_size);
        ptr += byte_size;
    }
    int cnt = raw_stream_write(ael_sink, buf, len * 2);
    mutex.unlock();
    return cnt;
}

int stream_bridge::read(char *buffer, int len) {
    mutex.lock();
    if (!spread_mono_source) {
        int cnt = raw_stream_read(ael_source, buffer, len);
        mutex.unlock();
        return cnt;
    }

    char buf[len / 2];
    int len_read = raw_stream_read(ael_source, buf, len / 2);
    uint8_t byte_size = ael_sink_info.bits / 8;
    char *ptr = buffer;
    for (int i = 0; i < len_read; i += byte_size) {
        memcpy(ptr, buf + i, byte_size);
        ptr += byte_size;
        memcpy(ptr, buf + i, byte_size);
        ptr += byte_size;
    }
    mutex.unlock();
    return len_read;
}

int stream_bridge::bytes_ready_to_read() {
    mutex.lock();
    int b = rb_bytes_filled(audio_element_get_input_ringbuf(ael_source));
    mutex.unlock();
    return b;
}

int stream_bridge::bytes_can_write() {
    mutex.lock();
    int b = rb_bytes_available(audio_element_get_input_ringbuf(ael_sink));
    mutex.unlock();
    return b;
}

// needs refining for the sake of the declaration, if needed ofc
void stream_bridge::configure_sink(int sample_rates, int channels, int bits, bool spread_mono) {
    mutex.lock();
    spread_mono_sink = channels == 1 && spread_mono;
    ael_sink_info.channels = channels;
    ael_sink_info.sample_rates = sample_rates;
    ael_sink_info.bits = bits;

    i2s_stream_set_clk(i2s_stream_writer, sample_rates, bits, spread_mono ? 2 : channels);
    logi(TAG, "sink configured to sr: %d, ch: %d, bi: %d", sample_rates, spread_mono ? 2 : channels, bits);
    mutex.unlock();
}

void stream_bridge::configure_source(int sample_rates, int channels, int bits, bool spread_mono) {
    mutex.lock();
    spread_mono_source = channels == 1 && spread_mono;
    ael_source_info.channels = channels;
    ael_source_info.sample_rates = sample_rates;
    ael_source_info.bits = bits;

    i2s_stream_set_clk(i2s_stream_reader, sample_rates, bits, spread_mono ? 2 : channels);
    logi(TAG, "source configured to sr: %d, ch: %d, bi: %d", sample_rates, spread_mono ? 2 : channels, bits);
    mutex.unlock();
}

void stream_bridge::set_source_volume(int vol) {
    mutex.lock();
    source_vol = vol;
    i2s_alc_volume_set(i2s_stream_reader, volume_convert_alc_source(vol));
    mutex.unlock();
}

void stream_bridge::set_sink_volume(int vol) {
    mutex.lock();
    sink_vol = vol;
    i2s_alc_volume_set(i2s_stream_writer, volume_convert_alc_sink(vol));
    mutex.unlock();
}

int stream_bridge::get_source_volume() {
    mutex.lock();
    int vol = source_vol;
    mutex.unlock();
    return vol;
}

int stream_bridge::get_sink_volume() {
    mutex.lock();
    int vol = sink_vol;
    mutex.unlock();
    return vol;
}
