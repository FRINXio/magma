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
  boost::recursive_mutex::scoped_lock scoped_lock(mutex);
  return executeSomething(
      cmd, "QCli.executeAndRead", [=]() { return cli->executeAndRead(cmd); });
}

Future<string> QueuedCli::execute(const Command& cmd) {
  boost::recursive_mutex::scoped_lock scoped_lock(mutex);
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
// TODO: refactor lambdas into separate methods
void QueuedCli::triggerDequeue() {
  if (shutdown)
    return;
  // switch to consumer thread
  via(serialExecutorKeepAlive, [dis = shared_from_this()]() {
    MLOG(MDEBUG) << "[" << dis->id << "] "
                 << "QCli.isProcessing:" << dis->isProcessing
                 << ", queue size:" << dis->queue.size();
    // do nothing if still waiting for remote device to respond
    if (!dis->isProcessing) {
      QueueEntry queueEntry;
      if (dis->queue.try_dequeue(queueEntry)) {
        dis->isProcessing = true;
        Future<string> cliFuture = queueEntry.obtainFutureFromCli();
        MLOG(MDEBUG) << "[" << dis->id << "] " << queueEntry.loggingPrefix
                     << " dequeued ('" << queueEntry.command
                     << "') and cli future obtained";
        if (dis->shutdown) {
          queueEntry.promise->setException(
              runtime_error("QCli: Shutting down"));
          return;
        }
        move(cliFuture)
            .via(dis->serialExecutorKeepAlive)
            .then(
                dis->serialExecutorKeepAlive,
                [dis, queueEntry](std::string result) -> Future<Unit> {
                  // after cliFuture completes, finish processing on consumer
                  // thread
                  MLOG(MDEBUG)
                      << "[" << dis->id << "] " << queueEntry.loggingPrefix
                      << " finished ('" << queueEntry.command
                      << "') with result '" << result << "'";
                  dis->isProcessing = false;
                  queueEntry.promise->setValue(result);
                  dis->triggerDequeue();
                  return Unit{};
                })
            .thenError(
                folly::tag_t<std::exception>{},
                [dis, queueEntry](std::exception const& e) -> Future<Unit> {
                  MLOG(MDEBUG)
                      << "[" << dis->id << "] " << queueEntry.loggingPrefix
                      << " failed ('" << queueEntry.command
                      << "')  with exception '" << e.what() << "'";
                  if (!dis->shutdown) {
                    dis->isProcessing = false;
                  }
                  auto cpException = runtime_error(e.what());
                  MLOG(MDEBUG)
                      << "[" << dis->id << "] " << queueEntry.loggingPrefix
                      << " copied exception " << cpException.what();
                  // TODO: exception type is not preserved,
                  // queueEntry.promise->setException(e) results in
                  // std::exception
                  queueEntry.promise->setException(cpException);
                  dis->triggerDequeue();
                  return Unit{};
                });
      }
    }
  });
}
} // namespace devmand::channels::cli
