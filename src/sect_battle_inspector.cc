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

namespace SectBattle {
    const int Inspector::kMaxSampleSeconds;
    Inspector::Inspector()
        :max_request_process_time_(0) {
        ::memset(requests_, 0x0, sizeof(requests_));
        ::memset(succeed_requests_, 0x0, sizeof(succeed_requests_));
    }

    void Inspector::RecordProcessStartTime(alpha::TimeStamp timestamp) {
        process_start_time_ = timestamp;
    }

    void Inspector::AddRequestNum(alpha::TimeStamp timestamp) {
        requests_[TimeStampToIndex(timestamp)]++;
    }

    void Inspector::AddSucceedRequestNum(alpha::TimeStamp timestamp) {
        succeed_requests_[TimeStampToIndex(timestamp)]++;
    }

    void Inspector::RecordProcessRequestTime(int milliseconds) {
        total_requests_.first++;
        total_requests_.second += milliseconds;
        max_request_process_time_ = std::max(milliseconds, max_request_process_time_);
    }

    int32_t Inspector::RequestProcessedPerSeconds() const {
        int32_t sum = std::accumulate(std::begin(requests_), std::end(requests_), 0);
        return static_cast<double>(sum) / kMaxSampleSeconds;
    }

    int32_t Inspector::SampleRequests(int latest_seconds) const {
        return Sample(&requests_[0], latest_seconds);
    }

    int32_t Inspector::SampleSucceedRequests(int latest_seconds) const {
        return Sample(&succeed_requests_[0], latest_seconds);
    }

    int32_t Inspector::AverageProcessTime() const {
        return static_cast<double>(total_requests_.second) / total_requests_.first;
    }

    alpha::TimeStamp Inspector::ProcessStartTime() const {
        return process_start_time_;
    }

    int32_t Inspector::Sample(const int32_t* array, int latest_seconds) const {
        latest_seconds = std::min(latest_seconds, kMaxSampleSeconds);
        alpha::TimeStamp now = time(nullptr);
        auto start = now - latest_seconds;
        auto index = TimeStampToIndex(start);
        uint64_t sum = 0;
        for (auto i = 0; i < latest_seconds; ++i) {
            sum += array[index];
            index = (index + 1 == kMaxSampleSeconds) ? 0 : index + 1;
        }
        int32_t average = static_cast<double>(sum) / latest_seconds;
        return average;
    }

    int Inspector::TimeStampToIndex(alpha::TimeStamp timestamp) const {
        return timestamp % kMaxSampleSeconds;
    }
}
