/*
 * Copyright (C) 2023 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL BLUEKITCHEN
 * GMBH OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "btstack_impl.cpp"

/*
 * btstack_impl.cpp
 *
 * ism:
 * C++ file form
 * Disable HAVE_BTSTACK_STDIN
 * Remove media playback & wav file storing
 * Remove cover art support
 *
 */

// *****************************************************************************
/* EXAMPLE_START(a2dp_sink_demo): A2DP Sink - Receive Audio Stream and Control Playback
 *
 * @text This A2DP Sink example demonstrates how to use the A2DP Sink service to
 * receive an audio data stream from a remote A2DP Source device. In addition,
 * the AVRCP Controller is used to get information on currently played media,
 * such are title, artist and album, as well as to control the playback,
 * i.e. to play, stop, repeat, etc. If HAVE_BTSTACK_STDIN is set, press SPACE on
 * the console to show the available AVDTP and AVRCP commands.
 *
 * @text To test with a remote device, e.g. a mobile phone,
 * pair from the remote device with the demo, then start playing music on the remote device.
 * Alternatively, set the device_addr_string to the Bluetooth address of your
 * remote device in the code, and call connect from the UI.
 *
 * @text For more info on BTstack audio, see our blog post
 * [A2DP Sink and Source on STM32 F4 Discovery Board](http://bluekitchen-gmbh.com/a2dp-sink-and-source-on-stm32-f4-discovery-board/).
 *
 */
// *****************************************************************************

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "btstack.h"
#include "btstack_config.h"
#include "audio_element.h"
#include "raw_stream.h"


#ifdef HAVE_BTSTACK_STDIN
#undef HAVE_BTSTACK_STDIN // no need for uart comm
//#include "btstack_stdin.h"
#endif

#include "sco_demo_util.h"


#define NUM_CHANNELS 2

#ifdef HAVE_BTSTACK_STDIN
static const char * device_addr_string = "00:1B:DC:08:E2:72"; // pts v5.0
static bd_addr_t device_addr;
#endif

static btstack_packet_callback_registration_t hci_event_callback_registration;

uint8_t sdp_avdtp_sink_service_buffer[150];
static uint8_t sdp_avrcp_target_service_buffer[150];
static uint8_t sdp_avrcp_controller_service_buffer[200];
static uint8_t device_id_sdp_service_buffer[100];
static uint8_t hfp_service_buffer[150];


static hci_con_handle_t acl_handle = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t sco_handle = HCI_CON_HANDLE_INVALID;


const uint8_t rfcomm_channel_nr = 1;
const char hfp_hf_service_name[] = "HFP HF Demo";

static uint8_t codecs[] = {
        HFP_CODEC_CVSD,
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
        HFP_CODEC_MSBC,
#endif
#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
        HFP_CODEC_LC3_SWB,
#endif
};

static uint16_t indicators[1] = {0x01};
static uint8_t negotiated_codec = HFP_CODEC_CVSD;
//static char cmd;

// we support all configurations with bitpool 2-53
static uint8_t media_sbc_codec_capabilities[] = {
        0xFF, // (AVDTP_SBC_16000 << 4) | AVDTP_SBC_STEREO,
        0xFF, // (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
        2, 53
};

// SBC Decoder for WAV file or live playback
static const btstack_sbc_decoder_t *sbc_decoder_instance;
static btstack_sbc_decoder_bluedroid_t sbc_decoder_context;

// sink state
static int volume_percentage = 0;
static avrcp_battery_status_t battery_status = AVRCP_BATTERY_STATUS_WARNING;

typedef struct {
    uint8_t reconfigure;
    uint8_t num_channels;
    uint16_t sampling_frequency;
    uint8_t block_length;
    uint8_t subbands;
    uint8_t min_bitpool_value;
    uint8_t max_bitpool_value;
    btstack_sbc_channel_mode_t channel_mode;
    btstack_sbc_allocation_method_t allocation_method;
} media_codec_configuration_sbc_t;

typedef enum {
    STREAM_STATE_CLOSED,
    STREAM_STATE_OPEN,
    STREAM_STATE_PLAYING,
    STREAM_STATE_PAUSED,
} stream_state_t;

typedef struct {
    uint8_t a2dp_local_seid;
    uint8_t media_sbc_codec_configuration[4];
} a2dp_sink_demo_stream_endpoint_t;
static a2dp_sink_demo_stream_endpoint_t a2dp_sink_demo_stream_endpoint;

typedef struct {
    bd_addr_t addr;
    uint16_t a2dp_cid;
    uint8_t a2dp_local_seid;
    stream_state_t stream_state;
    media_codec_configuration_sbc_t sbc_configuration;
} a2dp_sink_demo_a2dp_connection_t;
static a2dp_sink_demo_a2dp_connection_t a2dp_sink_demo_a2dp_connection;

typedef struct {
    bd_addr_t addr;
    uint16_t avrcp_cid;
    bool playing;
    uint16_t notifications_supported_by_target;
} a2dp_sink_demo_avrcp_connection_t;
static a2dp_sink_demo_avrcp_connection_t a2dp_sink_demo_avrcp_connection;

/* @section Main Application Setup
 *
 * @text The Listing MainConfiguration shows how to set up AD2P Sink and AVRCP services.
 * Besides calling init() method for each service, you'll also need to register several packet handlers:
 * - hci_packet_handler - handles legacy pairing, here by using fixed '0000' pin code.
 * - a2dp_sink_packet_handler - handles events on stream connection status (established, released), the media codec configuration, and, the status of the stream itself (opened, paused, stopped).
 * - handle_l2cap_media_data_packet - used to receive streaming data. If STORE_TO_WAV_FILE directive (check btstack_config.h) is used, the SBC decoder will be used to decode the SBC data into PCM frames. The resulting PCM frames are then processed in the SBC Decoder callback.
 * - avrcp_packet_handler - receives AVRCP connect/disconnect event.
 * - avrcp_controller_packet_handler - receives answers for sent AVRCP commands.
 * - avrcp_target_packet_handler - receives AVRCP commands, and registered notifications.
 * - stdin_process - used to trigger AVRCP commands to the A2DP Source device, such are get now playing info, start, stop, volume control. Requires HAVE_BTSTACK_STDIN.
 *
 * @text To announce A2DP Sink and AVRCP services, you need to create corresponding
 * SDP records and register them with the SDP service.
 *
 * @text Note, currently only the SBC codec is supported.
 * If you want to store the audio data in a file, you'll need to define STORE_TO_WAV_FILE.
 * If STORE_TO_WAV_FILE directive is defined, the SBC decoder needs to get initialized when a2dp_sink_packet_handler receives event A2DP_SUBEVENT_STREAM_STARTED.
 * The initialization of the SBC decoder requires a callback that handles PCM data:
 * - handle_pcm_data - handles PCM audio frames. Here, they are stored in a wav file if STORE_TO_WAV_FILE is defined, and/or played using the audio library.
 */

