#ifndef _JSONRPC_H
#define _JSONRPC_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <msgpack.h>
#include "myutils/zmq.hpp"

namespace jsonrpc {

    using namespace std;
    using namespace std::chrono;

    class MsgPackPacker {
        MsgPackPacker(const MsgPackPacker& copy) {
        }

    public:
        msgpack_sbuffer sb;
        msgpack_packer pk;

        MsgPackPacker() {
            msgpack_sbuffer_init(&sb);
            msgpack_packer_init(&pk, &sb, msgpack_sbuffer_write);
        }
        ~MsgPackPacker() {
            msgpack_sbuffer_destroy(&sb);
        }

        inline void pack_int  (int     v)  { msgpack_pack_int(&pk, v); }
        inline void pack_int8 (int8_t  v)  { msgpack_pack_int8(&pk, v); }
        inline void pack_int16(int16_t v)  { msgpack_pack_int16(&pk, v); }
        inline void pack_int32(int32_t v)  { msgpack_pack_int32(&pk, v); }
        inline void pack_int64(int64_t v)  { msgpack_pack_int64(&pk, v); }
        inline void pack_uint8 (uint8_t  v){ msgpack_pack_uint8(&pk, v); }
        inline void pack_uint16(uint16_t v){ msgpack_pack_uint16(&pk, v); }
        inline void pack_uint32(uint32_t v){ msgpack_pack_uint32(&pk, v); }
        inline void pack_uint64(uint64_t v){ msgpack_pack_uint64(&pk, v); }

        inline void pack_double(double v)  { msgpack_pack_double(&pk, v); }
        inline void pack_array(size_t size){ msgpack_pack_array(&pk, size); }
        inline void pack_map(size_t size)  { msgpack_pack_map(&pk, size); }
        inline void pack_bool(bool v)      { if (v) msgpack_pack_true(&pk); else msgpack_pack_false(&pk); }

        inline void pack_string(const string& str) { pack_string(str.c_str(), str.size()); }

        inline void pack_bin (const char* data, int len) {
            msgpack_pack_bin(&pk, len);
            msgpack_pack_bin_body(&pk, data, len);
        }

        inline void pack_string(const char* str, size_t len = 0)  { 
            if (len == 0) len = strlen(str);
            msgpack_pack_str(&pk, len);
            msgpack_pack_str_body(&pk, str, len); 
        }

        inline void pack_map_item(const char* key, int64_t val)
        {
            pack_string(key);
            pack_int64( val);
        }

        inline void pack_map_item(const char* key, uint32_t val)
        {
            pack_string(key);
            pack_uint32 (val);
        }

        inline void pack_map_item(const char* key, uint64_t val)
        {
            pack_string(key);
            pack_uint64(val);
        }

        inline void pack_map_item(const char* key, int32_t val)
        {
            pack_string(key);
            pack_int32(val);
        }

        inline void pack_map_item(const char* key, bool val)
        {
            pack_string(key);
            pack_bool(val);
        }

        inline void pack_map_item(const char* key, double val)
        {
            pack_string(key);
            pack_double(val);
        }
        inline void pack_map_item(const char* key, const string& str)
        {
            pack_string(key);
            pack_string(str);
        }
        inline void pack_map_item(const char* key, const char* str)
        {
            pack_string(key);
            pack_string(str);
        }
        inline void pack_map_item_obj(const char* key, const char* obj, size_t obj_len)
        {
            pack_string(key);
            msgpack_pack_str_body(&pk, obj, obj_len);
        }

        inline void pack_map_item_bin(const char* key, const char* bin, int bin_len)
        {
            pack_string(key);
            pack_bin(bin, bin_len);
        }

    };

    static inline bool is_map(msgpack_object& o)    { return o.type == MSGPACK_OBJECT_MAP; }

    static inline bool is_nil(msgpack_object& obj)  {  return obj.type == MSGPACK_OBJECT_NIL;   }

    static inline bool is_bin(msgpack_object& obj)  {   return obj.type == MSGPACK_OBJECT_BIN;  }

    static inline bool is_arr(msgpack_object& obj)  { return obj.type == MSGPACK_OBJECT_ARRAY; }

    static inline bool mp_get(msgpack_object& o, string* v)
    {
        if (o.type != MSGPACK_OBJECT_STR) return false;
        v->assign(o.via.str.ptr, o.via.str.size);
        return true;
    }

    static inline bool mp_get(msgpack_object& o, int64_t* v)
    {
        if (o.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
            *v = o.via.u64;
            return true;
        }
        else if (o.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
            *v = o.via.i64;
            return true;
        }
        else {
            return false;
        }
    }

    static inline bool mp_get(msgpack_object& o, int32_t* v)
    {
        int64_t v2;
        if (mp_get(o, &v2)) {
            *v = (int32_t)v2;
            return true;
        }
        else {
            return false;
        }
    }

