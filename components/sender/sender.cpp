//
// Created by ism on 01.06.2023.
//

#include <algorithm>
#include "sender.h"

sender_t::sender_t(controller_t *controller, size_t pipe_width) : control(controller), width(pipe_width) {}

sender_t::sender_t(size_t pipe_width) : sender_t(nullptr, pipe_width) {}

void sender_t::set_endpoint(const udp_endpoint_t &enp) {
    endpoint = enp;
}

void sender_t::send(uint8_t *data, size_t bytes) {
    if (control == nullptr) {
        while (bytes >= width) {
            send_raw(data, width);
            bytes -= width;
            data += width;
        }
        if (!bytes) return;

        send_raw(data, bytes);
        return;
    }
    size_t metadata_size = controller_t::md_size();
    if (metadata_size >= width) {
        logi(TAG, "Metadata size is too big for the pipe, cancel sending");
        return;
    }
    uint8_t swapped[metadata_size];
    while (bytes >= width) {
        // The last bytes of a packet - metadata, are put instead of the last bytes of data to avoid
        // array expansion/copy, this byte will be the first to send in the next loop.
        std::copy(data + width - metadata_size, data + width, swapped);
        control->remote_get_md(data + width - metadata_size);

        send_raw(data, width);
        std::copy(swapped, swapped + metadata_size, data + width - metadata_size);

        bytes -= width - metadata_size;
        data += width - metadata_size;
    }

    if (bytes + metadata_size > width) {
        // rare moment, but TODO
        logi(TAG, "Remaining packet is too big for the pipe");
        return;
    }
    // As there won't be a next loop anymore, the sequence should be expanded to put md.
    uint8_t remaining[bytes + metadata_size];
    std::copy(data, data + bytes, remaining);
    control->remote_get_md(remaining + bytes);
    send_raw(remaining, bytes + metadata_size);
}

void sender_t::send_raw(uint8_t *data, size_t bytes) {
    socket.send(data, bytes, endpoint);
}
