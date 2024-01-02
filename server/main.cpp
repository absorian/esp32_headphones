#include "impl.h"
#include "controller.h"
#include "sender.h"
#include "receiver.h"

//#include <SFML/Audio.hpp>
#include <portaudio.h>

#include <iostream>
#include <cstring>
#include <atomic>

#define NUM_CHANNELS_SPK 2
#define NUM_CHANNELS_MIC 1
#define SAMPLE_RATE 44100
#define PORT 533

#define PIPE_WIDTH 1024 - (controller_t::md_size() / 4 + (controller_t::md_size() % 4 != 0)) * 4

const char *TAG_GLOB = "Server";

class remote_sink_t {
    const char *TAG = "Remote sink";

    typedef short sample_t;
    static constexpr uint32_t frames_per_buf = PIPE_WIDTH / (NUM_CHANNELS_SPK * sizeof(sample_t));
    static constexpr PaSampleFormat pa_sample_type = paInt16;
public:
    ~remote_sink_t() {
        Pa_Terminate();
    }

    explicit remote_sink_t(sender_t &sender)
            : sender(sender) {
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
        Pa_OpenStream(
                &stream,
                &pa_params,
                nullptr,
                SAMPLE_RATE,
                frames_per_buf,
                paClipOff,      /* we won't output out of range samples so don't bother clipping them */
                send_to_remote,
                this);
        Pa_StartStream(stream);
    }

    void stop() {
        Pa_CloseStream(stream);
    }

private:
    static int send_to_remote(const void *input_buf, void *output_buf,
                              unsigned long frame_count, // 1 frame = N_CH samples
                              const PaStreamCallbackTimeInfo *time_info,
                              PaStreamCallbackFlags status_flags,
                              void *user_data) {
        auto *body = (remote_sink_t *) user_data;

        // sender_t is thread safe
        body->sender.send((uint8_t *) input_buf, frame_count * body->pa_params.channelCount * sizeof(sample_t));
        return paContinue;
    }

    PaStreamParameters pa_params{};
    PaStream *stream{};

    sender_t &sender;
};

class remote_source_t {
    const char *TAG = "Remote source";

    typedef short sample_t;
    static constexpr uint32_t frames_per_buf = PIPE_WIDTH / (NUM_CHANNELS_MIC * sizeof(sample_t));
    static constexpr PaSampleFormat pa_sample_type = paInt16;
public:
    ~remote_source_t() {
        Pa_Terminate();
    }

    explicit remote_source_t(receiver_t &receiver)
            : recv(receiver) {
        Pa_Initialize();
        pa_params.sampleFormat = pa_sample_type;
        pa_params.hostApiSpecificStreamInfo = nullptr;
        pa_params.channelCount = NUM_CHANNELS_MIC;

        recv.set_receive_callback(on_receive_data, this);
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
        Pa_OpenStream(
                &stream,
                nullptr,
                &pa_params,
                SAMPLE_RATE,
                frames_per_buf,
                0,
                nullptr,
                nullptr);
        Pa_StartStream(stream);
    }

    void stop() {
        Pa_CloseStream(stream);
    }

private:
    static void on_receive_data(const uint8_t *data, size_t bytes, void *client_data) {
        auto body = (remote_source_t *) client_data;
//        body->mutex.lock();
        Pa_WriteStream(body->stream, data, bytes / (NUM_CHANNELS_MIC * sizeof(sample_t)));
//        logi(body->TAG, "%d", bytes);
//        std::copy(data, data + bytes, body->storage);
//        body->storage_size = bytes;
//        body->mutex.unlock();
    }

    PaStreamParameters pa_params{};
    PaStream *stream{};

    mutex_t mutex;

    receiver_t &recv;
};

class headphones_t : public controller_t {
    const char *TAG = "Headphones";
public:
    headphones_t() : snd(this, PIPE_WIDTH), recv(this, PIPE_WIDTH), spk(snd), mic(recv) {

        spk.selectDeviceCli();
        mic.selectDeviceCli();
    }

    ~headphones_t() = default;

    void start(uint16_t port) {
        logi(TAG, "Starting to listen for connections");
        recv.start(port);
        remote_port = port;
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
        snd.send_immediate(nullptr, 0);
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

    void spk_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            spk.start();
        } else {
            spk.stop();
        }
    }

protected:
    void on_remote_state_change(state_t state) override {
        if (state >= STALL && !connected) {
            logi(TAG, "Successfully connected to %s", recv.get_endpoint().address().to_string().c_str());
            connected = true;
            snd.set_endpoint(udp_endpoint_t(recv.get_endpoint().address(), remote_port));
        } else if (state == DISCONNECT && connected) {
            logi(TAG, "Got disconnected, stopping enabled devices");
            connected = false;
            mic_enabled(false);
            spk_enabled(false);
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
                spk_enabled(true);
                mic_enabled(true);
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

    remote_sink_t spk;
    remote_source_t mic;
};


int main() {
    headphones_t hf;
    hf.start(PORT);
    char in;
    while (1) {
        std::cin >> in;
        if (in == 'q') {
            hf.send_state(controller_t::DISCONNECT);
            thread_t::sleep(1000);
            break;
        }
    }
    hf.stop();
    return EXIT_SUCCESS;
}