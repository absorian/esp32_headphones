//
// Created by ism on 05.01.2023.
//
#include <cstring>
#include "esp_log.h"

#include "hf_handle.h"
#include "hf_status.h"

#include "bluetooth_service.h"
#include "raw_stream.h"

static const char *TAG = "BT_HF";
static bool is_get_hfp = true;

extern audio_element_handle_t bt_stream_reader;
extern audio_element_handle_t raw_read;

static void bt_app_hf_client_audio_open(void) {
    ESP_LOGE(TAG, "bt_app_hf_client_audio_open");
    audio_element_info_t bt_info = {0};
    audio_element_getinfo(bt_stream_reader, &bt_info);
    bt_info.sample_rates = HFP_RESAMPLE_RATE;
    bt_info.channels = 1;
    bt_info.bits = 16;
    audio_element_setinfo(bt_stream_reader, &bt_info);
    audio_element_report_info(bt_stream_reader);
}

static void bt_app_hf_client_audio_close(void) {
    ESP_LOGE(TAG, "bt_app_hf_client_audio_close");
    int sample_rate = periph_bluetooth_get_a2dp_sample_rate();
    audio_element_info_t bt_info = {0};
    audio_element_getinfo(bt_stream_reader, &bt_info);
//    bt_info.sample_rates = a2dp_sample_rate;
    bt_info.sample_rates = sample_rate;
    bt_info.channels = 2;
    bt_info.bits = 16;
    audio_element_setinfo(bt_stream_reader, &bt_info);
    audio_element_report_info(bt_stream_reader);
}

static uint32_t bt_app_hf_client_outgoing_cb(uint8_t *p_buf, uint32_t sz) {
    int out_len_bytes = 0;
    char *enc_buffer = (char *) audio_malloc(sz);
    AUDIO_MEM_CHECK(TAG, enc_buffer, return 0)
    if (is_get_hfp) {
        out_len_bytes = raw_stream_read(raw_read, enc_buffer, sz);
    }

    if (out_len_bytes == sz) {
        is_get_hfp = false;
        memcpy(p_buf, enc_buffer, out_len_bytes);
        free(enc_buffer);
        return sz;
    } else {
        is_get_hfp = true;
        free(enc_buffer);
        return 0;
    }
}

static void bt_app_hf_client_incoming_cb(const uint8_t *buf, uint32_t sz) {
    if (bt_stream_reader) {
        if (audio_element_get_state(bt_stream_reader) == AEL_STATE_RUNNING) {
            audio_element_output(bt_stream_reader, (char *) buf, sz);
            esp_hf_client_outgoing_data_ready();
        }
    }
}

/* callback for HF_CLIENT */
void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param) {
    if (event <= ESP_HF_CLIENT_RING_IND_EVT) {
        ESP_LOGE(TAG, "APP HFP event: %s", c_hf_evt_str[event]);
    } else {
        ESP_LOGE(TAG, "APP HFP invalid event %d", event);
    }

    switch (event) {
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
            ESP_LOGE(TAG, "--connection state %s, peer feats 0x%lx, chld_feats 0x%lx",
                     c_connection_state_str[param->conn_stat.state],
                     param->conn_stat.peer_feat,
                     param->conn_stat.chld_feat);
            break;
        case ESP_HF_CLIENT_AUDIO_STATE_EVT:
            ESP_LOGE(TAG, "--audio state %s",
                     c_audio_state_str[param->audio_stat.state]);
#if CONFIG_HFP_AUDIO_DATA_PATH_HCI
            if ((param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED)
                || (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC)) {
                bt_app_hf_client_audio_open();
                esp_hf_client_register_data_callback(bt_app_hf_client_incoming_cb,
                                                     bt_app_hf_client_outgoing_cb);
            } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
                bt_app_hf_client_audio_close();
            }
#endif /* #if CONFIG_HFP_AUDIO_DATA_PATH_HCI */
            break;
        case ESP_HF_CLIENT_BVRA_EVT:
            ESP_LOGE(TAG, "--VR state %s",
                     c_vr_state_str[param->bvra.value]);
            break;
        case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT:
            ESP_LOGE(TAG, "--NETWORK STATE %s",
                     c_service_availability_status_str[param->service_availability.status]);
            break;
        case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT:
            ESP_LOGE(TAG, "--ROAMING: %s",
                     c_roaming_status_str[param->roaming.status]);
            break;
        case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT:
            ESP_LOGE(TAG, "-- signal strength: %d",
                     param->signal_strength.value);
            break;
        case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT:
            ESP_LOGE(TAG, "--battery level %d",
                     param->battery_level.value);
            break;
        case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT:
            ESP_LOGE(TAG, "--operator name: %s",
                     param->cops.name);
            break;
        case ESP_HF_CLIENT_CIND_CALL_EVT:
            ESP_LOGE(TAG, "--Call indicator %s",
                     c_call_str[param->call.status]);
            break;
        case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
            ESP_LOGE(TAG, "--Call setup indicator %s",
                     c_call_setup_str[param->call_setup.status]);
            break;
        case ESP_HF_CLIENT_CIND_CALL_HELD_EVT:
            ESP_LOGE(TAG, "--Call held indicator %s",
                     c_call_held_str[param->call_held.status]);
            break;
        case ESP_HF_CLIENT_BTRH_EVT:
            ESP_LOGE(TAG, "--response and hold %s",
                     c_resp_and_hold_str[param->btrh.status]);
            break;
        case ESP_HF_CLIENT_CLIP_EVT:
            ESP_LOGE(TAG, "--clip number %s",
                     (param->clip.number == NULL) ? "NULL" : (param->clip.number));
            break;
        case ESP_HF_CLIENT_CCWA_EVT:
            ESP_LOGE(TAG, "--call_waiting %s",
                     (param->ccwa.number == NULL) ? "NULL" : (param->ccwa.number));
            break;
        case ESP_HF_CLIENT_CLCC_EVT:
            ESP_LOGE(TAG, "--Current call: idx %d, dir %s, state %s, mpty %s, number %s",
                     param->clcc.idx,
                     c_call_dir_str[param->clcc.dir],
                     c_call_state_str[param->clcc.status],
                     c_call_mpty_type_str[param->clcc.mpty],
                     (param->clcc.number == NULL) ? "NULL" : (param->clcc.number));
            break;
        case ESP_HF_CLIENT_VOLUME_CONTROL_EVT:
            ESP_LOGE(TAG, "--volume_target: %s, volume %d",
                     c_volume_control_target_str[param->volume_control.type],
                     param->volume_control.volume);
            break;
        case ESP_HF_CLIENT_AT_RESPONSE_EVT:
            ESP_LOGE(TAG, "--AT response event, code %d, cme %d",
                     param->at_response.code, param->at_response.cme);
            break;
        case ESP_HF_CLIENT_CNUM_EVT:
            ESP_LOGE(TAG, "--subscriber type %s, number %s",
                     c_subscriber_service_type_str[param->cnum.type],
                     (param->cnum.number == NULL) ? "NULL" : param->cnum.number);
            break;
        case ESP_HF_CLIENT_BSIR_EVT:
            ESP_LOGE(TAG, "--inband ring state %s",
                     c_inband_ring_state_str[param->bsir.state]);
            break;
        case ESP_HF_CLIENT_BINP_EVT:
            ESP_LOGE(TAG, "--last voice tag number: %s",
                     (param->binp.number == NULL) ? "NULL" : param->binp.number);
            break;
        default:
            ESP_LOGE(TAG, "HF_CLIENT EVT: %d", event);
            break;
    }
}
