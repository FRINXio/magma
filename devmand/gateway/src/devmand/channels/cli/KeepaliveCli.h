// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <boost/thread/mutex.hpp>
#include <devmand/channels/cli/CancelableWTCallback.h>
#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/cli/CliThreadWheelTimekeeper.h>
#include <folly/Executor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/futures/Future.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace devmand::channels::cli {
using namespace std;
using devmand::channels::cli::CancelableWTCallback;
using devmand::channels::cli::CliThreadWheelTimekeeper;
using devmand::channels::cli::Command;

static constexpr chrono::seconds defaultKeepaliveInterval = chrono::seconds(60);

// CLI layer that should be above QueuedCli. Periodically schedules keepalive
// command to prevent dropping
// of inactive connection.
class KeepaliveCli : public Cli {
 public:
  static shared_ptr<KeepaliveCli> make(
      string id,
      shared_ptr<Cli> _cli,
      shared_ptr<folly::Executor> parentExecutor,
      shared_ptr<CliThreadWheelTimekeeper> _timekeeper,
      chrono::milliseconds heartbeatInterval = defaultKeepaliveInterval,
      string keepAliveCommand = "",
      chrono::milliseconds backoffAfterKeepaliveTimeout = chrono::seconds(5));

  ~KeepaliveCli() override;

  folly::SemiFuture<std::string> executeRead(const ReadCommand cmd) override;

  folly::SemiFuture<std::string> executeWrite(const WriteCommand cmd) override;

 private:
  struct KeepaliveParameters {
    string id;
    shared_ptr<Cli> cli; // underlying cli layer
    shared_ptr<CliThreadWheelTimekeeper> timekeeper;
    shared_ptr<folly::Executor> parentExecutor;
    folly::Executor::KeepAlive<folly::SerialExecutor> serialExecutorKeepAlive;
    string keepAliveCommand;
    chrono::milliseconds heartbeatInterval;
    chrono::milliseconds backoffAfterKeepaliveTimeout;
    atomic<bool> shutdown;
    boost::mutex mutex;
    shared_ptr<CancelableWTCallback> cb;
    KeepaliveParameters(KeepaliveParameters&&) = default;

   public:
    void setCurrentCallback(shared_ptr<CancelableWTCallback> _cb) {
      boost::mutex::scoped_lock scoped_lock(this->mutex);
      this->cb = _cb;
    }
  };

  shared_ptr<KeepaliveParameters> keepaliveParameters;

  KeepaliveCli(
      string id,
      shared_ptr<Cli> _cli,
      shared_ptr<folly::Executor> parentExecutor,
      shared_ptr<CliThreadWheelTimekeeper> _timekeeper,
      chrono::milliseconds heartbeatInterval,
      string keepAliveCommand,
      chrono::milliseconds backoffAfterKeepaliveTimeout);

  static void triggerSendKeepAliveCommand(
      shared_ptr<KeepaliveParameters> keepaliveParameters);
};
} // namespace devmand::channels::cli
