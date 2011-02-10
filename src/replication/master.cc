#include "master.hpp"

#include <boost/scoped_ptr.hpp>

#include "logger.hpp"
#include "replication/net_structs.hpp"
#include "replication/protocol.hpp"

namespace replication {

void master_t::hello(const mutex_acquisition_t& proof_of_acquisition) {
    net_hello_t msg;
    rassert(sizeof(msg.hello_magic) == 16);
    // TODO make a #define for this.
    memcpy(msg.hello_magic, "13rethinkdbrepl", 16);
    msg.replication_protocol_version = 1;
    msg.role = role_master;
    // TODO have this use actual database_magic!  Or die!
    msg.database_magic = 0;
    rassert(sizeof(msg.informal_name) == 32);
    // TODO possibly have a user configurable name.  Or decide not to.
    char informal_name[32] = "master";
    memcpy(msg.informal_name, informal_name, 32);

    slave_->write(&msg, sizeof(msg));
}

void master_t::get_cas(store_key_t *key, castime_t castime) {
    if (slave_) {
        size_t n = sizeof(headed<net_get_cas_t>) + key->size;
        scoped_malloc<headed<net_get_cas_t> > message(n);
        message->hdr.message_multipart_aspect = SMALL;
        message->hdr.msgcode = GET_CAS;
        message->hdr.msgsize = n;
        message->data.proposed_cas = castime.proposed_cas;
        message->data.timestamp = castime.timestamp;
        memcpy(message->data.key, key->contents, key->size);

        {
            mutex_acquisition_t lock(&message_contiguity_);
            slave_->write(message.get(), n);
        }
    }
}

void master_t::sarc(store_key_t *key, data_provider_t *data, mcflags_t flags, exptime_t exptime, castime_t castime, add_policy_t add_policy, replace_policy_t replace_policy, cas_t old_cas) {
    debugf("sarcing...\n");
    if (slave_) {
        debugf("the slave was alive.. sending...\n");
        if (add_policy == add_policy_yes) {
            if (replace_policy == replace_policy_yes) {
                setlike<net_set_t>(SET, key, data, flags, exptime, castime);
            } else if (replace_policy == replace_policy_no) {
                setlike<net_add_t>(ADD, key, data, flags, exptime, castime);
            } else {
                rassert(false, "invalid sarc operation");
                logWRN("invalid sarc operation in master.\n");
            }
        } else {
            if (replace_policy == replace_policy_yes) {
                setlike<net_replace_t>(REPLACE, key, data, flags, exptime, castime);
            } else if (replace_policy == replace_policy_if_cas_matches) {
                cas(key, data, flags, exptime, old_cas, castime);
            } else {
                rassert(false, "invalid sarc operation");
                logWRN("invalid sarc operation in master\n");
            }
        }
    }
    delete data;
}

template <class net_struct_type>
void master_t::setlike(int msgcode, store_key_t *key, data_provider_t *data, mcflags_t flags, exptime_t exptime, castime_t castime) {
    net_struct_type setstruct;
    setstruct.timestamp = castime.timestamp;
    setstruct.proposed_cas = castime.proposed_cas;
    setstruct.flags = flags;
    setstruct.exptime = exptime;
    setstruct.key_size = key->size;
    setstruct.value_size = data->get_size();

    stereotypical(msgcode, key, data, setstruct);
}

void master_t::cas(store_key_t *key, data_provider_t *data, mcflags_t flags, exptime_t exptime, cas_t unique, castime_t castime) {
    if (slave_) {
        net_cas_t casstruct;
        casstruct.timestamp = castime.timestamp;
        casstruct.expected_cas = unique;
        casstruct.proposed_cas = castime.proposed_cas;
        casstruct.flags = flags;
        casstruct.exptime = exptime;
        casstruct.key_size = key->size;
        casstruct.value_size = data->get_size();

        stereotypical(CAS, key, data, casstruct);
    }
}

void master_t::incr_decr(incr_decr_kind_t kind, store_key_t *key, uint64_t amount, castime_t castime) {
    if (slave_) {
        if (kind == incr_decr_INCR) {
            incr_decr_like<net_incr_t>(INCR, key, amount, castime);
        } else {
            rassert(kind == incr_decr_DECR);
            incr_decr_like<net_decr_t>(DECR, key, amount, castime);
        }
    }
}

template <class net_struct_type>
void master_t::incr_decr_like(uint8_t msgcode, store_key_t *key, uint64_t amount, castime_t castime) {
    headed<net_struct_type> msg;
    msg.hdr.message_multipart_aspect = SMALL;
    msg.hdr.msgcode = msgcode;
    msg.hdr.msgsize = sizeof(msg);
    msg.data.timestamp = castime.timestamp;
    msg.data.proposed_cas = castime.proposed_cas;
    msg.data.amount = amount;

    {
        mutex_acquisition_t lock(&message_contiguity_);
        slave_->write(&msg, sizeof(msg));
    }
}

void master_t::append_prepend(append_prepend_kind_t kind, store_key_t *key, data_provider_t *data, castime_t castime) {
    if (slave_) {
        if (kind == append_prepend_APPEND) {
            net_append_t appendstruct;
            appendstruct.timestamp = castime.timestamp;
            appendstruct.proposed_cas = castime.proposed_cas;
            appendstruct.key_size = key->size;
            appendstruct.value_size = data->get_size();

            stereotypical(APPEND, key, data, appendstruct);
        } else {
            rassert(kind == append_prepend_PREPEND);

            net_prepend_t prependstruct;
            prependstruct.timestamp = castime.timestamp;
            prependstruct.proposed_cas = castime.proposed_cas;
            prependstruct.key_size = key->size;
            prependstruct.value_size = data->get_size();

            stereotypical(PREPEND, key, data, prependstruct);
        }
    }
    delete data;
}

void master_t::delete_key(store_key_t *key, repli_timestamp timestamp) {
    if (slave_) {
        size_t n = sizeof(headed<net_delete_t>) + key->size;
        scoped_malloc<headed<net_delete_t> > message(n);
        message->hdr.message_multipart_aspect = SMALL;
        message->hdr.msgcode = DELETE;
        message->hdr.msgsize = n;
        message->data.timestamp = timestamp;
        message->data.key_size = key->size;
        memcpy(message->data.key, key->contents, key->size);

        {
            mutex_acquisition_t lock(&message_contiguity_);
            slave_->write(message.get(), n);
        }
    }
}

// For operations with keys and values whose structs use the stereotypical names.
template <class net_struct_type>
void master_t::stereotypical(int msgcode, store_key_t *key, data_provider_t *data, net_struct_type netstruct) {
    size_t n = sizeof(headed<net_struct_type>) + key->size + data->get_size();

    if (n <= 0xFFFF) {
        scoped_malloc<headed<net_struct_type> > message(n);
        message->hdr.message_multipart_aspect = SMALL;
        message->hdr.msgcode = msgcode;
        message->hdr.msgsize = n;
        message->data = netstruct;
        memcpy(message->data.keyvalue, key->contents, key->size);

        struct buffer_group_t group;
        group.add_buffer(data->get_size(), message->data.keyvalue + key->size);
        data->get_data_into_buffers(&group);

        {
            mutex_acquisition_t lock(&message_contiguity_);
            slave_->write(message.get(), n);
        }
    } else {
        // For now, we have no way to get the first chunk of all the
        // data, so the first chunk has size zero, and we send all the
        // other chunks contiguously.  Later, data_provider_t might
        // stream.

        n = sizeof(multipart_headed<net_struct_type>) + key->size;

        scoped_malloc<multipart_headed<net_struct_type> > message(n);

        message->hdr.message_multipart_aspect = FIRST;
        message->hdr.msgcode = msgcode;
        message->hdr.msgsize = n;
        message->data = netstruct;
        memcpy(message->data.keyvalue, key->contents, key->size);

        uint32_t ident = sources_.add(data);
        message->hdr.ident = ident;

        {
            mutex_acquisition_t lock(&message_contiguity_);
            slave_->write(message.get(), n);
        }

        // TODO: make sure we aren't taking liberties with the data_provider_t lifetime.
        coro_t::spawn(boost::bind(&master_t::send_data_with_ident, this, data, ident));
    }
}

void master_t::send_data_with_ident(data_provider_t *data, uint32_t ident) {
    size_t size = data->get_size();

    const const_buffer_group_t *group = data->get_data_as_buffers();

    ssize_t chunksize = 0xFFFF - sizeof(net_multipart_header_t);

    ssize_t n = size;

    net_multipart_header_t multi_header;
    multi_header.msgcode = MSGCODE_NIL;
    multi_header.ident = ident;

    std::vector<const_buffer_group_t::buffer_t>::size_type bufnum = 0;
    ssize_t off = 0;
    while (n > 0) {

        multi_header.message_multipart_aspect = n <= chunksize ? LAST : MIDDLE;
        ssize_t m = std::min(n, chunksize);
        n -= m;
        multi_header.msgsize = sizeof(net_multipart_header_t) + m;

        mutex_acquisition_t lock(&message_contiguity_);

        slave_->write(&multi_header, sizeof(multi_header));
        while (m > 0) {
            rassert(bufnum < group->num_buffers());
            const_buffer_group_t::buffer_t buf = group->get_buffer(bufnum);
            size_t write_size = std::min(m, buf.size - off);
            slave_->write(reinterpret_cast<const char *>(buf.data) + off, write_size);
            m -= write_size;
            off += write_size;

            if (buf.size == off) {
                bufnum++;
                off = 0;
            }
        }
    }

    // TODO: How do we declaim ownership of the data_provider_t?

    sources_.drop(ident);
}


void master_t::on_tcp_listener_accept(boost::scoped_ptr<linux_tcp_conn_t>& conn) {
    // TODO: Carefully handle case where a slave is already connected.

    // Right now we uncleanly close the slave connection.  What if
    // somebody has partially written a message to it (and writes the
    // rest of the message to conn?)  That will happen, the way the
    // code is, right now.
    {
        debugf("Receiving replica...\n");
        mutex_acquisition_t lock(&message_contiguity_);
        debugf("Acquired message contiguity lock.\n");
        slave_.reset();
        slave_.swap(conn);


        debugf("Received replica.\n");

        hello(lock);
    }
    // TODO when sending/receiving hello handshake, use database magic
    // to handle case where slave is already connected.

    // TODO receive hello handshake before sending other messages.
}



}  // namespace replication

