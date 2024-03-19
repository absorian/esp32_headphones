#include "net_transport.h"

#include "stream_bridge.h"
#include "wifi_util.h"
#include "common_util.h"

#include <net_controller.h>
#include <impl/log.h>
#include <impl/concurrency.h>

#include <atomic>

static const char *TAG = "NET_TRANSPORT";

const char *HOST_ADDR = "10.242.1.61";
const uint16_t PORT = 533;

enum client_state_t {
    CL_UNINIT = 0,
    CL_NOCONN,
    CL_REQUESTING,
    CL_CONNECTED
};

static int remote_cmd_cb(net_controller::cmd_t cmd, void *);

static void receive_cb(const uint8_t *, size_t, void *);

static size_t send_cb(uint8_t *data, size_t len, void *);

[[noreturn]] static void req_sender(void *);

static void event_cb(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);


static thread_t req_sender_thread;
static std::atomic<client_state_t> net_state(CL_UNINIT);
static endpoint_t cur_endpoint;

ESP_EVENT_DEFINE_BASE(NET_TRANSPORT);

int remote_cmd_cb(net_controller::cmd_t cmd, void *) {
    logi(TAG, "Received command: %d", cmd);

    if (cmd == net_controller::ST_DISCONNECT) {
        logi(TAG, "Got disconnected");
        sender::stop();

        if (net_state != CL_NOCONN) {
            net_state = CL_REQUESTING;
            net_controller::set_cmd(net_controller::ST_FULL, false);
        }
        return 0;
    }
    if (net_state != CL_CONNECTED) {
        logi(TAG, "Connected successfully");
        // on connect
        net_state = CL_CONNECTED;
    }

    switch (cmd) {
        case net_controller::ST_SPK_ONLY:
            sender::stop();
            break;
        case net_controller::ST_FULL:
            sender::start();
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

void receive_cb(const uint8_t *data, size_t len, void *) {
    stream_bridge::write(data, len);
}

size_t send_cb(uint8_t *data, size_t len, void *) {
    if (!stream_bridge::bytes_ready_to_read()) return 0;
    return stream_bridge::read(data, len);
}

[[noreturn]] static void req_sender(void *) { // implement keep alive (just ping (or data_transfer if needed)) in net_controller itself
    while (true) {
        sender::send_md();
        thread_sleep(500);
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
            logi(TAG, "Starting up net_transport");
            if (net_state != CL_UNINIT) break;
            wifi_util::connect();
            stream_bridge::configure_sink(44100, 2, 16);
            stream_bridge::configure_source(44100, 1, 16);
            endpoint_set_port(&cur_endpoint, PORT);
            endpoint_set_addr_v4(&cur_endpoint, HOST_ADDR);
            receiver::start();
            sender::set_endpoint(&cur_endpoint);
            net_state = CL_REQUESTING;

            net_controller::set_cmd(net_controller::ST_FULL, false);
            thread_launch(&req_sender_thread);
            break;
        case event_bridge::SVC_PAUSE:
            logi(TAG, "Shutting down net_transport");
            if (net_state == CL_UNINIT) break;
            if (net_state == CL_CONNECTED) {
                net_state = CL_NOCONN;
                net_controller::set_cmd(net_controller::ST_DISCONNECT, true);
            }
            thread_terminate(&req_sender_thread);
            net_controller::reset();
            wifi_util::shutdown();
            net_state = CL_UNINIT;
            break;
        case event_bridge::VOL_DATA_RQ:
            break;
        default:
            break;
    }
}

void net_transport::init() {
    net_controller::init();

    wifi_util::init();

    net_controller::set_remote_cmd_cb(remote_cmd_cb);
    net_controller::set_remote_ack_cb(remote_cmd_cb);

    receiver::set_cb(receive_cb);
    sender::set_cb(send_cb);

    thread_init(&req_sender_thread, req_sender, "net_request_sender", 0);

    event_bridge::set_listener(NET_TRANSPORT, event_cb);
}

// TODO: write net_transport::deinit
