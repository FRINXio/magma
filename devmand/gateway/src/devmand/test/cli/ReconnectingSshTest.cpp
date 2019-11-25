// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#define LOG_WITH_GLOG
#include <magma_logging.h>

#include <boost/algorithm/string/trim.hpp>
#include <devmand/Application.h>
#include <devmand/channels/cli/IoConfigurationBuilder.h>
#include <devmand/channels/cli/SshSessionAsync.h>
#include <devmand/channels/cli/SshSocketReader.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <devmand/devices/cli/PlaintextCliDevice.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/Ssh.h>
#include <folly/Singleton.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/futures/Future.h>
#include <gtest/gtest.h>
#include <chrono>
#include <ctime>
#include <thread>

namespace devmand {
namespace test {
namespace cli {

using namespace devmand::channels::cli;
using namespace devmand::test::utils::ssh;
using namespace std;
using namespace folly;
using devmand::channels::cli::sshsession::readCallback;
using devmand::channels::cli::sshsession::SshSession;
using devmand::channels::cli::sshsession::SshSessionAsync;
using folly::IOThreadPoolExecutor;
using namespace devmand::cartography;
using namespace devmand::devices;
using namespace devmand::devices::cli;

class ReconnectingSshTest : public ::testing::Test {
 protected:
  void SetUp() override {
    devmand::test::utils::log::initLog();
    devmand::test::utils::ssh::initSsh();
  }

  void TearDown() override {
    // ssh->close();
  }
};

static DeviceConfig getConfig(
    string port,
    std::chrono::seconds commandTimeout = defaultCommandTimeout) {
  DeviceConfig deviceConfig;
  ChannelConfig chnlCfg;
  std::map<std::string, std::string> kvPairs;
  kvPairs.insert(std::make_pair("stateCommand", "echo 123"));
  kvPairs.insert(std::make_pair("port", port));
  kvPairs.insert(std::make_pair("username", "root"));
  kvPairs.insert(std::make_pair("password", "root"));
  kvPairs.insert(std::make_pair(
      configMaxCommandTimeoutSeconds, to_string(commandTimeout.count())));
  chnlCfg.kvPairs = kvPairs;
  deviceConfig.channelConfigs.insert(std::make_pair("cli", chnlCfg));
  deviceConfig.ip = "localhost";
  deviceConfig.id = "ubuntu-test-device";
  return deviceConfig;
}

static void ensureConnected(const shared_ptr<Cli>& cli) {
  bool connected = false;
  int attempts = 0;
  while (!connected && attempts++ < 30) {
    MLOG(MDEBUG) << "Testing connection attempt:" << attempts;
    try {
      const string& echoResult =
          cli->executeRead(ReadCommand::create("echo 123", true)).get();
      EXPECT_EQ("123", boost::algorithm::trim_copy(echoResult));
      connected = true;
    } catch (const std::exception& e) {
      MLOG(MDEBUG) << "Not connected:" << e.what();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
  EXPECT_TRUE(connected);
}

TEST_F(ReconnectingSshTest, plaintextCliDevice) {
  int cmdTimeout = 5;
  IoConfigurationBuilder ioConfigurationBuilder(
      getConfig("22", std::chrono::seconds(cmdTimeout)));
  shared_ptr<Cli> cli =
      ioConfigurationBuilder.createAll(ReadCachingCli::createCache());
  ensureConnected(cli);
  // sleep so that cli stack will be destroyed
  string sleepCommand = "sleep ";
  sleepCommand.append(to_string(cmdTimeout + 1));
  EXPECT_THROW(
      {
        try {
          cli->executeRead(ReadCommand::create(sleepCommand, true))
              .get(); // timeout exception
        } catch (const std::exception& e) {
          EXPECT_STREQ("Timed out", e.what());
          throw;
        }
      },
      std::exception);

  ensureConnected(cli);
}

} // namespace cli
} // namespace test
} // namespace devmand
