#include "event_bridge.h"

#include <audio_event_iface.h>
#include <audio_common.h>
#include <impl.h>

#define IFACE_QUEUE_SIZE 7
#define IFACE_LISTEN_WAIT_TIME_MS 5 // TODO: remove wait time

// Assuming only single transport will be used at a time bidirectionally
static event_bridge::data_t evt_data_arr[IFACE_QUEUE_SIZE * 2];
static mutex_t evt_data_mut;

static void send_event(event_bridge::source_t source, event_bridge::cmd_t cmd, event_bridge::data_t *data);

void send_event(event_bridge::source_t source, event_bridge::cmd_t cmd, event_bridge::data_t *data, bool svc) {
    event_bridge::message_t msg;
    msg.cmd = cmd;
    msg.data = data;
    msg.data_len = sizeof(event_bridge::data_t);
    msg.need_free_data = false;
    msg.source = source;
    if (svc) {
        msg.source_type = AUDIO_ELEMENT_TYPE_SERVICE;
        audio_event_iface_sendout(source, &msg);
    } else {
        msg.source_type = AUDIO_ELEMENT_TYPE_PLAYER;
        audio_event_iface_cmd(source, &msg);
    }
}

event_bridge::data_t *event_bridge::get_data_container() {
    static int ptr = 0;
    evt_data_mut.lock();
    if (ptr == IFACE_QUEUE_SIZE * 2) ptr = 0;
    event_bridge::data_t *dp = evt_data_arr + ptr++;
    evt_data_mut.unlock();
    return dp;
}

event_bridge::source_t event_bridge::create_source() {
    audio_event_iface_cfg_t evt_iface_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_iface_cfg.internal_queue_size = IFACE_QUEUE_SIZE;
    evt_iface_cfg.external_queue_size = IFACE_QUEUE_SIZE;
    return audio_event_iface_init(&evt_iface_cfg);
}

void
event_bridge::send_service_event(event_bridge::source_t source, event_bridge::cmd_t cmd, event_bridge::data_t *data) {
    send_event(source, cmd, data, true);
}

void
event_bridge::send_client_event(event_bridge::source_t source, event_bridge::cmd_t cmd, event_bridge::data_t *data) {
    send_event(source, cmd, data, false);
}

void event_bridge::set_client_listener(event_bridge::source_t source, event_bridge::source_t listener) {
    audio_event_iface_set_listener(source, listener);
}

void event_bridge::set_service_listener(event_bridge::source_t source, event_bridge::source_t listener) {
    audio_event_iface_set_msg_listener(source, listener);
}

int event_bridge::listen(event_bridge::source_t source, event_bridge::message_t *msg, bool block) {
    return audio_event_iface_listen(source, msg, block ? portMAX_DELAY : pdMS_TO_TICKS(IFACE_LISTEN_WAIT_TIME_MS));
}
