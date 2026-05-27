// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "publisher_track_handler.hpp"
#include "moqbench.hpp"

#include <cxxopts.hpp>
#include <quicr/client.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace moqbench {
    PerfPublishTrackHandler::PerfPublishTrackHandler(const PerfConfig& perf_config)
      : PublishTrackHandler(perf_config.full_track_name, perf_config.track_mode, perf_config.priority, perf_config.ttl)
      , perf_config_(perf_config)
      , terminate_(false)
      , last_bytes_(0)
      , test_mode_(moqbench::TestMode::kNone)
      , group_id_(0)
      , object_id_(0)

    {
        memset(&test_metrics_, '\0', sizeof(test_metrics_));
    }

    std::shared_ptr<PerfPublishTrackHandler> PerfPublishTrackHandler::Create(const std::string& section_name,
                                                                             ini::IniFile& inif,
                                                                             std::uint32_t instance_id)
    {
        PerfConfig perf_config;
        PopulateScenarioFields(section_name, instance_id, inif, perf_config);
        return std::shared_ptr<PerfPublishTrackHandler>(new PerfPublishTrackHandler(perf_config));
    }

    void PerfPublishTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                SPDLOG_INFO("PerfPublishTrackeHandler - status kOk");
                auto track_alias = GetTrackAlias().value_or(0);
                SPDLOG_INFO("Track alias: {0} is ready to write", track_alias);

                if (!write_thread_.joinable()) {
                    write_thread_ = SpawnWriter();
                } else {
                    SPDLOG_INFO("Writer already running for track alias: {} - skipping respawn", track_alias);
                }

            } break;
            case Status::kNotConnected:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNotConnected");
                break;
            case Status::kNotAnnounced:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNotAnnounced");
                break;
            case Status::kPendingAnnounceResponse:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kPendingAnnounceResponse");
                break;
            case Status::kAnnounceNotAuthorized:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kAnnounceNotAuthorized");
                break;
            case Status::kNoSubscribers:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNoSubscribers");
                break;
            case Status::kSendingUnannounce:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kSendingUnannounce");
                break;
            case Status::kPaused:
                SPDLOG_INFO("PerfPublishTrackHandler - status kPaused");
                break;
            case Status::kNewGroupRequested:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kNewGroupRequested");
                break;
            case Status::kSubscriptionUpdated:
                SPDLOG_INFO("PerfPublishTrackeHandler - status kSubscriptionUpdated");
                break;
            default:
                SPDLOG_INFO("PerfPublishTrackeHandler - status UNKNOWN");
                break;
        }
    }

    void PerfPublishTrackHandler::MetricsSampled(const quicr::PublishTrackMetrics& metrics)
    {
        std::lock_guard<std::mutex> _(mutex_);
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

        if (test_mode_ == moqbench::TestMode::kRunning && last_bytes_ != 0) { // skip first metric reporting...
            // calculate bitrate metrics
            auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_metric_time_);
            std::uint64_t delta_bytes = metrics.bytes_published - last_bytes_;
            std::uint64_t bitrate = ((delta_bytes) * 8) / diff.count();
            test_metrics_.bitrate_total += bitrate;
            test_metrics_.max_publish_bitrate =
              bitrate > test_metrics_.max_publish_bitrate ? bitrate : test_metrics_.max_publish_bitrate;
            test_metrics_.min_publish_bitrate =
              bitrate < test_metrics_.min_publish_bitrate ? bitrate : test_metrics_.min_publish_bitrate;
            test_metrics_.metric_samples += 1;
            test_metrics_.avg_publish_bitrate = test_metrics_.bitrate_total / test_metrics_.metric_samples;

            SPDLOG_INFO("{}: Bitrate: {} {} delta bytes {}, delta time {}, objs: {} bw min/max/avg: {} / {} /  {}",
                        perf_config_.test_name,
                        bitrate,
                        FormatBitrate(bitrate),
                        delta_bytes,
                        diff.count(),
                        metrics.objects_published,
                        test_metrics_.min_publish_bitrate,
                        test_metrics_.max_publish_bitrate,
                        test_metrics_.avg_publish_bitrate);
        }

        last_metric_time_ = now;
        last_bytes_ = metrics.bytes_published;
    }

    std::chrono::time_point<std::chrono::system_clock> PerfPublishTrackHandler::PublishObjectWithMetrics(
      quicr::BytesSpan object_span)
    {
        std::lock_guard<std::mutex> _(mutex_);
        ObjectTestHeader test_header;
        memset(&test_header, '\0', sizeof(test_header));
        if (perf_config_.objects_per_group > 0) {
            if (!(object_id_ % perf_config_.objects_per_group)) {
                object_id_ = 0;
                group_id_ += 1;
            }
        } else {
            SPDLOG_WARN("{} Error - objects per groups <= 0", perf_config_.test_name);
        }

        quicr::ObjectHeaders object_headers;
        object_headers.group_id = group_id_;
        object_headers.object_id = object_id_;
        object_headers.payload_length = 0; // set later
        object_headers.priority = perf_config_.priority;
        object_headers.ttl = perf_config_.ttl;

        // get current time..
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();

        // update metrics
        if (test_metrics_.start_transmit_time == 0) {
            test_metrics_.start_transmit_time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        }

        // fill out test_header
        test_header.test_mode = moqbench::TestMode::kRunning;
        test_header.time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        // check how much we can write in the header
        auto header_bytes_to_copy =
          object_span.size() < sizeof(test_header) ? sizeof(test_header.test_mode) : sizeof(test_header);
        memcpy((void*)object_span.data(), (void*)&test_header, header_bytes_to_copy);
        object_headers.payload_length = object_span.size();

        // publish
        PublishObject(object_headers, object_span);

        SPDLOG_TRACE("PO, RUNNING, {}, {}, {}, {}, {}",
                     perf_config_.test_name,
                     group_id_,
                     object_id_,
                     publish_track_metrics_.objects_published,
                     publish_track_metrics_.bytes_published);

        // return current time in ms - publish time
        return now;
    }

    std::uint64_t PerfPublishTrackHandler::PublishTestComplete()
    {
        std::lock_guard<std::mutex> _(mutex_);
        test_mode_ = moqbench::TestMode::kComplete;
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();

        ObjectTestComplete test_complete;
        memset(&test_complete, '\0', sizeof(test_complete));

        // start_transmit_time is set when fist object is published
        test_metrics_.end_transmit_time = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

        // test_metrics_.end_transmit_time;
        test_metrics_.total_published_objects = publish_track_metrics_.objects_published + 1;
        test_metrics_.total_published_bytes = publish_track_metrics_.bytes_published + sizeof(test_complete);
        test_metrics_.total_objects_dropped_not_ok = publish_track_metrics_.objects_dropped_not_ok;

        test_complete.test_mode = test_mode_;
        test_complete.time = test_metrics_.end_transmit_time;
        memcpy(&test_complete.test_metrics, &test_metrics_, sizeof(test_metrics_));

        quicr::Bytes object_data(sizeof(test_complete));
        memcpy((void*)&object_data[0], (void*)&test_complete, sizeof(test_complete));

        object_id_ += 1;

        quicr::ObjectHeaders object_headers;
        object_headers.group_id = group_id_;
        object_headers.object_id = object_id_;
        object_headers.payload_length = 0;
        object_headers.priority = perf_config_.priority;
        object_headers.ttl = perf_config_.ttl;

        object_headers.payload_length = sizeof(test_complete);
        PublishObject(object_headers, object_data);

        auto total_transmit_time = test_metrics_.end_transmit_time - test_metrics_.start_transmit_time;
        SPDLOG_INFO("PO, COMPLETE, {}, {}, {}, {}, {}, {}",
                    perf_config_.test_name,
                    group_id_,
                    object_id_,
                    test_metrics_.total_published_objects,
                    test_metrics_.total_published_bytes,
                    total_transmit_time);
        SPDLOG_INFO("--------------------------------------------");
        SPDLOG_INFO("{}", perf_config_.test_name);
        SPDLOG_INFO("Publish Object - Complete");
        SPDLOG_INFO("      Total transmit time (ms) {}", total_transmit_time);
        SPDLOG_INFO("        Total pubished objects {}, bytes {}",
                    test_metrics_.total_published_objects,
                    test_metrics_.total_published_bytes);
        SPDLOG_INFO("                 Bitrate (bps)");
        SPDLOG_INFO("                           min {}", test_metrics_.min_publish_bitrate);
        SPDLOG_INFO("                           max {}", test_metrics_.max_publish_bitrate);
        SPDLOG_INFO("                           avg {}", test_metrics_.avg_publish_bitrate);
        SPDLOG_INFO("                               {}",
                    FormatBitrate(static_cast<std::uint32_t>(test_metrics_.avg_publish_bitrate)));
        SPDLOG_INFO("--------------------------------------------");

        return test_complete.time;
    }

    std::thread PerfPublishTrackHandler::SpawnWriter()
    {
        return std::thread([this] { WriteThread(); });
    }

    void PerfPublishTrackHandler::WriteThread()
    {
        quicr::Bytes object_0_buffer(perf_config_.first_object_size);
        quicr::Bytes object_not_0_buffer(perf_config_.object_size);

        for (std::size_t i = 0; i < object_0_buffer.size(); i++) {
            object_0_buffer[i] = i % 255;
        }

        for (std::size_t i = 0; i < object_not_0_buffer.size(); i++) {
            object_not_0_buffer[i] = i % 255;
        }

        group_id_ = 0;
        object_id_ = 0;

        if (perf_config_.total_test_time <= 0) {
            SPDLOG_WARN("Transmit time <= 0 - stopping test");
            return;
        }

        const std::chrono::time_point<std::chrono::system_clock> start_transmit_time = std::chrono::system_clock::now();
        auto transmit_time_ms = std::chrono::milliseconds(perf_config_.total_test_time);
        auto end_transmit_time = start_transmit_time + transmit_time_ms;

        // Delay before transmitting. If the status transitions to kPaused during this
        // delay, suspend the writer until status leaves kPaused (or terminate_ is set),
        // then restart the start_delay timer from the beginning.
        if (perf_config_.start_delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            test_mode_ = moqbench::TestMode::kWaitPreTest;

            bool restart_delay = true;
            while (restart_delay && !terminate_) {
                restart_delay = false;
                SPDLOG_INFO("{} Waiting start delay {} ms", perf_config_.test_name, perf_config_.start_delay);
                const std::chrono::time_point<std::chrono::system_clock> start =
                  std::chrono::system_clock::now();
                auto delay_ms = std::chrono::milliseconds(perf_config_.start_delay);
                auto end_time = start + delay_ms;
                while (!terminate_) {
                    if (GetStatus() == Status::kPaused) {
                        SPDLOG_INFO("{} Status is kPaused during start delay - suspending writer",
                                    perf_config_.test_name);
                        while (!terminate_ && GetStatus() == Status::kPaused) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        }
                        if (terminate_) {
                            break;
                        }
                        SPDLOG_INFO("{} Status left kPaused (now {}) - restarting start delay",
                                    perf_config_.test_name,
                                    static_cast<int>(GetStatus()));
                        restart_delay = true;
                        break;
                    }

                    auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now());
                    if (now >= end_time) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
            }
        }

        // Transmit
        SPDLOG_INFO("{} Start transmitting for {} ms", perf_config_.test_name, perf_config_.total_transmit_time);

        test_mode_ = moqbench::TestMode::kRunning;
        while (!terminate_) {
            std::chrono::time_point<std::chrono::system_clock> last_publish_time;
            if (object_id_ == 0) {
                quicr::BytesSpan object_span(object_0_buffer);
                last_publish_time = PublishObjectWithMetrics(object_span);
            } else {
                quicr::BytesSpan object_span(object_not_0_buffer);
                last_publish_time = PublishObjectWithMetrics(object_span);
            }

            // Check if we are done...
            if (last_publish_time >= end_transmit_time) {
                // publish COMPLETE object  - end of test
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                PublishTestComplete();
                std::this_thread::sleep_for(std::chrono::milliseconds(perf_config_.start_delay / 2));
                terminate_ = true;
                return;
            }

            // Wait interval
            if (perf_config_.transmit_interval >= 0) {
                std::uint64_t interval_us = (perf_config_.transmit_interval * 1000.0f);
                std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
            } else {
                SPDLOG_WARN("{} Transmit interval is < 0", perf_config_.test_name);
            }
            object_id_ += 1;
        };
        SPDLOG_WARN("{} Exiting writer thread.", perf_config_.test_name);
    }

    void PerfPublishTrackHandler::StopWriter()
    {
        terminate_ = true;
        if (write_thread_.joinable()) {
            write_thread_.join();
        }
    }
} // namespace
