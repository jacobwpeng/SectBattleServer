/*
 * =============================================================================
 *
 *       Filename:  tt_client.cc
 *        Created:  04/30/15 17:23:35
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "tt_client.h"
#include <arpa/inet.h>
#include <alpha/logger.h>
#include <alpha/event_loop.h>
#include <alpha/tcp_client.h>
#include <alpha/coroutine.h>
#include <alpha/net_address.h>
#include <alpha/format.h>

namespace tokyotyrant {
    Client::Client(alpha::EventLoop* loop)
        :loop_(loop), co_(nullptr), state_(ConnectionState::kDisconnected) {
        tcp_client_.reset(new alpha::TcpClient(loop_));
        using namespace std::placeholders;
        tcp_client_->SetOnConnected(std::bind(&Client::OnConnected, this, _1));
        tcp_client_->SetOnConnectError(std::bind(&Client::OnConnectError, this, _1));
        tcp_client_->SetOnClose(std::bind(&Client::OnDisconnected, this, _1));
    }

    Client::~Client() = default;

    void Client::SetCoroutine(alpha::Coroutine* co) {
        co_ = co;
    }

    int Client::Connnect(const alpha::NetAddress& addr) {
        if (addr_ != nullptr) {
            if (*addr_ == addr) {
                return kOk;
            } else {
                return kMiscellaneous;
            }
        } else {
            addr_.reset (new alpha::NetAddress(addr));
            tcp_client_->ConnectTo(addr, true);
            state_ = ConnectionState::kConnecting;
            co_->Yield();
            return Connected() ? kOk : kRefused;
        }
    }

    bool Client::Connected() const {
        return state_ == ConnectionState::kConnected;
    }

    int Client::Put(alpha::Slice key, alpha::Slice value) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC810;
        auto codec = CreateCodec(kMagic);
        KeyValuePairEncodeUnit unit(key, value);
        codec->AddEncodeUnit(&unit);

        return Request(codec.get());
    }

    int Client::PutKeep(alpha::Slice key, alpha::Slice value) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC811;
        auto codec = CreateCodec(kMagic);
        KeyValuePairEncodeUnit unit(key, value);
        codec->AddEncodeUnit(&unit);

        int err = Request(codec.get());
        if (err == 1) {
            return kExisting;
        } else {
            return err;
        }
    }

    int Client::PutCat(alpha::Slice key, alpha::Slice value) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC812;
        auto codec = CreateCodec(kMagic);
        KeyValuePairEncodeUnit unit(key, value);
        codec->AddEncodeUnit(&unit);

        return Request(codec.get());
    }

    int Client::PutNR(alpha::Slice key, alpha::Slice value) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC818;
        auto codec = CreateCodec(kMagic);
        KeyValuePairEncodeUnit unit(key, value);
        codec->AddEncodeUnit(&unit);

        codec->SetNoReply();
        return Request(codec.get());
    }

    int Client::Out(alpha::Slice key) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC820;
        auto codec = CreateCodec(kMagic);
        LengthPrefixedEncodeUnit unit(key);
        codec->AddEncodeUnit(&unit);

        return Request(codec.get());
    }

    int Client::Vanish() {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC872;
        auto codec = CreateCodec(kMagic);
        return Request(codec.get());
    }

    int Client::Get(alpha::Slice key, std::string* val) {
        assert (val);
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC830;
        auto codec = CreateCodec(kMagic);
        LengthPrefixedEncodeUnit unit(key);
        LengthPrefixedDecodeUnit res(val);
        codec->AddEncodeUnit(&unit);
        codec->AddDecodeUnit(&res);

        int err = Request(codec.get());
        if (err == 1) {
            return kNoRecord;
        } else {
            return err;
        }
    }

    int Client::Stat(std::string* stat) {
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC888;
        auto codec = CreateCodec(kMagic);
        LengthPrefixedDecodeUnit unit(stat);
        codec->AddDecodeUnit(&unit);
        return Request(codec.get());
    }

    int Client::ValueSize(alpha::Slice key, int32_t* size) {
        assert (size);
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC838;
        auto codec = CreateCodec(kMagic);
        LengthPrefixedEncodeUnit unit(key);
        codec->AddEncodeUnit(&unit);
        Int32DecodeUnit res(size);
        codec->AddDecodeUnit(&res);

        return Request(codec.get());
    }

    int Client::RecordNumber(int64_t* rnum) {
        assert (rnum);
        if (ConnectionError()) {
            return kInvalidOperation;
        }
        const int16_t kMagic = 0xC880;
        auto codec = CreateCodec(kMagic);
        Int64DecodeUnit unit(rnum);
        codec->AddDecodeUnit(&unit);

        return Request(codec.get());
    }

    std::unique_ptr<Iterator> Client::NewIterator() {
        if (ConnectionError()) {
            return nullptr;
        }
        const int16_t kMagic = 0xC850;
        auto codec = CreateCodec(kMagic);

        int err = Request(codec.get());
        if (err) {
            return nullptr;
        } else {
            auto it = std::unique_ptr<Iterator>(new Iterator(this));
            it->Next();
            return std::move(it);
        }
    }

    void Client::OnConnectError(const alpha::NetAddress& addr) {
        assert (addr_ && *addr_ == addr);
        ResetConnection();
        LOG_WARNING << "Connect to " << addr << " failed";
        co_->Resume();
    }

    void Client::OnConnected(alpha::TcpConnectionPtr conn) {
        DLOG_INFO << "Connected to " << conn->PeerAddr();
        using namespace std::placeholders;
        conn_ = conn;
        conn_->SetOnRead(std::bind(&Client::OnMessage, this, _1, _2));
        conn_->SetOnWriteDone(std::bind(&Client::OnWriteDone, this, _1));
        state_ = ConnectionState::kConnected;
        expired_ = false; //干掉超时造成的重连标志
        co_->Resume();
    }

    void Client::OnDisconnected(alpha::TcpConnectionPtr conn) {
        assert (conn_ == conn);
        (void)conn;
        ResetConnection();
        LOG_WARNING << "Connection to Remote server closed, addr = " << *addr_;
        co_->Resume();
    }

    void Client::OnMessage(alpha::TcpConnectionPtr conn, 
            alpha::TcpConnectionBuffer*) {
        assert (conn == conn_);
        (void)conn;
        co_->Resume();
    }

    void Client::OnWriteDone(alpha::TcpConnectionPtr conn) {
        assert (conn == conn_);
        (void)conn;
        co_->Resume();
    }

    void Client::OnTimeout() {
        expired_ = true;
        conn_->Close();
        conn_.reset();
    }

    void Client::ResetConnection() {
        if (conn_ && !conn_->closed()) {
            conn_->Close();
        }
        conn_.reset();
        state_ = ConnectionState::kDisconnected;
    }

    bool Client::ConnectionError() const {
        return conn_ == nullptr || conn_->closed();
    }

    void Client::Next(Iterator* it) {
        assert (it);
        const int16_t kMagic = 0xC851;
        auto codec = CreateCodec(kMagic);
        it->key_.clear();
        LengthPrefixedDecodeUnit unit(&it->key_);
        codec->AddDecodeUnit(&unit);

        int err = Request(codec.get());
        if (err) {
            it->status_ = err;
        } else {
            it->status_ = kOk;
        }
    }

    std::unique_ptr<ProtocolCodec> Client::CreateCodec(int magic) {
        using namespace std::placeholders;
        return std::unique_ptr<ProtocolCodec>(new ProtocolCodec(magic
                    , std::bind(&Client::Write, this, _1, _2)
                    , std::bind(&Client::MaxBytesCanWrite, this)
                    , std::bind(&Client::Read, this)));
    }

    int Client::Request(ProtocolCodec* codec) {
        const auto magic = codec->magic();
        DLOG_INFO << "codec->magic() = " << magic;
        while (!codec->Encode()) {
            if (unlikely(ConnectionError())) {
                return kSendError;
            } else {
                co_->Yield();
                if (unlikely(expired_)) {
                    return kTimeout;
                } else if (unlikely(ConnectionError())) {
                    return kSendError;
                }
            }
        }
        DLOG_INFO << "Encode done";

        if (codec->NoReply()) {
            return kOk;
        }

        auto status = kNeedsMore;
        while (status == kNeedsMore) {
            int consumed = 0;
            status = codec->Decode(&consumed);
            conn_->ReadBuffer()->ConsumeBytes(consumed);
            if (status == kErrorFromServer) {
                return codec->err();
            } else if (status != kOk && status != kNeedsMore) {
                LOG_WARNING << "kMiscellaneous, status = " << status;
                return kMiscellaneous;
            } else {
                DLOG_INFO << "Decode consume " << consumed << " bytes";
                if (status == kOk) {
                    return kOk;
                } else {
                    co_->Yield();
                    if (unlikely(expired_)) {
                        return kTimeout;
                    } else if (unlikely(ConnectionError())) {
                        return kRecvError;
                    }
                }
            }
        }

        return kOk;
    }

    size_t Client::MaxBytesCanWrite() {
        return conn_->BytesCanBytes();
    }

    bool Client::Write(const uint8_t* buffer, int size) {
        return conn_->Write(alpha::Slice(reinterpret_cast<const char*>(buffer), size));
    }
    
    alpha::Slice Client::Read() {
        return conn_->ReadBuffer()->Read();
    }

    Iterator::Iterator(Client* client)
        :client_(client) {
    }

    std::string Iterator::key() const {
        return key_;
    }

    int Iterator::status() const {
        return status_;
    }

    void Iterator::Next() {
        client_->Next(this);
    }
}
