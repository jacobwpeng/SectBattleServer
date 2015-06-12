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
#include <map>
#include <utility>

namespace SectBattle {
    class PeriodStatisticQueue {
        public:
            static const int kMaxSampleSeconds = 900;

            void Add(alpha::TimeStamp time);
            int SampleAverage(alpha::TimeStamp now, int seconds) const;

        private:
            int TimeStampToSeconds(alpha::TimeStamp time) const;
            void RemoveOutdated(alpha::TimeStamp preserved);
            std::map<int, int> statistics_;
    };
    class Inspector {
        public:
            Inspector();

            void RecordProcessStartTime(alpha::TimeStamp timestamp);
            void RecordProcessRequestTime(int us);
            void AddRequestNum(alpha::TimeStamp timestamp);
            void AddSucceedRequestNum(alpha::TimeStamp timestamp);
            double RequestProcessedPerSeconds() const;
            int32_t SampleRequests(int latest_seconds) const;
            int32_t SampleSucceedRequests(int latest_seconds) const;
            int32_t AverageProcessTime() const;
            alpha::TimeStamp ProcessStartTime() const;
            int MaxRequestProcessTime() const;

        private:
            static const int kMaxSampleSeconds = 900;
            int32_t Sample(const int32_t* array, int latest_seconds) const;

            alpha::TimeStamp process_start_time_ = 0;
            int max_request_process_time_ = 0;
            std::pair<uint64_t, uint64_t> total_requests_;
            PeriodStatisticQueue requests_;
            PeriodStatisticQueue succeed_requests_;
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_INSPECTOR_H__  ----- */
