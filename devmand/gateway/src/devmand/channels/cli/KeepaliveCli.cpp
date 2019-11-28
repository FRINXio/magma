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

shared_ptr<KeepaliveCli> KeepaliveCli::make(
    string id,
    shared_ptr<Cli> cli,
    shared_ptr<folly::Executor> parentExecutor,
    shared_ptr<folly::ThreadWheelTimekeeper> timekeeper,
    chrono::milliseconds heartbeatInterval,
    ReadCommand&& keepAliveCommand,
    chrono::milliseconds backoffAfterKeepaliveTimeout) {
  return shared_ptr<KeepaliveCli>(new KeepaliveCli(
      id,
      cli,
      parentExecutor,
      timekeeper,
      heartbeatInterval,
      move(keepAliveCommand),
      backoffAfterKeepaliveTimeout));
}

KeepaliveCli::KeepaliveCli(
    string _id,
    shared_ptr<Cli> _cli,
    shared_ptr<Executor> _parentExecutor,
    shared_ptr<ThreadWheelTimekeeper> _timekeeper,
    chrono::milliseconds _heartbeatInterval,
    ReadCommand&& _keepAliveCommand,
    chrono::milliseconds _backoffAfterKeepaliveTimeout)
    : id(_id),
      cli(_cli),
      timekeeper(_timekeeper),
      parentExecutor(_parentExecutor),
      serialExecutorKeepAlive(SerialExecutor::create(
          Executor::getKeepAliveToken(_parentExecutor.get()))),
      keepAliveCommand(_keepAliveCommand),
      heartbeatInterval(_heartbeatInterval),
      backoffAfterKeepaliveTimeout(_backoffAfterKeepaliveTimeout) {
  assert(_keepAliveCommand.skipCache());
  shutdown = false;
  MLOG(MDEBUG) << "[" << id << "] "
               << "initialized";
}

KeepaliveCli::~KeepaliveCli() {
  MLOG(MDEBUG) << "[" << id << "] "
               << "Destructor started";
  shutdown = true;
  serialExecutorKeepAlive = nullptr;
  parentExecutor = nullptr;
  cli = nullptr;
  timekeeper = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "Destructor done";
}

void KeepaliveCli::sendKeepAliveCommand() {
  if (shutdown)
    return;
  MLOG(MDEBUG) << "[" << id << "] "
               << "sendKeepAliveCommand";
  Future<string> result =
      via(serialExecutorKeepAlive).thenValue([dis = shared_from_this()](auto) {
        if (dis->shutdown)
          throw runtime_error("KACli: Shutting down");
        MLOG(MDEBUG) << "[" << dis->id << "] "
                     << "sendKeepAliveCommand executing keepalive command";
        return dis->executeRead(dis->keepAliveCommand);
      });

  scheduleNextPing(move(result));
  MLOG(MDEBUG) << "[" << id << "] "
               << "sendKeepAliveCommand() done";
}

void KeepaliveCli::scheduleNextPing(Future<string> keepAliveCmdFuture) {
  if (shutdown)
    return;
  MLOG(MDEBUG) << "[" << id << "] "
               << "scheduleNextPing";
  move(keepAliveCmdFuture)
      .via(serialExecutorKeepAlive)
      .thenValue([dis = shared_from_this()](auto) -> SemiFuture<Unit> {
        MLOG(MDEBUG) << "[" << dis->id << "] "
                     << "Creating sleep future";
        return futures::sleep(dis->heartbeatInterval, dis->timekeeper.get());
      })
      .thenValue([dis = shared_from_this()](auto) -> Unit {
        MLOG(MDEBUG) << "[" << dis->id << "] "
                     << "Woke up after sleep";
        dis->sendKeepAliveCommand();
        return Unit{};
      })
      .thenError(
          folly::tag_t<std::exception>{},
          [dis = shared_from_this()](std::exception const& e) -> Future<Unit> {
            MLOG(MINFO)
                << "[" << dis->id << "] "
                << "Got error running keepalive, backing off "
                << e.what(); // FIXME: real exception is not propagated here
            if (dis->shutdown)
              return Unit{}; // sleep might hang
            std::this_thread::sleep_for(
                dis->backoffAfterKeepaliveTimeout); // TODO: sleep via futures
                                                    // if possible
            MLOG(MDEBUG) << "[" << dis->id << "] "
                         << "Woke up after backing off";
            dis->sendKeepAliveCommand();
            return Unit{};
          });
}

Future<string> KeepaliveCli::executeRead(const ReadCommand cmd) {
  return cli->executeRead(cmd);
}

Future<string> KeepaliveCli::executeWrite(const WriteCommand cmd) {
  return cli->executeWrite(cmd);
}

} // namespace devmand::channels::cli
