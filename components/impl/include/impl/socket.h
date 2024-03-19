#ifndef IMPL_SOCKET_H
#define IMPL_SOCKET_H

#include <cstdint>

#ifdef ESP_PLATFORM

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <lwip/inet.h>

#elif defined (__WIN32__)
#define WSA_VER MAKEWORD(2, 2)

#include <winsock2.h>
#include <winsock.h>
#include <wspiapi.h>

#else
#include <sys/socket.h>
#endif

typedef struct sockaddr_storage endpoint_t;

#if defined (__WIN32__)
typedef SOCKET socket_t;
#else
typedef int socket_t;
#endif

void endpoint_clear(endpoint_t *enp);

void endpoint_set_addr_v4(endpoint_t *enp, const char *addr);

void endpoint_set_addr_v6(endpoint_t *enp, const char *addr);

void endpoint_set_port(endpoint_t *enp, uint16_t port, uint16_t family = AF_UNSPEC);

void endpoint_get_addr_v4(endpoint_t *enp, char *addr);

void endpoint_get_addr_v6(endpoint_t *enp, char *addr);

uint16_t endpoint_get_port(endpoint_t *enp);

void socket_init();

int socket_errno();

#endif //IMPL_SOCKET_H
