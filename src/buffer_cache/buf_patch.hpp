// Copyright 2010-2012 RethinkDB, all rights reserved.
#ifndef BUFFER_CACHE_BUF_PATCH_HPP_
#define BUFFER_CACHE_BUF_PATCH_HPP_

#include <string>

/*
 * This file provides the basic buf_patch_t type as well as a few low-level binary
 * patch implementations (currently memmove and memcpy patches)
 */

class buf_patch_t;

#include "buffer_cache/types.hpp"
#include "containers/scoped.hpp"
#include "serializer/types.hpp"

typedef uint32_t patch_counter_t;
typedef int8_t patch_operation_code_t;

/*
 * As the buf_patch code is used in both extract and server, it should not crash
 * in case of a failed patch deserialization (i.e. loading a patch from disk).
 * Instead patches should emit a patch_deserialization_error_t exception.
 */
class patch_deserialization_error_t {
public:
    explicit patch_deserialization_error_t(const std::string &message);
    const char *c_str() const { return message_.c_str(); }
private:
    std::string message_;
};
std::string patch_deserialization_message(const char *file, int line, const char *msg);
#define guarantee_patch_format(cond) do { \
        if (!(cond)) {                                                  \
            throw patch_deserialization_error_t(patch_deserialization_message(__FILE__, __LINE__, "buf patch deserialization error")); \
        }                                                               \
    } while (0)


/*
 * A buf_patch_t is an in-memory representation for a patch. A patch describes
 * a specific change which can be applied to a buffer.
 * Each buffer patch has a patch counter as well as a block sequence id. The block sequence id
 * is used to determine to which version of a block the patch applies. Within
 * one version of a block, the patch counter explicitly encodes an ordering, which
 * is used to ensure that patches can be applied in the correct order
 * (even if they get serialized to disk in a different order).
 *
 * While buf_patch_t provides the general interface of a buffer patch and a few
 * universal methods, subclasses are required to implement an apply_to_buf method
 * (which executes the actual patch operation) as well as methods which handle
 * serializing and deserializing the subtype specific data.
 */
class buf_patch_t {
public:
    virtual ~buf_patch_t() { }

    // Serializes the patch to the given destination address
    void serialize(char* destination) const;

    inline uint16_t get_serialized_size() const {
        return sizeof(uint16_t) + sizeof(block_id_t) + sizeof(patch_operation_code_t) + get_data_size();
    }
    inline static uint16_t get_min_serialized_size() {
        return sizeof(uint16_t) + sizeof(block_id_t) + sizeof(patch_operation_code_t);
    }

    inline block_id_t get_block_id() const {
        return block_id;
    }

    // This is called from buf_lock_t
    virtual void apply_to_buf(char* buf_data, block_size_t block_size) = 0;

protected:
    virtual uint16_t get_data_size() const = 0;

    // These are for usage in subclasses
    buf_patch_t(block_id_t block_id, patch_operation_code_t operation_code);
    virtual void serialize_data(char *destination) const = 0;

    static const patch_operation_code_t OPER_MEMCPY = 0;
    static const patch_operation_code_t OPER_MEMMOVE = 1;
    static const patch_operation_code_t OPER_LEAF_SHIFT_PAIRS = 2;
    static const patch_operation_code_t OPER_LEAF_INSERT_PAIR = 3;
    static const patch_operation_code_t OPER_LEAF_INSERT = 4;
    static const patch_operation_code_t OPER_LEAF_REMOVE = 5;
    static const patch_operation_code_t OPER_LEAF_ERASE_PRESENCE = 6;
    /* Assign an operation id to new subtypes here */
    /* Please note: you also have to "register" new operations in buf_patch_t::load_patch() */

private:
    block_id_t block_id;
    patch_operation_code_t operation_code;

    DISABLE_COPYING(buf_patch_t);
};

#endif /* BUFFER_CACHE_BUF_PATCH_HPP_ */

