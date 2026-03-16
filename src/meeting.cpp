// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "publisher_track_handler.hpp"
#include "subscriber_track_handler.hpp"

#include <cxxopts.hpp>
#include <quicr/client.h>
#include <quicr/defer.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace moqbench;

class PerfClient : public quicr::Client
{
  public:
    PerfClient(const quicr::ClientConfig& cfg,
               const std::string& configfile,
               std::uint32_t meeting_id,
               std::uint32_t instances,
               std::uint32_t instance_identifier)
      : quicr::Client(cfg)
      , configfile_(configfile)
      , meeting_id_(meeting_id)
      , instance_id_(instance_identifier)
      , instances_(instances)
    {
    }

    void StatusChanged(Status status)
    {
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Client status - kReady");
                inif_.load(configfile_);

                for (const auto& [section_name, _] : inif_) {
                    auto pub_handler = pub_track_handlers_.emplace_back(
                      PerfPublishTrackHandler::Create(section_name, inif_, instance_id_ + (meeting_id_ * 1000)));
                    PublishTrack(pub_handler);
                }

                for (std::uint32_t i = 1; i <= instances_; ++i) {
                    if (i == instance_id_) {
                        continue;
                    }

                    for (const auto& [section_name, _] : inif_) {
                        auto sub_handler = sub_track_handlers_.emplace_back(
                          PerfSubscribeTrackHandler::Create(section_name, inif_, i + (meeting_id_ * 1000)));
                        SubscribeTrack(sub_handler);
                    }
                }

                break;
            case Status::kNotReady:
                SPDLOG_INFO("Client status - kNotReady");
                break;
            case Status::kConnecting:
                SPDLOG_INFO("Client status - kConnecting");
                break;
            case Status::kNotConnected:
                SPDLOG_INFO("Client status - kNotConnected");
                break;
            case Status::kPendingServerSetup:
                SPDLOG_INFO("Client status - kPendingSeverSetup");
                break;

            case Status::kFailedToConnect:
                SPDLOG_ERROR("Client status - kFailedToConnect");
                terminate_ = true;
                break;
            case Status::kInternalError:
                SPDLOG_ERROR("Client status - kInternalError");
                terminate_ = true;
                break;
            case Status::kInvalidParams:
                SPDLOG_ERROR("Client status - kInvalidParams");
                terminate_ = true;
                break;
            default:
                SPDLOG_ERROR("Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    bool HandlersComplete()
    {
        std::lock_guard<std::mutex> _(mutex_);
        defer(std::this_thread::sleep_for(std::chrono::milliseconds(100)));

        if (sub_track_handlers_.empty() || pub_track_handlers_.empty()) {
            return false;
        }

        for (auto handler : pub_track_handlers_) {
            if (!handler->IsComplete()) {
                return false;
            }
        }

        for (auto handler : sub_track_handlers_) {
            if (!handler->IsComplete()) {
                return false;
            }
        }

        return true;
    }

    void Terminate()
    {
        std::lock_guard<std::mutex> _(mutex_);

        for (auto handler : sub_track_handlers_) {
            SPDLOG_INFO("unsubscribe track {}", handler->TestName());
            UnsubscribeTrack(handler);
        }

        for (auto handler : pub_track_handlers_) {
            handler->StopWriter();
            UnpublishTrack(handler);
        }

        terminate_ = true;
    }

  private:
    bool terminate_;
    std::string configfile_;
    ini::IniFile inif_;
    std::uint32_t meeting_id_;
    std::uint32_t instance_id_;
    std::uint32_t instances_;

    std::vector<std::shared_ptr<PerfSubscribeTrackHandler>> sub_track_handlers_;
    std::vector<std::shared_ptr<PerfPublishTrackHandler>> pub_track_handlers_;

    std::mutex mutex_;
};

std::atomic_bool terminate = false;

void
HandleTerminateSignal(int)
{
    terminate = true;
}

int
main(int argc, char** argv)
{
    // clang-format off
    cxxopts::Options options("MoqBench");
    options.add_options()
        ("endpoint_id",     "Name of the client",               cxxopts::value<std::string>()->default_value("perf@cisco.com"))
        ("connect_uri",     "Relay to connect to",              cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("meeting_id",      "Meeting identifier",               cxxopts::value<std::uint32_t>()->default_value("1"))
        ("n,instances",     "Number of instances being run",    cxxopts::value<std::uint32_t>())
        ("i,instance_id",   "Instance identifier number",       cxxopts::value<std::uint32_t>())
        ("c,config",        "Scenario config file",             cxxopts::value<std::string>())
        ("h,help",          "Print usage");
    // clang-format on

    cxxopts::ParseResult result;

    try {
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Caught exception while parsing arguments: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (result.count("help")) {
        std::cerr << options.help() << std::endl;
        return EXIT_SUCCESS;
    }

    quicr::TransportConfig config;
    config.tls_cert_filename = "";
    config.tls_key_filename = "";
    config.time_queue_max_duration = 5000;
    config.use_reset_wait_strategy = false;
    config.quic_qlog_path = "";

    auto endpoint_instance_id =
      result["endpoint_id"].as<std::string>() + ":" + std::to_string(result["instance_id"].as<std::uint32_t>());

    quicr::ClientConfig client_config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();
    client_config.endpoint_id = endpoint_instance_id;
    client_config.metrics_sample_ms = 5000;
    client_config.transport_config = config;
    client_config.tick_service_sleep_delay_us = 50'000;

    auto log_id = endpoint_instance_id;

    const auto logger = spdlog::stderr_color_mt(log_id);

    const auto meeting_id = result["meeting_id"].as<std::uint32_t>();
    const auto instance_id = result["instance_id"].as<std::uint32_t>();
    const auto instances = result["instances"].as<std::uint32_t>();

    auto client = std::make_shared<PerfClient>(
      client_config, result["config"].as<std::string>(), meeting_id, instances, instance_id);

    std::signal(SIGINT, HandleTerminateSignal);

    try {
        client->Connect();
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_CRITICAL(
          logger, "Failed to connect to relay '{0}' with exception: {1}", client_config.connect_uri, e.what());
        return EXIT_FAILURE;
    } catch (...) {
        SPDLOG_LOGGER_CRITICAL(logger, "Unexpected error connecting to relay");
        return EXIT_FAILURE;
    }

    while (!terminate && !client->HandlersComplete()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    client->Terminate();
    client->Disconnect();

    return EXIT_SUCCESS;
}
