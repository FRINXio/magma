// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/ReconnectingCli.h>
#include <magma_logging.h>

namespace devmand {
namespace channels {
namespace cli {

using namespace std;
using namespace folly;

shared_ptr<ReconnectingCli> ReconnectingCli::make(
    string id,
    shared_ptr<Executor> executor,
    function<shared_ptr<Cli>()>&& createCliStack,
    chrono::milliseconds quietPeriod) {
  return shared_ptr<ReconnectingCli>(new ReconnectingCli(
      id, executor, move(createCliStack), move(quietPeriod)));
}

ReconnectingCli::ReconnectingCli(
    string id,
    shared_ptr<Executor> executor,
    function<std::shared_ptr<Cli>()>&& createCliStack,
    chrono::milliseconds quietPeriod) {
  reconnectParameters = make_shared<ReconnectParameters>();
  reconnectParameters->id = id;
  reconnectParameters->executor = executor;
  reconnectParameters->createCliStack = move(createCliStack);
  reconnectParameters->maybeCli = nullptr;
  reconnectParameters->shutdown = false;
  reconnectParameters->isReconnecting = false;
  reconnectParameters->quietPeriod = quietPeriod;
  // start async (re)connect
  triggerReconnect(reconnectParameters);
}

ReconnectingCli::~ReconnectingCli() {
  reconnectParameters->shutdown = true;
  MLOG(MDEBUG) << "[" << reconnectParameters->id << "] "
               << "~RCli";
}

void ReconnectingCli::triggerReconnect(shared_ptr<ReconnectParameters> params) {
  if (params->shutdown)
    return;
  bool f = false;
  if (params->isReconnecting.compare_exchange_strong(f, true)) {
    via(params->executor.get(),
        [params]() -> Unit {
          MLOG(MDEBUG) << "[" << params->id << "] "
                       << "Recreating cli stack";
          params->maybeCli = nullptr;
          MLOG(MDEBUG) << "[" << params->id << "] "
                       << "Recreating cli stack - destroyed old stack";
          params->maybeCli = params->createCliStack();
          params->isReconnecting = false;
          MLOG(MDEBUG) << "[" << params->id << "] "
                       << "Recreating cli stack - done";
          return Unit{};
        })
        .thenError(
            folly::tag_t<std::exception>{},
            [params](std::exception const& e) -> Unit {
              // quiet period
              MLOG(MDEBUG) << "[" << params->id << "] "
                           << "triggerReconnect got error : " << e.what();
              std::this_thread::sleep_for(
                  params->quietPeriod); // TODO make this via futures::sleep()
              MLOG(MDEBUG)
                  << "[" << params->id << "] "
                  << "triggerReconnect reconnecting after quiet period";
              params->isReconnecting = false;
              triggerReconnect(params);
              return Unit{};
            });
  } else {
    MLOG(MDEBUG) << "[" << params->id << "] "
                 << "Already reconnecting";
  }
}

Future<string> ReconnectingCli::executeAndRead(
    const Command& cmd) { // TODO: cmd lifetime
  // capturing this is ok here - lambda is evaluated synchronously
  return executeSomething(
      "RCli.executeAndRead",
      [cmd](shared_ptr<Cli> cli) { return cli->executeAndRead(cmd); },
      cmd.toString());
}

Future<string> ReconnectingCli::execute(const Command& cmd) {
  // capturing this is ok here - lambda is evaluated synchronously
  return executeSomething(
      "RCli.execute",
      [cmd](shared_ptr<Cli> cli) { return cli->execute(cmd); },
      cmd.toString());
}

Future<string> ReconnectingCli::executeSomething(
    const string&& loggingPrefix,
    const function<Future<string>(shared_ptr<Cli>)>& innerFunc,
    const string&& loggingSuffix) {
  shared_ptr<Cli> cliOrNull = reconnectParameters->maybeCli;
  if (cliOrNull != nullptr) {
    return innerFunc(cliOrNull)
        .via(reconnectParameters->executor.get())
        .thenValue(
            [dis = shared_from_this(), loggingPrefix, loggingSuffix](
                string result) -> string {
              // TODO: move this to deeper layer
              MLOG(MDEBUG) << "[" << dis->reconnectParameters->id << "] "
                           << loggingPrefix << " got result of running '"
                           << loggingSuffix << "'";
              return result;
            })
        .thenError(
            folly::tag_t<std::exception>{},
            [dis = shared_from_this(), loggingPrefix, loggingSuffix](
                std::exception const& e) -> Future<string> {
              MLOG(MDEBUG) << "[" << dis->reconnectParameters->id << "] "
                           << loggingPrefix << " got error : " << e.what()
                           << " while running '" << loggingSuffix << "'";

              dis->triggerReconnect(dis->reconnectParameters);
              auto cpException = runtime_error(e.what());
              MLOG(MDEBUG) << "[" << dis->reconnectParameters->id << "] "
                           << loggingPrefix << " copied exception "
                           << cpException.what();
              // TODO: exception type is not preserved,
              // queueEntry.promise->setException(e) results in std::exception
              throw cpException;
            });
  } else {
    return makeFuture<string>(runtime_error("Not connected"));
  }
}

} // namespace cli
} // namespace channels
} // namespace devmand
