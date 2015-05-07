/*
 * =============================================================================
 *
 *       Filename:  tt_coded_stream.h
 *        Created:  04/30/15 16:45:42
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __TT_CODED_STREAM_H__
#define  __TT_CODED_STREAM_H__

#include <cstdint>
#include <string>
#include <alpha/slice.h>

namespace tokyotyrant {
    class ProtocolCodec;
    class CodedInputStream final {
        public:
            CodedInputStream(const uint8_t* buf, int size);

            bool Skip(int count);
            bool ReadString(std::string* buffer, int size);
            bool ReadPartialString(std::string* buffer, int vsize);
            bool ReadInt8(int8_t* val);
            bool ReadBigEndianInt32(int32_t* val);
            bool ReadBigEndianInt64(int64_t* val);
            int pos() const;

        private:
            bool Overflow(int amount);
            void Advance(int amount);
            const uint8_t* const original_buf_;
            const uint8_t* buf_;
            const int size_;
    };

    class CodedOutputStream final {
        public:
            CodedOutputStream(ProtocolCodec* codec);
            bool WriteBigEndianInt16(int16_t val);
            bool WriteBigEndianInt32(int32_t val);
            bool WriteBigEndianInt64(int64_t val);
            size_t WriteRaw(alpha::Slice val);
            //bool WriteLengthPrefixedString(alpha::Slice val);
            //bool WriteKeyValuePair(alpha::Slice key, alpha::Slice val);
            //void WritePartialLengthPrefixedString(alpha::Slice val)

        private:
            ProtocolCodec* codec_;
    };
}

#endif   /* ----- #ifndef __TT_CODED_STREAM_H__  ----- */
