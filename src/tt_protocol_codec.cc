/*
 * =============================================================================
 *
 *       Filename:  tt_protocol_codec.cc
 *        Created:  04/30/15 16:49:12
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "tt_protocol_codec.h"
#include <cassert>
#include <alpha/compiler.h>
#include <alpha/logger.h>
#include <alpha/format.h>

#include "tt_coded_stream.h"

namespace tokyotyrant {
    Int32DecodeUnit::Int32DecodeUnit(int32_t* val)
        :val_(val) {
    }

    CodecStatus Int32DecodeUnit::Decode(const uint8_t* buffer, int size, int* consumed) {
        *consumed = 0;
        if (static_cast<size_t>(size) < sizeof(int32_t)) {
            return kNeedsMore;
        }

        CodedInputStream stream(buffer, size);
        bool ok = stream.ReadBigEndianInt32(val_);
        *consumed = sizeof(int32_t);
        assert (ok);
        (void)ok;
        return kOk;
    }

    Int64DecodeUnit::Int64DecodeUnit(int64_t* val)
        :val_(val) {
    }

    CodecStatus Int64DecodeUnit::Decode(const uint8_t* buffer, int size, int* consumed) {
        *consumed = 0;
        if (static_cast<size_t>(size) < sizeof(int64_t)) {
            return kNeedsMore;
        }

        CodedInputStream stream(buffer, size);
        bool ok = stream.ReadBigEndianInt64(val_);
        *consumed = sizeof(int64_t);
        assert (ok);
        (void) ok;
        return kOk;
    }

    LengthPrefixedDecodeUnit::LengthPrefixedDecodeUnit(std::string* val)
        :val_(val) {
        val_->clear();
    }

    CodecStatus LengthPrefixedDecodeUnit::Decode(const uint8_t* buffer, int size
            , int* consumed) {
        *consumed = 0;
        CodedInputStream stream(buffer, size);
        if (vsize_ == -1 && !stream.ReadBigEndianInt32(&vsize_)) {
            return kNeedsMore;
        }
        DLOG_INFO << "Expected size = " << vsize_
            << ", val_->size() = " << val_->size();
        assert (vsize_ >= static_cast<int32_t>(val_->size()));
        bool ok = stream.ReadPartialString(val_, vsize_ - val_->size());
        *consumed += stream.pos();
        //LOG_INFO_IF(ok) << "val_ = " << *val_;
        return ok ? kOk : kNeedsMore;
    }

    KeyValuePairDecodeUnit::KeyValuePairDecodeUnit(std::string* key, std::string* val)
        :key_(key), val_(val) {
    }

    CodecStatus KeyValuePairDecodeUnit::Decode(const uint8_t* buffer, int size,
            int* consumed) {
        *consumed = 0;
        CodedInputStream stream(buffer, size);
        if (ksize_ == -1 && !stream.ReadBigEndianInt32(&ksize_)) {
            return kNeedsMore;
        }
        DLOG_INFO << "ksize_ = " << ksize_;
        *consumed = stream.pos();
        if (vsize_ == -1 && !stream.ReadBigEndianInt32(&vsize_)) {
            return kNeedsMore;
        }
        DLOG_INFO << "vsize_ = " << vsize_;
        *consumed = stream.pos();
        int ksize_now = static_cast<int>(key_->size());
        int vsize_now = static_cast<int>(val_->size());
        assert (ksize_ >= ksize_now);
        if (ksize_ > ksize_now) {
            bool ok = stream.ReadPartialString(key_, ksize_ - ksize_now);
            *consumed = stream.pos();
            if (!ok) {
                return kNeedsMore;
            }
            assert (ksize_ == static_cast<int>(key_->size()));
        }
        *consumed = stream.pos();

        assert (vsize_ >= vsize_now);
        if (vsize_ > vsize_now) {
            bool ok = stream.ReadPartialString(val_, vsize_ - vsize_now);
            *consumed = stream.pos();
            if (!ok) {
                return kNeedsMore;
            }
            assert (vsize_ == static_cast<int>(val_->size()));
        }
        *consumed = stream.pos();
        DLOG_INFO << "key.size() = " << key_->size()
            << ", val.size() = " << val_->size();
        return kOk;
    }

    RawDataEncodedUnit::RawDataEncodedUnit(alpha::Slice data)
        :data_(data) {
        assert (!data_.empty());
    }

    CodecStatus RawDataEncodedUnit::Encode(CodedOutputStream* stream) {
        if (!data_.empty()) {
            auto size = data_.size();
            auto nbytes = stream->WriteRaw(data_);
            data_ = data_.RemovePrefix(nbytes);
            if (nbytes != size) {
                return kFullBuffer;
            } else {
                assert (data_.empty());
                return kOk;
            }
        }
        return kOk;
    }

    KeyValuePairEncodeUnit::KeyValuePairEncodeUnit(alpha::Slice key, alpha::Slice val)
        :key_(key), val_(val) {
    }

    CodecStatus KeyValuePairEncodeUnit::Encode(CodedOutputStream* stream) {
        if (!done_key_size_ && !stream->WriteBigEndianInt32(key_.size())) {
            return kFullBuffer;
        }
        done_key_size_ = true;
        if (!done_val_size_ && !stream->WriteBigEndianInt32(val_.size())) {
            return kFullBuffer;
        }
        done_val_size_ = true;

        if (!key_.empty()) {
            auto nbytes = stream->WriteRaw(key_);
            if (nbytes != key_.size()) {
                key_ = key_.RemovePrefix(nbytes);
                return kFullBuffer;
            } else {
                key_ = key_.RemovePrefix(nbytes);
                assert (key_.empty());
            }
        }

        if (!val_.empty()) {
            auto nbytes = stream->WriteRaw(val_);
            if (nbytes != val_.size()) {
                val_ = val_.RemovePrefix(nbytes);
                return kFullBuffer;
            } else {
                val_ = val_.RemovePrefix(nbytes);
                assert (val_.empty());
            }
        }
        return kOk;
    }

    LengthPrefixedEncodeUnit::LengthPrefixedEncodeUnit(alpha::Slice val)
        :val_(val) {
    }

    CodecStatus LengthPrefixedEncodeUnit::Encode(CodedOutputStream* stream) {
        if (!done_size_ && !stream->WriteBigEndianInt32(val_.size())) {
            return kFullBuffer;
        }
        done_size_ = true;
        if (!val_.empty()) {
            auto nbytes = stream->WriteRaw(val_);
            if (nbytes != val_.size()) {
                val_ = val_.RemovePrefix(nbytes);
                return kFullBuffer;
            }
        }
        return kOk;
    }

    void LengthPrefixedEncodeUnit::Reset(alpha::Slice val) {
        val_ = val;
        done_size_ = false;
    }

    ProtocolCodec::ProtocolCodec(int16_t magic, WriteFunctor w
            , LeftSpaceFunctor l, ReadFunctor r)
        :magic_(magic), w_(w), l_(l), r_(r) {
    }

    void ProtocolCodec::AddDecodeUnit(ProtocolDecodeUnit* unit) {
        decode_units_.push_back(unit);
    }

    void ProtocolCodec::AddEncodeUnit(ProtocolEncodeUnit* unit) {
        encode_units_.push_back(unit);
    }
#if 0
    CodecStatus ProtocolCodec::Decode(const uint8_t* buffer, int size) {
        //DLOG_INFO << "size = " << size;
        int pos = 0;
        if (!parsed_code_) {
            //LOG_INFO << "try parse code";
            CodedInputStream stream(buffer, size);
            int8_t err;
            if (!stream.ReadInt8(&err)) {
                return CodecStatus::kNeedsMore;
            } else {
                ++consumed_;
                //DLOG_INFO << "Decode err done, err = " << static_cast<int>(err);
                parsed_code_ = true;
                if(err) {
                    LOG_INFO << "err";
                    return kErrorFromServer;
                } else {
                    pos += sizeof(int8_t);
                }
            }
        }

        if (!decode_units_.empty() && current_decode_unit_index_ < decode_units_.size()) {
            for (size_t idx = current_decode_unit_index_; 
                    idx < decode_units_.size(); 
                    ++idx) {
                int consumed = 0;
                auto unit = decode_units_[idx];
                auto status = unit->Decode(buffer + pos,
                        size - pos, &consumed);
                switch(status) {
                    case CodecStatus::kOk:
                        pos += consumed;
                        consumed_ += consumed;
                        ++current_decode_unit_index_;
                        break;
                    case CodecStatus::kNeedsMore:
                        pos += consumed;
                        consumed_ += consumed;
                    default:
                        return status;
                }
                DLOG_INFO << "Decode unit " << idx << " done"
                    << ", consumed_ = " << consumed_;;
            }
        }
        DLOG_INFO << "size = " << size << ", pos = " << pos;
        return size == pos ? kOk : kNotConsumed;
    }
#endif

    CodecStatus ProtocolCodec::Decode(int* consumed) {
        auto data = r_();
        const uint8_t* const original_buffer = 
            reinterpret_cast<const uint8_t*>(data.data());
        const uint8_t* buffer = original_buffer;
        const int original_size = static_cast<int>(data.size());
        int size = original_size;
        if (!parsed_code_) {
            CodedInputStream stream(buffer, size);
            if (!stream.ReadInt8(&err_)) {
                return kNeedsMore;
            } else {
                parsed_code_ = true;
                assert (stream.pos() == 1);
                *consumed = 1;
                if (err_) {
                    return CodecStatus::kErrorFromServer;
                } else {
                    buffer += 1;
                    size -= 1;
                }
            }
        }

        if (!decode_units_.empty() && current_decode_unit_index_ < decode_units_.size()) {
            for (size_t idx = current_decode_unit_index_; 
                    idx < decode_units_.size(); 
                    ++idx) {
                int nbytes = 0;
                auto unit = decode_units_[idx];
                auto status = unit->Decode(buffer, size, &nbytes);
                if (status != kOk && status != kNeedsMore) {
                    return status;
                } else {
                    buffer += nbytes;
                    size -= nbytes;
                    if (status == kOk) {
                        ++current_decode_unit_index_;
                    } else {
                        *consumed = buffer - original_buffer;
                        return kNeedsMore;
                    }
                }
                DLOG_INFO << "Decode unit " << idx << " done"
                    << ", consumed_ = " << consumed_;;
            }
        }

        *consumed = buffer - original_buffer;
        return *consumed == original_size ? kOk : kNotConsumed;
    }

    int ProtocolCodec::ConsumedBytes() const {
        return consumed_;
    }

    void ProtocolCodec::ClearConsumed() {
        consumed_ = 0;
    }

    int16_t ProtocolCodec::magic() const {
        return magic_;
    }

    bool ProtocolCodec::Encode() {
        CodedOutputStream stream(this);
        if (!encoded_magic_) {
            if (!stream.WriteBigEndianInt16(magic_)) {
                return false;
            }
            DLOG_INFO << "Write magic done";
            encoded_magic_ = true;
        }

        for (auto idx = current_encode_unit_index_; idx < encode_units_.size(); ++idx) {
            if (kFullBuffer == encode_units_[idx]->Encode(&stream)) {
                //DLOG_INFO << "Unit encode returns kFullBuffer, idx = " << idx;
                return false;
            }
            DLOG_INFO << "Encode unit " << idx << " done";
            ++current_encode_unit_index_;
        }
        return true;
    }

    size_t ProtocolCodec::MaxBytesCanWrite() const {
        return l_();
    }

    bool ProtocolCodec::Write(const uint8_t* buffer, int size) {
        //LOG_INFO << "size = " << size << ", buffer = \n"
        //    << alpha::HexDump(alpha::Slice((const char*)(buffer), size));
        return w_(buffer, size);
    }

    void ProtocolCodec::SetNoReply() {
        no_reply_ = true;
    }

    bool ProtocolCodec::NoReply() const {
        return no_reply_;
    }

    int8_t ProtocolCodec::err() const {
        return err_;
    }
}
