#include <impl/socket.h>

#ifdef ESP_PLATFORM
#include <esp_netif.h>

#elif !defined(__WIN32__)
#include <netinet/ip.h>
#include <arpa/inet.h>

#endif

#include <errno.h>
#include <string.h>

void endpoint_clear(endpoint_t *enp) {
    memset(enp, 0, sizeof(endpoint_t));
}

void endpoint_set_addr(endpoint_t *enp, const char *addr) {
    auto *enp4 = (struct sockaddr_in *) enp;
    enp4->sin_family = AF_INET;
    enp4->sin_addr.s_addr = inet_addr(addr);
}

void endpoint_get_addr(endpoint_t *enp, char *addr) {
    auto *enp4 = (struct sockaddr_in *) enp;
    inet_ntop(AF_INET, &enp4->sin_addr, addr, 16);
}

void endpoint_set_port(endpoint_t *enp, uint16_t port) {
    auto *enp4 = (struct sockaddr_in *) enp;
    enp4->sin_port = htons(port);
}

uint16_t endpoint_get_port(endpoint_t *enp) {
    auto *enp4 = (struct sockaddr_in *) enp;
    return ntohs(enp4->sin_port);
}

// void endpoint_set_addr_v6(endpoint_t *enp, const char *addr) {
//     auto *enp6 = (struct sockaddr_in6 *)enp;
//     enp6->sin6_family = AF_INET6;
//     inet_pton(AF_INET6, addr, &enp6->sin6_addr);
// }

// void endpoint_get_addr_v6(endpoint_t *enp, char *addr) {
//     auto *enp6 = (struct sockaddr_in6 *)enp;
//     inet_ntop(AF_INET6, &enp6->sin6_addr, addr, 40);
// }

void socket_init() {
#if defined (__WIN32__)
    WSADATA wsa_data;
    int res = WSAStartup(WSA_VER, &wsa_data);
    if (res != 0) {
        loge("WSAStartup returned err: %d", res);
    }
#elif defined(ESP_PLATFORM)
    ESP_ERROR_CHECK(esp_netif_init());
#endif
}

int socket_errno() {
#if defined (__WIN32__)
    return WSAGetLastError();
#else
    return errno;
#endif
}
