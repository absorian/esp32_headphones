#include "bt_transport.h"
#include "sco_util.h"
#include "stream_bridge.h"
#include "common_util.h"

#include <impl.h>
#include <btstack.h>
#include <btstack_port_esp32.h>
#include <btstack_stdio_esp32.h>

#include "sdkconfig.h"
#if !CONFIG_BT_ENABLED
#error "Bluetooth disabled - please set CONFIG_BT_ENABLED via menuconfig -> Component Config -> Bluetooth -> [x] Bluetooth"
#endif
#if !CONFIG_BT_CONTROLLER_ONLY
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ONLY via menuconfig -> Component Config -> Bluetooth -> Host -> Disabled"
#endif
#if ESP_IDF_VERSION_MAJOR >= 5
#if !CONFIG_BT_CONTROLLER_ENABLED
#error "Different Bluetooth Host stack selected - please set CONFIG_BT_CONTROLLER_ENABLED via menuconfig -> Component Config -> Bluetooth -> Controller -> Enabled"
#endif
#endif

// Config

#define BT_MY_DEVICE_CLASS 0x240404
#define BT_MY_NAME "ism hdph" // TODO: make the name configurable

static const uint8_t hf_codecs[] = {
        HFP_CODEC_CVSD,
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
        HFP_CODEC_MSBC,
#endif
#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
        HFP_CODEC_LC3_SWB,
#endif
};

static const uint8_t a2dp_codec_capabilities[] = {
        0xFF, // (AVDTP_SBC_16000 << 4) | AVDTP_SBC_STEREO,
        0xFF, // (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
        2, 53 // bitpool range
};

static const uint16_t hf_supported_features =
        (1 << HFP_HFSF_ESCO_S4) |
        (1 << HFP_HFSF_CLI_PRESENTATION_CAPABILITY) |
        //            (1 << HFP_HFSF_HF_INDICATORS) |
        (1 << HFP_HFSF_CODEC_NEGOTIATION) |
        //            (1 << HFP_HFSF_ENHANCED_CALL_STATUS) |
        //            (1 << HFP_HFSF_VOICE_RECOGNITION_FUNCTION) |
        //            (1 << HFP_HFSF_ENHANCED_VOICE_RECOGNITION_STATUS) |
        //            (1 << HFP_HFSF_VOICE_RECOGNITION_TEXT) |
        (1 << HFP_HFSF_EC_NR_FUNCTION) |
        (1 << HFP_HFSF_REMOTE_VOLUME_CONTROL);

static const uint8_t rfcomm_channel_nr = 1;
static const char hf_service_name[] = "HFP HF";

#define A2DP_NUM_CHANNELS 2
#define A2DP_BYTES_PER_SAMPLE 2

static const char *TAG = "BT_TRANSPORT";

//

typedef struct {
    uint8_t reconfigure;
    uint8_t num_channels;
    uint16_t sampling_frequency;
    uint8_t block_length;
    btstack_sbc_channel_mode_t channel_mode;
} sbc_codec_conf_t;

typedef struct {
    bd_addr_t addr;
    uint16_t a2dp_cid;
    uint8_t a2dp_local_seid;
    sbc_codec_conf_t sbc_configuration;
} a2dp_sink_a2dp_connection_t;

typedef struct {
    bd_addr_t addr;
    uint16_t avrcp_cid;
    bool playing;
    uint16_t notifications_supported_by_target;
} a2dp_sink_avrcp_connection_t;

static hci_con_handle_t acl_handle = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t sco_handle = HCI_CON_HANDLE_INVALID;

static uint8_t sdp_avdtp_sink_service_buffer[150];
static uint8_t sdp_avrcp_target_service_buffer[150];
static uint8_t sdp_avrcp_controller_service_buffer[200];
static uint8_t sdp_device_id_service_buffer[100];
static uint8_t sdp_hfp_service_buffer[150];

ESP_EVENT_DEFINE_BASE(BT_TRANSPORT);

static const btstack_sbc_decoder_t *sbc_decoder_instance;
btstack_sbc_decoder_bluedroid_t sbc_decoder_context;

