#include "sco_util.h"
#include "stream_bridge.h"

#include <hfp_codec.h>
#include <btstack_cvsd_plc.h>
#include <btstack.h>
#include <impl/log.h>

#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
#include "btstack_lc3.h"
#include "btstack_lc3_google.h"
#endif

static const char *TAG = "SCO_UTIL";

#define NUM_CHANNELS            1
#define SAMPLE_RATE_8KHZ        8000
#define SAMPLE_RATE_16KHZ       16000
#define SAMPLE_RATE_32KHZ       32000
#define BYTES_PER_FRAME         2

#if defined(ENABLE_HFP_SUPER_WIDE_BAND_SPEECH)
#define PREBUFFER_BYTES_MAX PREBUFFER_BYTES_32KHZ
#define SAMPLES_PER_FRAME_MAX 240
#elif defined(ENABLE_HFP_WIDE_BAND_SPEECH)
#define PREBUFFER_BYTES_MAX PREBUFFER_BYTES_16KHZ
#define SAMPLES_PER_FRAME_MAX 120
#else
#define PREBUFFER_BYTES_MAX PREBUFFER_BYTES_8KHZ
#define SAMPLES_PER_FRAME_MAX 60
#endif

// audio pre-buffer - also defines latency
#define SCO_PREBUFFER_MS      50
#define PREBUFFER_BYTES_8KHZ  (SCO_PREBUFFER_MS *  SAMPLE_RATE_8KHZ/1000 * BYTES_PER_FRAME)
#define PREBUFFER_BYTES_16KHZ (SCO_PREBUFFER_MS * SAMPLE_RATE_16KHZ/1000 * BYTES_PER_FRAME)
#define PREBUFFER_BYTES_32KHZ (SCO_PREBUFFER_MS * SAMPLE_RATE_32KHZ/1000 * BYTES_PER_FRAME)

// generic codec struct
typedef struct {
    void (*init)();

    void (*receive)(const uint8_t *packet, uint16_t size);

    void (*fill_payload)(uint8_t *payload_buffer, uint16_t sco_payload_length);

    void (*close)();

    uint16_t sample_rate;
} codec_support_t;

static btstack_cvsd_plc_state_t cvsd_plc_state;

#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
static const btstack_sbc_decoder_t *sbc_decoder_instance;
extern btstack_sbc_decoder_bluedroid_t sbc_decoder_context;
static const btstack_sbc_encoder_t *sbc_encoder_instance;
static btstack_sbc_encoder_bluedroid_t sbc_encoder_context;
#endif

#if defined(ENABLE_HFP_WIDE_BAND_SPEECH) || defined(ENABLE_HFP_SUPER_WIDE_BAND_SPEECH)
static hfp_codec_t hfp_codec;
#endif

#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
static const btstack_lc3_decoder_t * lc3_decoder;
static btstack_lc3_decoder_google_t lc3_decoder_context;
static btstack_lc3_encoder_google_t lc3_encoder_context;
static hfp_h2_sync_t    hfp_h2_sync;
#endif

// current configuration
static const codec_support_t *codec_current = nullptr;

static int audio_input_paused = 0;
static uint16_t audio_prebuffer_bytes;

// CVSD - 8 kHz

static void cvsd_init() {
    logi(TAG, "init CVSD");
    btstack_cvsd_plc_init(&cvsd_plc_state);
}

static void cvsd_receive(const uint8_t *packet, uint16_t size) {

    int16_t audio_frame_out[128];    //

    if (size > sizeof(audio_frame_out)) {
        logi(TAG, "cvsd_receive: SCO packet larger than local output buffer - dropping data.");
        return;
    }

    const int audio_bytes_read = size - 3;
    const int num_samples = audio_bytes_read / BYTES_PER_FRAME;

    // convert into host endian
    int16_t audio_frame_in[128];
    int i;
    for (i = 0; i < num_samples; i++) {
        audio_frame_in[i] = little_endian_read_16(packet, 3 + i * 2);
    }

    // treat packet as bad frame if controller does not report 'all good'
    bool bad_frame = (packet[1] & 0x30) != 0;

    btstack_cvsd_plc_process_data(&cvsd_plc_state, bad_frame, audio_frame_in, num_samples, audio_frame_out);

    stream_bridge::write(reinterpret_cast<char *>(audio_frame_out), audio_bytes_read);
}

