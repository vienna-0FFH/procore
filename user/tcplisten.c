#include <file.h>
#include <socket.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#define TCP_LISTENER_PORT 18082
#define TCP_PROBE_TEXT    "ucore-listener"

int
main(void) {
    struct sockaddr_in address;
    char buffer[sizeof(TCP_PROBE_TEXT) - 1];
    int listener;
    int connection;
    int received;

    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(TCP_LISTENER_PORT);
    address.sin_addr = INADDR_ANY;
    assert(bind(listener, &address, sizeof(address)) == 0);
    assert(listen(listener, 4) == 0);
    connection = accept(listener, NULL, 0);
    assert(connection >= 0);
    received = recv(connection, buffer, sizeof(buffer));
    assert(received == sizeof(buffer));
    assert(memcmp(buffer, TCP_PROBE_TEXT, sizeof(buffer)) == 0);
    assert(send(connection, buffer, sizeof(buffer)) == sizeof(buffer));
    assert(shutdown(connection, SHUT_WR) == 0);
    close(connection);
    close(listener);
    cprintf("TCP listener handshake and echo test pass.\n");
    return 0;
}