static uint16_t hf_indicators[1] = {0x01};
static uint8_t hf_negotiated_codec = HFP_CODEC_CVSD;

static a2dp_sink_a2dp_connection_t a2dp_conn_info;
static a2dp_sink_avrcp_connection_t avrcp_conn_info;

static avrcp_battery_status_t battery_status = AVRCP_BATTERY_STATUS_WARNING;

static void bt_stack_thread(void *);

void bt_stack_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

static thread_t *run_thread;

// Bt event callbacks

static void hci_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void a2dp_sink_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t event_size);

static void l2cap_media_cb(uint8_t seid, uint8_t *packet, uint16_t size);

static void avrcp_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void avrcp_controller_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void avrcp_target_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void hfp_hf_cb(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t event_size);

static void a2dp_pcm_data_cb(int16_t *data, int num_audio_frames, int num_channels, int sample_rate, void *context);

// Header read utils

static int read_media_data_header(uint8_t *packet, int size, int *offset, avdtp_media_packet_header_t *media_header);

static int read_sbc_header(uint8_t *packet, int size, int *offset, avdtp_sbc_codec_header_t *sbc_header);

// Defs

void bt_stack_thread(void *) {
    static btstack_packet_callback_registration_t hci_event_callback_registration;
    static uint8_t sbc_codec_configuration_dummy[4]; // or is it dummy

    btstack_stdio_init();
    btstack_init();

    l2cap_init();
    rfcomm_init();
    sdp_init();
#ifdef ENABLE_BLE
    // Initialize LE Security Manager. Needed for cross-transport key derivation
    sm_init();
#endif

    // Init profiles
    hfp_hf_init(rfcomm_channel_nr);
    hfp_hf_init_supported_features(hf_supported_features);
    hfp_hf_init_hf_indicators(sizeof(hf_indicators) / sizeof(uint16_t), hf_indicators);
    hfp_hf_init_codecs(sizeof(hf_codecs), hf_codecs);
    hfp_hf_register_packet_handler(hfp_hf_cb);

    a2dp_sink_init();
    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();

    // Configure A2DP Sink
    a2dp_sink_register_packet_handler(&a2dp_sink_cb);
    a2dp_sink_register_media_handler(&l2cap_media_cb);
    avdtp_stream_endpoint_t *local_stream_endpoint =
            a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
                                             a2dp_codec_capabilities, sizeof(a2dp_codec_capabilities),
                                             sbc_codec_configuration_dummy,
                                             sizeof(sbc_codec_configuration_dummy));
    btstack_assert(local_stream_endpoint != nullptr);
    // - Store stream enpoint's SEP ID, as it is used by A2DP API to identify the stream endpoint
