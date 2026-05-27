// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

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

class PerfSubClient : public quicr::Client
{
  public:
    PerfSubClient(const quicr::ClientConfig& cfg, const std::string& configfile, std::uint32_t test_identifier)
      : quicr::Client(cfg)
      , configfile_(configfile)
      , test_identifier_(test_identifier)
    {
    }

    void StatusChanged(Status status) override
    {
        switch (status) {
            case Status::kReady:
                SPDLOG_INFO("Client status - kReady");
                inif_.load(configfile_);
                for (const auto& section_pair : inif_) {
                    const std::string& section_name = section_pair.first;
                    SPDLOG_INFO("Starting test - {}", section_name);
                    auto sub_handler =
                      track_handlers_.emplace_back(moqbench::PerfSubscribeTrackHandler::Create(section_name, inif_, 0));
                    SubscribeTrack(sub_handler);
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

    void MetricsSampled(const quicr::ConnectionMetrics&) override {}

    bool HandlersComplete()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        bool ret = true;
        // Don't like this - should be dependent on a 'state'
        if (track_handlers_.size() > 0) {
            for (auto handler : track_handlers_) {
                if (!handler->IsComplete()) {
                    ret = false;
                }
            }
        } else {
            ret = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return ret;
    }

    void Terminate()
    {
        std::lock_guard<std::mutex> _(track_handlers_mutex_);
        for (auto handler : track_handlers_) {
            // Unpublish the track
            SPDLOG_INFO("unsubscribe track {}", handler->TestName());
            UnsubscribeTrack(handler);
        }
        // we are done
        terminate_ = true;
    }

  private:
    bool terminate_;
    std::string configfile_;
    ini::IniFile inif_;
    std::uint32_t test_identifier_;

    std::vector<std::shared_ptr<moqbench::PerfSubscribeTrackHandler>> track_handlers_;

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
        ("i,test_id", "Test idenfiter number", cxxopts::value<std::uint32_t>()->default_value("1"))
        ("c,config","Scenario config file", cxxopts::value<std::string>())
        ("d,debug","Enable debug")
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

    const auto logger = spdlog::stderr_color_mt(log_id);

    quicr::TransportConfig config;
    config.tls_cert_filename = "";
    config.tls_key_filename = "";
    config.time_queue_max_duration = 5000;
    config.use_reset_wait_strategy = false;
    config.quic_qlog_path = "";

    if (debug) {
        config.debug = true;
        logger->set_level(spdlog::level::debug);
    }

    auto endpoint_test_id =
      result["endpoint_id"].as<std::string>() + ":" + std::to_string(result["test_id"].as<std::uint32_t>());

    quicr::ClientConfig client_config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();
    client_config.endpoint_id = endpoint_test_id;
    client_config.metrics_sample_ms = 5000;
    client_config.transport_config = config;
    client_config.tick_service_sleep_delay_us = 50'000;

    auto log_id = endpoint_test_id;
    auto test_identifier = result["test_id"].as<std::uint32_t>();

    auto client = std::make_shared<PerfSubClient>(client_config, result["config"].as<std::string>(), test_identifier);

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
    std::this_thread::sleep_for(std::chrono::seconds(1));

    return EXIT_SUCCESS;
}
