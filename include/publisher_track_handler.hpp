#pragma once

#include <cstdint>
#include <quicr/client.h>

#include "inicpp.h"
#include "moqbench.hpp"
#include <chrono>

namespace moqbench {
    class PerfPublishTrackHandler : public quicr::PublishTrackHandler
    {
      private:
        PerfPublishTrackHandler(const PerfConfig&);

      public:
        static std::shared_ptr<PerfPublishTrackHandler> Create(const std::string& section_name,
                                                               ini::IniFile& inif,
                                                               std::uint32_t instance_id);
        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override;

        moqbench::TestMode TestMode() { return test_mode_; }

        std::chrono::time_point<std::chrono::system_clock> PublishObjectWithMetrics(quicr::BytesSpan object_span);
        std::uint64_t PublishTestComplete();

        std::thread SpawnWriter();
        void WriteThread();
        void StopWriter();

        bool IsComplete() { return (test_mode_ == moqbench::TestMode::kComplete); }

      private:
        PerfConfig perf_config_;
        std::atomic_bool terminate_;
        uint64_t last_bytes_;
        moqbench::TestMode test_mode_;
        uint64_t group_id_;
        uint64_t object_id_;

        std::thread write_thread_;
        std::chrono::time_point<std::chrono::system_clock> last_metric_time_;

        moqbench::TestMetrics test_metrics_;
        std::mutex mutex_;
    };
} // namespace moqbench
