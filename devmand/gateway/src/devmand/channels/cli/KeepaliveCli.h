// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <devmand/channels/cli/Cli.h>
#include <folly/Executor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/futures/Future.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace devmand::channels::cli {
using namespace std;
using devmand::channels::cli::Command;

static constexpr chrono::seconds defaultKeepaliveInterval = chrono::seconds(60);

// CLI layer that should be above QueuedCli. Periodically schedules keepalive
// command to prevent dropping
// of inactive connection.
class KeepaliveCli : public Cli, public enable_shared_from_this<KeepaliveCli> {
 public:
  static shared_ptr<KeepaliveCli> make(
      string id,
      shared_ptr<Cli> _cli,
      shared_ptr<folly::Executor> parentExecutor,
      shared_ptr<folly::ThreadWheelTimekeeper> _timekeeper,
      chrono::milliseconds heartbeatInterval = defaultKeepaliveInterval,
      ReadCommand&& keepAliveCommand = ReadCommand::create("\n", true),
      chrono::milliseconds backoffAfterKeepaliveTimeout = // TODO: remove
      chrono::seconds(5));

  ~KeepaliveCli() override;

  /*
   * Must be called after constructor to kick start keepalive.
   */
  void start();

  folly::Future<string> executeRead(const ReadCommand cmd) override;

  folly::Future<string> executeWrite(const WriteCommand cmd) override;

 private:
  string id;
  shared_ptr<Cli> cli; // underlying cli layer
  shared_ptr<folly::ThreadWheelTimekeeper> timekeeper;
  shared_ptr<folly::Executor> parentExecutor;
  folly::Executor::KeepAlive<folly::SerialExecutor> serialExecutorKeepAlive;
  const ReadCommand keepAliveCommand;
  const chrono::milliseconds heartbeatInterval;
  const chrono::milliseconds backoffAfterKeepaliveTimeout;
  atomic<bool> shutdown;

  KeepaliveCli(
      string id,
      shared_ptr<Cli> _cli,
      shared_ptr<folly::Executor> parentExecutor,
      shared_ptr<folly::ThreadWheelTimekeeper> _timekeeper,
      chrono::milliseconds heartbeatInterval,
      ReadCommand&& keepAliveCommand,
      chrono::milliseconds backoffAfterKeepaliveTimeout // TODO: remove
  );

  void scheduleNextPing(folly::Future<string> keepAliveCmdFuture);

  void sendKeepAliveCommand();
};
} // namespace devmand::channels::cli