/* LISTING_START(MainConfiguration): Setup Audio Sink and AVRCP services */
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t event_size);

static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size);

static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

#ifdef HAVE_BTSTACK_STDIN
static void stdin_process(char cmd);
#endif


long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

audio_element_handle_t bt_ael_mic;
audio_element_handle_t bt_ael_spk;

audio_event_iface_handle_t bt_evt_iface;

static bt_event_data_t evt_data_arr[DEFAULT_AUDIO_EVENT_IFACE_SIZE]; // size depends on external queue size

static bt_event_data_t *pick_event_data_ptr() {
    static int ptr = 0;
    if (ptr == DEFAULT_AUDIO_EVENT_IFACE_SIZE) ptr = 0;
    return evt_data_arr + ptr++;
}

static void bt_evt_iface_send_cmd(bt_event_cmd_t cmd, bt_event_data_t *data) {
    audio_event_iface_msg_t msg = {0};
    msg.cmd = cmd;
    msg.data = data;
    msg.data_len = sizeof(bt_event_data_t);
    msg.need_free_data = false;
    msg.source = EVENT_SOURCE_FROM_BT;
    msg.source_type = AUDIO_ELEMENT_TYPE_PERIPH;
    audio_event_iface_sendout(bt_evt_iface, &msg);
}

// for a2dp sink
static void handle_pcm_data(int16_t *data, int num_audio_frames, int num_channels, int sample_rate, void *context) {
    UNUSED(sample_rate);
    UNUSED(context);
    UNUSED(num_channels);   // must be stereo == 2

    raw_stream_write(bt_ael_spk, reinterpret_cast<char *>(data), num_audio_frames * NUM_CHANNELS * sizeof(int16_t));
}

static int sbc_codec_init(media_codec_configuration_sbc_t *configuration) {
//    if (media_initialized) return 0;
    sbc_decoder_instance = btstack_sbc_decoder_bluedroid_init_instance(&sbc_decoder_context);
    sbc_decoder_instance->configure(&sbc_decoder_context, SBC_MODE_STANDARD, handle_pcm_data, NULL);

    return 0;
}

/* @section Handle Media Data Packet
 *
 * @text Here the audio data, are received through the handle_l2cap_media_data_packet callback.
 * Currently, only the SBC media codec is supported. Hence, the media data consists of the media packet header and the SBC packet.
 * The SBC frame will be stored in a ring buffer for later processing (instead of decoding it to PCM right away which would require a much larger buffer).
 * If the audio stream wasn't started already and there are enough SBC frames in the ring buffer, start playback.
 */

static int read_media_data_header(uint8_t *packet, int size, int *offset, avdtp_media_packet_header_t *media_header);

static int read_sbc_header(uint8_t *packet, int size, int *offset, avdtp_sbc_codec_header_t *sbc_header);

//

static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size) {
    UNUSED(seid);
    int pos = 0;

    avdtp_media_packet_header_t media_header;
    if (!read_media_data_header(packet, size, &pos, &media_header)) return;

    avdtp_sbc_codec_header_t sbc_header;
    if (!read_sbc_header(packet, size, &pos, &sbc_header)) return;

    int packet_length = size - pos;
    uint8_t *packet_begin = packet + pos;

    sbc_decoder_instance->decode_signed_16(&sbc_decoder_context, 0, packet_begin, packet_length);
}