    static inline bool mp_get(msgpack_object& o, bool* v)
    {
        if (o.type == MSGPACK_OBJECT_BOOLEAN) {
            *v = o.via.boolean;
            return true;
        }
        else {
            return false;
        }
    }

    static inline bool mp_get(msgpack_object& o, double* v)
    {
        if (o.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
            *v = (double)o.via.u64;
            return true;
        }
        else if (o.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
            *v = (double)o.via.i64;
            return true;
        }
        else if (o.type == MSGPACK_OBJECT_FLOAT ||
                 o.type == MSGPACK_OBJECT_FLOAT32 ||
                 o.type == MSGPACK_OBJECT_FLOAT64)
        {
            *v = o.via.f64;
            return true;
        }
        else {
            return false;
        }
    }

    static inline bool get_map_field_int(msgpack_object& o, const char* key, int64_t* v)
    {
        if (!is_map(o)) return false;

        msgpack_object_kv* p = o.via.map.ptr;
        msgpack_object_kv* p_end = p + o.via.map.size;
        for (; p < p_end; p++) {
            if (p->key.type != MSGPACK_OBJECT_STR) continue;
            string str(p->key.via.str.ptr, p->key.via.str.size);
            if (str  == key){
                if (p->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                    *v = p->val.via.u64;
                    return true;
                }
                else if (p->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                    *v = p->val.via.i64;
                    return true;
                }
                else {
                    return false;
                }
            }
        }

        return false;
    }

    static inline bool get_map_field_bool(msgpack_object& o, const char* key, bool* v)
    {
        if (!is_map(o)) return false;

        msgpack_object_kv* p = o.via.map.ptr;
        msgpack_object_kv* p_end = p + o.via.map.size;
        for (; p < p_end; p++) {
            if (p->key.type != MSGPACK_OBJECT_STR) continue;
            string str(p->key.via.str.ptr, p->key.via.str.size);
            if (str == key){
                if (p->val.type == MSGPACK_OBJECT_BOOLEAN) {
                    *v = p->val.via.boolean;
                    return true;
                }
                else {
                    return false;
                }
            }
        }

        return false;
    }

    static inline bool get_map_field_str(msgpack_object& o, const char* key, string* v)
    {
        if (!is_map(o)) return false;

        msgpack_object_kv* p = o.via.map.ptr;
        msgpack_object_kv* p_end = p + o.via.map.size;
        for (; p < p_end; p++) {
            if (p->key.type != MSGPACK_OBJECT_STR) continue;
            string str(p->key.via.str.ptr, p->key.via.str.size);
            if (str == key){
                if (p->val.type == MSGPACK_OBJECT_STR) {
                    v->assign(p->val.via.str.ptr, p->val.via.str.size);
                    return true;
                }
                else {
                    return false;
                }
            }
        }

        return false;
    }

    static inline bool get_map_field_double(msgpack_object& o, const char* key, double* v)
    {
        if (!is_map(o)) return false;

        msgpack_object_kv* p = o.via.map.ptr;
        msgpack_object_kv* p_end = p + o.via.map.size;
        for (; p < p_end; p++) {
            if (p->key.type != MSGPACK_OBJECT_STR) continue;
            string str(p->key.via.str.ptr, p->key.via.str.size);
            if (str == key) {
                if (p->val.type == MSGPACK_OBJECT_FLOAT ||
                    p->val.type == MSGPACK_OBJECT_FLOAT32 ||
                    p->val.type == MSGPACK_OBJECT_FLOAT64 )
                {
                    *v = p->val.via.f64;
                    return true;
                }
                else if (p->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                    *v = (double)p->val.via.u64;
                    return true;
                }
                else if (p->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                    *v = (double)p->val.via.i64;
                    return true;
                }
                else {
                    return false;
                }
            }
        }

        return false;
    }

    static inline bool get_map_field_str_array(msgpack_object& o, const char* key, vector<string>* v)
    {
        if (!is_map(o)) return false;

        msgpack_object_kv* p = o.via.map.ptr;
        msgpack_object_kv* p_end = p + o.via.map.size;

        for (; p < p_end; p++) {
            if (p->key.type != MSGPACK_OBJECT_STR) continue;

            string str(p->key.via.str.ptr, p->key.via.str.size);
            if (str != key) continue;

            if (p->val.type == MSGPACK_OBJECT_ARRAY) {
                msgpack_object* tmp = p->val.via.array.ptr;
                for (uint32_t n = 0; n < p->val.via.array.size; n++, tmp++)
                {
                    if (tmp->type == MSGPACK_OBJECT_STR)
                        v->push_back(string(p->val.via.str.ptr, p->val.via.str.size));
                }
                return true;
            }
            else {
                return false;
            }
        }

        return false;
    }

    struct JsonRpcError{
        int    error;
        string message;
        string data;
        JsonRpcError(int aerr, const string& msg) :
            error(aerr),
            message(msg){}
    };

    struct JsonRpcMessage {
        string                      method;
        msgpack_object              params;
        msgpack_object              result;
        msgpack_zone                mp_zone;
        msgpack_object              root;

