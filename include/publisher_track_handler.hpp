#pragma once

#include "inicpp.h"
#include "moqbench.hpp"

#include <quicr/handlers/publish_track_handler.h>

#include <atomic>
#include <chrono>
#include <cstdint>

namespace moqbench {
    class PerfPublishTrackHandler : public quicr::PublishTrackHandler
    {
      private:
        PerfPublishTrackHandler(const PerfConfig&);

      public:
        static std::shared_ptr<PerfPublishTrackHandler> Create(const std::string& section_name,
                                                               ini::IniFile& inif,
                                                               std::uint32_t instance_id);

        ~PerfPublishTrackHandler() override;

        void StatusChanged(Status status) override;
        void MetricsSampled(const quicr::PublishTrackMetrics& metrics) override;

        moqbench::TestMode TestMode() { return test_mode_; }

        std::string TestName() { return perf_config_.test_name; }

        std::chrono::time_point<std::chrono::system_clock> PublishObjectWithMetrics(quicr::BytesSpan object_span);
        std::uint64_t PublishTestComplete();

        std::thread SpawnWriter();
        void WriteThread();
        void StopWriter();

        /// True only after COMPLETE has been published, the last subgroup has been
        /// ended, and the post-complete drain has finished. Using test_mode_ here
        /// lets a meeting client exit the instant COMPLETE is queued, which tears
        /// down the session before peers can receive it.
        bool IsComplete() const { return writer_finished_.load(std::memory_order_acquire); }

      private:
        PerfConfig perf_config_;
        std::atomic_bool terminate_;
        std::atomic_bool writer_finished_;
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
