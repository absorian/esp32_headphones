#include "net_transport.h"
#include "stream_bridge.h"
#include "common_util.h"

#include <sender.h>
#include <receiver.h>
#include <impl.h>

static const char *TAG = "NET_TRANSPORT";

const ip_address_t HOST_ADDR = ip_address_t::from_string("10.242.1.61");
const uint16_t PORT = 533;

static uint16_t remote_port{};
static ip_address_t remote_addr;

static void send_state(controller_t::state_t state);

static void receive_handler(const uint8_t *, size_t, void *);

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
            thread_t::sleep(500);
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

static event_bridge::source_t evt_source;
static event_bridge::source_t evt_incoming;
static thread_t *evt_thread;

static headphones_t *headphones;
static sender_t *sender;
static receiver_t *receiver;

void receive_handler(const uint8_t *data, size_t len, void *) {
    stream_bridge::write(data, len);
}

void send_state(controller_t::state_t state) {
    headphones->set_state(state);
    for (int i = 0; i < 3; ++i) {
        sender->send_immediate(nullptr, 0);
    }
}

static void net_loop(void *) {
    event_bridge::message_t msg;
    bool hdph_started = false;
    int64_t rq_conn_stamp = get_time_ms();

    while (true) {
        if (event_bridge::listen(evt_incoming, &msg, false) == ESP_OK) {
            auto dat = reinterpret_cast<event_bridge::data_t *>(msg.data);
            switch (static_cast<event_bridge::cmd_t>(msg.cmd)) {
                case event_bridge::MIC_ABS_VOL_DATA:
                    break;
                case event_bridge::SPK_ABS_VOL_DATA:
                    break;
                case event_bridge::SVC_START:
                    logi(TAG, "starting up headphones");
                    stream_bridge::configure_sink(44100, 2, 16);
                    stream_bridge::configure_source(44100, 1, 16);
                    remote_port = PORT;
                    remote_addr = HOST_ADDR;
                    sender->set_endpoint(udp_endpoint_t(remote_addr, remote_port));
                    receiver->start(remote_port);
                    hdph_started = true;
                    break;
                case event_bridge::SVC_PAUSE:
                    logi(TAG, "shutting down headphones");
                    if (headphones->get_cur_state() != controller_t::DISCONNECT)
                        send_state(controller_t::DISCONNECT);
                    receiver->stop();
                    hdph_started = false;
                    break;
                case event_bridge::CONNECTION_STATE:
                    break;
                case event_bridge::REQUEST_VOL_DATA:
                    break;
            }
        }

        thread_t::sleep(15); // for wdt TODO: figure out exact timings

        if (!hdph_started) {
            continue;
        }

        if (headphones->get_cur_state() == controller_t::DISCONNECT
            && get_time_ms() - rq_conn_stamp > 500) {

            rq_conn_stamp = get_time_ms();
            send_state(controller_t::FULL);
        }

        if (headphones->get_cur_state() == controller_t::FULL) {
            int rx_available = stream_bridge::bytes_ready_to_read();
            if (rx_available) {
                uint8_t rx_buf[rx_available];
                stream_bridge::read(rx_buf, rx_available);
                sender->send(rx_buf, rx_available);
            }
        }

    }
}

event_bridge::source_t net_transport::init() {
    headphones = new headphones_t();
    sender = new sender_t(reinterpret_cast<controller_t *>(headphones), DMA_BUF_SIZE);
    receiver = new receiver_t(reinterpret_cast<controller_t *>(headphones), DMA_BUF_SIZE);

    receiver->set_receive_callback(receive_handler, nullptr);

    evt_source = event_bridge::create_source();
    evt_incoming = event_bridge::create_source();
    event_bridge::set_service_listener(evt_source, evt_incoming);

    evt_thread = new thread_t(net_loop, nullptr);
    evt_thread->launch();

    return evt_source;
}

// TODO: write net_transport::deinit
