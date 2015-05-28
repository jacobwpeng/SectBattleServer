/*
 * =============================================================================
 *
 *       Filename:  sect_battle_inspector.h
 *        Created:  05/27/15 16:05:20
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_INSPECTOR_H__
#define  __SECT_BATTLE_INSPECTOR_H__

#include <alpha/time_util.h>
#include <utility>

namespace SectBattle {
    class Inspector {
        public:
            Inspector();

            void RecordProcessStartTime(alpha::TimeStamp timestamp);
            void RecordProcessRequestTime(int milliseconds);
            void AddRequestNum(alpha::TimeStamp timestamp);
            void AddSucceedRequestNum(alpha::TimeStamp timestamp);
            int32_t RequestProcessedPerSeconds() const;
            int32_t SampleRequests(int latest_seconds) const;
            int32_t SampleSucceedRequests(int latest_seconds) const;
            int32_t AverageProcessTime() const;
            alpha::TimeStamp ProcessStartTime() const;

        private:
            static const int kMaxSampleSeconds = 900;
            int32_t Sample(const int32_t* array, int latest_seconds) const;
            int TimeStampToIndex(alpha::TimeStamp timestamp) const;

            alpha::TimeStamp process_start_time_;
            int max_request_process_time_;
            std::pair<uint64_t, uint64_t> total_requests_;
            int32_t requests_[kMaxSampleSeconds];
            int32_t succeed_requests_[kMaxSampleSeconds];
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_INSPECTOR_H__  ----- */
