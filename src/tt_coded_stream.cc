/*
 * =============================================================================
 *
 *       Filename:  tt_coded_stream.cc
 *        Created:  04/30/15 16:46:42
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "tt_coded_stream.h"
#include <arpa/inet.h>
#include <cassert>
#include <alpha/compiler.h>
#include "tt_protocol_codec.h"

namespace tokyotyrant {
    CodedInputStream::CodedInputStream(const uint8_t* buffer, int size)
        :original_buf_(buffer), buf_(buffer), size_(size) {
    }

    bool CodedInputStream::Skip(int count) {
        if (Overflow(count)) {
            return false;
        }
        Advance(count);
        return true;
    }

    bool CodedInputStream::ReadString(std::string* buffer, int size) {
        if (Overflow(size)) {
            return false;
        }
        buffer->assign(reinterpret_cast<const char*>(buf_), size);
        Advance(size);
        return true;
    }

    bool CodedInputStream::ReadPartialString(std::string* buffer, int vsize) {
        const int expected_size = vsize;
        if (Overflow(vsize)) {
            vsize = original_buf_ + size_ - buf_;
        }
        assert (!Overflow(vsize));
        buffer->append(reinterpret_cast<const char*>(buf_), vsize);
        Advance(vsize);
        return expected_size == vsize;
    }

    bool CodedInputStream::ReadInt8(int8_t* val) {
        if (Overflow(sizeof(int8_t))) {
            return false;
        }
        *val = *reinterpret_cast<const int8_t*>(buf_);
        Advance(sizeof(int8_t));
        return true;
    }

    bool CodedInputStream::ReadBigEndianInt32(int32_t* val) {
        if (Overflow(sizeof(int32_t))) {
            return false;
        }

        *val = *reinterpret_cast<const int32_t*>(buf_);
        Advance(sizeof(int32_t));
        *val = ::ntohl(*val);
        return true;
    }

    bool CodedInputStream::ReadBigEndianInt64(int64_t* val) {
        if (Overflow(sizeof(int64_t))) {
            return false;
        }

        *val = *reinterpret_cast<const int64_t*>(buf_);
        Advance(sizeof(int64_t));
        *val = (((uint64_t) ntohl(*val)) << 32) + ntohl(*val >> 32);
        return true;
    }

    int CodedInputStream::pos() const {
        return buf_ - original_buf_;
    }

    bool CodedInputStream::Overflow(int amount) {
        if (original_buf_ + size_ < buf_ + amount) {
            return true;
        }
        return false;
    }

    void CodedInputStream::Advance(int amount) {
        assert (!Overflow(amount));
        buf_ += amount;
    }

    CodedOutputStream::CodedOutputStream(ProtocolCodec* codec)
        :codec_(codec) {
    }

    bool CodedOutputStream::WriteBigEndianInt16(int16_t val) {
        auto nbytes = codec_->MaxBytesCanWrite();
        if (nbytes < sizeof(int16_t)) {
            return false;
        }
        auto real_val = htons(val);
        bool ok = codec_->Write(reinterpret_cast<const uint8_t*>(&real_val), 
                sizeof(real_val));
        assert (ok);
        return ok;
    }

    bool CodedOutputStream::WriteBigEndianInt32(int32_t val) {
        auto nbytes = codec_->MaxBytesCanWrite();
        if (nbytes < sizeof(int32_t)) {
            return false;
        }
        auto real_val = ::htonl(val);
        bool ok = codec_->Write(reinterpret_cast<const uint8_t*>(&real_val), 
                sizeof(real_val));
        assert (ok);
        return ok;
    }

    bool CodedOutputStream::WriteBigEndianInt64(int64_t val) {
        auto nbytes = codec_->MaxBytesCanWrite();
        if (nbytes < sizeof(int64_t)) {
            return false;
        }

        auto real_val = (((uint64_t) htonl(val)) << 32) + htonl(val >> 32);
        bool ok = codec_->Write(reinterpret_cast<const uint8_t*>(&real_val), 
                sizeof(real_val));
        assert (ok);
        return ok;
    }

    size_t CodedOutputStream::WriteRaw(alpha::Slice val) {
        auto nbytes = std::min<int>(codec_->MaxBytesCanWrite(), val.size());
        bool ok = codec_->Write(reinterpret_cast<const uint8_t*>(val.data()), nbytes);
        if (unlikely(!ok)) {
            return 0;
        } else {
            return nbytes;
        }
    }
#if 0
    bool CodedOutputStream::WriteLengthPrefixedString(alpha::Slice val) {
        auto nbytes = codec_->MaxBytesCanWrite();
        if (nbytes < (sizeof(int32_t) + val.size())) {
            return false;
        }

        return WriteBigEndianInt32(val.size()) 
            && codec_->Write(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }

    bool CodedOutputStream::WriteKeyValuePair(alpha::Slice key, alpha::Slice val) {
        auto nbytes = codec_->MaxBytesCanWrite();
        //TODO: 溢出检查
        if (nbytes < (2 * sizeof(int32_t) + key.size() + val.size())) {
            return false;
        }

        return WriteBigEndianInt32(key.size()) 
            && WriteBigEndianInt32(val.size())
            && codec_->Write(reinterpret_cast<const uint8_t*>(key.data()), key.size())
            && codec_->Write(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }
#endif
}
