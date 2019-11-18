// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#define LOG_WITH_GLOG

#include <magma_logging.h>

#include <devmand/channels/cli/IoConfigurationBuilder.h>
#include <devmand/channels/cli/KeepaliveCli.h>
#include <devmand/channels/cli/PromptAwareCli.h>
#include <devmand/channels/cli/QueuedCli.h>
#include <devmand/channels/cli/ReadCachingCli.h>
#include <devmand/channels/cli/ReconnectingCli.h>
#include <devmand/channels/cli/SshSession.h>
#include <devmand/channels/cli/SshSessionAsync.h>
#include <devmand/channels/cli/SshSocketReader.h>
#include <devmand/channels/cli/TimeoutTrackingCli.h>
#include <folly/Singleton.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/IOThreadPoolExecutor.h>

namespace devmand {
namespace channels {
namespace cli {

using devmand::channels::cli::IoConfigurationBuilder;
using devmand::channels::cli::SshSocketReader;
using devmand::channels::cli::sshsession::readCallback;
using devmand::channels::cli::sshsession::SshSession;
using devmand::channels::cli::sshsession::SshSessionAsync;
using folly::CPUThreadPoolExecutor;
using folly::EvictingCacheMap;
using folly::IOThreadPoolExecutor;
using std::make_shared;
using std::string;

IoConfigurationBuilder::IoConfigurationBuilder(
    const DeviceConfig& _deviceConfig)
    : deviceConfig(_deviceConfig) {}

shared_ptr<Cli> IoConfigurationBuilder::createAll(
    shared_ptr<CliCache> commandCache) {
  return createAll(
      [=](shared_ptr<IOThreadPoolExecutor> executor) -> shared_ptr<Cli> {
        return createSSH(executor);
      },
      commandCache);
}

shared_ptr<Cli> IoConfigurationBuilder::createAll(
    function<shared_ptr<Cli>(shared_ptr<IOThreadPoolExecutor>)>
        underlyingCliLayerFactory,
    shared_ptr<CliCache> commandCache) {
  shared_ptr<folly::ThreadWheelTimekeeper> timekeeper =
      make_shared<folly::ThreadWheelTimekeeper>(); // TODO use singleton

  function<shared_ptr<Cli>()> cliFactory = [=]() -> shared_ptr<Cli> {
    shared_ptr<IOThreadPoolExecutor> executor =
        make_shared<IOThreadPoolExecutor>(
            10,
            std::make_shared<NamedThreadFactory>(
                "persession")); // TODO cast to Executor
    return getIo(
        underlyingCliLayerFactory(executor), timekeeper, commandCache);
  };

  // create reconnecting cli
  shared_ptr<CPUThreadPoolExecutor> rExecutor =
      std::make_shared<CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("rcli"));
  shared_ptr<ReconnectingCli> rcli =
      make_shared<ReconnectingCli>(deviceConfig.id, rExecutor, move(cliFactory));
  // create keepalive cli
  shared_ptr<CPUThreadPoolExecutor> kaExecutor =
      std::make_shared<CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("kacli"));
  return make_shared<KeepaliveCli>(rcli, kaExecutor, timekeeper);

  //// without rcli
  //  shared_ptr<IOThreadPoolExecutor> kaExecutor =
  //  std::make_shared<IOThreadPoolExecutor>(8,
  //  std::make_shared<NamedThreadFactory>("kacli"));
  //  return make_shared<KeepaliveCli>(cliFactory(), kaExecutor, timekeeper);
}

shared_ptr<Cli> IoConfigurationBuilder::createSSH(
    shared_ptr<IOThreadPoolExecutor> executor) {
  MLOG(MDEBUG) << "Creating CLI ssh device for " << deviceConfig.id << " (host: " << deviceConfig.ip
               << ")";
  const auto& plaintextCliKv = deviceConfig.channelConfigs.at("cli").kvPairs;
  // crate session
  const std::shared_ptr<SshSessionAsync>& session =
      std::make_shared<SshSessionAsync>(deviceConfig.id, executor);
  // opening SSH connection
  session
      ->openShell(
          deviceConfig.ip,
          std::stoi(plaintextCliKv.at("port")),
          plaintextCliKv.at("username"),
          plaintextCliKv.at("password"))
      .get();

  shared_ptr<CliFlavour> cl =
      plaintextCliKv.find("flavour") != plaintextCliKv.end()
      ? CliFlavour::create(plaintextCliKv.at("flavour"))
      : CliFlavour::create("");

  // create CLI
  const shared_ptr<PromptAwareCli>& cli =
      std::make_shared<PromptAwareCli>(session, cl);

  // initialize CLI
  cli->initializeCli();
  // resolve prompt needs to happen
  cli->resolvePrompt();
  // create async data reader
  event* sessionEvent = SshSocketReader::getInstance().addSshReader(
      readCallback, session->getSshFd(), session.get());
  session->setEvent(sessionEvent);
  return cli;
}

shared_ptr<Cli> IoConfigurationBuilder::getIo(
    shared_ptr<Cli> underlyingCliLayer,
    shared_ptr<folly::ThreadWheelTimekeeper> timekeeper,
    shared_ptr<CliCache> commandCache) {
  // create caching cli
  const shared_ptr<ReadCachingCli>& ccli =
      std::make_shared<ReadCachingCli>(deviceConfig.id, underlyingCliLayer, commandCache);
  // create timeout tracker
  const shared_ptr<TimeoutTrackingCli>& ttcli =
      std::make_shared<TimeoutTrackingCli>(
          deviceConfig.id,
          ccli,
          timekeeper,
          std::make_shared<folly::CPUThreadPoolExecutor>(
              5, std::make_shared<NamedThreadFactory>("ttcli")));
  // create Queued cli
  shared_ptr<QueuedCli> qcli = std::make_shared<QueuedCli>(
      deviceConfig.id,
      ttcli,
      std::make_shared<folly::CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("qcli")));
  return qcli;
}

} // namespace cli
} // namespace channels
} // namespace devmand
