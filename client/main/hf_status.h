//
// Created by ism on 05.01.2023.
//

#ifndef HEADPHONES_IDF_HF_STATUS_H
#define HEADPHONES_IDF_HF_STATUS_H

const char *c_hf_evt_str[] = {
        "CONNECTION_STATE_EVT",              /*!< connection state changed event */
        "AUDIO_STATE_EVT",                   /*!< audio connection state change event */
        "VR_STATE_CHANGE_EVT",                /*!< voice recognition state changed */
        "CALL_IND_EVT",                      /*!< call indication event */
        "CALL_SETUP_IND_EVT",                /*!< call setup indication event */
        "CALL_HELD_IND_EVT",                 /*!< call held indicator event */
        "NETWORK_STATE_EVT",                 /*!< network state change event */
        "SIGNAL_STRENGTH_IND_EVT",           /*!< signal strength indication event */
        "ROAMING_STATUS_IND_EVT",            /*!< roaming status indication event */
        "BATTERY_LEVEL_IND_EVT",             /*!< battery level indication event */
        "CURRENT_OPERATOR_EVT",              /*!< current operator name event */
        "RESP_AND_HOLD_EVT",                 /*!< response and hold event */
        "CLIP_EVT",                          /*!< Calling Line Identification notification event */
        "CALL_WAITING_EVT",                  /*!< call waiting notification */
        "CLCC_EVT",                          /*!< listing current calls event */
        "VOLUME_CONTROL_EVT",                /*!< audio volume control event */
        "AT_RESPONSE",                       /*!< audio volume control event */
        "SUBSCRIBER_INFO_EVT",               /*!< subscriber information event */
        "INBAND_RING_TONE_EVT",              /*!< in-band ring tone settings */
        "LAST_VOICE_TAG_NUMBER_EVT",         /*!< requested number from AG event */
        "RING_IND_EVT",                      /*!< ring indication event */
};

// esp_hf_client_connection_state_t
const char *c_connection_state_str[] = {
        "disconnected",
        "connecting",
        "connected",
        "slc_connected",
        "disconnecting",
};

// esp_hf_client_audio_state_t
const char *c_audio_state_str[] = {
        "disconnected",
        "connecting",
        "connected",
        "connected_msbc",
};

/// esp_hf_vr_state_t
const char *c_vr_state_str[] = {
        "disabled",
        "enabled",
};

// esp_hf_service_availability_status_t
const char *c_service_availability_status_str[] = {
        "unavailable",
        "available",
};

// esp_hf_roaming_status_t
const char *c_roaming_status_str[] = {
        "inactive",
        "active",
};

// esp_hf_client_call_state_t
const char *c_call_str[] = {
        "NO call in progress",
        "call in progress",
};

// esp_hf_client_callsetup_t
const char *c_call_setup_str[] = {
        "NONE",
        "INCOMING",
        "OUTGOING_DIALING",
        "OUTGOING_ALERTING"
};

// esp_hf_client_callheld_t
const char *c_call_held_str[] = {
        "NONE held",
        "Held and Active",
        "Held",
};

// esp_hf_response_and_hold_status_t
const char *c_resp_and_hold_str[] = {
        "HELD",
        "HELD ACCEPTED",
        "HELD REJECTED",
};

// esp_hf_client_call_direction_t
const char *c_call_dir_str[] = {
        "outgoing",
        "incoming",
};

// esp_hf_client_call_state_t
const char *c_call_state_str[] = {
        "active",
        "held",
        "dialing",
        "alerting",
        "incoming",
        "waiting",
        "held_by_resp_hold",
};

// esp_hf_current_call_mpty_type_t
const char *c_call_mpty_type_str[] = {
        "single",
        "multi",
};

// esp_hf_volume_control_target_t
const char *c_volume_control_target_str[] = {
        "SPEAKER",
        "MICROPHONE"
};

// esp_hf_at_response_code_t
const char *c_at_response_code_str[] = {
        "OK",
        "ERROR",
        "ERR_NO_CARRIER",
        "ERR_BUSY",
        "ERR_NO_ANSWER",
        "ERR_DELAYED",
        "ERR_BLACKLISTED",
        "ERR_CME",
};

// esp_hf_subscriber_service_type_t
const char *c_subscriber_service_type_str[] = {
        "unknown",
        "voice",
        "fax",
};

// esp_hf_client_in_band_ring_state_t
const char *c_inband_ring_state_str[] = {
        "NOT provided",
        "Provided",
};

#endif //HEADPHONES_IDF_HF_STATUS_H
