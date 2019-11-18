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

QueuedCli::QueuedCli(
    string _id,
    shared_ptr<Cli> _cli,
    shared_ptr<Executor> _parentExecutor)
    : id(_id),
      cli(_cli),
      parentExecutor(_parentExecutor),
      serialExecutorKeepAlive(SerialExecutor::create(
          Executor::getKeepAliveToken(_parentExecutor.get()))) {
  isProcessing = false;
  shutdown = false;
}

QueuedCli::~QueuedCli() {
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli";
  shutdown = true;
  isProcessing = true;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli: dequeuing " << queue.size() << " items";
  QueueEntry queueEntry;
  while (queue.try_dequeue(queueEntry)) {
    queueEntry.promise->setException(runtime_error("QCli: Shutting down"));
  }
  serialExecutorKeepAlive = nullptr;
  parentExecutor = nullptr;
  cli = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~QCli done";
}

Future<string> QueuedCli::executeAndRead(const Command& cmd) {
  return executeSomething(
      cmd, "QCli.executeAndRead", [=]() { return cli->executeAndRead(cmd); });
}

Future<string> QueuedCli::execute(const Command& cmd) {
  Command command = cmd;
  if (!command.isMultiCommand()) {
    MLOG(MWARNING) << "[" << id << "] "
                   << "Called execute with a single command " << cmd
                   << ", executeAndRead() should have been used";
    return executeAndRead(command);
  }

  const vector<Command>& commands = command.splitMultiCommand();
  vector<Future<string>> commmandsFutures;

  for (unsigned long i = 0; i < (commands.size() - 1); i++) {
    commmandsFutures.emplace_back(
        executeSomething(commands.at(i), "QCli.execute", [=]() {
          return cli->execute(commands.at(i));
        }));
  }

  commmandsFutures.emplace_back(executeAndRead(commands.back()));
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
  MLOG(MDEBUG) << "[" << id << "] " << prefix << " adding to queue ('" << cmd
               << "')";
  shared_ptr<Promise<string>> promise = std::make_shared<Promise<string>>();
  QueueEntry queueEntry;
  queueEntry.obtainFutureFromCli = move(innerFunc);
  queueEntry.promise = promise;
  queueEntry.command = cmd.toString();
  queueEntry.loggingPrefix = prefix;
  queue.enqueue(move(queueEntry));
  if (!isProcessing) {
    triggerDequeue();
  }
  return promise->getFuture();
}

/*
 * Start queue reading on consumer thread if queue contains new items.
 * It is safe to call this method anytime, it is thread safe.
 */
void QueuedCli::triggerDequeue() {
  if (shutdown)
    return;
  // switch to consumer thread
  via(serialExecutorKeepAlive, [=]() {
    MLOG(MDEBUG) << "[" << id << "] "
                 << "QCli.isProcessing:" << this->isProcessing
                 << ", queue size:" << queue.size();
    // do nothing if still waiting for remote device to respond
    if (!this->isProcessing) {
      QueueEntry queueEntry;
      if (queue.try_dequeue(queueEntry)) {
        isProcessing = true;
        Future<string> cliFuture = queueEntry.obtainFutureFromCli();
        MLOG(MDEBUG) << "[" << id << "] " << queueEntry.loggingPrefix
                     << " dequeued ('" << queueEntry.command
                     << "') and cli future obtained";
        if (shutdown)
          return;
        move(cliFuture)
            .via(serialExecutorKeepAlive)
            .then(
                serialExecutorKeepAlive,
                [this, queueEntry](std::string result) -> Future<Unit> {
                  // after cliFuture completes, finish processing on consumer
                  // thread
                  MLOG(MDEBUG) << "[" << id << "] " << queueEntry.loggingPrefix
                               << " finished ('" << queueEntry.command
                               << "') with result '" << result << "'";
                  isProcessing = false;
                  queueEntry.promise->setValue(result);
                  triggerDequeue();
                  return Unit{};
                })
            .thenError(
                folly::tag_t<std::exception>{},
                [this, queueEntry](std::exception const& e) -> Future<Unit> {
                  MLOG(MDEBUG) << "[" << id << "] " << queueEntry.loggingPrefix
                               << " failed ('" << queueEntry.command
                               << "')  with exception '" << e.what() << "'";
                  if (!shutdown) {
                    isProcessing = false;
                  }
                  auto cpException = runtime_error(e.what());
                  MLOG(MDEBUG) << "[" << id << "] " << queueEntry.loggingPrefix
                               << " copied exception " << cpException.what();
                  // TODO: exception type is not preserved,
                  // queueEntry.promise->setException(e) results in
                  // std::exception
                  queueEntry.promise->setException(cpException);
                  triggerDequeue();
                  return Unit{};
                });
      }
    }
  });
}
} // namespace devmand::channels::cli
