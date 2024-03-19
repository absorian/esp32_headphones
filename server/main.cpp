#include <net_controller.h>
#include <impl/log.h>
#include <impl/concurrency.h>

#include <portaudio.h>

#include <iostream>
#include <cstring>
#include <atomic>

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1
#define SAMPLE_RATE 44100

#define PORT 533

const char *TAG_GLOB = "Server";

class remote_sink_t {
    const char *TAG = "Remote sink";

    typedef short sample_t;
    static constexpr uint32_t frames_per_buf = DATA_WIDTH / (NUM_CHANNELS_SPK * sizeof(sample_t));
    static constexpr PaSampleFormat pa_sample_type = paInt16;
public:
    ~remote_sink_t() {
        Pa_Terminate();
    }

    explicit remote_sink_t() {
        Pa_Initialize();
        pa_params.sampleFormat = pa_sample_type;
        pa_params.hostApiSpecificStreamInfo = nullptr;
        pa_params.channelCount = NUM_CHANNELS_SPK;
    }

    void selectDeviceCli() {
        PaDeviceIndex devices_total = Pa_GetDeviceCount(), input_devices = 0;
        PaDeviceIndex idx_map[devices_total];
        PaDeviceIndex hosts_total = Pa_GetHostApiCount();

        logi(TAG, "Available hosts:");
        for (int i = 0; i < hosts_total; ++i) {
            auto *host_info = Pa_GetHostApiInfo(i);
            logi(TAG, "%i. %s", i + 1, host_info->name);
        }
        PaDeviceIndex selected;
        while (true) {
            logi(TAG, "Select host:");
            std::cin >> selected;
            if (selected <= hosts_total) break;
        }
        selected--;

        logi(TAG, "Available input devices at %s:", Pa_GetHostApiInfo(selected)->name);
        for (int i = 0; i < devices_total; i++) {
            auto *device_info = Pa_GetDeviceInfo(i);
            if (device_info->hostApi != selected || device_info->maxInputChannels < pa_params.channelCount) continue;

            idx_map[input_devices] = i;
            logi(TAG, "%i. %s", input_devices + 1, device_info->name);
            input_devices++;
        }
        while (true) {
            logi(TAG, "Select device:");
            std::cin >> selected;
            if (selected > input_devices) continue;

            pa_params.device = idx_map[selected - 1];
            pa_params.suggestedLatency = Pa_GetDeviceInfo(pa_params.device)->defaultLowInputLatency;
            break;
        }
    }

    void start() {
        if (!stream) {
            Pa_OpenStream(
                    &stream,
                    &pa_params,
                    nullptr,
                    SAMPLE_RATE,
                    frames_per_buf,
                    paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                    send_to_remote,
                    this);
        }
        Pa_StartStream(stream);
    }

    void stop() {
        Pa_StopStream(stream);
    }

private:
    static int send_to_remote(const void *input_buf, void *output_buf,
                              unsigned long frame_count, // 1 frame = N_CH samples
                              const PaStreamCallbackTimeInfo *time_info,
                              PaStreamCallbackFlags status_flags,
                              void *user_data) {
        auto *body = (remote_sink_t *) user_data;

        sender::send((uint8_t *) input_buf, frame_count * body->pa_params.channelCount * sizeof(sample_t));
        return paContinue;
    }

    PaStreamParameters pa_params{};
    PaStream *stream = nullptr;
};

class remote_source_t {
    const char *TAG = "Remote source";

    typedef short sample_t;
    static constexpr uint32_t frames_per_buf = DATA_WIDTH / (NUM_CHANNELS_MIC * sizeof(sample_t));
    static constexpr PaSampleFormat pa_sample_type = paInt16;
public:
    ~remote_source_t() {
        Pa_Terminate();
    }

    explicit remote_source_t() {
        Pa_Initialize();
        pa_params.sampleFormat = pa_sample_type;
        pa_params.hostApiSpecificStreamInfo = nullptr;
        pa_params.channelCount = NUM_CHANNELS_MIC;

        receiver::set_cb(ctx_func_t(on_receive_data, this));
    }

