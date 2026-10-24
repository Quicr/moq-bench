#include <cxxopts.hpp>
#include <quicr/utilities/format.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <sys/wait.h>

namespace {
    /// The formatted command is handed to /bin/sh, where a metacharacter in a path
    /// or URI would be executed rather than treated as data.
    bool IsShellSafe(std::string_view value)
    {
        constexpr std::string_view kAllowedPunctuation = " ._-/:@+=";
        return !value.empty() && std::all_of(value.begin(), value.end(), [&](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || kAllowedPunctuation.find(c) != std::string_view::npos;
        });
    }
}

int
main(int argc, char** argv)
{
    // clang-format off
    cxxopts::Options options("Parallel MoQ Bench");
    options.add_options()
        ("meetings",        "Runs a meetings test in parallel")
        ("subscribers",     "Runs a fanout subsriber test in parallel")
        ("c,config",        "Path to the moqbench config file",                         cxxopts::value<std::string>())
        ("r,relay",         "URI of the relay to connect to",                           cxxopts::value<std::string>()->default_value("moq://localhost:33435"))
        ("log_path",        "Directory in which the qperf_logs dir will be created",    cxxopts::value<std::string>()->default_value("."))
        ("clear_logs",      "Deletes the logs in the log path for a fresh run.")
        ("d,bench_path",    "Directory in which the moqbench executable resides",       cxxopts::value<std::string>()->default_value("."))
        ("n,instances",     "Number of instances being run",                            cxxopts::value<std::uint32_t>())
        ("h,help",          "Print usage");

    options.add_options("Meeting")
        ("m,num_meetings",  "Number of meetings to run concurrently",   cxxopts::value<std::uint32_t>()->default_value("1"));
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

    const auto run_meetings = result.count("meetings") > 0;

    if (!run_meetings && !result.count("subscribers")) {
        std::cerr << "Must provide either 'meetings' or 'subscribers' flag" << std::endl;
        return EXIT_FAILURE;
    }

    if (!result.count("config")) {
        std::cerr << "A config file is required, pass one with --config" << std::endl;
        return EXIT_FAILURE;
    }

    if (!result.count("instances")) {
        std::cerr << "A number of instances is required, pass one with --instances" << std::endl;
        return EXIT_FAILURE;
    }

    const auto config = result["config"].as<std::string>();
    const auto relay = result["relay"].as<std::string>();
    const auto bench_path = result["bench_path"].as<std::string>();
    const auto logs_dir = result["log_path"].as<std::string>() + "/qperf_logs";
    const auto instances = result["instances"].as<std::uint32_t>();

    if (instances == 0) {
        std::cerr << "Num instances must be greater than 0" << std::endl;
        return EXIT_FAILURE;
    }

    for (const auto* value : { &config, &relay, &bench_path, &logs_dir }) {
        if (!IsShellSafe(*value)) {
            std::cerr << "Refusing to build a shell command containing '" << *value << "'" << std::endl;
            return EXIT_FAILURE;
        }
    }

    std::error_code ec;
    if (result.count("clear_logs")) {
        std::filesystem::remove_all(logs_dir, ec);
        if (ec) {
            std::cerr << "Failed to clear log directory '" << logs_dir << "' (error=" << ec.message() << ")"
                      << std::endl;
            return EXIT_FAILURE;
        }
    }

    std::filesystem::create_directories(logs_dir, ec);
    if (ec) {
        std::cerr << "Failed to create log directory '" << logs_dir << "' (error=" << ec.message() << ")" << std::endl;
        return EXIT_FAILURE;
    }

    std::cerr << "=== Starting parallel run ===" << std::endl;

    std::string command;
    if (run_meetings) {
        const auto num_meetings = result["num_meetings"].as<std::uint32_t>();

        if (num_meetings == 0) {
            std::cerr << "Num meetings must be greater than 0" << std::endl;
            return EXIT_FAILURE;
        }

        std::cerr << "- Meetings: " << num_meetings << std::endl;
        std::cerr << "- Clients per meeting: " << instances << std::endl;

        command = quicr::format(
          "parallel --eta -j {} \"{}/moqbench --meeting --meeting_id {{1}} -i {{2}} -n {} -c '{}' -r '{}' "
          "> '{}/t_m{{1}}_c{{2}}_logs.txt' 2>&1\" ::: $(seq {}) ::: $(seq {})",
          static_cast<std::uint64_t>(num_meetings) * instances,
          bench_path,
          instances,
          config,
          relay,
          logs_dir,
          num_meetings,
          instances);

    } else {
        std::cerr << "- Subscribers: " << instances << std::endl;
        command =
          quicr::format("parallel --eta -j {} \"{}/moqbench --subscriber -i {{}} -c '{}' -r '{}' > '{}/t_{{}}logs.txt' "
                        "2>&1\" ::: $(seq {})",
                        instances,
                        bench_path,
                        config,
                        relay,
                        logs_dir,
                        instances);
    }

    const int ret = std::system(command.c_str());

    std::cerr << "=== Completed ===" << std::endl;
    std::cerr << "- Parallel returned: " << ret << std::endl;

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
