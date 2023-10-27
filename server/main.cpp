#include "impl.h"
#include "controller.h"
#include "sender.h"
#include "receiver.h"

//#include <SFML/Audio.hpp>
#include <portaudio.h>

#include <iostream>
#include <cstring>
#include <atomic>

#define NUM_CHANNELS 2
#define SAMPLE_RATE 44100
#define PORT 533

#define PIPE_WIDTH 1024 - (controller_t::md_size() / 4 + (controller_t::md_size() % 4 != 0)) * 4

const char *TAG_GLOB = "Server";

class remote_speaker_t {
    const char *TAG = "Remote speaker";

    typedef short sample_t;
    static constexpr uint32_t frames_per_buf = PIPE_WIDTH / (NUM_CHANNELS * sizeof(sample_t));
    static constexpr PaSampleFormat pa_sample_type = paInt16;
public:
    ~remote_speaker_t() {
        Pa_Terminate();
    }

    explicit remote_speaker_t(sender_t &sender)
            : sender(sender) {
        Pa_Initialize();
        pa_params.sampleFormat = pa_sample_type;
        pa_params.suggestedLatency = Pa_GetDeviceInfo(pa_params.device)->defaultLowInputLatency;
        pa_params.hostApiSpecificStreamInfo = nullptr;
    }

    void set_channel_count(int num_channels) {
        pa_params.channelCount = num_channels;
    }

    void selectDeviceCli() {
        PaDeviceIndex devices_total = Pa_GetDeviceCount(), input_devices = 0;
        PaDeviceIndex idx_map[devices_total];
        PaDeviceIndex hosts_total = Pa_GetHostApiCount();

        logi(TAG, "Available hosts:");
        for (int i = 0; i < hosts_total; ++i) {
            auto* host_info = Pa_GetHostApiInfo(i);
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
            auto* device_info = Pa_GetDeviceInfo(i);
            if (device_info->hostApi != selected || device_info->maxInputChannels < NUM_CHANNELS) continue;

            idx_map[input_devices] = i;
            logi(TAG, "%i. %s", input_devices + 1, device_info->name);
            input_devices++;
        }
        while (true) {
            logi(TAG, "Select device:");
            std::cin >> selected;
            if (selected > input_devices) continue;

            pa_params.device = idx_map[selected - 1];
            break;
        }
    }

    void start(uint32_t sample_rate) {
        Pa_OpenStream(
                &stream,
                &pa_params,
                nullptr,
                sample_rate,
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
        auto *data = (remote_speaker_t *) user_data;
        data->sender.send((uint8_t *) input_buf, frame_count * NUM_CHANNELS * sizeof(sample_t));
        return paContinue;
    }

    PaStreamParameters pa_params{};
    PaStream *stream{};

    sender_t &sender;
};

//class remote_speaker_t : public sf::SoundRecorder {
//    const char *TAG = "Remote speaker";
//public:
//    explicit remote_speaker_t(sender_t &sender)
//            : sf::SoundRecorder(), sender(sender) {
//        setProcessingInterval(sf::milliseconds(2));
//    }
//
//private:
//    bool onProcessSamples(const int16_t *samples, size_t sampleCount) override {
//        sender.send((uint8_t *) samples, sampleCount * 2);
//        return true;
//    }
//
//    sender_t &sender;
//};

class remote_mic_t {
    const char *TAG = "Remote microphone";
public:
    explicit remote_mic_t(receiver_t &receiver)
            : temp_storage(receiver.width), recv(receiver) {
        recv.set_receive_callback(on_receive_data, this);
    }

    void set_channel_count(uint8_t count) {
        num_channels = count;
    }

    void start(uint32_t sample_rate) {
//        initialize(num_channels, sample_rate);
//        play();
    }

private:
    static void on_receive_data(const uint8_t *data, size_t bytes, void *client_data) {
        auto body = (remote_mic_t *) client_data;
        body->mutex.lock();
        std::copy(data, data + bytes, body->temp_storage.begin());
        body->temp_actual_size = bytes;
        body->mutex.unlock();
    }

//    bool onGetData(Chunk &data) override {
//        mutex.lock();
//
//        data.sampleCount = temp_actual_size;
//        data.samples = (const sf::Int16 *) temp_storage.data();
//
//        mutex.unlock();
//        return true;
//    }
//
//    void onSeek(sf::Time timeOffset) override {
//        logi(TAG, "onSeek operation is not supported");
//    }

    uint8_t num_channels = 2;

    std::vector<uint8_t> temp_storage;
    size_t temp_actual_size = 0;
    mutex_t mutex;

    receiver_t &recv;
};

class headphones_t : public controller_t {
    const char *TAG = "Headphones";
public:
//    std::string select_record_device() {
//        int selected;
//        std::vector<std::string> devices = sf::SoundBufferRecorder::getAvailableDevices();
//        for (int i = 0; i < devices.size(); i++) {
//            std::cout << i + 1 << ") " << devices[i] << "\n";
//        }
//        do {
//            logi(TAG, "Choose recording device: ");
//            std::cin >> selected;
//            selected--;
//            std::cin.ignore(10000, '\n');
//        } while (!(selected >= 0 && selected < devices.size()));
//        return devices[selected];
//    }

    headphones_t() : snd(this, PIPE_WIDTH), recv(this, PIPE_WIDTH), spk(snd), mic(recv) {

        spk.set_channel_count(NUM_CHANNELS);
//        spk.setDevice(select_record_device());
        spk.selectDeviceCli();

        mic.set_channel_count(NUM_CHANNELS);
//        mic set device TODO
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
        for (int i = 0; i < 3; ++i) {
            snd.send(nullptr, 0);
        }
    }

    void mic_enabled(bool en) {
        static bool cur = false;
        if (en == cur) return;

        if ((cur = en)) {
            mic.start(SAMPLE_RATE);
        } else {
//            mic.stop();
        }
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
                mic_enabled(false);
                spk_enabled(false);
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

    remote_speaker_t spk;
    remote_mic_t mic;
};


int main() {
    headphones_t hf;
    hf.start(PORT);
    char in;
    while (1) {
        std::cin >> in;
        if (in == 'q') {
            hf.set_state(controller_t::DISCONNECT);
            thread_t::sleep(1000);
            break;
        }
    }
    hf.stop();
    return EXIT_SUCCESS;
}