//    stream_endpoint->a2dp_local_seid = avdtp_local_seid(local_stream_endpoint);

    // Configure AVRCP Controller + Target
    avrcp_register_packet_handler(&avrcp_cb);
    avrcp_controller_register_packet_handler(&avrcp_controller_cb);
    avrcp_target_register_packet_handler(&avrcp_target_cb);

    // Configure SDP
    // - Create and register A2DP Sink service record
    memset(sdp_avdtp_sink_service_buffer, 0, sizeof(sdp_avdtp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_avdtp_sink_service_buffer, sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE, nullptr, nullptr);
    btstack_assert(de_get_len(sdp_avdtp_sink_service_buffer) <= sizeof(sdp_avdtp_sink_service_buffer));
    sdp_register_service(sdp_avdtp_sink_service_buffer);
    // - Create AVRCP Controller service record and register it with SDP. We send Category 1 commands to the media player, e.g. play/pause
    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    uint16_t controller_supported_features = 1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER;
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, sdp_create_service_record_handle(),
                                       controller_supported_features, nullptr, nullptr);
    btstack_assert(de_get_len(sdp_avrcp_controller_service_buffer) <= sizeof(sdp_avrcp_controller_service_buffer));
    sdp_register_service(sdp_avrcp_controller_service_buffer);
    // - Create and register A2DP Sink service record
    //   -  We receive Category 2 commands from the media player, e.g. volume up/down
    memset(sdp_avrcp_target_service_buffer, 0, sizeof(sdp_avrcp_target_service_buffer));
    uint16_t target_supported_features = 1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER;
    avrcp_target_create_sdp_record(sdp_avrcp_target_service_buffer,
                                   sdp_create_service_record_handle(), target_supported_features, nullptr, NULL);
    btstack_assert(de_get_len(sdp_avrcp_target_service_buffer) <= sizeof(sdp_avrcp_target_service_buffer));
    sdp_register_service(sdp_avrcp_target_service_buffer);
    // - Create and register Device ID (PnP) service record
    memset(sdp_device_id_service_buffer, 0, sizeof(sdp_device_id_service_buffer));
    device_id_create_sdp_record(sdp_device_id_service_buffer,
                                sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len(sdp_device_id_service_buffer) <= sizeof(sdp_device_id_service_buffer));
    sdp_register_service(sdp_device_id_service_buffer);
    // - Create and register HFP HF service record
    memset(sdp_hfp_service_buffer, 0, sizeof(sdp_hfp_service_buffer));
    hfp_hf_create_sdp_record_with_codecs(sdp_hfp_service_buffer, sdp_create_service_record_handle(),
                                         rfcomm_channel_nr, hf_service_name, hf_supported_features, sizeof(hf_codecs),
                                         hf_codecs);
    btstack_assert(de_get_len(sdp_hfp_service_buffer) <= sizeof(sdp_hfp_service_buffer));
    sdp_register_service(sdp_hfp_service_buffer);

    // Configure GAP - discovery / connection
    // - Set local name with a template Bluetooth address, that will be automatically
    //   replaced with an actual address once it is available, i.e. when BTstack boots
    //   up and starts talking to a Bluetooth module.
    gap_set_local_name(BT_MY_NAME);
    // - Allow to show up in Bluetooth inquiry
    gap_discoverable_control(1);
    // - Set Class of Device - Service Class: Audio, Major Device Class: Audio, Minor: Headphones
    gap_set_class_of_device(BT_MY_DEVICE_CLASS);
    // - Allow for role switch in general and sniff mode
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH); // TODO: try to restore sniffing
    // - Allow for role switch on outgoing connections
    //   - This allows A2DP Source, e.g. smartphone, to become master when we re-connect to it.
    gap_set_allow_role_switch(true);

    // Register for HCI events
    hci_event_callback_registration.callback = &hci_cb;
    hci_add_event_handler(&hci_event_callback_registration);
    hci_register_sco_packet_handler(&hci_cb);

    sco_util::init();

    logi(TAG, "btstack loop start");
    hci_power_control(HCI_POWER_OFF); // run loop will hang without this (don't know why)
    btstack_run_loop_execute();
}

void bt_stack_event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto dat = reinterpret_cast<event_bridge::data_t *>(event_data);
    switch (static_cast<event_bridge::cmd_t>(event_id)) {
        case event_bridge::SVC_START:
            logi(TAG, "starting up HCI");
            hci_power_control(HCI_POWER_ON);
            break;
        case event_bridge::SVC_PAUSE:
            logi(TAG, "shutting down HCI");
//            if (sco_handle != HCI_CON_HANDLE_INVALID)
//                gap_disconnect(sco_handle);
//            if (acl_handle != HCI_CON_HANDLE_INVALID)
            gap_disconnect(acl_handle);
            hci_power_control(HCI_POWER_OFF);
            break;
        case event_bridge::VOL_DATA_MIC: {
            if (acl_handle == HCI_CON_HANDLE_INVALID) break;
            logi(TAG, "changing mic volume");
            int gain = map(dat->absolute_volume, 0, 127, 0, 15);
            hfp_hf_set_microphone_gain(acl_handle, gain);
            break;
        }
        case event_bridge::VOL_DATA_SPK: {
            if (acl_handle == HCI_CON_HANDLE_INVALID) break;
            logi(TAG, "changing spk volume");
            avrcp_target_volume_changed(avrcp_conn_info.avrcp_cid, dat->absolute_volume);
            int gain = map(dat->absolute_volume, 0, 127, 0, 15);
            hfp_hf_set_speaker_gain(acl_handle, gain);
            break;
        }
        default:
            break;
    }
}

