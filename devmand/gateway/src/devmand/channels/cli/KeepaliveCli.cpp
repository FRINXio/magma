// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/Command.h>
#include <devmand/channels/cli/KeepaliveCli.h>
#include <magma_logging.h>

namespace devmand::channels::cli {

using devmand::channels::cli::Command;
using namespace std;
using namespace folly;

KeepaliveCli::KeepaliveCli(
    shared_ptr<Cli> _cli,
    shared_ptr<Executor> _parentExecutor,
    shared_ptr<ThreadWheelTimekeeper> _timekeeper,
    Command&& _keepAliveCommand,
    chrono::milliseconds _heartbeatInterval,
    chrono::milliseconds _backoffAfterKeepaliveTimeout)
    : cli(_cli),
      timekeeper(_timekeeper),
      parentExecutor(_parentExecutor),
      serialExecutorKeepAlive(SerialExecutor::create(
          Executor::getKeepAliveToken(_parentExecutor.get()))),
      keepAliveCommand(_keepAliveCommand),
      heartbeatInterval(_heartbeatInterval),
      backoffAfterKeepaliveTimeout(_backoffAfterKeepaliveTimeout) {
  assert(_keepAliveCommand.skipCache());
  shutdown = false;

  //  sendKeepAliveCommand();
  MLOG(MDEBUG) << "KACli initialized";
}

KeepaliveCli::~KeepaliveCli() {
  MLOG(MDEBUG) << "~KACli";
  shutdown = true;
  serialExecutorKeepAlive = nullptr;
  parentExecutor = nullptr;
  cli = nullptr;
  timekeeper = nullptr;
  MLOG(MDEBUG) << "~KACli done";
}

void KeepaliveCli::sendKeepAliveCommand() {
  if (shutdown)
    return;
  MLOG(MDEBUG) << "KACli: sendKeepAliveCommand()";
  Future<string> result = via(serialExecutorKeepAlive).thenValue([=](auto) {
    if (shutdown)
      throw runtime_error("KACli: Shutting down");
    MLOG(MDEBUG) << "KACli: sendKeepAliveCommand executing keepalive command";
    return this->executeAndRead(this->keepAliveCommand);
  });

  scheduleNextPing(move(result));
  MLOG(MDEBUG) << "KACli: sendKeepAliveCommand() done";
}

void KeepaliveCli::scheduleNextPing(Future<string> keepAliveCmdFuture) {
  if (shutdown)
    return;
  MLOG(MDEBUG) << "KACli: scheduleNextPing";
  move(keepAliveCmdFuture)
      .via(serialExecutorKeepAlive)
      .thenValue([=](auto) -> SemiFuture<Unit> {
        MLOG(MDEBUG) << "KACli: Creating sleep future";
        return futures::sleep(this->heartbeatInterval, timekeeper.get());
      })
      .thenValue([=](auto) -> Unit {
        MLOG(MDEBUG) << "KACli: Woke up after sleep";
        this->sendKeepAliveCommand();
        return Unit{};
      })
      .thenError(
          folly::tag_t<std::exception>{},
          [&](std::exception const& e) -> Future<Unit> {
            LOG(INFO)
                << "KACli: Got error running keepalive, backing off "
                << e.what(); // FIXME: real exception is not propagated here
            if (shutdown)
              return Unit{}; // sleep might hang
            std::this_thread::sleep_for(
                backoffAfterKeepaliveTimeout); // TODO: sleep via futures if
                                               // possible
            MLOG(MDEBUG) << "KACli: Woke up after backing off";
            this->sendKeepAliveCommand();
            return Unit{};
          });
}

Future<string> KeepaliveCli::executeAndRead(const Command& cmd) {
  return cli->executeAndRead(cmd);
}

Future<string> KeepaliveCli::execute(const Command& cmd) {
  return cli->execute(cmd);
}
} // namespace devmand::channels::cli