static int read_sbc_header(uint8_t *packet, int size, int *offset, avdtp_sbc_codec_header_t *sbc_header) {
    int sbc_header_len = 12; // without crc
    int pos = *offset;

    if (size - pos < sbc_header_len) {
        printf("Not enough data to read SBC header, expected %d, received %d\n", sbc_header_len, size - pos);
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

static int read_media_data_header(uint8_t *packet, int size, int *offset, avdtp_media_packet_header_t *media_header) {
    int media_header_len = 12; // without crc
    int pos = *offset;

    if (size - pos < media_header_len) {
        printf("Not enough data to read media packet header, expected %d, received %d\n", media_header_len, size - pos);
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

static void dump_sbc_configuration(media_codec_configuration_sbc_t *configuration) {
    printf("    - num_channels: %d\n", configuration->num_channels);
    printf("    - sampling_frequency: %d\n", configuration->sampling_frequency);
    printf("    - channel_mode: %d\n", configuration->channel_mode);
    printf("    - block_length: %d\n", configuration->block_length);
    printf("    - subbands: %d\n", configuration->subbands);
    printf("    - allocation_method: %d\n", configuration->allocation_method);
    printf("    - bitpool_value [%d, %d] \n", configuration->min_bitpool_value, configuration->max_bitpool_value);
    printf("\n");
}


static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    bd_addr_t event_addr;

    switch (packet_type) {
        case HCI_SCO_DATA_PACKET:
            // forward received SCO / audio packets to SCO component
            if (READ_SCO_CONNECTION_HANDLE(packet) != sco_handle) break;
            sco_demo_receive(packet, size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case BTSTACK_EVENT_STATE:
                    // list supported codecs after stack has started up
                    if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING) break;
                    printf("dump_supported_codecs\n");
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    // inform about pin code request and respond with '0000'
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_demo_send(sco_handle);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    uint16_t local_cid;
    uint8_t status;
    bd_addr_t address;

    a2dp_sink_demo_avrcp_connection_t *connection = &a2dp_sink_demo_avrcp_connection;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (packet[2]) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                printf("AVRCP: Connection failed, status 0x%02x\n", status);
                connection->avrcp_cid = 0;
                return;
            }

            connection->avrcp_cid = local_cid;
            avrcp_subevent_connection_established_get_bd_addr(packet, address);
            printf("AVRCP: Connected to %s, cid 0x%02x\n", bd_addr_to_str(address), connection->avrcp_cid);

#ifdef HAVE_BTSTACK_STDIN
            // use address for outgoing connections
            avrcp_subevent_connection_established_get_bd_addr(packet, device_addr);
#endif

            avrcp_target_support_event(connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            avrcp_target_support_event(connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_BATT_STATUS_CHANGED);
            avrcp_target_battery_status_changed(connection->avrcp_cid, battery_status);

            // query supported events:
            avrcp_controller_get_supported_events(connection->avrcp_cid);
            return;
        }

        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            printf("AVRCP: Channel released: cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            connection->avrcp_cid = 0;
            connection->notifications_supported_by_target = 0;
            return;
        default:
            break;
    }
}

static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    // helper to print c strings
    uint8_t avrcp_subevent_value[256];
    uint8_t play_status;
    uint8_t event_id;

    a2dp_sink_demo_avrcp_connection_t *avrcp_connection = &a2dp_sink_demo_avrcp_connection;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    if (avrcp_connection->avrcp_cid == 0) return;

    memset(avrcp_subevent_value, 0, sizeof(avrcp_subevent_value));
    switch (packet[2]) {
        case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID:
            avrcp_connection->notifications_supported_by_target |= (1
                    << avrcp_subevent_get_capability_event_id_get_event_id(packet));
            break;
        case AVRCP_SUBEVENT_GET_CAPABILITY_EVENT_ID_DONE:

            printf("AVRCP Controller: supported notifications by target:\n");
            for (event_id = (uint8_t) AVRCP_NOTIFICATION_EVENT_FIRST_INDEX;
                 event_id < (uint8_t) AVRCP_NOTIFICATION_EVENT_LAST_INDEX; event_id++) {
                printf("   - [%s] %s\n",
                       (avrcp_connection->notifications_supported_by_target & (1 << event_id)) != 0 ? "X" : " ",
                       avrcp_notification2str((avrcp_notification_event_id_t) event_id));
            }
            printf("\n\n");

            // automatically enable notifications
            avrcp_controller_enable_notification(avrcp_connection->avrcp_cid,
                                                 AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(avrcp_connection->avrcp_cid,
                                                 AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(avrcp_connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);

            break;

        case AVRCP_SUBEVENT_NOTIFICATION_STATE:
            event_id = (avrcp_notification_event_id_t) avrcp_subevent_notification_state_get_event_id(packet);
            printf("AVRCP Controller: %s notification registered\n", avrcp_notification2str(
                    static_cast<avrcp_notification_event_id_t>(event_id)));
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:
            printf("AVRCP Controller: Playback position changed, position %d ms\n",
                   (unsigned int) avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet));
            break;
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
            printf("AVRCP Controller: Playback status changed %s\n",
                   avrcp_play_status2str(avrcp_subevent_notification_playback_status_changed_get_play_status(packet)));
            play_status = avrcp_subevent_notification_playback_status_changed_get_play_status(packet);
            switch (play_status) {
                case AVRCP_PLAYBACK_STATUS_PLAYING:
                    avrcp_connection->playing = true;
                    break;
                default:
                    avrcp_connection->playing = false;
                    break;
            }
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
            printf("AVRCP Controller: Playing content changed\n");
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
            printf("AVRCP Controller: Track changed\n");
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:
            printf("AVRCP Controller: Available Players Changed\n");
            break;

        case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE: {
            uint8_t shuffle_mode = avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet);
            uint8_t repeat_mode = avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet);
            printf("AVRCP Controller: %s, %s\n", avrcp_shuffle2str(shuffle_mode), avrcp_repeat2str(repeat_mode));
            break;
        }
        case AVRCP_SUBEVENT_NOW_PLAYING_TRACK_INFO:
            printf("AVRCP Controller: Track %d\n", avrcp_subevent_now_playing_track_info_get_track(packet));
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TOTAL_TRACKS_INFO:
            printf("AVRCP Controller: Total Tracks %d\n",
                   avrcp_subevent_now_playing_total_tracks_info_get_total_tracks(packet));
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
            if (avrcp_subevent_now_playing_title_info_get_value_len(packet) > 0) {
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_title_info_get_value(packet),
                       avrcp_subevent_now_playing_title_info_get_value_len(packet));
                printf("AVRCP Controller: Title %s\n", avrcp_subevent_value);
            }
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
            if (avrcp_subevent_now_playing_artist_info_get_value_len(packet) > 0) {
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_artist_info_get_value(packet),
                       avrcp_subevent_now_playing_artist_info_get_value_len(packet));
                printf("AVRCP Controller: Artist %s\n", avrcp_subevent_value);
            }
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
            if (avrcp_subevent_now_playing_album_info_get_value_len(packet) > 0) {
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_album_info_get_value(packet),
                       avrcp_subevent_now_playing_album_info_get_value_len(packet));
                printf("AVRCP Controller: Album %s\n", avrcp_subevent_value);
            }
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
            if (avrcp_subevent_now_playing_genre_info_get_value_len(packet) > 0) {
                memcpy(avrcp_subevent_value, avrcp_subevent_now_playing_genre_info_get_value(packet),
                       avrcp_subevent_now_playing_genre_info_get_value_len(packet));
                printf("AVRCP Controller: Genre %s\n", avrcp_subevent_value);
            }
            break;

        case AVRCP_SUBEVENT_PLAY_STATUS:
            printf("AVRCP Controller: Song length %"PRIu32" ms, Song position %"PRIu32" ms, Play status %s\n",
                   avrcp_subevent_play_status_get_song_length(packet),
                   avrcp_subevent_play_status_get_song_position(packet),
                   avrcp_play_status2str(avrcp_subevent_play_status_get_play_status(packet)));
            break;

        case AVRCP_SUBEVENT_OPERATION_COMPLETE:
            printf("AVRCP Controller: %s complete\n",
                   avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;

        case AVRCP_SUBEVENT_OPERATION_START:
            printf("AVRCP Controller: %s start\n",
                   avrcp_operation2str(avrcp_subevent_operation_start_get_operation_id(packet)));
            break;

        case AVRCP_SUBEVENT_NOTIFICATION_EVENT_TRACK_REACHED_END:
            printf("AVRCP Controller: Track reached end\n");
            break;

        case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
            printf("AVRCP Controller: Set Player App Value %s\n",
                   avrcp_ctype2str(avrcp_subevent_player_application_value_response_get_command_type(packet)));
            break;
        default:
            break;
    }
}

