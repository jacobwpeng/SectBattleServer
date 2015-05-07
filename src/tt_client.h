/*
 * =============================================================================
 *
 *       Filename:  tt_client.h
 *        Created:  04/30/15 17:22:20
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __TT_CLIENT_H__
#define  __TT_CLIENT_H__

#include <alpha/slice.h>
#include <alpha/tcp_connection.h>
#include "tt_protocol_codec.h"

namespace alpha {
    class NetAddress;
    class Coroutine;
    class EventLoop;
    class TcpClient;
}

namespace tokyotyrant {
    enum Code {
        kSuccess = 0,
        kInvalidOperation = 1,
        kNoHost = 2,
        kRefused = 3,
        kSendError = 4,
        kRecvError = 5,
        kExisting = 6,
        kNoRecord = 7,
        kTimeout = 8,
        kMiscellaneous = 9999
    };

    class Iterator;
    class Client {
        public:
            using MatchKeysCallback = std::function<void(alpha::Slice)>;
            Client(alpha::EventLoop* loop);
            ~Client();
            void SetCoroutine(alpha::Coroutine* co);
            int Connnect(const alpha::NetAddress& addr);
            bool Connected() const;
            int Put(alpha::Slice key, alpha::Slice value);
            int PutKeep(alpha::Slice key, alpha::Slice value);
            int PutCat(alpha::Slice key, alpha::Slice value);
            int PutNR(alpha::Slice key, alpha::Slice value);
            int Out(alpha::Slice key);
            int Vanish();
            int Get(alpha::Slice key, std::string* val);
            template<typename InputIterator, typename MapType>
            int MultiGet(InputIterator first, InputIterator last, MapType* map) {
                if (state_ == ConnectionState::kDisconnected) {
                    return kInvalidOperation;
                }
                const int16_t kMagic = 0xC831;
                auto codec = CreateCodec(kMagic);
                int32_t rnum;
                RepeatedLengthPrefixedEncodeUnit<InputIterator> unit(first, last);
                Int32DecodeUnit size_decode_unit(&rnum);
                RepeatedKeyValuePairDecodeUnit<MapType> 
                    repeated_key_value_pair_decode_unit(&rnum, map);
                codec->AddEncodeUnit(&unit);
                codec->AddDecodeUnit(&size_decode_unit);
                codec->AddDecodeUnit(&repeated_key_value_pair_decode_unit);

                return Request(codec.get());
            }
            int Stat(std::string* stat);
            int ValueSize(alpha::Slice key, int32_t* size);
            int RecordNumber(int64_t* rnum);
            std::unique_ptr<Iterator> NewIterator();
            template<typename OutputIterator>
            int GetForwardMatchKeys(alpha::Slice prefix, int32_t max, OutputIterator out) {
                if (state_ == ConnectionState::kDisconnected) {
                    return kInvalidOperation;
                }
                const int16_t kMagic = 0xC858;
                auto codec = CreateCodec(kMagic);
                IntegerEncodeUnit<int32_t> psize_encode_unit(prefix.size());
                IntegerEncodeUnit<int32_t> max_encode_unit(max);
                RawDataEncodedUnit prefix_encode_unit(prefix);
                int32_t knum;
                Int32DecodeUnit knum_decode_unit(&knum);
                RepeatedLengthPrefixedDecodeUnit<OutputIterator> 
                    keys_decode_unit(&knum, out);
                codec->AddEncodeUnit(&psize_encode_unit);
                codec->AddEncodeUnit(&max_encode_unit);
                codec->AddEncodeUnit(&prefix_encode_unit);
                codec->AddDecodeUnit(&knum_decode_unit);
                codec->AddDecodeUnit(&keys_decode_unit);

                return Request(codec.get());
            }

        private:
            enum class ConnectionState {
                kConnected = 1,
                kConnecting = 2,
                kDisconnected = 3
            };
            void OnConnectError(const alpha::NetAddress& addr);
            void OnConnected(alpha::TcpConnectionPtr conn);
            void OnDisconnected(alpha::TcpConnectionPtr conn);
            void OnMessage(alpha::TcpConnectionPtr conn, alpha::TcpConnectionBuffer* buffer);
            void OnWriteDone(alpha::TcpConnectionPtr conn);
            void OnTimeout();
            void ResetConnection();
            bool ConnectionError() const;

            void Next(Iterator* it);
            std::unique_ptr<ProtocolCodec> CreateCodec(int magic);
            int Request(ProtocolCodec* codec);
            size_t MaxBytesCanWrite();
            bool Write(const uint8_t* buffer, int size);
            alpha::Slice Read();

            friend class Iterator;
            bool expired_ = false;
            alpha::EventLoop* loop_;
            alpha::Coroutine* co_;
            //顺序很重要
            std::unique_ptr<alpha::TcpClient> tcp_client_;
            alpha::TcpConnectionPtr conn_;
            ConnectionState state_;
            std::unique_ptr<alpha::NetAddress> addr_;
    };

    class Iterator {
        public:
            void Next();
            std::string key() const;
            int status() const;

        private:
            Iterator(Client* client);
            friend class Client;
            Client* client_;
            int status_ = kSuccess;
            std::string key_;
    };
}

#endif   /* ----- #ifndef __TT_CLIENT_H__  ----- */