static void cvsd_fill_payload(uint8_t *payload_buffer, uint16_t sco_payload_length) {
    uint16_t bytes_to_copy = sco_payload_length;

    // get data from ringbuffer
    uint16_t pos = 0;
    if (!audio_input_paused) {
        uint16_t samples_to_copy = sco_payload_length / 2;
        uint32_t bytes_read = 0;
        bytes_read = stream_bridge::read(reinterpret_cast<char *>(payload_buffer), bytes_to_copy);
        // flip 16 on big endian systems
        // @note We don't use (uint16_t *) casts since all sample addresses are odd which causes crahses on some systems
        if (btstack_is_big_endian()) {
            uint16_t i;
            for (i = 0; i < samples_to_copy / 2; i += 2) {
                uint8_t tmp = payload_buffer[i * 2];
                payload_buffer[i * 2] = payload_buffer[i * 2 + 1];
                payload_buffer[i * 2 + 1] = tmp;
            }
        }
        bytes_to_copy -= bytes_read;
        pos += bytes_read;
    }

    // fill with 0 if not enough
    if (bytes_to_copy) {
        memset(payload_buffer + pos, 0, bytes_to_copy);
        audio_input_paused = 1;
    }
}

static void cvsd_close(void) {
    logi(TAG, "used CVSD with PLC, number of proccesed frames: \n - %d good frames, \n - %d bad framesn",
         cvsd_plc_state.good_frames_nr, cvsd_plc_state.bad_frames_nr);
}

static const codec_support_t codec_cvsd = {
        .init         = &cvsd_init,
        .receive      = &cvsd_receive,
        .fill_payload = &cvsd_fill_payload,
        .close        = &cvsd_close,
        .sample_rate = SAMPLE_RATE_8KHZ
};


// encode using hfp_codec
#if defined(ENABLE_HFP_WIDE_BAND_SPEECH) || defined(ENABLE_HFP_SUPER_WIDE_BAND_SPEECH)

static void codec_fill_payload(uint8_t *payload_buffer, uint16_t sco_payload_length) {

    if (!audio_input_paused) {
        int num_samples = hfp_codec_num_audio_samples_per_frame(&hfp_codec);
        btstack_assert(num_samples <= SAMPLES_PER_FRAME_MAX);
        uint16_t samples_available = stream_bridge::bytes_ready_to_read() / BYTES_PER_FRAME;
        if (hfp_codec_can_encode_audio_frame_now(&hfp_codec) && samples_available >= num_samples) {
            int16_t sample_buffer[SAMPLES_PER_FRAME_MAX];

            stream_bridge::read(reinterpret_cast<char *>(sample_buffer), num_samples * BYTES_PER_FRAME);
            hfp_codec_encode_audio_frame(&hfp_codec, sample_buffer);
        }
    }
    // get data from encoder, fill with 0 if not enough
    if (audio_input_paused || hfp_codec_num_bytes_available(&hfp_codec) < sco_payload_length) {
        // just send '0's
        memset(payload_buffer, 0, sco_payload_length);
        audio_input_paused = 1;
    } else {
        hfp_codec_read_from_stream(&hfp_codec, payload_buffer, sco_payload_length);
    }
}

#endif

// mSBC - 16 kHz

#ifdef ENABLE_HFP_WIDE_BAND_SPEECH

static void handle_pcm_data(int16_t *data, int num_samples, int num_channels, int sample_rate, void *context) {
    int bytes = num_samples * num_channels * BYTES_PER_FRAME;
    stream_bridge::write(reinterpret_cast<char *>(data), bytes);
}

static void msbc_init() {
    logi(TAG, "init mSBC");
    sbc_decoder_instance = btstack_sbc_decoder_bluedroid_init_instance(&sbc_decoder_context);
    sbc_decoder_instance->configure(&sbc_decoder_context, SBC_MODE_mSBC, &handle_pcm_data, nullptr);

    sbc_encoder_instance = btstack_sbc_encoder_bluedroid_init_instance(&sbc_encoder_context);
    hfp_codec_init_msbc_with_codec(&hfp_codec, sbc_encoder_instance, &sbc_encoder_context);
}

static void msbc_receive(const uint8_t *packet, uint16_t size) {
    uint8_t status = (packet[1] >> 4) & 3;
    sbc_decoder_instance->decode_signed_16(&sbc_decoder_context, status, packet + 3, size - 3);
}

static void msbc_close() {
    logi(TAG,
         "used mSBC with PLC, number of processed frames: \n - %d good frames, \n - %d zero frames, \n - %d bad framesn",
         sbc_decoder_context.good_frames_nr, sbc_decoder_context.zero_frames_nr, sbc_decoder_context.bad_frames_nr);
}

static const codec_support_t codec_msbc = {
        .init         = &msbc_init,
        .receive      = &msbc_receive,
        .fill_payload = &codec_fill_payload,
        .close        = &msbc_close,
        .sample_rate = SAMPLE_RATE_16KHZ
};

#endif /* ENABLE_HFP_WIDE_BAND_SPEECH */

#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
#define LC3_SWB_SAMPLES_PER_FRAME 240
#define LC3_SWB_OCTETS_PER_FRAME   58

