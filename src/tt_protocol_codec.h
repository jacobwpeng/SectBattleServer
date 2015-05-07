/*
 * =============================================================================
 *
 *       Filename:  tt_protocol_codec.h
 *        Created:  04/30/15 16:36:38
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __TT_PROTOCOL_CODEC_H__
#define  __TT_PROTOCOL_CODEC_H__

#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <string>
#include <type_traits>
#include <functional>
#include <alpha/logger.h>
#include <alpha/format.h>
#include "tt_coded_stream.h"

namespace tokyotyrant {
    enum CodecStatus {
        kOk = 0,
        kNeedsMore = 100,
        kNotConsumed = 101,
        kFullBuffer = 102,
        kNoData = 103,
        kErrorFromServer = 104
    };

    class ProtocolDecodeUnit {
        public:
            virtual ~ProtocolDecodeUnit() = default;
            virtual CodecStatus Decode(const uint8_t* buffer, int size, int* consumed) = 0;
    };

    class ProtocolEncodeUnit {
        public:
            virtual ~ProtocolEncodeUnit() = default;
            virtual CodecStatus Encode(CodedOutputStream* stream) = 0;
    };

    class Int32DecodeUnit final : public ProtocolDecodeUnit {
        public:
            Int32DecodeUnit(int32_t* val);
            virtual CodecStatus Decode(const uint8_t* buffer, int size, 
                    int* consumed) override;
        private:
            int32_t* val_;
    };

    class Int64DecodeUnit final : public ProtocolDecodeUnit {
        public:
            Int64DecodeUnit(int64_t* val);
            virtual CodecStatus Decode(const uint8_t* buffer, int size, 
                    int* consumed) override;
        private:
            int64_t* val_;
    };

    class LengthPrefixedDecodeUnit final : public ProtocolDecodeUnit {
        public:
            LengthPrefixedDecodeUnit(std::string* val);
            virtual CodecStatus Decode(const uint8_t* buffer, int size, 
                    int* consumed) override;
        private:
            int32_t vsize_ = -1;
            std::string* val_;
    };

    template<typename OutputIterator>
    class RepeatedLengthPrefixedDecodeUnit final : public ProtocolDecodeUnit {
        public:
            RepeatedLengthPrefixedDecodeUnit(int32_t* knum, OutputIterator it)
                :knum_(knum), it_(it) {
            }
            virtual CodecStatus Decode(const uint8_t* buffer, int size, 
                    int* consumed) override {
                *consumed = 0;
                if (*knum_ != 0 && unit_ == nullptr) {
                    val_.reset(new std::string());
                    unit_.reset(new LengthPrefixedDecodeUnit(val_.get()));
                }

                while (*knum_ != 0) {
                    int nbytes = 0;
                    auto status = unit_->Decode(buffer, size, &nbytes);
                    *consumed += nbytes;
                    buffer += nbytes;
                    size -= nbytes;

                    if (status != kOk && status != kNeedsMore) {
                        return status;
                    } else if (status == kOk) {
                        *it_ = *val_;
                        ++it_;
                        val_->clear();
                        unit_.reset(new LengthPrefixedDecodeUnit(val_.get()));
                        --*knum_;
                    } else {
                        return kNeedsMore;
                    }
                }

                return kOk;
            }

        private:
            int32_t* knum_;
            std::unique_ptr<std::string> val_;
            std::unique_ptr<LengthPrefixedDecodeUnit> unit_;
            OutputIterator it_;
    };

    class KeyValuePairDecodeUnit final : public ProtocolDecodeUnit {
        public:
            KeyValuePairDecodeUnit(std::string* key, std::string* val);
            CodecStatus Decode(const uint8_t* buffer, int size, int* consumed);

        private:
            int ksize_ = -1;
            int vsize_ = -1;
            std::string* key_;
            std::string* val_;
    };

    template<typename MapType>
    class RepeatedKeyValuePairDecodeUnit final : public ProtocolDecodeUnit {
        public:
            RepeatedKeyValuePairDecodeUnit(int* num, MapType* map)
                :num_(num), map_(map) {
            }

            virtual CodecStatus Decode(const uint8_t* buffer, int size, int* consumed) 
                override {
                    *consumed = 0;
                    if (*num_ != 0 && single_unit_ == nullptr) {
                        key_.reset (new std::string);
                        val_.reset (new std::string);
                        single_unit_.reset (new KeyValuePairDecodeUnit(
                                    key_.get(), val_.get()));
                    }

                    while (*num_ != 0) {
                        int nbytes = 0;
                        auto status = single_unit_->Decode(buffer, size, &nbytes);
                        *consumed += nbytes;
                        buffer += nbytes;
                        size -= nbytes;
                        if (status != kOk && status != kNeedsMore) {
                            return status;
                        } else if (status == kOk) {
                            map_->emplace(*key_, *val_);
                            key_->clear();
                            val_->clear();
                            single_unit_.reset (new KeyValuePairDecodeUnit(
                                        key_.get(), val_.get()));
                            --*num_;
                        } else {
                            return status;
                        }
                    }
                    return kOk;
            }

        private:
            int* num_;
            MapType* map_;
            std::unique_ptr<std::string> key_;
            std::unique_ptr<std::string> val_;
            std::unique_ptr<KeyValuePairDecodeUnit> single_unit_;
    };

    template<typename IntegerType>
    class IntegerEncodeUnit final : public ProtocolEncodeUnit {
        public:
            IntegerEncodeUnit(typename std::enable_if<
                    std::is_same<IntegerType, int32_t>::value
                    || std::is_same<IntegerType, int64_t>::value, IntegerType>::type val)
                :val_(val) {
            }

            virtual CodecStatus Encode(CodedOutputStream* stream) {
                if (std::is_same<IntegerType, int32_t>::value) {
                    return stream->WriteBigEndianInt32(val_) ? kOk : kFullBuffer;
                } else {
                   return stream->WriteBigEndianInt64(val_) ? kOk : kFullBuffer;
                }
            }

        private:
            IntegerType val_;
    };

    class RawDataEncodedUnit final : public ProtocolEncodeUnit {
        public:
            RawDataEncodedUnit(alpha::Slice data);
            virtual CodecStatus Encode(CodedOutputStream* stream);

        private:
            alpha::Slice data_;
    };

    //所有传入*EncodeUnit的参数必须保证在对应Encode调用前保持有效
    class KeyValuePairEncodeUnit final : public ProtocolEncodeUnit {
        public:
            KeyValuePairEncodeUnit(alpha::Slice key, alpha::Slice val);
            virtual CodecStatus Encode(CodedOutputStream* stream);

        private:
            bool done_key_size_ = false;
            bool done_val_size_ = false;
            alpha::Slice key_;
            alpha::Slice val_;
    };

    class LengthPrefixedEncodeUnit final : public ProtocolEncodeUnit {
        public:
            LengthPrefixedEncodeUnit(alpha::Slice val);
            virtual CodecStatus Encode(CodedOutputStream* stream);

        private:
            void Reset(alpha::Slice val);
            template<typename InputIterator>
            friend class RepeatedLengthPrefixedEncodeUnit;
            bool done_size_ = false;
            alpha::Slice val_;
    };

    template<typename InputIterator>
    class RepeatedLengthPrefixedEncodeUnit final : public ProtocolEncodeUnit  {
        public:
            RepeatedLengthPrefixedEncodeUnit(InputIterator begin, InputIterator end)
                :encode_size_done_(false), it_(begin), end_(end){
                size_ = std::distance(begin, end);
            }

            virtual CodecStatus Encode(CodedOutputStream* stream) {
                if (size_ == 0) {
                    return kNoData;
                }
                if (!encode_size_done_ && !stream->WriteBigEndianInt32(size_)) {
                    return kFullBuffer;
                }
                if (single_unit_ == nullptr) {
                    single_unit_.reset (new LengthPrefixedEncodeUnit(*it_));
                }
                while (it_ != end_) {
                    auto status = single_unit_->Encode(stream);
                    if (status == kOk) {
                        ++it_;
                        if (it_ != end_) {
                            single_unit_->Reset(*it_);
                        }
                    } else {
                        return status;
                    }
                }
                return kOk;
            }

        private:
            bool encode_size_done_;
            std::unique_ptr<LengthPrefixedEncodeUnit> single_unit_;
            InputIterator it_;
            InputIterator end_;
            int size_ = 0;
    };

    class ProtocolCodec {
        public:
            using WriteFunctor = std::function<bool (const uint8_t*, int size)>;
            using LeftSpaceFunctor = std::function<size_t ()>;
            using ReadFunctor = std::function<alpha::Slice ()>;
            ProtocolCodec(int16_t magic, WriteFunctor, LeftSpaceFunctor, ReadFunctor);
            void AddDecodeUnit(ProtocolDecodeUnit* unit);
            void AddEncodeUnit(ProtocolEncodeUnit* unit);
            CodecStatus Decode(int* consumed);
            int ConsumedBytes() const;
            void ClearConsumed();
            int16_t magic() const;

            bool Encode();
            size_t MaxBytesCanWrite() const;
            bool Write(const uint8_t* buffer, int size);
            void SetNoReply();
            bool NoReply() const;
            int8_t err() const;

        private:
            int8_t err_;
            int16_t magic_;
            bool encoded_magic_ = false;
            bool parsed_code_ = false;
            bool no_reply_ = false;
            int consumed_ = 0;
            size_t current_decode_unit_index_ = 0;
            size_t current_encode_unit_index_ = 0;
            WriteFunctor w_;
            LeftSpaceFunctor l_;
            ReadFunctor r_;
            std::vector<ProtocolDecodeUnit*> decode_units_;
            std::vector<ProtocolEncodeUnit*> encode_units_;
    };
}

#endif   /* ----- #ifndef __TT_PROTOCOL_CODEC_H__  ----- */
