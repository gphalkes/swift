/*
 *  nat_test_server.c
 *  NAT type testing (server).
 *
 *  Created by Gertjan Halkes.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

//FIXME: add timestamp to log output

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>

#define REQUEST_MAGIC 0x5a9e5fa1
#define REPLY_MAGIC 0xa655c5d5
#define REPLY_SEC_MAGIC 0x85e4a5ca

/** Alert the user of a fatal error and quit.
    @param fmt The format string for the message. See fprintf(3) for details.
    @param ... The arguments for printing.
*/
void fatal(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

static int has_secondary;

int main(int argc, char *argv[]) {
    struct sockaddr_in local, remote, secondary;
    uint32_t packet[3];
    int c, sock, sock2, sock3, sock4;
    ssize_t result;

    local.sin_addr.s_addr = INADDR_ANY;

    while ((c = getopt(argc, argv, "s:")) > 0) {
        switch (c) {
            case 's':
                has_secondary = 1;
                secondary.sin_addr.s_addr = inet_addr(optarg);
                break;
            default:
                fatal("Unknown option %c\n", c);
                break;
        }
    }

    if (argc - optind != 3)
        fatal("Usage: nat_test_server [<options>] <primary address> <primary port> <secondary port>\n");

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = inet_addr(argv[optind++]);
    local.sin_port = htons(atoi(argv[optind++]));

    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
        fatal("Error opening primary socket: %m\n");
    if (bind(sock, (struct sockaddr *) &local, sizeof(local)) < 0)
        fatal("Error binding primary socket: %m\n");

    if (has_secondary) {
        secondary.sin_family = AF_INET;
        secondary.sin_port = local.sin_port;

        if ((sock3 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
            fatal("Error opening primary socket on secondary address: %m\n");
        if (bind(sock3, (struct sockaddr *) &secondary, sizeof(secondary)) < 0)
            fatal("Error binding primary socket on secondary address: %m\n");
    }

    local.sin_port = htons(atoi(argv[optind++]));

    if ((sock2 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
        fatal("Error opening secondary socket: %m\n");
    if (bind(sock2, (struct sockaddr *) &local, sizeof(local)) < 0)
        fatal("Error binding secondary socket: %m\n");

    if (has_secondary) {
        secondary.sin_port = local.sin_port;

        if ((sock4 = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
            fatal("Error opening secondary socket on secondary address: %m\n");
        if (bind(sock4, (struct sockaddr *) &secondary, sizeof(secondary)) < 0)
            fatal("Error binding secondary socket on secondary address: %m\n");
    }

    while (1) {
        socklen_t socklen = sizeof(remote);
        if ((result = recvfrom(sock, &packet, sizeof(packet), 0, (struct sockaddr *) &remote, &socklen)) < 0) {
            if (errno == EAGAIN)
                continue;
            fatal("Error receiving packet: %m\n");
        } else if (result != 4 || ntohl(packet[0]) != REQUEST_MAGIC) {
            fprintf(stderr, "Strange packet received from %s\n", inet_ntoa(remote.sin_addr));
        } else {
            fprintf(stderr, "Received packet from %s:%d\n", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));
            packet[0] = htonl(REPLY_MAGIC);
            packet[1] = remote.sin_addr.s_addr;
            *(uint16_t *)(packet + 2) = remote.sin_port;
    retry:
            if (sendto(sock, packet, 10, 0, (const struct sockaddr *) &remote, socklen) < 10) {
                if (errno == EAGAIN)
                    goto retry;
                fatal("Error sending packet on primary socket\n");
            }
    retry2:
            if (sendto(sock2, packet, 10, 0, (const struct sockaddr *) &remote, socklen) < 10) {
                if (errno == EAGAIN)
                    goto retry2;
                fatal("Error sending packet on secondary socket\n");
            }

            if (has_secondary) {
                packet[0] = htonl(REPLY_SEC_MAGIC);
        retry3:
                if (sendto(sock3, packet, 4, 0, (const struct sockaddr *) &remote, socklen) < 4) {
                    if (errno == EAGAIN)
                        goto retry3;
                    fatal("Error sending packet on primary socket on secondary address\n");
                }
        retry4:
                if (sendto(sock4, packet, 4, 0, (const struct sockaddr *) &remote, socklen) < 4) {
                    if (errno == EAGAIN)
                        goto retry4;
                    fatal("Error sending packet on secondary socket on secondary address\n");
                }
            }

        }
    }
    return 0;
}
