// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/Command.h>
#include <devmand/channels/cli/PromptAwareCli.h>
#include <devmand/channels/cli/SshSessionAsync.h>
#include <folly/executors/IOThreadPoolExecutor.h>

using devmand::channels::cli::CliInitializer;
using devmand::channels::cli::Command;
using devmand::channels::cli::PromptAwareCli;
using devmand::channels::cli::PromptResolver;
using std::string;

namespace devmand {
namespace channels {
namespace cli {

SemiFuture<Unit> PromptAwareCli::resolvePrompt() {
  return cliFlavour->resolver->resolvePrompt(session, cliFlavour->newline)
      .thenValue([dis = shared_from_this()](string _prompt) {
        dis->prompt = _prompt;
      });
}

SemiFuture<Unit> PromptAwareCli::initializeCli(const string secret) {
  return cliFlavour->initializer->initialize(session, secret);
}

folly::Future<string> PromptAwareCli::executeRead(const ReadCommand cmd) {
  const string& command = cmd.toString();

  return session->write(command)
      .thenValue([dis = shared_from_this(), command](...) {
        return dis->session->readUntilOutput(command);
      })
      .thenValue([dis = shared_from_this()](const string& output) {
        auto returnOutputParameter = [output](...) { return output; };
        return dis->session->write(dis->cliFlavour->newline)
            .thenValue(returnOutputParameter);
      })
      .thenValue([dis = shared_from_this()](const string& output) {
        auto concatOutputParameter = [output](const string& readUntilOutput) {
          return output + readUntilOutput;
        };
        return dis->session->readUntilOutput(dis->prompt)
            .thenValue(concatOutputParameter);
      });
}

PromptAwareCli::PromptAwareCli(
    shared_ptr<SshSessionAsync> _session,
    shared_ptr<CliFlavour> _cliFlavour)
    : session(_session), cliFlavour(_cliFlavour) {}

folly::Future<std::string> PromptAwareCli::executeWrite(
    const WriteCommand cmd) {
  const string& command = cmd.toString();
  return session->write(command)
      .thenValue([dis = shared_from_this(), command](...) {
        return dis->session->readUntilOutput(command);
      })
      .thenValue([dis = shared_from_this(), command](const string& output) {
        return dis->session->write(dis->cliFlavour->newline)
            .thenValue([output, command](...) { return output + command; });
      });
}
} // namespace cli
} // namespace channels
} // namespace devmand
