#include "event_bridge.h"

#include <impl.h>

esp_err_t
event_bridge::set_listener(esp_event_base_t event_base, esp_event_handler_t event_handler, void *event_handler_arg) {
    return set_listener_specific(event_base, ESP_EVENT_ANY_ID, event_handler, event_handler_arg);
}

esp_err_t
event_bridge::set_listener_specific(esp_event_base_t event_base, int32_t event_id, esp_event_handler_t event_handler,
                                    void *event_handler_arg) {
    return esp_event_handler_register(event_base, event_id, event_handler, event_handler_arg);
}

esp_err_t event_bridge::post(esp_event_base_t event_base, int32_t event_id, esp_event_base_t event_from_base,
                             data_t *event_data) {
    if (event_data) {
        event_data->from = event_from_base;
        return esp_event_post(event_base, event_id, event_data, sizeof (data_t), portMAX_DELAY);
    }
    data_t dat = { // esp_event_post copies internally into heap
            .from = event_from_base
    };
    return esp_event_post(event_base, event_id, &dat, sizeof (data_t), portMAX_DELAY);
}
