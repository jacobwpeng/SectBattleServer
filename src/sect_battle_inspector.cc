/*
 * =============================================================================
 *
 *       Filename:  sect_battle_inspector.cc
 *        Created:  05/27/15 16:05:57
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_inspector.h"
#include <cstring> //memset
#include <algorithm>
#include <alpha/logger.h>

namespace SectBattle {
    const int PeriodStatisticQueue::kMaxSampleSeconds;

    void PeriodStatisticQueue::Add(alpha::TimeStamp time) {
        RemoveOutdated(time);
        statistics_[TimeStampToSeconds(time)]++;
    }

    int32_t PeriodStatisticQueue::SampleAverage(alpha::TimeStamp now, int seconds) const {
        seconds = std::min(seconds, kMaxSampleSeconds);
        auto now_seconds = TimeStampToSeconds(now);
        int average = 0;
        std::for_each(statistics_.lower_bound(now_seconds - seconds + 1),
                statistics_.upper_bound(now_seconds),
                [&average](const std::pair<int, int>& p) {
                average += p.second;
        });
        return average;
    }

    int PeriodStatisticQueue::TimeStampToSeconds(alpha::TimeStamp time) const {
        return time / 1000;
    }

    void PeriodStatisticQueue::RemoveOutdated(alpha::TimeStamp latest) {
        auto preserved = TimeStampToSeconds(latest) - kMaxSampleSeconds;
        if (statistics_.empty() || statistics_.begin()->first >= preserved) {
            return;
        }

        auto first = statistics_.begin();
        auto last = statistics_.lower_bound(preserved);
        if (last != statistics_.end() && last->first == preserved) {
            if (last != statistics_.end()) {
                ++last;
            }
        }

        statistics_.erase(first, last);
    }

    Inspector::Inspector() {
    }

    void Inspector::RecordProcessStartTime(alpha::TimeStamp timestamp) {
        process_start_time_ = timestamp;
    }

    void Inspector::AddRequestNum(alpha::TimeStamp timestamp) {
        requests_.Add(timestamp);
    }

    void Inspector::AddSucceedRequestNum(alpha::TimeStamp timestamp) {
        succeed_requests_.Add(timestamp);
    }

    void Inspector::RecordProcessRequestTime(int milliseconds) {
        total_requests_.first++;
        total_requests_.second += milliseconds;
        max_request_process_time_ = std::max(milliseconds, max_request_process_time_);
    }

    double Inspector::RequestProcessedPerSeconds() const {
        auto sum = requests_.SampleAverage(alpha::Now(),
                PeriodStatisticQueue::kMaxSampleSeconds);
        return static_cast<double>(sum) / PeriodStatisticQueue::kMaxSampleSeconds;
    }

    int32_t Inspector::SampleRequests(int latest_seconds) const {
        return requests_.SampleAverage(alpha::Now(), latest_seconds);
    }

    int32_t Inspector::SampleSucceedRequests(int latest_seconds) const {
        return succeed_requests_.SampleAverage(alpha::Now(), latest_seconds);
    }

    int32_t Inspector::AverageProcessTime() const {
        if (total_requests_.first == 0) return 0;
        return static_cast<double>(total_requests_.second) / total_requests_.first;
    }

    alpha::TimeStamp Inspector::ProcessStartTime() const {
        return process_start_time_;
    }
}
