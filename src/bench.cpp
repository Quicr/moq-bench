// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "client_callbacks.hpp"
#include "meeting_client_callbacks.hpp"
#include "moqbench.hpp"
#include "publisher_client_callbacks.hpp"
#include "subscriber_client_callbacks.hpp"

#include <cxxopts.hpp>
#include <quicr/session_callbacks.h>
#include <quicr/session_manager.h>
#include <quicr/utilities/defer.h>
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

class PerfManagerCallbacks : public quicr::SessionManager::Callbacks
{
  public:
    PerfManagerCallbacks(std::shared_ptr<PerfClientCallbacks> session_callbacks)
      : session_callbacks_(std::move(session_callbacks))
    {
    }

    void OnSessionRemoved(const std::shared_ptr<quicr::Session>& session) override
    {
        session_callbacks_->Terminate(session);
    }

  private:
    std::shared_ptr<PerfClientCallbacks> session_callbacks_;
};

auto
MakeClientCallbacks(const cxxopts::ParseResult& result)
  -> std::tuple<quicr::ClientConfig, std::shared_ptr<PerfClientCallbacks>>
{
    quicr::TransportConfig config;
    config.tls_cert_filename = "";
    config.tls_key_filename = "";
    config.time_queue_max_duration = 5000;
    config.quic_qlog_path = "";
    config.metrics_sample_ms = 5000;

    if (result.count("debug")) {
        config.debug = true;
        spdlog::set_level(spdlog::level::debug);
    }

    quicr::ClientConfig client_config;
    client_config.connect_uri = result["connect_uri"].as<std::string>();
    client_config.transport_config = config;
    client_config.tick_service_sleep_delay_us = 500'000;
    client_config.endpoint_id = result["endpoint_id"].as<std::string>();

    auto config_file = result["config"].as<std::string>();

    if (result.count("meeting")) {
        client_config.endpoint_id += ":" + std::to_string(result["instance_id"].as<std::uint32_t>());

        const auto meeting_id = result["meeting_id"].as<std::uint32_t>();
        const auto instance_id = result["instance_id"].as<std::uint32_t>();
        const auto instances = result["instances"].as<std::uint32_t>();
        const auto timeout_grace = result["timeout_grace"].as<std::uint64_t>();

        return std::make_tuple(
          client_config,
          std::make_shared<PerfMeetingClientCallbacks>(config_file, meeting_id, instances, instance_id, timeout_grace));
    } else if (result.count("publisher")) {
        SPDLOG_INFO("--------------------------------------------");
        SPDLOG_INFO("Starting...pub");
        SPDLOG_INFO("\tconfig file {}", config_file);
        SPDLOG_INFO("\tclient config:");
        SPDLOG_INFO("\t\tconnect_uri = {}", client_config.connect_uri);
        SPDLOG_INFO("\t\tendpoint = {}", client_config.endpoint_id);
        SPDLOG_INFO("--------------------------------------------");

        return std::make_tuple(client_config, std::make_shared<PerfPublisherClientCallbacks>(config_file));
    } else if (result.count("subscriber")) {
        client_config.endpoint_id += ":" + std::to_string(result["instance_id"].as<std::uint32_t>());

        return std::make_tuple(client_config, std::make_shared<PerfSubscriberClientCallbacks>(config_file));
    }

    throw std::invalid_argument("moq-bench mode must be specified");
}

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
    cxxopts::Options options("MoQ Bench");
    options.add_options()
        ("meeting",         "Run a meeting benchmark")
        ("publisher",       "Run a benchmark publisher")
        ("subscriber",      "Run a subscriber benchmark")
        ("c,config",        "Scenario config file",         cxxopts::value<std::string>())
        ("endpoint_id",     "Name of the client",           cxxopts::value<std::string>()->default_value("perf@cisco.com"))
        ("r,connect_uri",   "Relay to connect to",          cxxopts::value<std::string>()->default_value("moq://localhost:1234"))
        ("d,debug",         "Enable debug")
        ("h,help",          "Print usage")
        ("i,instance_id",   "Instance identifier number (ignored by publisher mode)",   cxxopts::value<std::uint32_t>());

    options.add_options("Meeting")
        ("meeting_id",      "Meeting identifier",               cxxopts::value<std::uint32_t>()->default_value("1"))
        ("n,instances",     "Number of instances being run",    cxxopts::value<std::uint32_t>())
        ("timeout_grace",   "Milliseconds beyond the configured test time to wait for a peer's test complete object before giving up on its track",
                                                                cxxopts::value<std::uint64_t>()->default_value("30000"));
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

    const auto [config, callbacks] = ::MakeClientCallbacks(result);
    quicr::SessionManager session_mgr(std::make_shared<PerfManagerCallbacks>(callbacks));

    auto w_session = session_mgr.AddTransport(config, callbacks);

    const auto session = w_session.lock();
    if (!session) {
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, HandleTerminateSignal);

    try {
        const auto status = session->GetStatus();
        if (status != quicr::Session::Status::kConnecting && status != quicr::Session::Status::kReady) {
            SPDLOG_CRITICAL(
              "Failed to start client (relay={}, status={})", config.connect_uri, static_cast<int>(status));
            return EXIT_FAILURE;
        }
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("Failed to connect to relay (relay={}, error={})", config.connect_uri, e.what());
        return EXIT_FAILURE;
    } catch (...) {
        SPDLOG_CRITICAL("Unknown error connecting to relay");
        return EXIT_FAILURE;
    }

    while (!terminate && !callbacks->ShouldTerminate() && !callbacks->HandlersComplete()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Unpublish/unsubscribe while the session is still up so COMPLETE can flush
    // instead of dropping the connection out from under peers.
    callbacks->Terminate(session);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    return EXIT_SUCCESS;
}
