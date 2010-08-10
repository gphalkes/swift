/*
 *  nat_test.cpp
 *  NAT type testing.
 *
 *  Created by Gertjan Halkes.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */

#include "swift.h"

#define REQUEST_MAGIC 0x5a9e5fa1
#define REPLY_MAGIC 0xa655c5d5
#define REPLY_SEC_MAGIC 0x85e4a5ca
#define MAX_TRIES 3
namespace swift {

static void on_may_receive(SOCKET sock);
static void on_may_send(SOCKET sock);
static tint test_start;
static int tries;
static int packets_since_last_try;

static sckrwecb_t callbacks(0, on_may_receive, on_may_send, NULL);

#warning Change addresses to actual addresses used in test
static Address servers[2] = { Address("dutigp.st.ewi.tudelft.nl:18375"),
    Address("127.0.0.3:18375") };


static void on_may_receive(SOCKET sock) {
    Datagram data(sock);

    data.Recv();

    uint32_t magic = data.Pull32();
    if ((magic != REPLY_MAGIC && magic != REPLY_SEC_MAGIC) ||
            (magic == REPLY_MAGIC && data.size() != 6) || (magic == REPLY_SEC_MAGIC && data.size() != 0))
    {
        dprintf("%s #0 NATTEST weird packet %s \n", tintstr(), data.address().str());
        return;
    }

    if (magic == REPLY_MAGIC) {
        uint32_t ip = data.Pull32();
        uint16_t port = data.Pull16();
        Address reported(ip, port);
        dprintf("%s #0 NATTEST incoming %s %s\n", tintstr(), data.address().str(), reported.str());
    } else {
        dprintf("%s #0 NATTEST incoming secondary %s\n", tintstr(), data.address().str());
    }
    packets_since_last_try++;
}

static void on_may_send(SOCKET sock) {
    callbacks.may_write = NULL;
    Datagram::Listen3rdPartySocket(callbacks);

    for (size_t i = 0; i < (sizeof(servers)/sizeof(servers[0])); i++) {
        Datagram request(sock, servers[i]);

        request.Push32(REQUEST_MAGIC);
        request.Send();
    }
    test_start = NOW;

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr *) &name, &namelen) < 0) {
        dprintf("%s #0 NATTEST could not get local address\n", tintstr());
    } else {
        Address local(ntohl(name.sin_addr.s_addr), ntohs(name.sin_port));
        dprintf("%s #0 NATTEST local %s\n", tintstr(), local.str());
    }
}

void nat_test_update(void) {
    if (tries < MAX_TRIES && NOW - test_start > 30 * TINT_SEC) {
        if (tries == 0) {
            Address any;
            SOCKET sock = Datagram::Bind(any, callbacks);
            callbacks.sock = sock;
        } else if (packets_since_last_try == 0) {
            // Keep on trying if we didn't receive _any_ packet in response to our last request
            tries--;
        }
        tries++;
        callbacks.may_write = on_may_send;
        Datagram::Listen3rdPartySocket(callbacks);
    }
}

}
