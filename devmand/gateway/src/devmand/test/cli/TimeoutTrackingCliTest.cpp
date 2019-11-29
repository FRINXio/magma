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
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <devmand/devices/Device.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/MockCli.h>
#include <devmand/test/cli/utils/Ssh.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/ThreadWheelTimekeeper.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>

namespace devmand {
namespace test {
namespace cli {

using namespace devmand::test::utils::cli;
using namespace std;
using namespace std::chrono_literals;
using namespace devmand::channels::cli;
using devmand::Application;
using devmand::cartography::ChannelConfig;
using devmand::cartography::DeviceConfig;
using devmand::devices::Device;
using devmand::devices::State;
using devmand::test::utils::cli::AsyncCli;
using devmand::test::utils::cli::EchoCli;
using folly::CPUThreadPoolExecutor;
using namespace devmand::test::utils::ssh;

class TimeoutCliTest : public ::testing::Test {
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

static const chrono::milliseconds timeout = 1000ms;

static shared_ptr<TimeoutTrackingCli> getCli(shared_ptr<Cli> delegate) {
  shared_ptr<TimeoutTrackingCli> cli = TimeoutTrackingCli::make(
      "test",
      delegate,
      make_shared<folly::ThreadWheelTimekeeper>(),
      make_shared<CPUThreadPoolExecutor>(1),
      timeout);
  return cli;
}

TEST_F(TimeoutCliTest, cleanDestructOnTimeout) {
  shared_ptr<AsyncCli> delegate = getMockCli<EchoCli>(3, testExec);
  auto testedCli = getCli(delegate);

  SemiFuture<string> future =
      testedCli->executeRead(ReadCommand::create("not returning"));

  // Destruct cli
  testedCli.reset();

  EXPECT_ANY_THROW({ move(future).via(testExec.get()).get(10s); });
}

TEST_F(TimeoutCliTest, cleanDestructOnError) {
  shared_ptr<AsyncCli> delegate = getMockCli<ErrCli>(0, testExec);
  auto testedCli = getCli(delegate);

  SemiFuture<string> future =
      testedCli->executeRead(ReadCommand::create("not returning"));

  // Destruct cli
  testedCli.reset();

  EXPECT_ANY_THROW({ move(future).via(testExec.get()).get(10s); });
}

TEST_F(TimeoutCliTest, cleanDestructOnSuccess) {
  shared_ptr<AsyncCli> delegate = getMockCli<EchoCli>(0, testExec);
  auto testedCli = getCli(delegate);

  SemiFuture<string> future =
      testedCli->executeRead(ReadCommand::create("returning"));

  // Destruct cli
  testedCli.reset();

  ASSERT_EQ(move(future).via(testExec.get()).get(10s), "returning");
}

TEST_F(TimeoutCliTest, DISABLED_cleanDestruct) {
  shared_ptr<CPUThreadPoolExecutor> mockCliExecutor =
      make_shared<CPUThreadPoolExecutor>(2);
  vector<unsigned int> durations = {5};
  auto delegate =
      make_shared<AsyncCli>(make_shared<EchoCli>(), mockCliExecutor, durations);
  shared_ptr<CPUThreadPoolExecutor> executor =
      make_shared<CPUThreadPoolExecutor>(1);
  shared_ptr<folly::ThreadWheelTimekeeper> timekeeper =
      make_shared<folly::ThreadWheelTimekeeper>();
  shared_ptr<TimeoutTrackingCli> cli =
      TimeoutTrackingCli::make("test", delegate, timekeeper, executor, 1000ms);
  Future<string> future =
      cli->executeRead(ReadCommand::create("not returning"))
          .via(mockCliExecutor.get())
          .thenError(tag_t<FutureTimeout>{}, [](FutureTimeout const& e) {
            MLOG(MDEBUG) << "Read completed with error: " << e.what();
            return Future<string>(e);
          });
  executor.reset();
  timekeeper.reset();
  cli.reset();
  // Wait for async session to finish
  mockCliExecutor->join();
  // Assert future finished
  ASSERT_EQ(true, future.isReady());
  ASSERT_EQ(true, future.hasException());
}

} // namespace cli
} // namespace test
} // namespace devmand
