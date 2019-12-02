// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <boost/range/size_type.hpp>
#include <devmand/channels/cli/QueuedCli.h>

namespace devmand::channels::cli {
using namespace std;
using namespace folly;

shared_ptr<QueuedCli> QueuedCli::make(
    string id,
    shared_ptr<Cli> cli,
    shared_ptr<Executor> parentExecutor) {
  return shared_ptr<QueuedCli>(new QueuedCli(id, cli, parentExecutor));
}

QueuedCli::QueuedCli(
    string _id,
    shared_ptr<Cli> _cli,
    shared_ptr<Executor> _parentExecutor) {
  queuedParameters = shared_ptr<QueuedParameters>(new QueuedParameters{
      _id,
      _cli,
      _parentExecutor,
      SerialExecutor::create(
          Executor::getKeepAliveToken(_parentExecutor.get())),
      {},
      {false},
      {false},
      {}});
}

QueuedCli::~QueuedCli() {
  string id = queuedParameters->id;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli started";
  queuedParameters->shutdown = true;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli: dequeuing " << queuedParameters->queue.size()
               << " items";
  {
    QueueEntry queueEntry;
    while (queuedParameters->queue.try_dequeue(queueEntry)) {
      MLOG(MDEBUG) << "[" << id << "] (" << queueEntry.command.getIdx() << ") "
                   << "~QCli: fulfilling promise with exception";
      queueEntry.promise->setException(runtime_error("QCli: Shutting down"));
    }
  } // drop queueEntry to release queuedParameters from its obtain.. function
  while (queuedParameters.use_count() >
         1) { // TODO cancel currently running future
    MLOG(MDEBUG) << "[" << id << "] "
                 << "~QCli sleeping";
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  queuedParameters = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli done";
}

Future<string> QueuedCli::executeRead(const ReadCommand cmd) {
  boost::recursive_mutex::scoped_lock scoped_lock(queuedParameters->mutex);
  return executeSomething(
      cmd, "QCli.executeRead", [params = queuedParameters, cmd]() {
        return params->cli->executeRead(cmd);
      });
}

Future<string> QueuedCli::executeWrite(const WriteCommand cmd) {
  boost::recursive_mutex::scoped_lock scoped_lock(queuedParameters->mutex);
  Command command = cmd;
  if (!command.isMultiCommand()) {
    MLOG(MWARNING) << "[" << queuedParameters->id << "] "
                   << "Called executeWrite with a single command " << cmd
                   << ", executeRead() should have been used";
    return executeRead(ReadCommand::create(cmd)); // TODO what is appropriate?
  }

  const vector<Command>& commands = command.splitMultiCommand();
  vector<Future<string>> commmandsFutures;

  for (unsigned long i = 0; i < (commands.size() - 1); i++) {
    commmandsFutures.emplace_back(
        executeSomething(commands.at(i), "QCli.executeWrite", [=]() {
          return queuedParameters->cli->executeWrite(
              WriteCommand::create(commands.at(i)));
        }));
  }

  commmandsFutures.emplace_back(
      executeRead(ReadCommand::create(commands.back())));
  Future<string> future = reduce(
      commmandsFutures.begin(),
      commmandsFutures.end(),
      string(""),
      [](string s1, string s2) { return s1 + s2; });

  return future;
}

Future<string> QueuedCli::executeSomething(
    const Command& cmd,
    const string& prefix,
    function<Future<string>()> innerFunc) {
  shared_ptr<Promise<string>> promise = std::make_shared<Promise<string>>();
  QueueEntry queueEntry = QueueEntry{move(innerFunc), promise, cmd, prefix};
  MLOG(MDEBUG) << "[" << queuedParameters->id << "] ("
               << queueEntry.command.getIdx() << ") " << prefix
               << " adding to queue ('" << cmd << "')";

  queuedParameters->queue.enqueue(move(queueEntry));
  if (!queuedParameters->isProcessing) {
    triggerDequeue(queuedParameters);
  }
  return promise->getFuture();
}

/*
 * Start queue reading on consumer thread if queue contains new items.
 * It is safe to call this method anytime, it is thread safe.
 */
// TODO: refactor lambdas into separate methods
void QueuedCli::triggerDequeue(shared_ptr<QueuedParameters> queuedParameters) {
  if (queuedParameters->shutdown)
    return;
  // switch to consumer thread
  via(queuedParameters->serialExecutorKeepAlive, [params = queuedParameters]() {
    MLOG(MDEBUG) << "[" << params->id << "] "
                 << "QCli.isProcessing:" << params->isProcessing
                 << ", queue size:" << params->queue.size();
    // do nothing if still waiting for remote device to respond
    if (!params->isProcessing) {
      QueueEntry queueEntry;
      if (params->queue.try_dequeue(queueEntry)) {
        if (params->shutdown) {
          MLOG(MDEBUG) << "[" << params->id << "] ("
                       << queueEntry.command.getIdx() << ") "
                       << queueEntry.loggingPrefix << " Shutting down";
          queueEntry.promise->setException(
              runtime_error("QCli: Shutting down"));
          return;
        }
        params->isProcessing = true;
        Future<string> cliFuture = queueEntry.obtainFutureFromCli();
        MLOG(MDEBUG) << "[" << params->id << "] ("
                     << queueEntry.command.getIdx() << ") "
                     << queueEntry.loggingPrefix
                     << " dequeued and cli future obtained";
        move(cliFuture)
            .via(params->serialExecutorKeepAlive)
            .then(
                params->serialExecutorKeepAlive,
                [params, queueEntry](std::string result) -> Future<Unit> {
                  // after cliFuture completes, finish processing on consumer
                  // thread
                  MLOG(MDEBUG) << "[" << params->id << "] ("
                               << queueEntry.command.getIdx() << ") "
                               << queueEntry.loggingPrefix << " succeeded";
                  params->isProcessing = false;
                  queueEntry.promise->setValue(result);
                  triggerDequeue(params);
                  return Unit{};
                })
            .thenError(
                folly::tag_t<std::exception>{},
                [params, queueEntry](std::exception const& e) -> Future<Unit> {
                  MLOG(MDEBUG)
                      << "[" << params->id << "] ("
                      << queueEntry.command.getIdx() << ") "
                      << queueEntry.loggingPrefix << " failed  with exception '"
                      << e.what() << "'";
                  if (!params->shutdown) {
                    params->isProcessing = false;
                  }
                  auto cpException = runtime_error(e.what());
                  // TODO: exception type is not preserved,
                  // queueEntry.promise->setException(e) results in
                  // std::exception
                  queueEntry.promise->setException(cpException);
                  triggerDequeue(params);
                  return Unit{};
                });
      }
    }
  });
}
} // namespace devmand::channels::cli
