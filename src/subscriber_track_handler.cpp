// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "subscriber_track_handler.hpp"
#include "moqbench.hpp"

#include <cxxopts.hpp>
#include <quicr/client.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <stack>
#include <string>
#include <thread>

namespace moqbench {

    std::atomic_bool terminate = false;

    /**
     * @brief  Subscribe track handler
     * @details Subscribe track handler used for the subscribe command line option.
     */
    PerfSubscribeTrackHandler::PerfSubscribeTrackHandler(const PerfConfig& perf_config,
                                                         std::uint32_t test_identifier,
                                                         bool publish_initiated)
      : SubscribeTrackHandler(perf_config.full_track_name,
                              perf_config.priority,
                              quicr::messages::GroupOrder::kOriginalPublisherOrder,
                              quicr::messages::FilterType::kLargestObject,
                              std::nullopt,
                              publish_initiated)
      , terminate_(false)
      , perf_config_(perf_config)
      , first_pass_(true)
      , last_bytes_(0)
      , local_now_(0)
      , last_local_now_(0)
      , total_objects_(0)
      , total_bytes_(0)
      , test_identifier_(test_identifier)
      , test_mode_(moqbench::TestMode::kNone)
      , max_bitrate_(0)
      , min_bitrate_(0)
      , avg_bitrate_(0.0)
      , metric_samples_(0)
      , bitrate_total_(0)
      , max_object_time_delta_(0)
      , min_object_time_delta_(std::numeric_limits<std::int64_t>::max())
      , avg_object_time_delta_(0.0)
      , total_time_delta_(0)
      , max_object_arrival_delta_(0)
      , min_object_arrival_delta_(std::numeric_limits<std::int64_t>::max())
      , avg_object_arrival_delta_(0.0)
      , total_arrival_delta_(0)
    {
    }

    std::shared_ptr<PerfSubscribeTrackHandler> PerfSubscribeTrackHandler::Create(const std::string& section_name,
                                                                                 ini::IniFile& inif,
                                                                                 std::uint32_t instance_id)
    {
        PerfConfig perf_config;
        PopulateScenarioFields(section_name, instance_id, inif, perf_config);
        return std::shared_ptr<PerfSubscribeTrackHandler>(new PerfSubscribeTrackHandler(perf_config, instance_id));
    }

