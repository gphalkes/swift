/*
 *  swift.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */

#include <stdlib.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/select.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <string.h>

//#include <glog/logging.h>
#include "swift.h"
#include "datagram.h"

using namespace std;
using namespace swift;

swift::tint Channel::last_tick = 0;
int Channel::MAX_REORDERING = 4;
bool Channel::SELF_CONN_OK = false;
swift::tint Channel::TIMEOUT = TINT_SEC*60;
std::vector<Channel*> Channel::channels(1);
socket_callbacks_t Channel::sockets[SWFT_MAX_SOCK_OPEN] = {};
int Channel::socket_count = 0;
Address Channel::tracker;
tbheap Channel::send_queue;
FILE* Channel::debug_file = NULL;
#include "ext/simple_selector.cpp"
PeerSelector* Channel::peer_selector = new SimpleSelector();
tint Channel::MIN_PEX_REQUEST_INTERVAL = TINT_SEC;

Channel::Channel    (FileTransfer* transfer, int socket, Address peer_addr) :
    peer_(peer_addr),
    socket_(socket==INVALID_SOCKET?sockets[0].sock:socket), // FIXME
    transfer_(transfer), peer_channel_id_(0), own_id_mentioned_(false),
    data_in_(TINT_NEVER,bin64_t::NONE), data_in_dbl_(bin64_t::NONE),
    data_out_cap_(bin64_t::ALL), hint_out_size_(0), pex_requested_(false),
    last_pex_request_time_(0), next_pex_request_time_(0),
    pex_request_outstanding_(false), useless_pex_count_(0),
    rtt_avg_(TINT_SEC), dev_avg_(0), dip_avg_(TINT_SEC), last_send_time_(0),
    last_recv_time_(0), last_data_out_time_(0), last_data_in_time_(0),
    last_loss_time_(0), next_send_time_(0), cwnd_(1), send_interval_(TINT_SEC),
    send_control_(PING_PONG_CONTROL), sent_since_recv_(0), ack_rcvd_recent_(0),
    ack_not_rcvd_recent_(0), owd_min_bin_(0), owd_min_bin_start_(NOW),
    owd_cur_bin_(0), dgrams_sent_(0), dgrams_rcvd_(0)
{
    if (peer_==Address())
        peer_ = tracker;
    this->id_ = channels.size();
    channels.push_back(this);
    transfer_->hs_in_.push_back(id_);
    for(int i=0; i<4; i++) {
        owd_min_bins_[i] = TINT_NEVER;
        owd_current_[i] = TINT_NEVER;
    }
    Reschedule();
    dprintf("%s #%u init %s\n",tintstr(),id_,peer_.str());
}


Channel::~Channel () {
    channels[id_] = NULL;
}


void     swift::SetTracker(const Address& tracker) {
    Channel::tracker = tracker;
}


int Channel::DecodeID(int scrambled) {
    return scrambled ^ (int)Datagram::start;
}
int Channel::EncodeID(int unscrambled) {
    return unscrambled ^ (int)Datagram::start;
}


int     swift::Listen (Address addr) {
    int sock = Datagram::Bind(addr);
    if (sock!=INVALID_SOCKET) {
        socket_callbacks_t cb(sock);
        cb.may_read = &Channel::RecvDatagram;
        Channel::sockets[Channel::socket_count++] = cb;
    }
    return sock;
}


void    swift::Shutdown (int sock_des) {
    for(int i=0; i<Channel::socket_count; i++)
        if (sock_des==-1 || Channel::sockets[i].sock==sock_des) {
            Datagram::Close(Channel::sockets[i].sock);
            Channel::sockets[i] = Channel::sockets[--Channel::socket_count];
        }
}


void    swift::Loop (tint till) {
    Channel::Loop(till);
}


bool    swift::Listen3rdPartySocket (socket_callbacks_t cb) {
    int i=0;
    while (i<Channel::socket_count && Channel::sockets[i].sock!=cb.sock) i++;
    if (i==Channel::socket_count) {
        if (i==SWFT_MAX_SOCK_OPEN)
            return false;
        else
            Channel::socket_count++;
	}
    Channel::sockets[i]=cb;
    if (!cb.may_read && !cb.may_write && !cb.on_error)
        Channel::sockets[i] = Channel::sockets[--Channel::socket_count];
    return true;
}


int      swift::Open (const char* filename, const Sha1Hash& hash) {
    FileTransfer* ft = new FileTransfer(filename, hash);
    if (ft && ft->file().file_descriptor()) {

        /*if (FileTransfer::files.size()<fdes)  // FIXME duplication
            FileTransfer::files.resize(fdes);
        FileTransfer::files[fdes] = ft;*/

        // initiate tracker connections
        if (Channel::tracker!=Address())
            new Channel(ft);

        return ft->file().file_descriptor();
    } else {
        if (ft)
            delete ft;
        return -1;
    }
}


void    swift::Close (int fd) {
    if (fd<FileTransfer::files.size() && FileTransfer::files[fd])
        delete FileTransfer::files[fd];
}


void    swift::AddPeer (Address address, const Sha1Hash& root) {
    Channel::peer_selector->AddPeer(address,root);
}


uint64_t  swift::Size (int fdes) {
    if (FileTransfer::files.size()>fdes && FileTransfer::files[fdes])
        return FileTransfer::files[fdes]->file().size();
    else
        return 0;
}


bool  swift::IsComplete (int fdes) {
    if (FileTransfer::files.size()>fdes && FileTransfer::files[fdes])
        return FileTransfer::files[fdes]->file().is_complete();
    else
        return 0;
}


uint64_t  swift::Complete (int fdes) {
    if (FileTransfer::files.size()>fdes && FileTransfer::files[fdes])
        return FileTransfer::files[fdes]->file().complete();
    else
        return 0;
}


uint64_t  swift::SeqComplete (int fdes) {
    if (FileTransfer::files.size()>fdes && FileTransfer::files[fdes])
        return FileTransfer::files[fdes]->file().seq_complete();
    else
        return 0;
}


const Sha1Hash& swift::RootMerkleHash (int file) {
    FileTransfer* trans = FileTransfer::file(file);
    if (!trans)
        return Sha1Hash::ZERO;
    return trans->file().root_hash();
}


/**    <h2> swift handshake </h2>
 Basic rules:
 <ul>
 <li>    to send a datagram, a channel must be created
 (channels are cheap and easily recycled)
 <li>    a datagram must contain either the receiving
 channel id (scrambled) or the root hash
 </ul>
 <b>Note:</b>
 */
