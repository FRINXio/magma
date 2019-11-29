// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#define LOG_WITH_GLOG
#include <magma_logging.h>

#include <devmand/cartography/DeviceConfig.h>
#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/cli/IoConfigurationBuilder.h>
#include <devmand/channels/cli/PromptAwareCli.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <devmand/devices/Device.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/MockCli.h>
#include <devmand/test/cli/utils/Ssh.h>
#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/ThreadWheelTimekeeper.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>

namespace devmand {
namespace test {
namespace cli {

using namespace devmand::channels::cli;
using namespace devmand::test::utils::cli;
using namespace std;
using namespace folly;
using namespace std::chrono_literals;
using devmand::Application;
using devmand::cartography::ChannelConfig;
using devmand::cartography::DeviceConfig;
using devmand::devices::Device;
using devmand::devices::State;
using devmand::test::utils::cli::AsyncCli;
using devmand::test::utils::cli::EchoCli;
using folly::CPUThreadPoolExecutor;
using std::atomic_bool;
using std::make_shared;
using namespace devmand::test::utils::ssh;

class PromptAwareCliTest : public ::testing::Test {
 protected:
  shared_ptr<CPUThreadPoolExecutor> testExec;

  void SetUp() override {
    devmand::test::utils::log::initLog();
    devmand::test::utils::ssh::initSsh();
    testExec = make_shared<CPUThreadPoolExecutor>(1);
  }

  void TearDown() override {
    MLOG(MDEBUG) << "Waiting for test executor to finish";
    testExec->join();
  }
};

class MockSession : public SessionAsync {
 private:
  int counter = 0;
  shared_ptr<CPUThreadPoolExecutor> testExec;

 public:
  MockSession(shared_ptr<CPUThreadPoolExecutor> _testExec)
      : testExec(_testExec){};

  virtual Future<Unit> write(const string& command) {
    (void)command;
    return via(testExec.get(), [command]() {
      if (command == "error") {
        throw std::runtime_error("error");
      }
      return unit;
    });
  };

  virtual Future<string> read(int timeoutMillis) {
    (void)timeoutMillis;
    counter++;
    return via(testExec.get(), [c = counter]() {
      if (c == 1) {
        return "PROMPT>\nPROMPT>";
      }
      return "";
    });
  };

  virtual Future<string> readUntilOutput(const string& lastOutput) {
    (void)lastOutput;
    return via(testExec.get(), []() { return ""; });
  };
};

static shared_ptr<PromptAwareCli> getCli(
    shared_ptr<CPUThreadPoolExecutor> testExec) {
  shared_ptr<MockSession> ptr = make_shared<MockSession>(testExec);
  return PromptAwareCli::make(
      "test",
      ptr,
      CliFlavour::create(""),
      std::make_shared<folly::IOThreadPoolExecutor>(
          1, std::make_shared<folly::NamedThreadFactory>("rccli")));
}

TEST_F(PromptAwareCliTest, cleanDestructOnSuccess) {
  auto testedCli = getCli(testExec);

  SemiFuture<string> future =
      testedCli->executeRead(ReadCommand::create("returning"));

  // Destruct cli
  testedCli.reset();

  ASSERT_EQ(move(future).via(testExec.get()).get(10s), "");
}

TEST_F(PromptAwareCliTest, cleanDestructOnWriteSuccess) {
  auto testedCli = getCli(testExec);

  SemiFuture<string> future =
      testedCli->executeWrite(WriteCommand::create("returning"));

  // Destruct cli
  testedCli.reset();

  ASSERT_EQ(move(future).via(testExec.get()).get(10s), "returning");
}

TEST_F(PromptAwareCliTest, cleanDestructOnError) {
  auto testedCli = getCli(testExec);

  SemiFuture<string> future =
      testedCli->executeRead(ReadCommand::create("error"));

  // Destruct cli
  testedCli.reset();

  EXPECT_ANY_THROW(move(future).via(testExec.get()).get(10s));
}

TEST_F(PromptAwareCliTest, promptAwareCli) {
  atomic_bool exceptionCaught(false);
  shared_ptr<IOThreadPoolExecutor> executor =
      std::make_shared<IOThreadPoolExecutor>(8);
  shared_ptr<Cli> cli = IoConfigurationBuilder::createPromptAwareCli(
                            executor,
                            IoConfigurationBuilder::makeConnectionParameters(
                                "localtest",
                                "localhost",
                                "root",
                                "root",
                                "",
                                9999,
                                chrono::seconds(5),
                                chrono::seconds(5)))
                            .get();

  Future<string> future =
      cli->executeRead(ReadCommand::create("echo 123"))
          .via(executor.get())
          .thenError(
              tag_t<std::runtime_error>{},
              [&exceptionCaught](std::runtime_error const& e) {
                MLOG(MDEBUG) << "Read completed with error: " << e.what();
                exceptionCaught.store(true);
                return Future<string>(e);
              });

  cli.reset(); // we destroy the connection

  // Assert the future completed exceptionally
  for (int i = 0; i < 20; i++) {
    if (exceptionCaught.load()) {
      return;
    } else {
      this_thread::sleep_for(chrono::seconds(1));
    }
  }

  FAIL();
}

} // namespace cli
} // namespace test
} // namespace devmand