    void PerfSubscribeTrackHandler::StatusChanged(Status status)
    {
        switch (status) {
            case Status::kOk: {
                auto track_alias = GetTrackAlias();
                if (track_alias.has_value()) {
                    SPDLOG_INFO(
                      "{}, {}, {} Ready to read", test_identifier_, perf_config_.test_name, track_alias.value());
                }
                break;
            }
            case Status::kNotConnected:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotConnected", test_identifier_, perf_config_.test_name);
                break;
            case Status::kNotSubscribed:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotSubscribed", test_identifier_, perf_config_.test_name);
                break;
            case Status::kPendingResponse:
                SPDLOG_INFO(
                  "{}, {} Subscribe Handler - kPendingSubscribeResponse", test_identifier_, perf_config_.test_name);
                break;

            // rest of these terminate
            case Status::kSendingUnsubscribe:
                SPDLOG_INFO("{}, {} Subscribe Handler - kSendingUnsubscribe", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            case Status::kError:
                SPDLOG_INFO("{}, {} Subscribe Handler - kSubscribeError", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            case Status::kNotAuthorized:
                SPDLOG_INFO("{}, {} Subscribe Handler - kNotAuthorized", test_identifier_, perf_config_.test_name);
                terminate_ = true;
                break;
            default:
                SPDLOG_INFO("{}, {} Subscribe Handler - UNKNOWN", test_identifier_, perf_config_.test_name);
                // leave...
                terminate_ = true;
                break;
        }
    }

    void PerfSubscribeTrackHandler::ObjectReceived(const quicr::ObjectHeaders& object_header,
                                                   quicr::BytesSpan data_span)
    {
        auto received_time = std::chrono::system_clock::now();
        local_now_ = std::chrono::time_point_cast<std::chrono::microseconds>(received_time).time_since_epoch().count();

        total_objects_ += 1;
        total_bytes_ += data_span.size();

        if (first_pass_) {

            last_local_now_ = local_now_;
            start_data_time_ = local_now_;
        }

        memcpy(&test_mode_, data_span.data(), sizeof(std::uint8_t));

        if (test_mode_ == moqbench::TestMode::kRunning) {

            moqbench::ObjectTestHeader test_header;
            memset(&test_header, '\0', sizeof(test_header));
            memcpy(&test_header,
                   &data_span[0],
                   data_span.size() < sizeof(test_header) ? sizeof(test_header.test_mode) : sizeof(test_header));

            auto remote_now = test_header.time;
            std::int64_t transmit_delta = local_now_ - remote_now;
            std::int64_t arrival_delta = local_now_ - last_local_now_;

            if (transmit_delta <= 0) {
                SPDLOG_INFO("-- negative/zero transmit delta (check ntp) -- {} {} {} {} {}",
                            object_header.group_id,
                            object_header.object_id,
                            local_now_,
                            remote_now,
                            transmit_delta);
            }

            if (arrival_delta <= 0) {
                SPDLOG_INFO("-- negative/zero arrival delta -- {} {} {} {} {}",
                            object_header.group_id,
                            object_header.object_id,
                            local_now_,
                            last_local_now_,
                            arrival_delta);
            }

            if (first_pass_) {
                SPDLOG_INFO("--------------------------------------------");
                SPDLOG_INFO("{}", perf_config_.test_name);
                SPDLOG_INFO("Started Receiving");
                SPDLOG_INFO("\tTest time {} ms", perf_config_.total_transmit_time);
                SPDLOG_INFO("--------------------------------------------");
            }

            SPDLOG_TRACE("OR, RUNNING, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                         test_identifier_,
                         perf_config_.test_name,
                         object_header.group_id,
                         object_header.object_id,
                         data_span.size(),
                         local_now_,
                         remote_now,
                         transmit_delta,
                         arrival_delta,
                         total_objects_,
                         total_bytes_);

            if (!first_pass_) {

                total_time_delta_ += transmit_delta;
                max_object_time_delta_ = transmit_delta > (std::int64_t)max_object_time_delta_
                                           ? transmit_delta
                                           : (std::int64_t)max_object_time_delta_;
                min_object_time_delta_ = transmit_delta < (std::int64_t)min_object_time_delta_
                                           ? transmit_delta
                                           : (std::int64_t)min_object_time_delta_;

                total_arrival_delta_ += arrival_delta;
                max_object_arrival_delta_ = arrival_delta > (std::int64_t)max_object_arrival_delta_
                                              ? arrival_delta
                                              : (std::int64_t)max_object_arrival_delta_;
                min_object_arrival_delta_ = arrival_delta < (std::int64_t)min_object_arrival_delta_
                                              ? arrival_delta
                                              : (std::int64_t)min_object_arrival_delta_;
            }

        } else if (test_mode_ == moqbench::TestMode::kComplete) {

            ObjectTestComplete test_complete;

            memset(&test_complete, '\0', sizeof(test_complete));
            memcpy(&test_complete, data_span.data(), sizeof(test_complete));

            std::int64_t total_time = local_now_ - start_data_time_;
            avg_object_time_delta_ = (double)total_time_delta_ / (double)total_objects_;
            avg_object_arrival_delta_ =
              (double)total_arrival_delta_ / (double)total_objects_ - 1; // subtract 1st object

            SPDLOG_INFO("--------------------------------------------");
            SPDLOG_INFO("{}", perf_config_.test_name);
            SPDLOG_INFO("Testing Complete");
            SPDLOG_INFO("       Total test run time (ms) {}", total_time / 1000.0f);
            SPDLOG_INFO("      Configured test time (ms) {}", perf_config_.total_transmit_time);
            SPDLOG_INFO("       Total subscribed objects {}, bytes {}", total_objects_, total_bytes_);
            SPDLOG_INFO("        Total published objects {}, bytes {}",
                        test_complete.test_metrics.total_published_objects,
                        test_complete.test_metrics.total_published_bytes);
            SPDLOG_INFO("       Subscribed delta objects {}, bytes {}",
                        test_complete.test_metrics.total_published_objects - total_objects_,
                        test_complete.test_metrics.total_published_bytes - total_bytes_);
            SPDLOG_INFO("                  Bitrate (bps):");
            SPDLOG_INFO("                            min {}", min_bitrate_);
            SPDLOG_INFO("                            max {}", max_bitrate_);
            SPDLOG_INFO("                            avg {:.3f}", avg_bitrate_);
            SPDLOG_INFO("                                {}", FormatBitrate(static_cast<std::uint32_t>(avg_bitrate_)));
            SPDLOG_INFO("        Object time delta (us):");
            SPDLOG_INFO("                            min {}", min_object_time_delta_);
            SPDLOG_INFO("                            max {}", max_object_time_delta_);
            SPDLOG_INFO("                            avg {:04.3f} ", avg_object_time_delta_);
            SPDLOG_INFO("     Object arrival delta (us):");
            SPDLOG_INFO("                            min {}", min_object_arrival_delta_);
            SPDLOG_INFO("                            max {}", max_object_arrival_delta_);
            SPDLOG_INFO("                            avg {:04.3f}", avg_object_arrival_delta_);
            SPDLOG_INFO("                            over_multiplier {}",
                        static_cast<int>(avg_object_arrival_delta_ / (perf_config_.transmit_interval * 10000)));
            SPDLOG_INFO("--------------------------------------------");

            // id,test_name,total_time,total_transmit_time,total_objects,total_bytes,sent_object,sent_bytes,min_bitrate,
            //       max_bitrate,avg_bitrate,min_time,maxtime,avg_time,min_arrival,max_arrival,avg_arrival,
            //       delta_objects,arrival_over_multiplier
            SPDLOG_INFO("OR COMPLETE, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                        test_identifier_,
                        perf_config_.test_name,
                        total_time,
                        perf_config_.total_transmit_time,
                        total_objects_,
                        total_bytes_,
                        test_complete.test_metrics.total_published_objects,
                        test_complete.test_metrics.total_published_bytes,
                        min_bitrate_,
                        max_bitrate_,
                        avg_bitrate_,
                        min_object_time_delta_,
                        max_object_time_delta_,
                        avg_object_time_delta_,
                        min_object_arrival_delta_,
                        max_object_arrival_delta_,
                        avg_object_arrival_delta_,
                        test_complete.test_metrics.total_published_objects - total_objects_,
                        static_cast<int>(avg_object_arrival_delta_ / (perf_config_.transmit_interval * 10000)));
            terminate_ = true;
            return;
        } else {
            SPDLOG_WARN(
              "OR, {}, {} - unknown data identifier {}", test_identifier_, perf_config_.test_name, (int)test_mode_);
        }

        last_local_now_ = local_now_;
        first_pass_ = false;
    }

    void PerfSubscribeTrackHandler::MetricsSampled(const quicr::SubscribeTrackMetrics& metrics)
    {
        metrics_ = metrics;
        if (last_bytes_ == 0) {
            last_metric_time_ =
              std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            last_bytes_ = metrics.bytes_received;
            return;
        }

        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - last_metric_time_);

        if (test_mode_ == moqbench::TestMode::kRunning) {
            std::uint64_t delta_bytes = metrics_.bytes_received - last_bytes_;
            std::uint64_t bitrate = ((delta_bytes) * 8) / std::max(diff.count(), std::int64_t(1));
            metric_samples_ += 1;
            bitrate_total_ += bitrate;
            if (min_bitrate_ == 0) {
                min_bitrate_ = bitrate;
            }
            max_bitrate_ = bitrate > max_bitrate_ ? bitrate : max_bitrate_;
            min_bitrate_ = bitrate < min_bitrate_ ? bitrate : min_bitrate_;
            avg_bitrate_ = (double)bitrate_total_ / (double)metric_samples_;
            SPDLOG_INFO("Metrics:, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
                        test_identifier_,
                        perf_config_.test_name,
                        bitrate,
                        FormatBitrate(bitrate),
                        delta_bytes,
                        diff.count(),
                        metrics_.objects_received,
                        metrics_.bytes_received,
                        max_bitrate_,
                        min_bitrate_,
                        avg_bitrate_);
        }

        last_metric_time_ = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
        last_bytes_ = metrics.bytes_received;
    }
}
