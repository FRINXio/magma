// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#define LOG_WITH_GLOG
#include <magma_logging.h>

#include <boost/algorithm/string/trim.hpp>
#include <devmand/cartography/DeviceConfig.h>
#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/cli/QueuedCli.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/MockCli.h>
#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/Future.h>
#include <gtest/gtest.h>
#include <chrono>

namespace devmand {
namespace test {
namespace cli {

using namespace devmand::channels::cli;
using namespace devmand::test::utils::cli;
using namespace std;

shared_ptr<CPUThreadPoolExecutor> executor =
    make_shared<CPUThreadPoolExecutor>(8);

class QueuedCliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    devmand::test::utils::log::initLog();
  }
};

TEST_F(QueuedCliTest, queuedCli) {
  vector<unsigned int> durations = {2};
  shared_ptr<QueuedCli> cli = QueuedCli::make(
      "testConnection",
      make_shared<AsyncCli>(make_shared<EchoCli>(), executor, durations),
      executor);

  vector<string> results{"one", "two", "three", "four", "five", "six"};

  // create requests
  vector<Command> cmds;
  for (const auto& str : results) {
    cmds.push_back(Command::makeReadCommand(str));
  }

  // send requests
  vector<folly::Future<string>> futures;
  for (const auto& cmd : cmds) {
    MLOG(MDEBUG) << "Executing command '" << cmd;
    futures.push_back(cli->executeAndRead(cmd));
  }

  // collect values
  const vector<folly::Try<string>>& values =
      collectAll(futures.begin(), futures.end()).get();

  // check values
  EXPECT_EQ(values.size(), results.size());
  for (unsigned int i = 0; i < values.size(); ++i) {
    EXPECT_EQ(boost::algorithm::trim_copy(values[i].value()), results[i]);
  }
}

TEST_F(QueuedCliTest, queueOrderingTest) {
  unsigned int iterations = 200;
  unsigned int parallelThreads = 165;
  shared_ptr<CPUThreadPoolExecutor> queuedCliParallelExecutor =
      make_shared<CPUThreadPoolExecutor>(parallelThreads);
  Command cmd = Command::makeReadCommand("1\n2\n3\n4\n5\n6\n7\n8\n9");
  const shared_ptr<QueuedCli>& cli =
      make_shared<QueuedCli>("testOrder", make_shared<EchoCli>(), executor);

  vector<Future<string>> queuedFutures;

  for (unsigned long i = 0; i < iterations; i++) {
    queuedFutures.emplace_back(folly::via(
        queuedCliParallelExecutor.get(),
        [cli, cmd]() { return cli->execute(cmd); }));
  }

  vector<string> output =
      collect(queuedFutures.begin(), queuedFutures.end()).get();

  for (unsigned long i = 0; i < iterations; i++) {
    EXPECT_EQ(output[i], "123456789");
  }
}

TEST_F(QueuedCliTest, queuedCliMT) {
  const int loopcount = 10;
  vector<unsigned int> durations = {1};

  shared_ptr<QueuedCli> cli = QueuedCli::make(
      "testConnection",
      make_shared<AsyncCli>(make_shared<EchoCli>(), executor, durations),
      executor);

  // create requests
  vector<folly::Future<string>> futures;
  Command cmd = Command::makeReadCommand("hello");
  for (int i = 0; i < loopcount; ++i) {
    MLOG(MDEBUG) << "Executing command '" << cmd;
    futures.push_back(
        folly::via(executor.get(), [&]() { return cli->executeAndRead(cmd); }));
  }

  // collect values
  const vector<folly::Try<string>>& values =
      collectAll(futures.begin(), futures.end()).get();

  // check values
  EXPECT_EQ(values.size(), loopcount);
  for (auto v : values) {
    EXPECT_EQ(boost::algorithm::trim_copy(v.value()), "hello");
  }
}

TEST_F(QueuedCliTest, threadSafety) {
  shared_ptr<CPUThreadPoolExecutor> testExec =
      make_shared<CPUThreadPoolExecutor>(32, 1, 100000);

  shared_ptr<QueuedCli> cli =
      QueuedCli::make("testConnection", make_shared<EchoCli>(), executor);

  vector<Future<string>> execs;
  for (int i = 0; i < 100000; i++) {
    Future<string> future = via(testExec.get(), [cli, i]() {
      return cli->executeAndRead(Command::makeReadCommand(to_string(i))).get();
    });
    execs.push_back(std::move(future));
  }

  for (uint i = 0; i < execs.size(); i++) {
    const string& basicString = std::move(execs.at(i)).get();
    MLOG(MDEBUG) << "received " << i;
  }
  MLOG(MDEBUG) << "done";
}

} // namespace cli
} // namespace test
} // namespace devmand
