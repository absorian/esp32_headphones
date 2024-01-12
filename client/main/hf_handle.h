//
// Created by ism on 05.01.2023.
//

#ifndef HEADPHONES_IDF_HF_HANDLE_H
#define HEADPHONES_IDF_HF_HANDLE_H

#include "audio_element.h"
#include "audio_mem.h"

#include "esp_hf_client_api.h"

#define HFP_RESAMPLE_RATE 16000

void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param);

static void bt_app_hf_client_audio_open(void);

static void bt_app_hf_client_audio_close(void);


#endif //HEADPHONES_IDF_HF_HANDLE_H