void bt_transport::init() { // TODO: write bt_transport::deinit
    event_bridge::set_listener(BT_TRANSPORT, bt_stack_event_handler);

    run_thread = new thread_t(bt_stack_thread, nullptr, 15, 8192);
    run_thread->launch();
}

void hci_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    bd_addr_t event_addr;

    switch (packet_type) {
        case HCI_SCO_DATA_PACKET:
            if (READ_SCO_CONNECTION_HANDLE(packet) != sco_handle) break;
            sco_util::receive(packet, size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case BTSTACK_EVENT_STATE:
                    logi(TAG, "HCI state %d", btstack_event_state_get_state(packet));
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    logi(TAG, "HCI pin code request - using '0000'");
                    hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_util::send(sco_handle);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void a2dp_sink_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t event_size) {
    uint8_t status;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (packet[2]) {
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
            logi(TAG, "A2DP received non SBC codec - not implemented");
            break;
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
            logi(TAG, "A2DP received SBC codec configuration");
            a2dp_conn_info.sbc_configuration.reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(
                    packet);
            a2dp_conn_info.sbc_configuration.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(
                    packet);
            a2dp_conn_info.sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(
                    packet);
            a2dp_conn_info.sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(
                    packet);

            switch (a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet)) {
                case AVDTP_CHANNEL_MODE_JOINT_STEREO:
                    a2dp_conn_info.sbc_configuration.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_STEREO:
                    a2dp_conn_info.sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_DUAL_CHANNEL:
                    a2dp_conn_info.sbc_configuration.channel_mode = SBC_CHANNEL_MODE_DUAL_CHANNEL;
                    break;
                case AVDTP_CHANNEL_MODE_MONO:
                    a2dp_conn_info.sbc_configuration.channel_mode = SBC_CHANNEL_MODE_MONO;
                    break;
                default:
                    btstack_assert(false);
                    break;
            }
            stream_bridge::configure_sink(a2dp_conn_info.sbc_configuration.sampling_frequency,
                                          a2dp_conn_info.sbc_configuration.num_channels,
                                          a2dp_conn_info.sbc_configuration.block_length);
            break;
        }

        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                loge(TAG, "A2DP streaming connection failed, status 0x%02x", status);
                break;
            }

            a2dp_subevent_stream_established_get_bd_addr(packet, a2dp_conn_info.addr);
            a2dp_conn_info.a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
            a2dp_conn_info.a2dp_local_seid = a2dp_subevent_stream_established_get_local_seid(packet);

            logi(TAG, "A2DP streaming connection is established");
            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_RQ, BT_TRANSPORT);
            break;
        case A2DP_SUBEVENT_STREAM_STARTED: {
            logi(TAG, "A2DP stream started");
            if (a2dp_conn_info.sbc_configuration.reconfigure) {
                logi(TAG, "A2DP stream reconfigure");
            }

            sbc_decoder_instance = btstack_sbc_decoder_bluedroid_init_instance(&sbc_decoder_context);
            sbc_decoder_instance->configure(&sbc_decoder_context, SBC_MODE_STANDARD, a2dp_pcm_data_cb, nullptr);
            break;
        }
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            logi(TAG, "A2DP stream paused");
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            logi(TAG, "A2DP stream released");
            break;

        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            logi(TAG, "A2DP signaling connection released");
            a2dp_conn_info.a2dp_cid = 0;
            break;

        default:
            break;
    }
}

void l2cap_media_cb(uint8_t seid, uint8_t *packet, uint16_t size) {
    int pos = 0;

    avdtp_media_packet_header_t media_header;
    if (!read_media_data_header(packet, size, &pos, &media_header)) return;

    avdtp_sbc_codec_header_t sbc_header;
    if (!read_sbc_header(packet, size, &pos, &sbc_header)) return;

    int packet_length = size - pos;
    uint8_t *packet_begin = packet + pos;

    sbc_decoder_instance->decode_signed_16(&sbc_decoder_context, 0, packet_begin, packet_length);
}

