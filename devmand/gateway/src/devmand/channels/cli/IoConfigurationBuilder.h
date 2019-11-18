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

class IoConfigurationBuilder {

 public:
  IoConfigurationBuilder(const DeviceConfig& deviceConfig);

  shared_ptr<Cli> createAll(
      shared_ptr<CliCache> commandCache);

  shared_ptr<Cli> createAll(
      function<shared_ptr<Cli>(shared_ptr<folly::IOThreadPoolExecutor>)>
          underlyingCliLayerFactory,
      shared_ptr<CliCache> commandCache); // visible for testing

 private:
    DeviceConfig deviceConfig;
  shared_ptr<Cli> createSSH(
      shared_ptr<folly::IOThreadPoolExecutor> executor);

  shared_ptr<Cli> getIo(
      shared_ptr<Cli> underlyingCliLayer,
      shared_ptr<folly::ThreadWheelTimekeeper> timekeeper,
      shared_ptr<CliCache> commandCache = ReadCachingCli::createCache());
};
} // namespace devmand::channels::cli