static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    uint8_t volume;
    char const *button_state;
    avrcp_operation_id_t operation_id;

    switch (packet[2]) {
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED: {
            volume = avrcp_subevent_notification_volume_changed_get_absolute_volume(packet);
            volume_percentage = volume * 100 / 127;
            printf("AVRCP Target    : Volume set to %d%% (%d)\n", volume_percentage, volume);
            bt_event_data_t *evt_data = pick_event_data_ptr();
            evt_data->absolute_volume = volume;
            bt_evt_iface_send_cmd(SPK_ABS_VOL_DATA, evt_data);
            break;
        }
        case AVRCP_SUBEVENT_OPERATION:
            operation_id = static_cast<avrcp_operation_id_t>(avrcp_subevent_operation_get_operation_id(packet));
            button_state = avrcp_subevent_operation_get_button_pressed(packet) > 0 ? "PRESS" : "RELEASE";
            switch (operation_id) {
                case AVRCP_OPERATION_ID_VOLUME_UP:
                    printf("AVRCP Target    : VOLUME UP (%s)\n", button_state);
                    break;
                case AVRCP_OPERATION_ID_VOLUME_DOWN:
                    printf("AVRCP Target    : VOLUME DOWN (%s)\n", button_state);
                    break;
                default:
                    return;
            }
            break;
        default:
            printf("AVRCP Target    : Event 0x%02x is not parsed\n", packet[2]);
            break;
    }
}

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    uint8_t status;

    uint8_t allocation_method;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    a2dp_sink_demo_a2dp_connection_t *a2dp_conn = &a2dp_sink_demo_a2dp_connection;

    switch (packet[2]) {
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
            printf("A2DP  Sink      : Received non SBC codec - not implemented\n");
            break;
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
            printf("A2DP  Sink      : Received SBC codec configuration\n");
            a2dp_conn->sbc_configuration.reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(
                    packet);
            a2dp_conn->sbc_configuration.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(
                    packet);
            a2dp_conn->sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(
                    packet);
            a2dp_conn->sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(
                    packet);
            a2dp_conn->sbc_configuration.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(
                    packet);
            a2dp_conn->sbc_configuration.min_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(
                    packet);
            a2dp_conn->sbc_configuration.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(
                    packet);

            allocation_method = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);

            // Adapt Bluetooth spec definition to SBC Encoder expected input
            a2dp_conn->sbc_configuration.allocation_method = (btstack_sbc_allocation_method_t) (allocation_method - 1);

            switch (a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet)) {
                case AVDTP_CHANNEL_MODE_JOINT_STEREO:
                    a2dp_conn->sbc_configuration.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_STEREO:
                    a2dp_conn->sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO;
                    break;
                case AVDTP_CHANNEL_MODE_DUAL_CHANNEL:
                    a2dp_conn->sbc_configuration.channel_mode = SBC_CHANNEL_MODE_DUAL_CHANNEL;
                    break;
                case AVDTP_CHANNEL_MODE_MONO:
                    a2dp_conn->sbc_configuration.channel_mode = SBC_CHANNEL_MODE_MONO;
                    break;
                default:
                    btstack_assert(false);
                    break;
            }
            dump_sbc_configuration(&a2dp_conn->sbc_configuration);
            break;
        }

        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                printf("A2DP  Sink      : Streaming connection failed, status 0x%02x\n", status);
                break;
            }

            a2dp_subevent_stream_established_get_bd_addr(packet, a2dp_conn->addr);
            a2dp_conn->a2dp_cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
            a2dp_conn->a2dp_local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
            a2dp_conn->stream_state = STREAM_STATE_OPEN;

            printf("A2DP  Sink      : Streaming connection is established, address %s, cid 0x%02x, local seid %d\n",
                   bd_addr_to_str(a2dp_conn->addr), a2dp_conn->a2dp_cid, a2dp_conn->a2dp_local_seid);
#ifdef HAVE_BTSTACK_STDIN
            // use address for outgoing connections
            memcpy(device_addr, a2dp_conn->addr, 6);
#endif
            break;

#ifdef ENABLE_AVDTP_ACCEPTOR_EXPLICIT_START_STREAM_CONFIRMATION
            case A2DP_SUBEVENT_START_STREAM_REQUESTED:
            printf("A2DP  Sink      : Explicit Accept to start stream, local_seid %d\n", a2dp_subevent_start_stream_requested_get_local_seid(packet));
            a2dp_sink_start_stream_accept(a2dp_cid, a2dp_local_seid);
            break;
#endif
        case A2DP_SUBEVENT_STREAM_STARTED: {
            printf("A2DP  Sink      : Stream started\n");
            a2dp_conn->stream_state = STREAM_STATE_PLAYING;
            if (a2dp_conn->sbc_configuration.reconfigure) {
                printf("A2DP  Sink      : Stream reconfigure\n");
//                media_processing_close(); // iface cmd reconfigure -> i2s_set_clk on the other end
            }
            auto *conf = &a2dp_sink_demo_a2dp_connection.sbc_configuration;
            audio_element_set_music_info(bt_ael_spk, conf->sampling_frequency,
                                         conf->channel_mode == SBC_CHANNEL_MODE_MONO ? 1 : 2,
                                         conf->block_length);
            audio_element_report_info(bt_ael_spk);

            // prepare media processing
            sbc_codec_init(&a2dp_conn->sbc_configuration);
            // audio stream is started when buffer reaches minimal level
            break;
        }
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            printf("A2DP  Sink      : Stream paused\n");
            a2dp_conn->stream_state = STREAM_STATE_PAUSED;
//            media_processing_pause();
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            printf("A2DP  Sink      : Stream released\n");
            a2dp_conn->stream_state = STREAM_STATE_CLOSED;
//            media_processing_close();
            break;

        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            printf("A2DP  Sink      : Signaling connection released\n");
            a2dp_conn->a2dp_cid = 0;
//            media_processing_close();
            break;

        default:
            break;
    }
}

