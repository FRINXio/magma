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

ReconnectingCli::ReconnectingCli(
    string _id,
    shared_ptr<Executor> _executor,
    std::function<std::shared_ptr<Cli>()>&& _createCliStack)
    : id(_id), executor(_executor), createCliStack(_createCliStack) {
  cli = createCliStack();
}

ReconnectingCli::~ReconnectingCli() {
  MLOG(MDEBUG) << "[" << id << "] "
               << "~RCli";
  createCliStack = nullptr;
  executor = nullptr;
  cli = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~RCli done";
}

Future<string> ReconnectingCli::executeAndRead(const Command& cmd) {
  return executeSomething(
      "RCli.executeAndRead",
      [=]() { return cli->executeAndRead(cmd); },
      cmd.toString());
}

Future<string> ReconnectingCli::execute(const Command& cmd) {
  return executeSomething(
      "RCli.execute", [=]() { return cli->execute(cmd); }, cmd.toString());
}

Future<string> ReconnectingCli::executeSomething(
    const string&& loggingPrefix,
    const function<Future<string>()>& innerFunc,
    const string&& loggingSuffix) {
  return innerFunc()
      .via(executor.get())
      .thenValue([=](string result) -> string {
        MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix
                     << " got result of running '" << loggingSuffix << "'";
        return result;
      })
      .thenError(
          // TODO: implement quiet period so that exeptions while reconnect is
          // in process are ignored
          folly::tag_t<std::exception>{},
          [=](std::exception const& e) -> Future<string> {
            MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix
                         << " got error, recreating cli stack: " << e.what()
                         << " while running '" << loggingSuffix << "'";
            cli = createCliStack();
            MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix
                         << " recreating cli stack done";
            auto cpException = runtime_error(e.what());
            MLOG(MDEBUG) << "[" << id << "] " << loggingPrefix
                         << " copied exception " << cpException.what();
            // TODO: exception type is not preserved,
            // queueEntry.promise->setException(e) results in std::exception
            throw cpException;
          });
}
} // namespace cli
} // namespace channels
} // namespace devmand
