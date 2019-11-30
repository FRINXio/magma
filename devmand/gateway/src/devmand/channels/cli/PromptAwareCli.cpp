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
  return promptAwareParameters->cliFlavour->resolver
      ->resolvePrompt(
          promptAwareParameters->session,
          promptAwareParameters->cliFlavour->newline)
      .thenValue([params = promptAwareParameters](string _prompt) {
        params->prompt = _prompt;
      });
}

SemiFuture<Unit> PromptAwareCli::initializeCli(const string secret) {
  return promptAwareParameters->cliFlavour->initializer->initialize(
      promptAwareParameters->session, secret);
}

folly::Future<string> PromptAwareCli::executeRead(const ReadCommand cmd) {
  const string& command = cmd.raw();

  return promptAwareParameters->session->write(command)
      .thenValue([params = promptAwareParameters, command](...) {
        return params->session->readUntilOutput(command);
      })
      .thenValue([params = promptAwareParameters](const string& output) {
        auto returnOutputParameter = [output](...) { return output; };
        return params->session->write(params->cliFlavour->newline)
            .thenValue(returnOutputParameter);
      })
      .thenValue([params = promptAwareParameters](const string& output) {
        auto concatOutputParameter = [output](const string& readUntilOutput) {
          return output + readUntilOutput;
        };
        return params->session->readUntilOutput(params->prompt)
            .thenValue(concatOutputParameter);
      });
}

PromptAwareCli::PromptAwareCli(
    string id,
    shared_ptr<SshSessionAsync> _session,
    shared_ptr<CliFlavour> _cliFlavour) {
  promptAwareParameters = shared_ptr<PromptAwareParameters>(
      new PromptAwareParameters{id, _session, _cliFlavour, {}});
}

PromptAwareCli::~PromptAwareCli() {
  string& id = promptAwareParameters->id;
  while (promptAwareParameters.use_count() > 1) {
    MLOG(MDEBUG) << "[" << id << "] "
                 << "~PromptAwareCli sleeping";
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  promptAwareParameters = nullptr;
  MLOG(MDEBUG) << "[" << id << "] "
               << "~PromptAwareCli done";
}

folly::Future<std::string> PromptAwareCli::executeWrite(
    const WriteCommand cmd) {
  const string& command = cmd.raw();
  return promptAwareParameters->session->write(command)
      .thenValue([params = promptAwareParameters, command](...) {
        return params->session->readUntilOutput(command);
      })
      .thenValue(
          [params = promptAwareParameters, command](const string& output) {
            return params->session->write(params->cliFlavour->newline)
                .thenValue([output, command](...) { return output + command; });
          });
}
} // namespace cli
} // namespace channels
} // namespace devmand
