#pragma once

#include "client_callbacks.hpp"
#include "publisher_track_handler.hpp"
#include "subscriber_track_handler.hpp"

#include <quicr/utilities/defer.h>

#include <chrono>
#include <memory>
#include <string>

namespace moqbench {

    class PerfPublisherClientCallbacks : public PerfClientCallbacks
    {
      public:
        PerfPublisherClientCallbacks(const std::string& config_file)
          : PerfClientCallbacks(config_file)
        {
        }

        void StatusChanged(const std::shared_ptr<quicr::Session>& session, quicr::Session::Status status) override
        {
            std::lock_guard<std::mutex> _(mutex_);
            switch (status) {
                case quicr::Session::Status::kReady:
                    SPDLOG_INFO("PerfPubClient - kReady");
                    inif_.load(config_file_);
                    for (const auto& section_pair : inif_) {
                        const std::string& section_name = section_pair.first;
                        auto pub_handler = track_handlers_.emplace_back(
                          moqbench::PerfPublishTrackHandler::Create(section_name, inif_, 0));
                        session->PublishTrack(pub_handler);
                    }
                    break;

                case quicr::Session::Status::kNotReady:
                    SPDLOG_INFO("PerfPubClient - kNotReady");
                    break;
                case quicr::Session::Status::kConnecting:
                    SPDLOG_INFO("PerfPubClient - kConnecting");
                    break;
                case quicr::Session::Status::kDisconnecting:
                    SPDLOG_INFO("PerfPubClient - kDisconnecting");
                    break;
                case quicr::Session::Status::kPendingServerSetup:
                    SPDLOG_INFO("PerfPubClient - kPendingSeverSetup");
                    break;

                // All of the rest of these are 'errors' and will set terminate_.
                case quicr::Session::Status::kInternalError:
                    SPDLOG_INFO("PerfPubClient - kInternalError - terminate");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kInvalidParams:
                    SPDLOG_INFO("PerfPubClient - kInvalidParams - terminate");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kNotConnected:
                    SPDLOG_INFO("PerfPubClient - kNotConnected - terminate");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kFailedToConnect:
                    SPDLOG_INFO("PerfPubClient - kFailedToConnect - terminate");
                    terminate_ = true;
                    break;
                default:
                    SPDLOG_INFO("PerfPubClient - UNKNOWN - Connection failed {0}", static_cast<int>(status));
                    terminate_ = true;
                    break;
            }
        }

        bool HandlersComplete() override
        {
            std::lock_guard<std::mutex> _(mutex_);
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

        void Terminate(const std::shared_ptr<quicr::Session>& session) override
        {
            std::lock_guard<std::mutex> _(mutex_);

            // Closing the session reports it as removed, which lands back here a second time.
            if (terminate_.exchange(true)) {
                return;
            }

            SPDLOG_INFO("PerfPubClient - stopping {} publish track(s)", track_handlers_.size());

            for (auto handler : track_handlers_) {
                SPDLOG_INFO("unpublish track {}", handler->TestName());
                handler->StopWriter();
                session->UnpublishTrack(handler);
            }
        }

      private:
        std::vector<std::shared_ptr<moqbench::PerfPublishTrackHandler>> track_handlers_;
    };

}
