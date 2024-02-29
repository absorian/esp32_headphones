#include "event_bridge.h"

static esp_event_loop_handle_t evt_loop = nullptr;

esp_err_t event_bridge::init() {
    if (evt_loop) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_event_loop_args_t loop_args = {
            .queue_size = 16,
            .task_name = "evt_bridge",
            .task_priority = 15,
            .task_stack_size = 4096,
            .task_core_id = 0
    };

    esp_err_t err;

    err = esp_event_loop_create(&loop_args, &evt_loop);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t
event_bridge::set_listener(esp_event_base_t event_base, esp_event_handler_t event_handler, void *event_handler_arg) {
    return set_listener_specific(event_base, ESP_EVENT_ANY_ID, event_handler, event_handler_arg);
}

esp_err_t
event_bridge::set_listener_specific(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler,
                                    void *event_handler_arg) {
    if (!evt_loop) return ESP_ERR_INVALID_STATE;
    return esp_event_handler_register_with(evt_loop, event_base, event_id, event_handler, event_handler_arg);
}

esp_err_t event_bridge::post(esp_event_base_t event_base, int32_t event_id, esp_event_base_t event_from_base,
                             data_t *event_data) {
    if (!evt_loop) return ESP_ERR_INVALID_STATE;
    if (event_data) {
        event_data->from = event_from_base;
        return esp_event_post_to(evt_loop, event_base, event_id, event_data, sizeof (data_t), portMAX_DELAY);
    }
    data_t dat = { // esp_event_post copies internally into heap
            .from = event_from_base
    };
    return esp_event_post_to(evt_loop, event_base, event_id, &dat, sizeof (data_t), portMAX_DELAY);
}
