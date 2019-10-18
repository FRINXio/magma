// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <devmand/channels/cli/Channel.h>
#include <devmand/channels/cli/Command.h>
#include <devmand/devices/Device.h>

namespace devmand {
namespace devices {
namespace cli {

using namespace devmand::channels::cli;

class PlaintextCliDevice : public Device {
 public:
  PlaintextCliDevice(Application& application, const Id& id, const std::string& stateCommand, const std::shared_ptr<Channel> &channel);
  PlaintextCliDevice() = delete;
  virtual ~PlaintextCliDevice() = default;
  PlaintextCliDevice(const PlaintextCliDevice&) = delete;
  PlaintextCliDevice& operator=(const PlaintextCliDevice&) = delete;
  PlaintextCliDevice(PlaintextCliDevice&&) = delete;
  PlaintextCliDevice& operator=(PlaintextCliDevice&&) = delete;

  static std::unique_ptr<devices::Device> createDevice(
      Application& app,
      const cartography::DeviceConfig& deviceConfig);

 public:
  std::shared_ptr<State> getState() override;

 protected:
  void setConfig(const folly::dynamic& config) override {
    (void)config;
    LOG(ERROR) << "set config on unconfigurable device";
  }

 private:
  std::shared_ptr<Channel> channel;
  const Command stateCommand;
};

} // namespace cli
} // namespace devices
} // namespace devmand