static void hfp_hf_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *event, uint16_t event_size) {
    UNUSED(channel);
    uint8_t status;
    bd_addr_t event_addr;

    switch (packet_type) {
        case HCI_SCO_DATA_PACKET:
            if (READ_SCO_CONNECTION_HANDLE(event) != sco_handle) break;
            sco_demo_receive(event, event_size); // -> pcm_cb
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(event)) {
                case BTSTACK_EVENT_STATE:
                    if (btstack_event_state_get_state(event) != HCI_STATE_WORKING) break;
                    printf("dump_supported_codecs\n");
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    // inform about pin code request
                    printf("Pin code request - using '0000'\n");
                    hci_event_pin_code_request_get_bd_addr(event, event_addr);
                    gap_pin_code_response(event_addr, "0000");
                    break;

                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_demo_send(sco_handle); // <- from pipeline element
                    break;

                case HCI_EVENT_HFP_META:
                    switch (hci_event_hfp_meta_get_subevent_code(event)) {
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_service_level_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                printf("Connection failed, status 0x%02x\n", status);
                                break;
                            }
                            acl_handle = hfp_subevent_service_level_connection_established_get_acl_handle(event);
                            bd_addr_t device_addr;
                            hfp_subevent_service_level_connection_established_get_bd_addr(event, device_addr);
                            printf("Service level connection established %s.\n\n", bd_addr_to_str(device_addr));
                            break;

                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
                            acl_handle = HCI_CON_HANDLE_INVALID;
                            printf("Service level connection released.\n\n");
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
                            status = hfp_subevent_audio_connection_established_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                printf("Audio connection establishment failed with status 0x%02x\n", status);
                                break;
                            }
                            sco_handle = hfp_subevent_audio_connection_established_get_sco_handle(event);
                            printf("Audio connection established with SCO handle 0x%04x.\n", sco_handle);
                            negotiated_codec = hfp_subevent_audio_connection_established_get_negotiated_codec(event);
                            switch (negotiated_codec) {
                                case HFP_CODEC_CVSD:
                                    printf("Using CVSD codec.\n");
                                    break;
                                case HFP_CODEC_MSBC:
                                    printf("Using mSBC codec.\n");
                                    break;
                                case HFP_CODEC_LC3_SWB:
                                    printf("Using LC3-SWB codec.\n");
                                    break;
                                default:
                                    printf("Using unknown codec 0x%02x.\n", negotiated_codec);
                                    break;
                            }
                            sco_demo_set_codec(negotiated_codec);
                            hci_request_sco_can_send_now_event();
                            break;

                        case HFP_SUBEVENT_CALL_ANSWERED:
                            printf("Call answered\n");
                            break;

                        case HFP_SUBEVENT_CALL_TERMINATED:
                            printf("Call terminated\n");
                            break;

                        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED: {
                            sco_handle = HCI_CON_HANDLE_INVALID;
                            printf("Audio connection released\n");
                            sco_demo_close();
                            break;
                        }
                        case HFP_SUBEVENT_COMPLETE:
                            status = hfp_subevent_complete_get_status(event);
                            if (status == ERROR_CODE_SUCCESS) {
//                                printf("Cmd \'%c\' succeeded\n", cmd);
                            } else {
//                                printf("Cmd \'%c\' failed with status 0x%02x\n", cmd, status);
                            }
                            break;

                        case HFP_SUBEVENT_AG_INDICATOR_MAPPING:
                            printf("AG Indicator Mapping | INDEX %d: range [%d, %d], name '%s'\n",
                                   hfp_subevent_ag_indicator_mapping_get_indicator_index(event),
                                   hfp_subevent_ag_indicator_mapping_get_indicator_min_range(event),
                                   hfp_subevent_ag_indicator_mapping_get_indicator_max_range(event),
                                   (const char *) hfp_subevent_ag_indicator_mapping_get_indicator_name(event));
                            break;

                        case HFP_SUBEVENT_AG_INDICATOR_STATUS_CHANGED:
                            printf("AG Indicator Status  | INDEX %d: status 0x%02x, '%s'\n",
                                   hfp_subevent_ag_indicator_status_changed_get_indicator_index(event),
                                   hfp_subevent_ag_indicator_status_changed_get_indicator_status(event),
                                   (const char *) hfp_subevent_ag_indicator_status_changed_get_indicator_name(event));
                            break;
                        case HFP_SUBEVENT_NETWORK_OPERATOR_CHANGED:
                            printf("NETWORK_OPERATOR_CHANGED, operator mode %d, format %d, name %s\n",
                                   hfp_subevent_network_operator_changed_get_network_operator_mode(event),
                                   hfp_subevent_network_operator_changed_get_network_operator_format(event),
                                   (char *) hfp_subevent_network_operator_changed_get_network_operator_name(event));
                            break;
                        case HFP_SUBEVENT_EXTENDED_AUDIO_GATEWAY_ERROR:
                            printf("EXTENDED_AUDIO_GATEWAY_ERROR_REPORT, status 0x%02x\n",
                                   hfp_subevent_extended_audio_gateway_error_get_error(event));
                            break;
                        case HFP_SUBEVENT_START_RINGING:
                            printf("** START Ringing **\n");
                            break;
                        case HFP_SUBEVENT_RING:
                            printf("** Ring **\n");
                            break;
                        case HFP_SUBEVENT_STOP_RINGING:
                            printf("** STOP Ringing **\n");
                            break;
                        case HFP_SUBEVENT_NUMBER_FOR_VOICE_TAG:
                            printf("Phone number for voice tag: %s\n",
                                   (const char *) hfp_subevent_number_for_voice_tag_get_number(event));
                            break;
                        case HFP_SUBEVENT_SPEAKER_VOLUME: {
                            auto gain = hfp_subevent_speaker_volume_get_gain(event);
                            printf("Speaker volume: gain %u\n",
                                   gain);
                            bt_event_data_t *evt_data = pick_event_data_ptr();
                            evt_data->absolute_volume = map(gain, 0, 15, 0, 127);
                            bt_evt_iface_send_cmd(SPK_ABS_VOL_DATA, evt_data);
                            break;
                        }
                        case HFP_SUBEVENT_MICROPHONE_VOLUME: {
                            auto gain = hfp_subevent_microphone_volume_get_gain(event);
                            printf("Microphone volume: gain %u\n",
                                   gain);
                            bt_event_data_t *evt_data = pick_event_data_ptr();
                            evt_data->absolute_volume = map(gain, 0, 15, 0, 127);
                            bt_evt_iface_send_cmd(SPK_ABS_VOL_DATA, evt_data);
                            break;
                        }
                        case HFP_SUBEVENT_CALLING_LINE_IDENTIFICATION_NOTIFICATION:
                            printf("Caller ID, number '%s', alpha '%s'\n",
                                   (const char *) hfp_subevent_calling_line_identification_notification_get_number(
                                           event),
                                   (const char *) hfp_subevent_calling_line_identification_notification_get_alpha(
                                           event));
                            break;
                        case HFP_SUBEVENT_ENHANCED_CALL_STATUS:
                            printf("Enhanced call status:\n");
                            printf("  - call index: %d \n", hfp_subevent_enhanced_call_status_get_clcc_idx(event));
                            printf("  - direction : %s \n",
                                   hfp_enhanced_call_dir2str(hfp_subevent_enhanced_call_status_get_clcc_dir(event)));
                            printf("  - status    : %s \n", hfp_enhanced_call_status2str(
                                    hfp_subevent_enhanced_call_status_get_clcc_status(event)));
                            printf("  - mode      : %s \n",
                                   hfp_enhanced_call_mode2str(hfp_subevent_enhanced_call_status_get_clcc_mode(event)));
                            printf("  - multipart : %s \n",
                                   hfp_enhanced_call_mpty2str(hfp_subevent_enhanced_call_status_get_clcc_mpty(event)));
                            printf("  - type      : %d \n", hfp_subevent_enhanced_call_status_get_bnip_type(event));
                            printf("  - number    : %s \n", hfp_subevent_enhanced_call_status_get_bnip_number(event));
                            break;

                        case HFP_SUBEVENT_VOICE_RECOGNITION_ACTIVATED:
                            status = hfp_subevent_voice_recognition_activated_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                printf("Voice Recognition Activate command failed, status 0x%02x\n", status);
                                break;
                            }

                            switch (hfp_subevent_voice_recognition_activated_get_enhanced(event)) {
                                case 0:
                                    printf("\nVoice recognition ACTIVATED\n\n");
                                    break;
                                default:
                                    printf("\nEnhanced voice recognition ACTIVATED.\n");
                                    printf("Start new audio enhanced voice recognition session \n\n");
//                                           bd_addr_to_str(device_addr));
                                    status = hfp_hf_enhanced_voice_recognition_report_ready_for_audio(acl_handle);
                                    break;
                            }
                            break;

                        case HFP_SUBEVENT_VOICE_RECOGNITION_DEACTIVATED:
                            status = hfp_subevent_voice_recognition_deactivated_get_status(event);
                            if (status != ERROR_CODE_SUCCESS) {
                                printf("Voice Recognition Deactivate command failed, status 0x%02x\n", status);
                                break;
                            }
                            printf("\nVoice Recognition DEACTIVATED\n\n");
                            break;

                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_HF_READY_FOR_AUDIO:
                            status = hfp_subevent_enhanced_voice_recognition_hf_ready_for_audio_get_status(event);
                            printf("Enhanced Voice recognition: READY FOR AUDIO command, status 0x%02x\n", status);
                            break;

                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_READY_TO_ACCEPT_AUDIO_INPUT:
                            printf("\nEnhanced Voice recognition AG status: AG READY TO ACCEPT AUDIO INPUT\n\n");
                            break;
                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_IS_STARTING_SOUND:
                            printf("\nEnhanced Voice recognition AG status: AG IS STARTING SOUND\n\n");
                            break;
                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_IS_PROCESSING_AUDIO_INPUT:
                            printf("\nEnhanced Voice recognition AG status: AG IS PROCESSING AUDIO INPUT\n\n");
                            break;

                        case HFP_SUBEVENT_ENHANCED_VOICE_RECOGNITION_AG_MESSAGE:
                            printf("\nEnhanced Voice recognition AG message: \'%s\'\n",
                                   hfp_subevent_enhanced_voice_recognition_ag_message_get_text(event));
                            break;

                        case HFP_SUBEVENT_ECHO_CANCELING_AND_NOISE_REDUCTION_DEACTIVATE:
                            status = hfp_subevent_echo_canceling_and_noise_reduction_deactivate_get_status(event);
                            printf("Echo Canceling and Noise Reduction Deactivate command, status 0x%02x\n", status);
                            break;
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