static bool lc3swb_frame_callback(bool bad_frame, const uint8_t * frame_data, uint16_t frame_len) {

    // skip H2 header for good frames
    if (!bad_frame){
        btstack_assert(frame_data != nullptr);
        frame_data += 2;
    }

    uint8_t tmp_BEC_detect = 0;
    uint8_t BFI = bad_frame ? 1 : 0;
    int16_t samples[LC3_SWB_SAMPLES_PER_FRAME];
    (void) lc3_decoder->decode_signed_16(&lc3_decoder_context, frame_data, BFI,
                                         samples, 1, &tmp_BEC_detect);

    // samples in callback in host endianess, ready for playback
    int bytes = LC3_SWB_SAMPLES_PER_FRAME * BYTES_PER_FRAME;
    stream_bridge::write(reinterpret_cast<char *>(samples), bytes);

    // frame is good, if it isn't a bad frame and we didn't detect other errors
    return !bad_frame && (tmp_BEC_detect == 0);
}

static void lc3swb_init(){
    logi(TAG, "init LC3-SWB");

    hfp_codec.lc3_encoder_context = &lc3_encoder_context;
    const btstack_lc3_encoder_t * lc3_encoder = btstack_lc3_encoder_google_init_instance( &lc3_encoder_context);
    hfp_codec_init_lc3_swb(&hfp_codec, lc3_encoder, &lc3_encoder_context);

    // init lc3 decoder
    lc3_decoder = btstack_lc3_decoder_google_init_instance(&lc3_decoder_context);
    lc3_decoder->configure(&lc3_decoder_context, SAMPLE_RATE_32KHZ, BTSTACK_LC3_FRAME_DURATION_7500US, LC3_SWB_OCTETS_PER_FRAME);

    // init HPF H2 framing
    hfp_h2_sync_init(&hfp_h2_sync, &lc3swb_frame_callback);
}

static void lc3swb_receive(const uint8_t * packet, uint16_t size) {
    uint8_t packet_status = (packet[1] >> 4) & 3;
    bool bad_frame = packet_status != 0;
    hfp_h2_sync_process(&hfp_h2_sync, bad_frame, &packet[3], size-3);
}

static void lc3swb_close() {}

static const codec_support_t codec_lc3swb = {
        .init         = &lc3swb_init,
        .receive      = &lc3swb_receive,
        .fill_payload = &codec_fill_payload,
        .close        = &lc3swb_close,
        .sample_rate = SAMPLE_RATE_32KHZ
};
#endif


void sco_util::set_codec(uint8_t codec) {
    switch (codec) {
        case HFP_CODEC_CVSD:
            codec_current = &codec_cvsd;
            break;
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
        case HFP_CODEC_MSBC:
            codec_current = &codec_msbc;
            break;
#endif
#ifdef ENABLE_HFP_SUPER_WIDE_BAND_SPEECH
            case HFP_CODEC_LC3_SWB:
                codec_current = &codec_lc3swb;
                break;
#endif
        default:
            btstack_assert(false);
            break;
    }

    stream_bridge::configure_source(codec_current->sample_rate, NUM_CHANNELS, BYTES_PER_FRAME * 8);
    stream_bridge::configure_sink(codec_current->sample_rate, NUM_CHANNELS, BYTES_PER_FRAME * 8);

    codec_current->init();

    audio_input_paused = 1;

    audio_prebuffer_bytes = SCO_PREBUFFER_MS * (codec_current->sample_rate / 1000) * BYTES_PER_FRAME;
    logi(TAG, "set_codec done");
}

void sco_util::send(hci_con_handle_t sco_handle) {
    if (sco_handle == HCI_CON_HANDLE_INVALID) return;

    int sco_packet_length = hci_get_sco_packet_length_for_connection(sco_handle);
    int sco_payload_length = sco_packet_length - 3;

    hci_reserve_packet_buffer();
    uint8_t *sco_packet = hci_get_outgoing_packet_buffer();

    // resume if pre-buffer is filled
    if (audio_input_paused) {
        if (stream_bridge::bytes_ready_to_read() >= audio_prebuffer_bytes) {
            // resume sending
            audio_input_paused = 0;
        }
    }

    // fill payload by codec
    codec_current->fill_payload(&sco_packet[3], sco_payload_length);

    // set handle + flags
    little_endian_store_16(sco_packet, 0, sco_handle);
    // set len
    sco_packet[2] = sco_payload_length;
    // finally send packet
    hci_send_sco_packet_buffer(sco_packet_length);

    // request another send event
    hci_request_sco_can_send_now_event();
}

void sco_util::receive(uint8_t *packet, uint16_t size) {
    codec_current->receive(packet, size);
}

void sco_util::init() {
#ifdef ENABLE_CLASSIC_LEGACY_CONNECTIONS_FOR_SCO_DEMOS
    logi(TAG, "disable BR/EDR Secure Connctions due to incompatibilities with SCO connections");
    gap_secure_connections_enable(false);
#endif

    // Set SCO for CVSD (mSBC or other codecs automatically use 8-bit transparent mode)
    hci_set_sco_voice_setting(0x60);    // linear, unsigned, 16-bit, CVSD
}

void sco_util::close() {
    codec_current->close();
    codec_current = nullptr;
}
