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
#include <devmand/channels/cli/KeepaliveCli.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <devmand/devices/cli/PlaintextCliDevice.h>
#include <devmand/test/cli/utils/Log.h>
#include <devmand/test/cli/utils/Ssh.h>
#include <folly/Singleton.h>
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

using namespace std::chrono_literals;

class ReconnectingSshTest : public ::testing::Test {
 protected:
  shared_ptr<server> ssh;
  void SetUp() override {
    devmand::test::utils::log::initLog();
    devmand::test::utils::ssh::initSsh();
    ssh = startSshServer();
  }

  void TearDown() override {
    ssh->close();
  }
};

static DeviceConfig getConfig(
    string port,
    std::chrono::seconds commandTimeout = defaultCommandTimeout,
    std::chrono::seconds keepaliveTimeout = defaultKeepaliveInterval) {
  DeviceConfig deviceConfig;
  ChannelConfig chnlCfg;
  std::map<std::string, std::string> kvPairs;
  kvPairs.insert(std::make_pair("stateCommand", "echo 123"));
  kvPairs.insert(std::make_pair("port", port));
  kvPairs.insert(std::make_pair("username", "root"));
  kvPairs.insert(std::make_pair("password", "root"));
  kvPairs.insert(std::make_pair(
      configMaxCommandTimeoutSeconds, to_string(commandTimeout.count())));
  kvPairs.insert(std::make_pair(
      configKeepAliveIntervalSeconds, to_string(keepaliveTimeout.count())));
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
      std::this_thread::sleep_for(500ms);
    }
  }
  EXPECT_TRUE(connected);
}

TEST_F(ReconnectingSshTest, commandTimeout) {
  int cmdTimeout = 5;
  IoConfigurationBuilder ioConfigurationBuilder(getConfig(
      "9999", std::chrono::seconds(cmdTimeout), std::chrono::seconds(60)));
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

  ssh->close();
  ssh = startSshServer();

  // FIXME
      std::this_thread::sleep_for(std::chrono::seconds(50));
//  ensureConnected(cli);
}

TEST_F(ReconnectingSshTest, serverDisconnectSendCommands) {
  int cmdTimeout = 60;
  IoConfigurationBuilder ioConfigurationBuilder(getConfig(
      "9999", std::chrono::seconds(cmdTimeout), std::chrono::seconds(60)));
  shared_ptr<Cli> cli =
      ioConfigurationBuilder.createAll(ReadCachingCli::createCache());

  ensureConnected(cli);
  ssh->close();
  ssh = startSshServer();
  ensureConnected(cli);
}

TEST_F(ReconnectingSshTest, serverDisconnectWaithForKeepalive) {
  int cmdTimeout = 5;
  int keepaliveFreq = 5;
  IoConfigurationBuilder ioConfigurationBuilder(getConfig(
      "9999",
      std::chrono::seconds(cmdTimeout),
      std::chrono::seconds(keepaliveFreq)));
  shared_ptr<Cli> cli =
      ioConfigurationBuilder.createAll(ReadCachingCli::createCache());

  ensureConnected(cli);

  // Disconnect from server side
  ssh->close();
  ssh = startSshServer();

  MLOG(MDEBUG) << "Waiting for CLI to reconnect";

  int attempt = 0;
  while (true) {
    if (ssh->isConnected()) {
      break;
    }
    if ((attempt++) == 30) {
      FAIL() << "CLI did not reconnect, something went wrong";
    }

    std::this_thread::sleep_for(1s);
  }
}

TEST_F(ReconnectingSshTest, keepalive) {
  int cmdTimeout = 5;
  int keepaliveTimeout = 10;
  IoConfigurationBuilder ioConfigurationBuilder(getConfig(
      "22",
      std::chrono::seconds(cmdTimeout),
      std::chrono::seconds(keepaliveTimeout)));
  shared_ptr<Cli> cli =
      ioConfigurationBuilder.createAll(ReadCachingCli::createCache());
  std::this_thread::sleep_for(std::chrono::seconds(20));
  // TODO: check that keepalive was run, move to separate test
}

} // namespace cli
} // namespace test
} // namespace devmand
