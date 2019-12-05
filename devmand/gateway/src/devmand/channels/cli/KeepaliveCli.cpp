// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/CancelableWTCallback.h>
#include <devmand/channels/cli/CliThreadWheelTimekeeper.h>
#include <devmand/channels/cli/Command.h>
#include <devmand/channels/cli/KeepaliveCli.h>
#include <magma_logging.h>

namespace devmand::channels::cli {

using devmand::channels::cli::CancelableWTCallback;
using devmand::channels::cli::Command;
using namespace std;
using namespace folly;

shared_ptr<KeepaliveCli> KeepaliveCli::make(
    string id,
    shared_ptr<Cli> cli,
    shared_ptr<folly::Executor> parentExecutor,
    shared_ptr<folly::Timekeeper> timekeeper,
    chrono::milliseconds heartbeatInterval,
    string keepAliveCommand,
    chrono::milliseconds backoffAfterKeepaliveTimeout) {
  const shared_ptr<KeepaliveCli>& result =
      shared_ptr<KeepaliveCli>(new KeepaliveCli(
          id,
          cli,
          parentExecutor,
          timekeeper,
          heartbeatInterval,
          move(keepAliveCommand),
          backoffAfterKeepaliveTimeout));
  return result;
}

KeepaliveCli::KeepaliveCli(
    string _id,
    shared_ptr<Cli> _cli,
    shared_ptr<Executor> _parentExecutor,
    shared_ptr<Timekeeper> _timekeeper,
    chrono::milliseconds _heartbeatInterval,
    string _keepAliveCommand,
    chrono::milliseconds _backoffAfterKeepaliveTimeout) {
  keepaliveParameters = shared_ptr<KeepaliveParameters>(new KeepaliveParameters{
      /* id */ _id,
      /* cli */ _cli,
      /* timekeeper */ _timekeeper,
      /* parentExecutor */ _parentExecutor,
      /* serialExecutorKeepAlive */
      SerialExecutor::create(
          Executor::getKeepAliveToken(_parentExecutor.get())),
      /* keepAliveCommand */ move(_keepAliveCommand),
      /* heartbeatInterval */ _heartbeatInterval,
      /* backoffAfterKeepaliveTimeout */ _backoffAfterKeepaliveTimeout,
      /* shutdown */ {false},
      {},
      {}});

  MLOG(MDEBUG) << "[" << _id << "] "
               << "initialized";
  triggerSendKeepAliveCommand(keepaliveParameters);
}

KeepaliveCli::~KeepaliveCli() {
  string id = keepaliveParameters->id;
  keepaliveParameters->shutdown = true;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~KeepaliveCli started";
  while (keepaliveParameters.use_count() >
         1) { // TODO cancel currently running future
    MLOG(MDEBUG) << "[" << id << "] "
                 << "~KeepaliveCli sleeping";
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (keepaliveParameters->cb.use_count() >= 1) {
      keepaliveParameters->cb->callbackCanceled();
      keepaliveParameters->cb.reset();
    }
  }
  keepaliveParameters = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~KeepaliveCli done";
}

void KeepaliveCli::triggerSendKeepAliveCommand(
    shared_ptr<KeepaliveParameters> keepaliveParameters) {
  if (keepaliveParameters->shutdown) {
    MLOG(MDEBUG) << "[" << keepaliveParameters->id << "] "
                 << "triggerSendKeepAliveCommand shutting down";
    return;
  }
  ReadCommand cmd =
      ReadCommand::create(keepaliveParameters->keepAliveCommand, true);
  MLOG(MDEBUG) << "[" << keepaliveParameters->id << "] (" << cmd << ") "
               << "triggerSendKeepAliveCommand created new command";

  via(keepaliveParameters->serialExecutorKeepAlive)
      .thenValue([params = keepaliveParameters, cmd](auto) {
        MLOG(MDEBUG)
            << "[" << params->id << "] (" << cmd << ") "
            << "triggerSendKeepAliveCommand executing keepalive command";

        return params->cli->executeRead(cmd);
      })
      .thenValue([params = keepaliveParameters, cmd](auto) -> SemiFuture<Unit> {
        MLOG(MDEBUG) << "[" << params->id << "] (" << cmd << ") "
                     << "Creating sleep future";
        shared_ptr<CancelableWTCallback> cb =
            std::static_pointer_cast<CliThreadWheelTimekeeper>(
                params->timekeeper)
                ->cancelableSleep(params->heartbeatInterval);
        params->setCurrentCallback(cb);
        return cb->getSemiFuture();
      })
      .thenValue([keepaliveParameters, cmd](auto) -> Unit {
        MLOG(MDEBUG) << "[" << keepaliveParameters->id << "] (" << cmd << ") "
                     << "Woke up after sleep";
        triggerSendKeepAliveCommand(keepaliveParameters);
        return Unit{};
      })
      .thenError([params = keepaliveParameters,
                  cmd](const exception_wrapper& e) {
        MLOG(MINFO) << "[" << params->id << "] (" << cmd << ") "
                    << "Got error running keepalive, backing off " << e.what();

        //thrown by sleep - we terminate prematurely because we are in the destructor
        if (e.is_compatible_with<folly::FutureNoTimekeeper>()) {
            return makeFuture(unit);
        }

        return futures::sleep(
                   params->backoffAfterKeepaliveTimeout,
                   params->timekeeper.get())
            .via(params->serialExecutorKeepAlive)
            .thenValue([params, cmd](auto) -> Unit {
              MLOG(MDEBUG) << "[" << params->id << "] (" << cmd << ") "
                           << "Woke up after backing off";
              triggerSendKeepAliveCommand(params);
              return Unit{};
            });
      });
}

folly::SemiFuture<std::string> KeepaliveCli::executeRead(
    const ReadCommand cmd) {
  return keepaliveParameters->cli->executeRead(cmd);
}

folly::SemiFuture<std::string> KeepaliveCli::executeWrite(
    const WriteCommand cmd) {
  return keepaliveParameters->cli->executeWrite(cmd);
}

} // namespace devmand::channels::cli
