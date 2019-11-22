// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <devmand/channels/cli/Command.h>
#include <devmand/channels/cli/SshSession.h>
#include <event2/event.h>
#include <folly/executors/IOExecutor.h>
#include <folly/executors/SerialExecutor.h>
#include <folly/futures/Future.h>

namespace devmand {
namespace channels {
namespace cli {
namespace sshsession {

using boost::lockfree::capacity;
using boost::lockfree::spsc_queue;
using devmand::channels::cli::sshsession::SshSession;
using folly::Future;
using folly::IOExecutor;
using folly::makeFuture;
using folly::Promise;
using folly::SerialExecutor;
using folly::Unit;
using folly::via;
using std::condition_variable;
using std::mutex;
using std::shared_ptr;
using std::string;

void readCallback(evutil_socket_t fd, short, void* ptr);

class SshSessionAsync {
 private:
  folly::Executor::KeepAlive<SerialExecutor> executor;
  SshSession session;
  event* sessionEvent = nullptr;
  spsc_queue<string, capacity<200>> readQueue;
  std::atomic_bool reading;
  std::atomic_bool matchingExpectedOutput;
  struct ReadingState {
    shared_ptr<Promise<string>> promise;
    string currentLastOutput;
    string outputSoFar;
  } readingState;

 public:
  explicit SshSessionAsync(string _id, shared_ptr<IOExecutor> _executor);
  ~SshSessionAsync();

  Future<Unit> openShell(
      const string& ip,
      int port,
      const string& username,
      const string& password);
  Future<Unit> write(const string& command);
  Future<string> read(
      int timeoutMillis); // for clearing ssh channel and prompt resolving
  Future<string> readUntilOutput(const string& lastOutput);
  void setEvent(event*);
  void readSshDataToBuffer();
  void processDataInBuffer();
  socket_t getSshFd();
  void matchExpectedOutput();
};

} // namespace sshsession
} // namespace cli
} // namespace channels
} // namespace devmand
