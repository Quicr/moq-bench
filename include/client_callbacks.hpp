#pragma once

#include "inicpp.h"

#include <quicr/session_callbacks.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace moqbench {

    class PerfClientCallbacks : public quicr::Session::ClientCallbacks
    {
      public:
        PerfClientCallbacks(const std::string& config_file)
          : terminate_(false)
          , config_file_(config_file)
        {
        }

        void StatusChanged(const std::shared_ptr<quicr::Session>& session, quicr::Session::Status status) override = 0;

        virtual bool HandlersComplete() = 0;

        virtual void Terminate(const std::shared_ptr<quicr::Session>& session) = 0;

        bool ShouldTerminate() const { return terminate_; }

      protected:
        std::atomic_bool terminate_;
        std::string config_file_;
        ini::IniFile inif_;

        mutable std::mutex mutex_;
    };
}
