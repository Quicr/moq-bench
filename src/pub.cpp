// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "moqbench.hpp"
#include "publisher_track_handler.hpp"

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

class PerfPubClient : public quicr::Client
{
  public:
    PerfPubClient(const quicr::ClientConfig& cfg, const std::string& configfile)
      : quicr::Client(cfg)
      , configfile_(configfile)
    {
    }

    void StatusChanged(Status status) override
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("PerfPubClient - kReady");
                inif_.load(configfile_);
                for (const auto& section_pair : inif_) {
                    const std::string& section_name = section_pair.first;
                    auto pub_handler =
                      track_handlers_.emplace_back(moqbench::PerfPublishTrackHandler::Create(section_name, inif_, 0));
                    PublishTrack(pub_handler);
                }
                break;

            case Status::kNotReady:
                SPDLOG_INFO("PerfPubClient - kNotReady");
                break;
            case Status::kConnecting:
                SPDLOG_INFO("PerfPubClient - kConnecting");
                break;
            case Status::kDisconnecting:
                SPDLOG_INFO("PerfPubClient - kDisconnecting");
                break;
            case Status::kPendingServerSetup:
                SPDLOG_INFO("PerfPubClient - kPendingSeverSetup");
                break;

            // All of the rest of these are 'errors' and will set terminate_.
            case Status::kInternalError:
                SPDLOG_INFO("PerfPubClient - kInternalError - terminate");
                terminate_ = true;
                break;
            case Status::kInvalidParams:
                SPDLOG_INFO("PerfPubClient - kInvalidParams - terminate");
                terminate_ = true;
                break;
            case Status::kNotConnected:
                SPDLOG_INFO("PerfPubClient - kNotConnected - terminate");
                terminate_ = true;
                break;
            case Status::kFailedToConnect:
                SPDLOG_INFO("PerfPubClient - kFailedToConnect - terminate");
                terminate_ = true;
                break;
            default:
                SPDLOG_INFO("PerfPubClient - UNKNOWN - Connection failed {0}", static_cast<int>(status));
                terminate_ = true;
                break;
        }
    }

    void MetricsSampled(const quicr::ConnectionMetrics&) override {}

    bool GetTerminateStatus() { return terminate_; }

    bool HandlersComplete()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        bool ret = true;
        // Don't like this - should be dependent on a 'state'
        if (track_handlers_.size() > 0) {
            for (auto handler : track_handlers_) {
                if (!handler->IsComplete()) {
                    ret = false;
                    break;
                }
            }
        } else {
            ret = false;
        }
        return ret;
    }

    void Terminate()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        for (auto handler : track_handlers_) {
            // Stop the handler writer thread...
            handler->StopWriter();
            // Unpublish the track
            UnpublishTrack(handler);
        }
        // we are done
        terminate_ = true;
    }

  private:
    bool terminate_;
    std::string configfile_;
    ini::IniFile inif_;
    std::vector<std::shared_ptr<moqbench::PerfPublishTrackHandler>> track_handlers_;
    std::mutex track_handlers_mutex_;
};

bool terminate = false;

void
HandleTerminateSignal(int)
{
    terminate = true;
}

int
main(int argc, char** argv)
{
    bool debug{ false };

    // clang-format off
    cxxopts::Options options("MoqBench");
    options.add_options()
        ("endpoint_id","Name of the client", cxxopts::value<std::string>()->default_value("perf@cisco.com"))
        ("connect_uri", "Relay to connect to", cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("c,config", "Scenario config file", cxxopts::value<std::string>()->default_value("./config.ini"))
        ("d,debug", "Enable debug")
        ("h,help", "Print usage");
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

    if (result.count("debug")) {
        debug = true;
    }

    quicr::TransportConfig config;
    config.tls_cert_filename = "";
    config.tls_key_filename = "";
    config.time_queue_max_duration = 5000;
    config.use_reset_wait_strategy = false;
    config.quic_qlog_path = "";

    quicr::ClientConfig client_config;
    client_config.endpoint_id = result["endpoint_id"].as<std::string>();
    client_config.metrics_sample_ms = 5000;
    client_config.transport_config = config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();
    client_config.tick_service_sleep_delay_us = 500'000;

    const auto logger = spdlog::stderr_color_mt("PERF");

    if (debug) {
        config.debug = true;
        spdlog::set_level(spdlog::level::debug);
    }

    auto config_file = result["config"].as<std::string>();
    SPDLOG_INFO("--------------------------------------------");
    SPDLOG_INFO("Starting...pub");
    SPDLOG_INFO("\tconfig file {}", config_file);
    SPDLOG_INFO("\tclient config:");
    SPDLOG_INFO("\t\tconnect_uri = {}", client_config.connect_uri);
    SPDLOG_INFO("\t\tendpoint = {}", client_config.endpoint_id);
    SPDLOG_INFO("--------------------------------------------");

    std::signal(SIGINT, HandleTerminateSignal);

    auto client = std::make_shared<PerfPubClient>(client_config, config_file);

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
