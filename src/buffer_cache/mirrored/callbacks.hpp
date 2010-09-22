#ifndef __BUFFER_CACHE_CALLBACKS_HPP__
#define __BUFFER_CACHE_CALLBACKS_HPP__

template<class mc_config_t> class mc_buf_t;
template<class mc_config_t> class mc_transaction_t;
template<class mc_config_t> class mc_cache_t;

template<class mc_config_t>
struct mc_block_available_callback_t :
    public intrusive_list_node_t<mc_block_available_callback_t<mc_config_t> >
{
    virtual ~mc_block_available_callback_t() {}
    virtual void on_block_available(mc_buf_t<mc_config_t> *buf) = 0;
};

template<class mc_config_t>
struct mc_transaction_begin_callback_t {
    virtual ~mc_transaction_begin_callback_t() {}
    virtual void on_txn_begin(mc_transaction_t<mc_config_t> *txn) = 0;
};

template<class mc_config_t>
struct mc_transaction_commit_callback_t {
    virtual ~mc_transaction_commit_callback_t() {}
    virtual void on_txn_commit(mc_transaction_t<mc_config_t> *txn) = 0;
};

#endif // __BUFFER_CACHE_CALLBACKS_HPP__
