// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/Channel.h>

namespace devmand {
namespace channels {
namespace cli {

class Channel : public channels::Channel, public devmand::channels::cli::Cli {
 public:
  Channel(const std::shared_ptr<devmand::channels::cli::Cli> cli);
  Channel() = delete;
  virtual ~Channel();
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;
  Channel(Channel&&) = delete;
  Channel& operator=(Channel&&) = delete;

  folly::Future<std::string> executeAndRead(const Command& cmd) override;
  folly::Future<std::string> execute(
      const Command& cmd) override;

 private:
  const std::shared_ptr<devmand::channels::cli::Cli> cli;
};

} // namespace cli
} // namespace channels
} // namespace devmand
