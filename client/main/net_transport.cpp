#include "net_transport.h"
#include "stream_bridge.h"
#include "wifi_util.h"
#include "common_util.h"

#include <controller.h>
#include <sender.h>
#include <receiver.h>
#include <impl.h>

static const char *TAG = "NET_TRANSPORT";

const ip_address_t HOST_ADDR = ip_address_t::from_string("10.242.1.61");
const uint16_t PORT = 533;

static uint16_t remote_port{};
static ip_address_t remote_addr;

static void send_state(controller_t::state_t state);

static void receive_cb(const uint8_t *, size_t, void *);

[[noreturn]] static void send_handler(void *ctx);

[[noreturn]] static void ping_handler(void *ctx);

static void event_cb(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

class headphones_t : public controller_t {
    const char *TAG = "Headphones";

public:
    headphones_t() : controller_t() {}

protected:
    void on_remote_state_change(state_t state) override {
        if (state > DISCONNECT) {
            logi(TAG, "from %s:%d connected with state %d", remote_addr.to_string().c_str(), remote_port, state);
        } else {
            logi(TAG, "got disconnected");
            thread_t::sleep(500); // TODO: find another way to prevent instant reconnecting, mb packet time exchange
            return;
        }

        switch (state) {
            case STALL:
                logi(TAG, "The state is STALL");
                break;
            case SPK_ONLY:
                logi(TAG, "The state is SPK_ONLY");
                break;
            case FULL:
                logi(TAG, "The state is FULL");
                break;
            default:
                break;
        }
    }

    void on_remote_cmd_receive(cmd_t cmd) override {
        logi(TAG, "Received a command: %d", cmd);
    }
};

ESP_EVENT_DEFINE_BASE(NET_TRANSPORT);

static thread_t *send_task;
static thread_t *ping_task;

static std::atomic<bool> net_started(false);

static headphones_t *headphones;
static sender_t *sender;
static receiver_t *receiver;

void receive_cb(const uint8_t *data, size_t len, void *) {
    stream_bridge::write(data, len);
}

[[noreturn]] void send_handler(void *ctx) { // commit and make as sender_t cb
    auto *buf = (uint8_t *) calloc(1, DMA_BUF_SIZE);
    assert(buf);

    while (true) {
        if (!net_started || headphones->get_cur_state() != controller_t::FULL) {
            thread_t::sleep(50); // TODO: add spin lock instead of delay
            continue;
        }
        int rx_available = stream_bridge::bytes_ready_to_read();
        if (rx_available) {
            if (rx_available > DMA_BUF_SIZE) rx_available = DMA_BUF_SIZE;
            stream_bridge::read(buf, rx_available);
            sender->send(buf, rx_available);
        }
    }
}

[[noreturn]] void ping_handler(void *ctx) {
    while (true) {
        if (net_started && headphones->get_cur_state() == controller_t::DISCONNECT) {
            send_state(controller_t::FULL);
        }
        thread_t::sleep(500);
    }
}

void send_state(controller_t::state_t state) {
    headphones->set_state(state);
    for (int i = 0; i < 3; ++i) {
        sender->send_immediate(nullptr, 0);
    }
}

void event_cb(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    auto dat = reinterpret_cast<event_bridge::data_t *>(event_data);
    switch (static_cast<event_bridge::cmd_t>(event_id)) {
        case event_bridge::VOL_DATA_MIC:
            break;
        case event_bridge::VOL_DATA_SPK:
            break;
        case event_bridge::SVC_START:
            logi(TAG, "starting up net");
            wifi_util::connect();
            stream_bridge::configure_sink(44100, 2, 16);
            stream_bridge::configure_source(44100, 1, 16);
            remote_port = PORT;
            remote_addr = HOST_ADDR;
            sender->set_endpoint(udp_endpoint_t(remote_addr, remote_port));
            receiver->start(remote_port); // TODO: do not bind on client
            send_task->launch();
            net_started = true;
            break;
        case event_bridge::SVC_PAUSE:
            logi(TAG, "shutting down net");
            if (headphones->get_cur_state() != controller_t::DISCONNECT)
                send_state(controller_t::DISCONNECT);
            receiver->stop();
            send_task->terminate();
            wifi_util::shutdown();
            net_started = false;
            break;
        case event_bridge::VOL_DATA_RQ:
            break;
        default:
            break;
    }
}

void net_transport::init() {
    wifi_util::init();

    headphones = new headphones_t();
    sender = new sender_t(reinterpret_cast<controller_t *>(headphones), DMA_BUF_SIZE);
    receiver = new receiver_t(reinterpret_cast<controller_t *>(headphones), DMA_BUF_SIZE);

    receiver->set_receive_callback(receive_cb, nullptr);

    event_bridge::set_listener(NET_TRANSPORT, event_cb);

    send_task = new thread_t(send_handler, nullptr, 5); // TODO: manipulate stack size
    ping_task = new thread_t(ping_handler, nullptr, 0); // manipulate stack size
    ping_task->launch();
}

// TODO: write net_transport::deinit
