#ifndef STREAM_BRIDGE_H
#define STREAM_BRIDGE_H

#define DMA_BUF_COUNT 4
#define DMA_BUF_SIZE 960

#include <cstddef>
#include <stdint-gcc.h>

namespace stream_bridge {
    typedef void (*data_handler_t)(void *, size_t, void *);

    void init();

    int write(const void *buffer, int len, uint32_t wait_time = 0);

    int read(void *buffer, int len, uint32_t wait_time = 0);

    int bytes_ready_to_read();

    int bytes_can_write();

    void configure_sink(int sample_rates, int channels, int bits);

    void configure_source(int sample_rates, int channels, int bits);

    void set_source_volume(int vol);

    void set_sink_volume(int vol);

    int get_source_volume();

    int get_sink_volume();
}


#endif //STREAM_BRIDGE_H
