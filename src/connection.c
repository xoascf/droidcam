/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "connection.h"

SOCKET wifiServerSocket = INVALID_SOCKET;
extern atomic_bool v_running;

const char* DROIDCAM_CONNECT_ERROR = \
    "Connect failed, please try again.\n"
    "Check IP and Port.\n"
    "Check network connection.\n";

SOCKET Connect(const char* ip, int port, const char **errormsg) {
    int flags;
    struct sockaddr_in sin;
    SOCKET sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

    printf("connecting to %s:%d\n", ip, port);
    if(sock == INVALID_SOCKET) {
        errprint("socket() error %d '%s'\n", errno, strerror(errno));
        *errormsg = strerror(errno);
        goto _error_out;
    }
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ip);
    sin.sin_port = htons(port);

    flags = fcntl(sock, F_GETFL, NULL);
    if(flags < 0) {
        errprint("fcntl() error %d '%s'\n", errno, strerror(errno));
        *errormsg = strerror(errno);
        close(sock);
        sock = INVALID_SOCKET;
        goto _error_out;
    }
    flags |= O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);

    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);

    connect(sock, (struct sockaddr*)&sin, sizeof(sin));
    if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS) || (select(sock+1, NULL, &set, NULL, &timeout) <= 0)) {
        dbgprint("connect timeout/error=%d '%s'\n", errno, strerror(errno));
        *errormsg = DROIDCAM_CONNECT_ERROR;
        close(sock);
        sock = INVALID_SOCKET;
        goto _error_out;
    }

    flags = fcntl(sock, F_GETFL, NULL);
    flags &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0
    || setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
        perror("setsockopt failed");

_error_out:
    dbgprint(" - return fd: %d\n", sock);
    return sock;
}

int Send(const char * buffer, int bytes, SOCKET s) {
    ssize_t w = 0;
    char *ptr = (char*) buffer;
    while (bytes > 0) {
        w = send(s, ptr, bytes, 0);
        if (w <= 0) {
            return -1;
        }
        bytes -= w;
        ptr += w;
    }
    return 1;
}

int Recv(const char* buffer, int bytes, SOCKET s) {
    return recv(s, (char*)buffer, bytes, 0);
}

int RecvAll(const char* buffer, int bytes, SOCKET s) {
    return recv(s, (char*)buffer, bytes, MSG_WAITALL);
}

int RecvNonBlock(char * buffer, int bytes, SOCKET s) {
    int res = recv(s, buffer, bytes, MSG_DONTWAIT);
    return (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) ? 0 : res;
}

int RecvNonBlockUDP(char * buffer, int bytes, SOCKET s) {
    struct sockaddr_in from;
    socklen_t fromLen = sizeof(from);
    int res = recvfrom(s, buffer, bytes, MSG_DONTWAIT, (struct sockaddr *)&from, &fromLen);
    return (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) ? 0 : res;
}

int SendUDPMessage(SOCKET s, const char *message, int length, char *ip, int port) {
    struct sockaddr_in sin;
    sin.sin_port = htons((uint16_t)port);
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(ip);
    return sendto(s, message, length, 0, (struct sockaddr *)&sin, sizeof(sin));
}

SOCKET CreateUdpSocket(void) {
    return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

static int StartInetServer(int port)
{
    int flags = 0;
    struct sockaddr_in sin;

    sin.sin_family    = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = htons(port);

    wifiServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(wifiServerSocket == INVALID_SOCKET)
    {
        MSG_LASTERROR("Could not create socket");
        goto _error_out;
    }

    if(bind(wifiServerSocket, (struct sockaddr*)&sin, sizeof(sin)) < 0)
    {
        MSG_LASTERROR("Error: bind");
        goto _error_out;
    }
    if(listen(wifiServerSocket, 1) < 0)
    {
        MSG_LASTERROR("Error: listen");
        goto _error_out;
    }

    flags = fcntl(wifiServerSocket, F_GETFL, NULL);
    if(flags < 0)
    {
        MSG_LASTERROR("Error: fcntl");
        goto _error_out;
    }
    flags |= O_NONBLOCK;
    fcntl(wifiServerSocket, F_SETFL, flags);

    return 1;

_error_out:
    if (wifiServerSocket != INVALID_SOCKET){
        close(wifiServerSocket);
        wifiServerSocket = INVALID_SOCKET;
    }

    return 0;
}

void connection_cleanup() {
    if (wifiServerSocket != INVALID_SOCKET) {
        close(wifiServerSocket);
        wifiServerSocket = INVALID_SOCKET;
    }
}

void disconnect(SOCKET s) {
    close(s);
}

SOCKET accept_connection(int port)
{
    int flags;
    SOCKET client =  INVALID_SOCKET;

    dbgprint("serverSocket=%d\n", wifiServerSocket);
    if (wifiServerSocket == INVALID_SOCKET && !StartInetServer(port))
        goto _error_out;

    errprint("waiting on port %d..", port);
    while(v_running && (client = accept(wifiServerSocket, NULL, NULL)) == INVALID_SOCKET)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR){
            usleep(50000);
            continue;
        }
        MSG_LASTERROR("Accept Failed");
        break;
    }
    errprint("got socket %d\n", client);

    if (client != INVALID_SOCKET) {
        // Blocking..
        flags = fcntl(wifiServerSocket, F_GETFL, NULL);
        flags |= O_NONBLOCK;
        fcntl(wifiServerSocket, F_SETFL, flags);
    }

_error_out:
    return client;
}
