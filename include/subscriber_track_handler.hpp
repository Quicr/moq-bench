#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"
#include "moqbench.hpp"

namespace moqbench {
    class PerfSubscribeTrackHandler : public quicr::SubscribeTrackHandler
    {
      private:
        PerfSubscribeTrackHandler(const PerfConfig& perf_config, std::uint32_t test_identifier, bool publish_initiated=false);

      public:
        static std::shared_ptr<PerfSubscribeTrackHandler> Create(const std::string& section_name,
                                                                 ini::IniFile& inif,
                                                                 std::uint32_t test_identifier);
        void ObjectReceived(const quicr::ObjectHeaders&, quicr::BytesSpan) override;
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::SubscribeTrackMetrics& metrics) override;
        const quicr::SubscribeTrackMetrics& GetMetrics() const noexcept { return metrics_; }

        bool IsComplete() { return terminate_; }

        std::string TestName() { return perf_config_.test_name; }

      private:
        std::atomic_bool terminate_;
        PerfConfig perf_config_;
        quicr::SubscribeTrackMetrics metrics_;
        bool first_pass_;
        std::chrono::time_point<std::chrono::system_clock> last_metric_time_;
        uint64_t last_bytes_;
        std::uint64_t local_now_;
        std::uint64_t last_local_now_;
        std::uint64_t start_data_time_;
        std::uint64_t total_objects_;
        std::uint64_t total_bytes_;
        std::uint32_t test_identifier_;
        moqbench::TestMode test_mode_;

        std::uint64_t max_bitrate_;
        std::uint64_t min_bitrate_;
        double avg_bitrate_;

        std::uint32_t metric_samples_;
        std::uint64_t bitrate_total_;

        std::int64_t max_object_time_delta_;
        std::int64_t min_object_time_delta_;
        double avg_object_time_delta_;
        std::int64_t total_time_delta_;

        std::int64_t max_object_arrival_delta_;
        std::int64_t min_object_arrival_delta_;
        double avg_object_arrival_delta_;
        std::int64_t total_arrival_delta_;
    };

} // namespace