#ifdef HAVE_BTSTACK_STDIN
static void show_usage(void){
    bd_addr_t      iut_address;
    gap_local_bd_addr(iut_address);
    printf("\n--- A2DP Sink Demo Console %s ---\n", bd_addr_to_str(iut_address));
    printf("b - A2DP Sink create connection to addr %s\n", bd_addr_to_str(device_addr));
    printf("B - A2DP Sink disconnect\n");

    printf("\n--- AVRCP Controller ---\n");
    printf("c - AVRCP create connection to addr %s\n", bd_addr_to_str(device_addr));
    printf("C - AVRCP disconnect\n");
    printf("O - get play status\n");
    printf("j - get now playing info\n");
    printf("k - play\n");
    printf("K - stop\n");
    printf("L - pause\n");
    printf("u - start fast forward\n");
    printf("U - stop  fast forward\n");
    printf("n - start rewind\n");
    printf("N - stop rewind\n");
    printf("i - forward\n");
    printf("I - backward\n");
    printf("M - mute\n");
    printf("r - skip\n");
    printf("q - query repeat and shuffle mode\n");
    printf("v - repeat single track\n");
    printf("w - delay report\n");
    printf("x - repeat all tracks\n");
    printf("X - disable repeat mode\n");
    printf("z - shuffle all tracks\n");
    printf("Z - disable shuffle mode\n");

    printf("a/A - register/deregister TRACK_CHANGED\n");
    printf("R/P - register/deregister PLAYBACK_POS_CHANGED\n");

    printf("s/S - send/release long button press REWIND\n");

    printf("\n--- Volume and Battery Control ---\n");
    printf("t - volume up   for 10 percent\n");
    printf("T - volume down for 10 percent\n");
    printf("V - toggle Battery status from AVRCP_BATTERY_STATUS_NORMAL to AVRCP_BATTERY_STATUS_FULL_CHARGE\n");

//#ifdef ENABLE_AVRCP_COVER_ART
//    printf("\n--- Cover Art Client ---\n");
//    printf("d - connect to addr %s\n", bd_addr_to_str(device_addr));
//    printf("D - disconnect\n");
//    if (a2dp_sink_demo_cover_art_client_connected == false){
//        if (a2dp_sink_demo_avrcp_connection.avrcp_cid == 0){
//            printf("Not connected, press 'b' or 'c' to first connect AVRCP, then press 'd' to connect cover art client\n");
//        } else {
//            printf("Not connected, press 'd' to connect cover art client\n");
//        }
//    } else if (a2dp_sink_demo_image_handle[0] == 0){
//        printf("No image handle, use 'j' to get current track info\n");
//    }
//    printf("---\n");
//#endif

}