void avrcp_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    uint16_t local_cid;
    uint8_t status;
    bd_addr_t address;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (packet[2]) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                loge(TAG, "AVRCP connection failed, status 0x%02x", status);
                avrcp_conn_info.avrcp_cid = 0;
                return;
            }

            avrcp_conn_info.avrcp_cid = local_cid;
            avrcp_subevent_connection_established_get_bd_addr(packet, address);
            logi(TAG, "AVRCP connected to %s, cid 0x%02x", bd_addr_to_str(address), avrcp_conn_info.avrcp_cid);

            avrcp_target_support_event(avrcp_conn_info.avrcp_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            avrcp_target_support_event(avrcp_conn_info.avrcp_cid, AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED);
            avrcp_target_battery_status_changed(avrcp_conn_info.avrcp_cid, battery_status);

            // query supported events:
            avrcp_controller_get_supported_events(avrcp_conn_info.avrcp_cid);
            return;
        }

        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            logi(TAG, "AVRCP channel released: cid 0x%02x", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            avrcp_conn_info.avrcp_cid = 0;
            avrcp_conn_info.notifications_supported_by_target = 0;
            return;
        default:
            break;
    }
}

void avrcp_controller_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    if (avrcp_conn_info.avrcp_cid == 0) return;

    switch (packet[2]) {
        case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID:
            avrcp_conn_info.notifications_supported_by_target |= (1
                    << avrcp_subevent_get_capability_event_id_get_event_id(packet));
            break;
        case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE: {
            logi(TAG, "AVRCP_CT supported notifications by target:");
            for (auto event_id = (uint8_t) AVRCP_NOTIFICATION_EVENT_FIRST_INDEX;
                 event_id < (uint8_t) AVRCP_NOTIFICATION_EVENT_LAST_INDEX; event_id++) {
                logi(TAG, "   - [%s] %s",
                     (avrcp_conn_info.notifications_supported_by_target & (1 << event_id)) != 0 ? "X" : " ",
                     avrcp_notification2str((avrcp_notification_event_id_t) event_id));
            }

            // automatically enable notifications
            avrcp_controller_enable_notification(avrcp_conn_info.avrcp_cid,
                                                 AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(avrcp_conn_info.avrcp_cid,
                                                 AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(avrcp_conn_info.avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);

            break;
        }
        case AVRCP_SUBEVENT_NOTIFICATION_STATE: {
            uint8_t event_id = (avrcp_notification_event_id_t) avrcp_subevent_notification_state_get_event_id(packet);
            logi(TAG, "AVRCP_CT %s notification registered", avrcp_notification2str(
                    static_cast<avrcp_notification_event_id_t>(event_id)));
            break;
        }
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED: {
            logi(TAG, "AVRCP_CT playback status changed %s",
                 avrcp_play_status2str(avrcp_subevent_notification_playback_status_changed_get_play_status(packet)));
            uint8_t play_status = avrcp_subevent_notification_playback_status_changed_get_play_status(packet);
            switch (play_status) {
                case AVRCP_PLAYBACK_STATUS_PLAYING:
                    avrcp_conn_info.playing = true;
                    break;
                default:
                    avrcp_conn_info.playing = false;
                    break;
            }
            break;
        }
        case AVRCP_SUBEVENT_OPERATION_COMPLETE:
            logi(TAG, "AVRCP_CT %s complete",
                 avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;

        case AVRCP_SUBEVENT_OPERATION_START:
            logi(TAG, "AVRCP_CT %s start",
                 avrcp_operation2str(avrcp_subevent_operation_start_get_operation_id(packet)));
            break;

        case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
            logi(TAG, "AVRCP_CT set Player App Value %s",
                 avrcp_ctype2str(avrcp_subevent_player_application_value_response_get_command_type(packet)));
            break;
        default:
            break;
    }
}

void avrcp_target_cb(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    switch (packet[2]) {
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED: {
            uint8_t volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
            logi(TAG, "AVRCP_TG volume set to %d", volume);
            event_bridge::data_t evt_data = {
                    .absolute_volume = volume
            };
            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_SPK, BT_TRANSPORT, &evt_data);
            break;
        }
        case AVRCP_SUBEVENT_OPERATION: {
            auto operation_id = static_cast<avrcp_operation_id_t>(avrcp_subevent_operation_get_operation_id(
                    packet));
            const char *button_state = avrcp_subevent_operation_get_button_pressed(packet) > 0 ? "PRESS" : "RELEASE";
            switch (operation_id) {
                case AVRCP_OPERATION_ID_VOLUME_UP:
                    logi(TAG, "AVRCP_TG volume up (%s)", button_state);
                    break;
                case AVRCP_OPERATION_ID_VOLUME_DOWN:
                    logi(TAG, "AVRCP_TG volume down (%s)", button_state);
                    break;
                default:
                    return;
            }
            break;
        }
        default:
            logi(TAG, "AVRCP_TG event 0x%02x is not parsed", packet[2]);
            break;
    }
}

void hfp_hf_cb(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t event_size) {
    static uint8_t hfp_first_vol_set = 0;
    uint8_t status;
    bd_addr_t event_addr;

    switch (packet_type) {
        case HCI_SCO_DATA_PACKET:
            if (READ_SCO_CONNECTION_HANDLE(event) != sco_handle) break;
            sco_util::receive(event, event_size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(event)) {
                case HCI_EVENT_PIN_CODE_REQUEST: // Duplicate from hci_cb, is it really needed?
                    // inform about pin code request
                    logi(TAG, "?Pin code request - using '0000'");
                    hci_event_pin_code_request_get_bd_addr(event, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_util::send(sco_handle);
                    break;

                case HCI_EVENT_HFP_META:
                    switch (hci_event_hfp_meta_get_subevent_code(event)) {
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_service_level_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                loge(TAG, "HFP connection failed, status 0x%02x", status);
                                break;
                            }
                            acl_handle = hfp_subevent_service_level_connection_established_get_acl_handle(event);
                            bd_addr_t device_addr;
                            hfp_subevent_service_level_connection_established_get_bd_addr(event, device_addr);
                            logi(TAG, "HFP service level connection established %s", bd_addr_to_str(device_addr));
                            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_RQ, BT_TRANSPORT);
                            hfp_first_vol_set = 0;
                            break;

                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
                            acl_handle = HCI_CON_HANDLE_INVALID;
                            logi(TAG, "HFP service level connection released");
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_audio_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                loge(TAG, "HFP audio connection establishment failed with status 0x%02x", status);
                                break;
                            }
                            sco_handle = hfp_subevent_audio_connection_established_get_sco_handle(event);
                            logi(TAG, "HFP audio connection established with SCO handle 0x%04x", sco_handle);
                            hf_negotiated_codec = hfp_subevent_audio_connection_established_get_negotiated_codec(event);
                            switch (hf_negotiated_codec) {
                                case HFP_CODEC_CVSD:
                                    logi(TAG, "HFP using CVSD codec");
                                    break;
                                case HFP_CODEC_MSBC:
                                    logi(TAG, "HFP using mSBC codec");
                                    break;
                                case HFP_CODEC_LC3_SWB:
                                    logi(TAG, "HFP using LC3-SWB codec");
                                    break;
                                default:
                                    logi(TAG, "HFP using unknown codec 0x%02x", hf_negotiated_codec);
                                    break;
                            }
                            sco_util::set_codec(hf_negotiated_codec);
                            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_RQ, BT_TRANSPORT);
                            hci_request_sco_can_send_now_event();
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED: {
                            sco_handle = HCI_CON_HANDLE_INVALID;
                            logi(TAG, "HFP audio connection released");
                            sco_util::close();
                            stream_bridge::configure_sink(a2dp_conn_info.sbc_configuration.sampling_frequency,
                                                          a2dp_conn_info.sbc_configuration.num_channels,
                                                          a2dp_conn_info.sbc_configuration.block_length);
                            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_RQ, BT_TRANSPORT);
                            break;
                        }
                        case HFP_SUBEVENT_COMPLETE: // Check status of the client-sent cmd
                            status = hfp_subevent_complete_get_status(event);
                            if (status == ERROR_CODE_SUCCESS) {
//                                printf("Cmd \'%c\' succeeded\n", cmd);
                            } else {
//                                printf("Cmd \'%c\' failed with status 0x%02x\n", cmd, status);
                            }
                            break;
                        case HFP_SUBEVENT_SPEAKER_VOLUME: {
                            if (hfp_first_vol_set != 2) {
                                hfp_first_vol_set++;
                                break;
                            }
                            auto gain = hfp_subevent_speaker_volume_get_gain(event);
                            logi(TAG, "HFP speaker volume: gain %u", gain);
                            event_bridge::data_t evt_data = {
                                    .absolute_volume = static_cast<uint8_t>(map(gain, 0, 15, 0, 127))
                            };
                            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_SPK, BT_TRANSPORT, &evt_data);
                            break;
                        }
                        case HFP_SUBEVENT_MICROPHONE_VOLUME: {
                            if (hfp_first_vol_set != 2) {
                                hfp_first_vol_set++;
                                break;
                            }
                            auto gain = hfp_subevent_microphone_volume_get_gain(event);
                            logi(TAG, "HFP microphone volume: gain %u", gain);
                            event_bridge::data_t evt_data = {
                                    .absolute_volume = static_cast<uint8_t>(map(gain, 0, 15, 0, 127))
                            };
                            event_bridge::post(APPLICATION, event_bridge::VOL_DATA_MIC, BT_TRANSPORT, &evt_data);
                            break;
                        }
                        default:
                            break;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void a2dp_pcm_data_cb(int16_t *data, int num_audio_frames, int num_channels, int sample_rate, void *context) {
    stream_bridge::write(reinterpret_cast<char *>(data), num_audio_frames * A2DP_NUM_CHANNELS * A2DP_BYTES_PER_SAMPLE);
}

