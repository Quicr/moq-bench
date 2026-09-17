#pragma once

#include "client_callbacks.hpp"
#include "publisher_track_handler.hpp"
#include "subscriber_track_handler.hpp"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace moqbench {

    class PerfMeetingClientCallbacks : public PerfClientCallbacks
    {
      public:
        PerfMeetingClientCallbacks(const std::string& configfile,
                                   std::uint32_t meeting_id,
                                   std::uint32_t instances,
                                   std::uint32_t instance_identifier,
                                   std::uint64_t timeout_grace_ms)
          : PerfClientCallbacks(configfile)
          , meeting_id_(meeting_id)
          , instance_id_(instance_identifier)
          , instances_(instances)
          , timeout_grace_ms_(timeout_grace_ms)
        {
        }

        quicr::Reply<const quicr::PublishResponse, quicr::PublishErrorCode> PublishReceived(
          [[maybe_unused]] const std::shared_ptr<quicr::Session>& session,
          [[maybe_unused]] std::uint64_t request_id,
          const quicr::PublishAttributes& publish_attributes,
          [[maybe_unused]] std::weak_ptr<quicr::SubscribeNamespaceHandler> sub_ns_handler) override
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (const auto& handler : sub_track_handlers_) {
                const auto tfn = handler->GetFullTrackName();
                const auto& pub_tfn = publish_attributes.track_full_name;

                if (tfn.name_space != pub_tfn.name_space || tfn.name != pub_tfn.name) {
                    continue;
                }

                // Already bound to a prior PUBLISH; keep looking for a free handler.
                if (handler->GetRequestId().has_value()) {
                    continue;
                }

                std::ostringstream ns_str;
                auto ns_entries = tfn.name_space.GetEntries();

                for (const auto entry : ns_entries) {
                    ns_str << '/';
                    ns_str << std::string(entry.begin(), entry.end());
                }

                SPDLOG_INFO("Publish Received matching Subscribe track; test name: {} ns: {} name: {} forward: {}",
                            handler->TestName(),
                            ns_str.str(),
                            std::string(tfn.name.begin(), tfn.name.end()),
                            static_cast<int>(publish_attributes.forward));

                return quicr::PublishResponse{ { .forward = true }, handler };
            }

            return quicr::Unexpected<quicr::Error<quicr::PublishErrorCode>>(quicr::PublishErrorCode::kInternalError,
                                                                            "No matching subscribe track");
        }

        void StatusChanged(const std::shared_ptr<quicr::Session>& session, quicr::Session::Status status) override
        {
            switch (status) {
                case quicr::Session::Status::kReady: {
                    SPDLOG_INFO("Client status - kReady");
                    inif_.load(config_file_);

                    std::vector<std::shared_ptr<PerfPublishTrackHandler>> pubs_to_start;
                    std::vector<std::shared_ptr<quicr::SubscribeNamespaceHandler>> nss_to_start;

                    {
                        std::lock_guard<std::mutex> _(mutex_);

                        for (const auto& [section_name, _] : inif_) {
                            auto pub_handler = pub_track_handlers_.emplace_back(
                              PerfPublishTrackHandler::Create(section_name, inif_, instance_id_ + (meeting_id_ * 1000)));
                            pubs_to_start.push_back(pub_handler);
                        }

                        for (std::uint32_t i = 1; i <= instances_; ++i) {
                            if (i == instance_id_) {
                                continue;
                            }

                            for (const auto& [section_name, _] : inif_) {
                                auto sub_handler = sub_track_handlers_.emplace_back(PerfSubscribeTrackHandler::Create(
                                  section_name, inif_, i + (meeting_id_ * 1000), timeout_grace_ms_));

                                sub_handler->SetPublishInitiated();

                                nss_to_start.push_back(quicr::SubscribeNamespaceHandler::Create(
                                  sub_handler->GetFullTrackName().name_space,
                                  quicr::SubscribeNamespaceHandler::Mode::kTracks));
                            }
                        }
                    }

                    // Session calls can deliver PublishReceived on this thread; do not hold mutex_.
                    for (const auto& pub_handler : pubs_to_start) {
                        session->PublishTrack(pub_handler);
                    }

                    for (const auto& sub_ns : nss_to_start) {
                        session->SubscribeNamespace(sub_ns);
                    }

                    break;
                }
                case quicr::Session::Status::kNotReady:
                    SPDLOG_INFO("Client status - kNotReady");
                    break;
                case quicr::Session::Status::kConnecting:
                    SPDLOG_INFO("Client status - kConnecting");
                    break;
                case quicr::Session::Status::kNotConnected:
                    SPDLOG_INFO("Client status - kNotConnected - terminate");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kPendingServerSetup:
                    SPDLOG_INFO("Client status - kPendingSeverSetup");
                    break;

                case quicr::Session::Status::kFailedToConnect:
                    SPDLOG_ERROR("Client status - kFailedToConnect");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kInternalError:
                    SPDLOG_ERROR("Client status - kInternalError");
                    terminate_ = true;
                    break;
                case quicr::Session::Status::kInvalidParams:
                    SPDLOG_ERROR("Client status - kInvalidParams");
                    terminate_ = true;
                    break;
                default:
                    SPDLOG_ERROR("Connection failed {0}", static_cast<int>(status));
                    terminate_ = true;
                    break;
            }
        }

        bool HandlersComplete() override
        {
            bool complete = false;
            {
                std::lock_guard<std::mutex> _(mutex_);

                if (sub_track_handlers_.empty() || pub_track_handlers_.empty()) {
                    complete = false;
                } else {
                    complete = true;
                    for (auto handler : pub_track_handlers_) {
                        if (!handler->IsComplete()) {
                            complete = false;
                            break;
                        }
                    }

                    if (complete) {
                        for (auto handler : sub_track_handlers_) {
                            if (!handler->IsComplete() && !handler->HasTimedOut()) {
                                complete = false;
                                break;
                            }
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return complete;
        }

        void Terminate(const std::shared_ptr<quicr::Session>& session) override
        {
            if (terminate_.exchange(true)) {
                return;
            }

            std::lock_guard<std::mutex> _(mutex_);

            for (auto handler : sub_track_handlers_) {
                SPDLOG_INFO("unsubscribe track {}", handler->TestName());
                session->UnsubscribeTrack(handler);
            }

            for (auto handler : pub_track_handlers_) {
                handler->StopWriter();
                session->UnpublishTrack(handler);
            }
        }

      private:
        std::uint32_t meeting_id_;
        std::uint32_t instance_id_;
        std::uint32_t instances_;
        std::uint64_t timeout_grace_ms_;

        std::vector<std::shared_ptr<PerfSubscribeTrackHandler>> sub_track_handlers_;
        std::vector<std::shared_ptr<PerfPublishTrackHandler>> pub_track_handlers_;
    };

}