static void stdin_process(char cmd) {
    uint8_t status = ERROR_CODE_SUCCESS;
    uint8_t volume;
    avrcp_battery_status_t old_battery_status;

    a2dp_sink_demo_a2dp_connection_t *  a2dp_connection  = &a2dp_sink_demo_a2dp_connection;
    a2dp_sink_demo_avrcp_connection_t * avrcp_connection = &a2dp_sink_demo_avrcp_connection;

    switch (cmd){
        case 'b':
            status = a2dp_sink_establish_stream(device_addr, &a2dp_connection->a2dp_cid);
            printf(" - Create AVDTP connection to addr %s, and local seid %d, cid 0x%02x.\n",
                   bd_addr_to_str(device_addr), a2dp_connection->a2dp_local_seid, a2dp_connection->a2dp_cid);
            break;
        case 'B':
            printf(" - AVDTP disconnect from addr %s.\n", bd_addr_to_str(device_addr));
            a2dp_sink_disconnect(a2dp_connection->a2dp_cid);
            break;
        case 'c':
            printf(" - Create AVRCP connection to addr %s.\n", bd_addr_to_str(device_addr));
            status = avrcp_connect(device_addr, &avrcp_connection->avrcp_cid);
            break;
        case 'C':
            printf(" - AVRCP disconnect from addr %s.\n", bd_addr_to_str(device_addr));
            status = avrcp_disconnect(avrcp_connection->avrcp_cid);
            break;

        case '\n':
        case '\r':
            break;
        case 'w':
            printf("Send delay report\n");
            avdtp_sink_delay_report(a2dp_connection->a2dp_cid, a2dp_connection->a2dp_local_seid, 100);
            break;
            // Volume Control
        case 't':
            volume_percentage = volume_percentage <= 90 ? volume_percentage + 10 : 100;
            volume = volume_percentage * 127 / 100;
            printf(" - volume up   for 10 percent, %d%% (%d) \n", volume_percentage, volume);
            status = avrcp_target_volume_changed(avrcp_connection->avrcp_cid, volume);
            avrcp_volume_changed(volume);
            break;
        case 'T':
            volume_percentage = volume_percentage >= 10 ? volume_percentage - 10 : 0;
            volume = volume_percentage * 127 / 100;
            printf(" - volume down for 10 percent, %d%% (%d) \n", volume_percentage, volume);
            status = avrcp_target_volume_changed(avrcp_connection->avrcp_cid, volume);
            avrcp_volume_changed(volume);
            break;
        case 'V':
            old_battery_status = battery_status;

            if (battery_status < AVRCP_BATTERY_STATUS_FULL_CHARGE){
                battery_status = (avrcp_battery_status_t)((uint8_t) battery_status + 1);
            } else {
                battery_status = AVRCP_BATTERY_STATUS_NORMAL;
            }
            printf(" - toggle battery value, old %d, new %d\n", old_battery_status, battery_status);
            status = avrcp_target_battery_status_changed(avrcp_connection->avrcp_cid, battery_status);
            break;
        case 'O':
            printf(" - get play status\n");
            status = avrcp_controller_get_play_status(avrcp_connection->avrcp_cid);
            break;
        case 'j':
            printf(" - get now playing info\n");
            status = avrcp_controller_get_now_playing_info(avrcp_connection->avrcp_cid);
            break;
        case 'k':
            printf(" - play\n");
            status = avrcp_controller_play(avrcp_connection->avrcp_cid);
            break;
        case 'K':
            printf(" - stop\n");
            status = avrcp_controller_stop(avrcp_connection->avrcp_cid);
            break;
        case 'L':
            printf(" - pause\n");
            status = avrcp_controller_pause(avrcp_connection->avrcp_cid);
            break;
        case 'u':
            printf(" - start fast forward\n");
            status = avrcp_controller_press_and_hold_fast_forward(avrcp_connection->avrcp_cid);
            break;
        case 'U':
            printf(" - stop fast forward\n");
            status = avrcp_controller_release_press_and_hold_cmd(avrcp_connection->avrcp_cid);
            break;
        case 'n':
            printf(" - start rewind\n");
            status = avrcp_controller_press_and_hold_rewind(avrcp_connection->avrcp_cid);
            break;
        case 'N':
            printf(" - stop rewind\n");
            status = avrcp_controller_release_press_and_hold_cmd(avrcp_connection->avrcp_cid);
            break;
        case 'i':
            printf(" - forward\n");
            status = avrcp_controller_forward(avrcp_connection->avrcp_cid);
            break;
        case 'I':
            printf(" - backward\n");
            status = avrcp_controller_backward(avrcp_connection->avrcp_cid);
            break;
        case 'M':
            printf(" - mute\n");
            status = avrcp_controller_mute(avrcp_connection->avrcp_cid);
            break;
        case 'r':
            printf(" - skip\n");
            status = avrcp_controller_skip(avrcp_connection->avrcp_cid);
            break;
        case 'q':
            printf(" - query repeat and shuffle mode\n");
            status = avrcp_controller_query_shuffle_and_repeat_modes(avrcp_connection->avrcp_cid);
            break;
        case 'v':
            printf(" - repeat single track\n");
            status = avrcp_controller_set_repeat_mode(avrcp_connection->avrcp_cid, AVRCP_REPEAT_MODE_SINGLE_TRACK);
            break;
        case 'x':
            printf(" - repeat all tracks\n");
            status = avrcp_controller_set_repeat_mode(avrcp_connection->avrcp_cid, AVRCP_REPEAT_MODE_ALL_TRACKS);
            break;
        case 'X':
            printf(" - disable repeat mode\n");
            status = avrcp_controller_set_repeat_mode(avrcp_connection->avrcp_cid, AVRCP_REPEAT_MODE_OFF);
            break;
        case 'z':
            printf(" - shuffle all tracks\n");
            status = avrcp_controller_set_shuffle_mode(avrcp_connection->avrcp_cid, AVRCP_SHUFFLE_MODE_ALL_TRACKS);
            break;
        case 'Z':
            printf(" - disable shuffle mode\n");
            status = avrcp_controller_set_shuffle_mode(avrcp_connection->avrcp_cid, AVRCP_SHUFFLE_MODE_OFF);
            break;
        case 'a':
            printf("AVRCP: enable notification TRACK_CHANGED\n");
            status = avrcp_controller_enable_notification(avrcp_connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            break;
        case 'A':
            printf("AVRCP: disable notification TRACK_CHANGED\n");
            status = avrcp_controller_disable_notification(avrcp_connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            break;
        case 'R':
            printf("AVRCP: enable notification PLAYBACK_POS_CHANGED\n");
            status = avrcp_controller_enable_notification(avrcp_connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED);
            break;
        case 'P':
            printf("AVRCP: disable notification PLAYBACK_POS_CHANGED\n");
            status = avrcp_controller_disable_notification(avrcp_connection->avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_POS_CHANGED);
            break;
        case 's':
            printf("AVRCP: send long button press REWIND\n");
            status = avrcp_controller_start_press_and_hold_cmd(avrcp_connection->avrcp_cid, AVRCP_OPERATION_ID_REWIND);
            break;
        case 'S':
            printf("AVRCP: release long button press REWIND\n");
            status = avrcp_controller_release_press_and_hold_cmd(avrcp_connection->avrcp_cid);
            break;
        default:
            show_usage();
            return;
    }
    if (status != ERROR_CODE_SUCCESS){
        printf("Could not perform command, status 0x%02x\n", status);
    }
}
#endif

int btstack_main() {
//    UNUSED(argc);
//    (void) argv;

    raw_stream_cfg_t r_cfg = RAW_STREAM_CFG_DEFAULT();
    r_cfg.type = AUDIO_STREAM_WRITER;
    bt_ael_spk = raw_stream_init(&r_cfg);
    r_cfg.type = AUDIO_STREAM_READER;
    bt_ael_mic = raw_stream_init(&r_cfg);

    audio_event_iface_cfg_t evt_iface_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt_iface_cfg.internal_queue_size = 0;
    evt_iface_cfg.external_queue_size = DEFAULT_AUDIO_EVENT_IFACE_SIZE;
    bt_evt_iface = audio_event_iface_init(&evt_iface_cfg);

    // init protocols
    l2cap_init();
    rfcomm_init();
    sdp_init();
#ifdef ENABLE_BLE
    // Initialize LE Security Manager. Needed for cross-transport key derivation
    sm_init();
#endif

    // Init profiles
    uint16_t hf_supported_features =
            (1 << HFP_HFSF_ESCO_S4) |
//            (1 << HFP_HFSF_CLI_PRESENTATION_CAPABILITY) |
//            (1 << HFP_HFSF_HF_INDICATORS) |
            (1 << HFP_HFSF_CODEC_NEGOTIATION) |
//            (1 << HFP_HFSF_ENHANCED_CALL_STATUS) |
//            (1 << HFP_HFSF_VOICE_RECOGNITION_FUNCTION) |
//            (1 << HFP_HFSF_ENHANCED_VOICE_RECOGNITION_STATUS) |
//            (1 << HFP_HFSF_VOICE_RECOGNITION_TEXT) |
//            (1 << HFP_HFSF_EC_NR_FUNCTION) |
            (1 << HFP_HFSF_REMOTE_VOLUME_CONTROL);

    hfp_hf_init(rfcomm_channel_nr);
    hfp_hf_init_supported_features(hf_supported_features);
    hfp_hf_init_hf_indicators(sizeof(indicators) / sizeof(uint16_t), indicators);
    hfp_hf_init_codecs(sizeof(codecs), codecs);
    hfp_hf_register_packet_handler(hfp_hf_packet_handler);

    a2dp_sink_init();
    avrcp_init();
    avrcp_controller_init();
    avrcp_target_init();

    // Configure A2DP Sink
    a2dp_sink_register_packet_handler(&a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(&handle_l2cap_media_data_packet);
    a2dp_sink_demo_stream_endpoint_t *stream_endpoint = &a2dp_sink_demo_stream_endpoint;
    avdtp_stream_endpoint_t *local_stream_endpoint =
            a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
                                             media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities),
                                             stream_endpoint->media_sbc_codec_configuration,
                                             sizeof(stream_endpoint->media_sbc_codec_configuration));
    btstack_assert(local_stream_endpoint != NULL);
    // - Store stream enpoint's SEP ID, as it is used by A2DP API to identify the stream endpoint
    stream_endpoint->a2dp_local_seid = avdtp_local_seid(local_stream_endpoint);


    // Configure AVRCP Controller + Target
    avrcp_register_packet_handler(&avrcp_packet_handler);
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);
    avrcp_target_register_packet_handler(&avrcp_target_packet_handler);


    // Configure SDP

    // - Create and register A2DP Sink service record
    memset(sdp_avdtp_sink_service_buffer, 0, sizeof(sdp_avdtp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_avdtp_sink_service_buffer, sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE, NULL, NULL);
    btstack_assert(de_get_len(sdp_avdtp_sink_service_buffer) <= sizeof(sdp_avdtp_sink_service_buffer));
    sdp_register_service(sdp_avdtp_sink_service_buffer);


    // - Create AVRCP Controller service record and register it with SDP. We send Category 1 commands to the media player, e.g. play/pause
    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    uint16_t controller_supported_features = 1 << AVRCP_CONTROLLER_SUPPORTED_FEATURE_CATEGORY_PLAYER_OR_RECORDER;
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, sdp_create_service_record_handle(),
                                       controller_supported_features, NULL, NULL);
    btstack_assert(de_get_len(sdp_avrcp_controller_service_buffer) <= sizeof(sdp_avrcp_controller_service_buffer));
    sdp_register_service(sdp_avrcp_controller_service_buffer);

    // - Create and register A2DP Sink service record
    //   -  We receive Category 2 commands from the media player, e.g. volume up/down
    memset(sdp_avrcp_target_service_buffer, 0, sizeof(sdp_avrcp_target_service_buffer));
    uint16_t target_supported_features = 1 << AVRCP_TARGET_SUPPORTED_FEATURE_CATEGORY_MONITOR_OR_AMPLIFIER;
    avrcp_target_create_sdp_record(sdp_avrcp_target_service_buffer,
                                   sdp_create_service_record_handle(), target_supported_features, NULL, NULL);
    btstack_assert(de_get_len(sdp_avrcp_target_service_buffer) <= sizeof(sdp_avrcp_target_service_buffer));
    sdp_register_service(sdp_avrcp_target_service_buffer);

    // - Create and register Device ID (PnP) service record
    memset(device_id_sdp_service_buffer, 0, sizeof(device_id_sdp_service_buffer));
    device_id_create_sdp_record(device_id_sdp_service_buffer,
                                sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len(device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
    sdp_register_service(device_id_sdp_service_buffer);

    // - Create and register HFP HF service record
    memset(hfp_service_buffer, 0, sizeof(hfp_service_buffer));
    hfp_hf_create_sdp_record_with_codecs(hfp_service_buffer, sdp_create_service_record_handle(),
                                         rfcomm_channel_nr, hfp_hf_service_name, hf_supported_features, sizeof(codecs),
                                         codecs);
    btstack_assert(de_get_len(hfp_service_buffer) <= sizeof(hfp_service_buffer));
    sdp_register_service(hfp_service_buffer);

    // Configure GAP - discovery / connection

    // - Set local name with a template Bluetooth address, that will be automatically
    //   replaced with an actual address once it is available, i.e. when BTstack boots
    //   up and starts talking to a Bluetooth module.
    gap_set_local_name("A2DP Sink Demo 00:00:00:00:00:00");

    // - Allow to show up in Bluetooth inquiry
    gap_discoverable_control(1);

    // - Set Class of Device - Service Class: Audio, Major Device Class: Audio, Minor: Headphones
    gap_set_class_of_device(0x240404); // TODO: change for appropriate through macro

    // - Allow for role switch in general and sniff mode
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);

    // - Allow for role switch on outgoing connections
    //   - This allows A2DP Source, e.g. smartphone, to become master when we re-connect to it.
    gap_set_allow_role_switch(true);

    // Register for HCI events
    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    hci_register_sco_packet_handler(&hci_packet_handler);

    sco_demo_init();

#ifdef HAVE_BTSTACK_STDIN
    // parse human-readable Bluetooth address
    sscanf_bd_addr(device_addr_string, device_addr);
    btstack_stdin_setup(stdin_process);
#endif

    // turn on!
    printf("Starting BTstack ...\n");
    hci_power_control(HCI_POWER_ON);
    return 0;
}