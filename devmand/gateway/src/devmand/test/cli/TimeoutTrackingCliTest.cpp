// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#define LOG_WITH_GLOG
#include <magma_logging.h>

#include <devmand/cartography/DeviceConfig.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/MockCli.h>
#include <devmand/test/cli/utils/Ssh.h>
#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>
#include <chrono>

namespace devmand {
namespace test {
namespace cli {

using namespace devmand::channels::cli;
using namespace devmand::test::utils::cli;
using namespace std;

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

} // namespace cli
} // namespace test
} // namespace devmand
