// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <devmand/cartography/DeviceConfig.h>
#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/cli/CliFlavour.h>
#include <devmand/channels/cli/ReadCachingCli.h>
#include <folly/executors/IOThreadPoolExecutor.h>
#include <folly/futures/ThreadWheelTimekeeper.h>

namespace devmand::channels::cli {

using devmand::cartography::DeviceConfig;
using devmand::channels::cli::Cli;
using devmand::channels::cli::CliFlavour;
using namespace std;

using folly::IOThreadPoolExecutor;
using folly::SemiFuture;

static constexpr auto configKeepAliveIntervalSeconds =
    "keepAliveIntervalSeconds";
static constexpr auto configMaxCommandTimeoutSeconds =
    "maxCommandTimeoutSeconds";

class IoConfigurationBuilder {
 private:
  struct ConnectionParameters {
    string username;
    string password;
    string ip;
    string id;
    int port;
    shared_ptr<CliFlavour> flavour;
    chrono::seconds kaTimeout;
    chrono::seconds cmdTimeout;
  };

 public:
  IoConfigurationBuilder(const DeviceConfig& deviceConfig);

  ~IoConfigurationBuilder();

  shared_ptr<Cli> createAll(shared_ptr<CliCache> commandCache);

  static Future<shared_ptr<Cli>> createPromptAwareCli(
      shared_ptr<folly::IOThreadPoolExecutor> executor,
      shared_ptr<ConnectionParameters> params);

  static shared_ptr<ConnectionParameters> makeConnectionParameters(
      string id,
      string hostname,
      string username,
      string password,
      string flavour,
      int port,
      chrono::seconds kaTimeout,
      chrono::seconds cmdTimeout);

 private:
  shared_ptr<ConnectionParameters> connectionParameters;

  shared_ptr<Cli> createAllUsingFactory(shared_ptr<CliCache> commandCache);

  static chrono::seconds loadTimeout(
      const std::map<std::string, std::string>& plaintextCliKv,
      const string& configKey,
      chrono::seconds defaultValue);
};
} // namespace devmand::channels::cli
