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
    std::chrono::milliseconds _timeoutInterval) {
  timeoutTrackingParameters =
      shared_ptr<TimeoutTrackingParameters>(new TimeoutTrackingParameters{
          _id, _cli, _timekeeper, _executor, _timeoutInterval, {false}});
}

TimeoutTrackingCli::~TimeoutTrackingCli() {
  string id = timeoutTrackingParameters->id;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli started";
  timeoutTrackingParameters->shutdown = true;
  while (timeoutTrackingParameters.use_count() >
         1) { // TODO cancel currently running future
    MLOG(MDEBUG) << "[" << timeoutTrackingParameters->id << "] "
                 << "~TTCli sleeping";
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  timeoutTrackingParameters = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~TTCli done";
}

folly::SemiFuture<std::string> TimeoutTrackingCli::executeRead(
    const ReadCommand cmd) {
  return executeSomething(
             cmd,
             "TTCli.executeRead",
             [this, cmd]() {
               return timeoutTrackingParameters->cli->executeRead(cmd);
             })
      .semi();
}

folly::SemiFuture<std::string> TimeoutTrackingCli::executeWrite(
    const WriteCommand cmd) {
  return executeSomething(cmd, "TTCli.executeWrite", [this, cmd]() {
    return timeoutTrackingParameters->cli->executeWrite(cmd);
  });
}

Future<string> TimeoutTrackingCli::executeSomething(
    const Command& cmd,
    const string&& loggingPrefix,
    const function<SemiFuture<string>()>& innerFunc) {
  MLOG(MDEBUG) << "[" << timeoutTrackingParameters->id << "] (" << cmd.getIdx()
               << ") " << loggingPrefix << "('" << cmd << "') called";
  if (timeoutTrackingParameters->shutdown) {
    return Future<string>(runtime_error("TTCli Shutting down"));
  }
  SemiFuture<string> inner =
      innerFunc(); // we expect that this method does not block
  MLOG(MDEBUG) << "[" << timeoutTrackingParameters->id << "] (" << cmd.getIdx()
               << ") "
               << "Obtained future from underlying cli";
  return move(inner)
      .via(timeoutTrackingParameters->executor.get())
      .onTimeout(
          timeoutTrackingParameters->timeoutInterval,
          [params = timeoutTrackingParameters, cmd](...) -> Future<string> {
            // NOTE: timeoutTrackingParameters must be captured mainly for
            // executor
            MLOG(MDEBUG) << "[" << params->id << "] (" << cmd.getIdx() << ") "
                         << "timing out";
            throw FutureTimeout();
          },
          timeoutTrackingParameters->timekeeper.get())
      .thenValue(
          [params = timeoutTrackingParameters, cmd](string result) -> string {
            MLOG(MDEBUG) << "[" << params->id << "] (" << cmd.getIdx() << ") "
                         << "succeeded";
            return result;
          });
}

} // namespace devmand::channels::cli