int read_media_data_header(uint8_t *packet, int size, int *offset, avdtp_media_packet_header_t *media_header) {
    int media_header_len = 12; // without crc
    int pos = *offset;

    if (size - pos < media_header_len) {
        loge(TAG, "Not enough data to read media packet header, expected %d, received %d", media_header_len,
             size - pos);
        return 0;
    }

    media_header->version = packet[pos] & 0x03;
    media_header->padding = get_bit16(packet[pos], 2);
    media_header->extension = get_bit16(packet[pos], 3);
    media_header->csrc_count = (packet[pos] >> 4) & 0x0F;
    pos++;

    media_header->marker = get_bit16(packet[pos], 0);
    media_header->payload_type = (packet[pos] >> 1) & 0x7F;
    pos++;

    media_header->sequence_number = big_endian_read_16(packet, pos);
    pos += 2;

    media_header->timestamp = big_endian_read_32(packet, pos);
    pos += 4;

    media_header->synchronization_source = big_endian_read_32(packet, pos);
    pos += 4;
    *offset = pos;
    return 1;
}

int read_sbc_header(uint8_t *packet, int size, int *offset, avdtp_sbc_codec_header_t *sbc_header) {
    int sbc_header_len = 12; // without crc
    int pos = *offset;

    if (size - pos < sbc_header_len) {
        loge(TAG, "Not enough data to read SBC header, expected %d, received %d", sbc_header_len, size - pos);
        return 0;
    }

    sbc_header->fragmentation = get_bit16(packet[pos], 7);
    sbc_header->starting_packet = get_bit16(packet[pos], 6);
    sbc_header->last_packet = get_bit16(packet[pos], 5);
    sbc_header->num_frames = packet[pos] & 0x0f;
    pos++;
    *offset = pos;
    return 1;
}