        int                         err_code;
        string                      err_msg;
        int                         id;
        system_clock::time_point    recv_time;

        string _recv_data;

        JsonRpcMessage()
            : id(0)
            , err_code(0)
        {
            msgpack_zone_init(&mp_zone, 2048);
            params.type = MSGPACK_OBJECT_NIL;
            result.type = MSGPACK_OBJECT_NIL;
        }

        ~JsonRpcMessage() {
            msgpack_zone_destroy(&mp_zone);
        }
    };

    class JsonRpcClient_Callback {
    public:
        virtual void on_connected    () {}
        virtual void on_disconnected () {}
        virtual void on_notification (shared_ptr<JsonRpcMessage> rpcmsg) {}
        virtual void on_call_result  (int callid, shared_ptr<JsonRpcMessage> cr) {}
    };

    class JsonRpcClient {

        struct ResultWaiter {
            condition_variable  cond;
            shared_ptr<JsonRpcMessage> result;
        };

    public:
        JsonRpcClient();

        virtual ~JsonRpcClient();

        bool connect(const std::string& addr, JsonRpcClient_Callback* callback);

        void close();

        shared_ptr<JsonRpcMessage> call(const char* method, const char* params, size_t params_size, int timeout = 6000);
        
        int asnyc_call(const char* method, const char* params, size_t params_size);

    private:
        void callback_run();
        void main_run();
        void do_send(const char* buf, size_t size);
        void do_connect();
        void do_recv();
        void do_send_heartbeat();

        void call_callback(function<void()>);

    private:
        string                      m_addr;
        zmq::context_t              m_zmq_ctx;
        zmq::socket_t*              m_push_sock;
        zmq::socket_t*              m_pull_sock;
        zmq::socket_t*              m_remote_sock;
        JsonRpcClient_Callback*     m_callback;
        thread*                     m_main_thread;
        thread*                     m_callback_thread;
        volatile bool               m_should_exit;
        bool                        m_connected;
        system_clock::time_point    m_last_hb_rsp_time;
        atomic_int                  m_cur_callid;
        mutex                       m_waiter_map_lock;
        map<int, ResultWaiter*>     m_waiter_map;
        list < function<void()>>    m_asyncall_queue;
        mutex                       m_asyncall_lock;
        condition_variable          m_asyncall_cond;
    };

    class Connection {
    public:
        virtual string id() = 0;
        virtual bool send(const char* data, size_t size) = 0;
    };

    class JsonRpcServer_Callback {
    public:
        virtual void on_call  (shared_ptr<Connection> connection, shared_ptr<JsonRpcMessage> req) = 0;
        virtual void on_close (shared_ptr<Connection> connection) = 0;
    };


    class JsonRpcServer {
    public:
        JsonRpcServer(JsonRpcServer_Callback* callback) : m_callback(callback) {}

        void on_recv(shared_ptr<Connection> connection, const char* data, size_t len);

        //bool notify(shared_ptr<Connection> connection, shared_ptr<JsonRpcMessage> msg);

        bool send(shared_ptr<Connection> connection, const void* data, size_t len);

        void on_close(shared_ptr<Connection> connection);

    private:
        JsonRpcServer_Callback* m_callback;
    };

    class ZmqJsonRpcServer;

    class ZmqJsonRpcServer_Connection;

    class ZmqJsonRpcServer {

        friend class ZmqJsonRpcServer_Connection;

    public:

        ZmqJsonRpcServer(JsonRpcServer* server);

        virtual ~ZmqJsonRpcServer();

        bool listen(const std::string& addr);

        void close();

    private:

        void main_run();
        //void do_send(const string& client, const char* data, size_t size);
        void do_send(const string& client, zmq::message_t& data);
        void do_connect();
        void do_recv();
        void do_send_heartbeat();
        void do_listen();

        bool send(const string& id, const char* data, size_t size);

    private:
        JsonRpcServer*      m_server;
        string              m_addr;
        mutex               m_send_lock;
        zmq::context_t      m_zmq_ctx;
        zmq::socket_t*      m_push_sock;
        zmq::socket_t*      m_pull_sock;
        zmq::socket_t*      m_remote_sock;
        thread*             m_main_thread;
        volatile bool       m_should_exit;
        bool                m_connected;
        map<string, shared_ptr<ZmqJsonRpcServer_Connection>> m_client_map;
    };

    class ZmqJsonRpcServer_Connection : public Connection {
        friend class ZmqJsonRpcServer;
    public:

        ZmqJsonRpcServer_Connection(const string& id, const string& addr, ZmqJsonRpcServer* server) :
            m_id(id),
            m_addr(addr),
            m_server(server)
        {}

        virtual string id() override { return m_id; }

        virtual bool send(const char* data, size_t size) override {
            return m_server->send(m_id, data, size);
        }

        void set_addr(const string& addr) {
            m_addr = addr;
        }

    private:
        string m_id;
        string m_addr;
        ZmqJsonRpcServer* m_server;
    };


}

#endif