    void selectDeviceCli() {
        PaDeviceIndex devices_total = Pa_GetDeviceCount(), output_devices = 0;
        PaDeviceIndex idx_map[devices_total];
        PaDeviceIndex hosts_total = Pa_GetHostApiCount();

        logi(TAG, "Available hosts:");
        for (int i = 0; i < hosts_total; ++i) {
            auto *host_info = Pa_GetHostApiInfo(i);
            logi(TAG, "%i. %s", i + 1, host_info->name);
        }
        PaDeviceIndex selected;
        while (true) {
            logi(TAG, "Select host:");
            std::cin >> selected;
            if (selected <= hosts_total) break;
        }
        selected--;

        logi(TAG, "Available output devices at %s:", Pa_GetHostApiInfo(selected)->name);
        for (int i = 0; i < devices_total; i++) {
            auto *device_info = Pa_GetDeviceInfo(i);
            if (device_info->hostApi != selected || device_info->maxOutputChannels < pa_params.channelCount) continue;

            idx_map[output_devices] = i;
            logi(TAG, "%i. %s", output_devices + 1, device_info->name);
            output_devices++;
        }
        while (true) {
            logi(TAG, "Select device:");
            std::cin >> selected;
            if (selected > output_devices) continue;

            pa_params.device = idx_map[selected - 1];
            pa_params.suggestedLatency = Pa_GetDeviceInfo(pa_params.device)->defaultLowInputLatency;
            break;
        }
    }

    void start() {
        if (!stream) {
            Pa_OpenStream(
                    &stream,
                    nullptr,
                    &pa_params,
                    SAMPLE_RATE,
                    frames_per_buf,
                    0,
                    nullptr,
                    nullptr);
        }
        Pa_StartStream(stream);
    }

    void stop() {
        Pa_StopStream(stream);
    }

private:
    static void on_receive_data(const uint8_t *data, size_t bytes, void *client_data) {
        auto body = (remote_source_t *) client_data;
        Pa_WriteStream(body->stream, data, bytes / (NUM_CHANNELS_MIC * sizeof(sample_t)));
    }

    PaStreamParameters pa_params{};
    PaStream *stream = nullptr;
};

enum server_state_t {
    SV_NOACCEPT = 0,
    SV_ACCEPT,
    SV_CONNECTED
};

struct server_util_t {
    remote_sink_t spk;
    remote_source_t mic;
};
static std::atomic<server_state_t> conn_state;


static int remote_cmd_cb(net_controller::cmd_t cmd, void *par) {
    if (conn_state == SV_NOACCEPT) return -1;
    auto ctx = reinterpret_cast<server_util_t*>(par);

    logi(TAG_GLOB, "Received command: %d", cmd);

    if (cmd == net_controller::ST_DISCONNECT) {
        logi(TAG_GLOB, "Got disconnected");
        ctx->spk.stop();
        ctx->mic.stop();
        conn_state = SV_ACCEPT;
        return 0;
    }
    if (conn_state == SV_ACCEPT) {
        endpoint_t enp;
        receiver::get_endpoint(&enp);
        char addr[16];
        endpoint_get_addr_v4(&enp, addr);
        logi(TAG_GLOB, "Successfully connected to %s:%d", addr, endpoint_get_port(&enp));
        sender::set_endpoint(&enp);
        conn_state = SV_CONNECTED;
        ctx->spk.start();
    }
    switch (cmd) {
        case net_controller::ST_SPK_ONLY:
            ctx->mic.stop();
            break;
        case net_controller::ST_FULL:
            ctx->mic.start();
            break;
        case net_controller::CTL_PLAY_PAUSE:
            break;
        case net_controller::CTL_NEXT:
            break;
        case net_controller::CTL_PREV:
            break;
        default:
            break;
    }
    return 0;
}

int main() {
    net_controller::init();

    server_util_t util;

    util.spk.selectDeviceCli();
    util.mic.selectDeviceCli();

    net_controller::set_remote_cmd_cb(ctx_func_t(remote_cmd_cb, &util));
    net_controller::set_remote_ack_cb(ctx_func_t(remote_cmd_cb, &util));

    conn_state = SV_ACCEPT;

    receiver::bind(PORT);
    receiver::start();

    char in;
    while (true) {
        std::cin >> in;
        if (in == 'q') {
            bool connected = (conn_state == SV_CONNECTED);
            conn_state = SV_NOACCEPT;
            if (connected) net_controller::set_cmd(net_controller::ST_DISCONNECT, true);
            break;
        }
    }
    receiver::stop();
    return EXIT_SUCCESS;
}