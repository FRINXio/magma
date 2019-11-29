// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/Command.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <magma_logging.h>

namespace devmand::channels::cli {

using namespace std;
using namespace folly;
using devmand::channels::cli::Command;

shared_ptr<TimeoutTrackingCli> TimeoutTrackingCli::make(
    string id,
    shared_ptr<Cli> cli,
    shared_ptr<folly::ThreadWheelTimekeeper> timekeeper,
    shared_ptr<folly::Executor> executor,
    std::chrono::milliseconds timeoutInterval) {
  return shared_ptr<TimeoutTrackingCli>(
      new TimeoutTrackingCli(id, cli, timekeeper, executor, timeoutInterval));
}

TimeoutTrackingCli::TimeoutTrackingCli(
    string _id,
    shared_ptr<Cli> _cli,
    shared_ptr<folly::ThreadWheelTimekeeper> _timekeeper,
    shared_ptr<folly::Executor> _executor,
    std::chrono::milliseconds _timeoutInterval)
    : id(_id),
      cli(_cli),
      timekeeper(_timekeeper),
      executor(_executor),
      timeoutInterval(_timeoutInterval) {
  shutdown = false;
}

TimeoutTrackingCli::~TimeoutTrackingCli() {
  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli started";
  shutdown = true;
  cli = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli cli nulled";
  executor = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli executor nulled";
  timekeeper = nullptr;

  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli done";
}

Future<string> TimeoutTrackingCli::executeRead(const ReadCommand cmd) {
  return executeSomething(cmd, "TTCli.executeRead", [this, cmd]() {
    return cli->executeRead(cmd);
  });
}

Future<string> TimeoutTrackingCli::executeWrite(const WriteCommand cmd) {
  return executeSomething(cmd, "TTCli.executeWrite", [this, cmd]() {
    return cli->executeWrite(cmd);
  });
}

Future<string> TimeoutTrackingCli::executeSomething(
    const Command& cmd,
    const string&& loggingPrefix,
    const function<Future<string>()>& innerFunc) {
  MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix << "('" << cmd
               << "') called";
  if (shutdown) {
    return Future<string>(runtime_error("TTCli Shutting down"));
  }
  Future<string> inner =
      innerFunc(); // we expect that this method does not block
  MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix << "('" << cmd
               << "') obtained future from underlying cli";
  return move(inner)
      .via(executor.get())
      .onTimeout(
          timeoutInterval,
          [_id = this->id, loggingPrefix, cmd](...) -> Future<string> {
            MLOG(MDEBUG) << "[" << _id << "] " << loggingPrefix << "('" << cmd
                         << "') timing out";
            throw FutureTimeout();
          },
          timekeeper.get());
}

} // namespace devmand::channels::cli
