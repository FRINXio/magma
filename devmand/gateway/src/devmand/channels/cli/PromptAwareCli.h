// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <devmand/channels/cli/Cli.h>
#include <devmand/channels/cli/CliFlavour.h>
#include <devmand/channels/cli/SshSessionAsync.h>
#include <folly/futures/Future.h>

namespace devmand {
namespace channels {
namespace cli {

using devmand::channels::cli::CliInitializer;
using devmand::channels::cli::PromptResolver;
using devmand::channels::cli::sshsession::SshSessionAsync;
using folly::SemiFuture;
using folly::Unit;
using std::shared_ptr;
using std::string;

class PromptAwareCli : public Cli {
 private:
  struct PromptAwareParameters {
    string id;
    shared_ptr<SshSessionAsync> session;
    shared_ptr<CliFlavour> cliFlavour;
    string prompt;
  };
  shared_ptr<PromptAwareParameters> promptAwareParameters;

 public:
  PromptAwareCli(
      string id,
      shared_ptr<SshSessionAsync> session,
      shared_ptr<CliFlavour> cliFlavour);

  ~PromptAwareCli();

  SemiFuture<Unit> resolvePrompt();
  SemiFuture<Unit> initializeCli(const string secret);
  folly::SemiFuture<std::string> executeRead(const ReadCommand cmd);
  folly::SemiFuture<std::string> executeWrite(const WriteCommand cmd);
};

} // namespace cli
} // namespace channels
} // namespace devmand
