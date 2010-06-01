/*
 *  transfer.cpp
 *  some transfer-scope code
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */
#include <errno.h>
#include <string>
#include <sstream>
#include "swift.h"

#include "ext/seq_picker.cpp" // FIXME FIXME FIXME FIXME

using namespace swift;

std::vector<FileTransfer*> FileTransfer::files(20);

#define BINHASHSIZE (sizeof(bin64_t)+sizeof(Sha1Hash))

// FIXME: separate Bootstrap() and Download(), then Size(), Progress(), SeqProgress()

FileTransfer::FileTransfer (const char* filename, const Sha1Hash& _root_hash) :
    file_(filename,_root_hash), cb_installed(0)
{
    if (files.size()<fd()+1)
        files.resize(fd()+1);
    files[fd()] = this;
    picker_ = new SeqPiecePicker(this);
    picker_->Randomize(rand()&63);
    init_time_ = Datagram::Time();
}


void    Channel::CloseTransfer (FileTransfer* trans) {
    for(int i=0; i<Channel::channels.size(); i++)
        if (Channel::channels[i] && Channel::channels[i]->transfer_==trans)
            delete Channel::channels[i];
}


void swift::AddProgressCallback (int transfer, TransferProgressCallback cb) {
    FileTransfer* trans = FileTransfer::file(transfer);
    if (!trans)
        return;
    trans->callbacks[trans->cb_installed++] = cb;
}


void swift::RemoveProgressCallback (int transfer, TransferProgressCallback cb) {
    FileTransfer* trans = FileTransfer::file(transfer);
    if (!trans)
        return;
    for(int i=0; i<trans->cb_installed; i++)
        if (trans->callbacks[i]==cb)
            trans->callbacks[i]=trans->callbacks[--trans->cb_installed];
}


FileTransfer::~FileTransfer ()
{
    Channel::CloseTransfer(this);
    files[fd()] = NULL;
    delete picker_;
}


FileTransfer* FileTransfer::Find (const Sha1Hash& root_hash) {
    for(int i=0; i<files.size(); i++)
        if (files[i] && files[i]->root_hash()==root_hash)
            return files[i];
    return NULL;
}


int swift:: Find (Sha1Hash hash) {
    FileTransfer* t = FileTransfer::Find(hash);
    if (t)
        return t->fd();
    return -1;
}



bool FileTransfer::OnPexIn (const Address& addr) {
    for(int i=0; i<hs_in_.size(); i++) {
        Channel* c = Channel::channel(hs_in_[i]);
        if (c && c->transfer().fd()==this->fd() && (c->peer()==addr ||
                (!c->is_established() && c->peer().is_same_ip(addr)))) {
            return false; // already connected or connecting to this IP address
        }
    }
    if (hs_in_.size()<20)
        new Channel(this,Channel::default_socket(),addr);
    return true;
}


int FileTransfer::RandomChannel (int own_id) {
    binqueue choose_from;
    int i;

    for (i = 0; i < (int) hs_in_.size(); i++) {
        if (hs_in_[i] == own_id)
            continue;
        Channel *c = Channel::channel(hs_in_[i]);
        if (c == NULL || c->transfer().fd() != this->fd()) {
            /* Channel was closed or is not associated with this FileTransfer (anymore). */
            hs_in_[i] = hs_in_[0];
            hs_in_.pop_front();
            i--;
            continue;
        }
        if (!c->is_established())
            continue;
        choose_from.push_back(hs_in_[i]);
    }
    if (choose_from.size() == 0)
        return -1;

    return choose_from[((double) rand() / RAND_MAX) * choose_from.size()];
}

