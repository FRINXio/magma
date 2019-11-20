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

IoConfigurationBuilder::IoConfigurationBuilder(
    const DeviceConfig& deviceConfig) {
  const std::map<std::string, std::string>& plaintextCliKv =
      deviceConfig.channelConfigs.at("cli").kvPairs;
  connectionParameters = make_shared<ConnectionParameters>();
  connectionParameters->id = deviceConfig.id;
  connectionParameters->ip = deviceConfig.ip;
  connectionParameters->username = plaintextCliKv.at("username");
  connectionParameters->password = plaintextCliKv.at("password");
  connectionParameters->port = std::stoi(plaintextCliKv.at("port"));
  connectionParameters->flavour =
      plaintextCliKv.find("flavour") != plaintextCliKv.end()
      ? CliFlavour::create(plaintextCliKv.at("flavour"))
      : CliFlavour::create("");
  connectionParameters->kaTimeout = loadTimeout(
      plaintextCliKv, configKeepAliveIntervalSeconds, defaultKeepaliveInterval);
  connectionParameters->cmdTimeout = loadTimeout(
      plaintextCliKv, configMaxCommandTimeoutSeconds, defaultCommandTimeout);
}

IoConfigurationBuilder::~IoConfigurationBuilder() {
  MLOG(MDEBUG) << "~IoConfigurationBuilder";
}

shared_ptr<Cli> IoConfigurationBuilder::createAll(
    shared_ptr<CliCache> commandCache) {
  return createAll(createSSH, commandCache);
}

chrono::seconds IoConfigurationBuilder::loadTimeout(
    const std::map<std::string, std::string>& plaintextCliKv,
    const string& configKey,
    chrono::seconds defaultValue) {
  if (plaintextCliKv.find(configKey) != plaintextCliKv.end()) {
    return chrono::seconds(stoi(plaintextCliKv.at(configKey)));
  } else {
    return defaultValue;
  }
}

shared_ptr<Cli> IoConfigurationBuilder::createAll(
    function<shared_ptr<Cli>(
        shared_ptr<IOThreadPoolExecutor>,
        shared_ptr<ConnectionParameters>)> underlyingCliLayerFactory,
    shared_ptr<CliCache> commandCache) {
  shared_ptr<folly::ThreadWheelTimekeeper> timekeeper =
      make_shared<folly::ThreadWheelTimekeeper>(); // TODO use singleton when
                                                   // folly is initialized

  // TODO make Executor sharing configurable
  function<shared_ptr<Cli>()> cliFactory = [underlyingCliLayerFactory,
                                            params = connectionParameters,
                                            timekeeper,
                                            commandCache]() -> shared_ptr<Cli> {
    shared_ptr<IOThreadPoolExecutor> executor =
        make_shared<IOThreadPoolExecutor>(
            10, std::make_shared<NamedThreadFactory>("persession"));
    return getIo(
        params,
        underlyingCliLayerFactory(executor, params),
        timekeeper,
        commandCache);
  };

  // create reconnecting cli
  shared_ptr<CPUThreadPoolExecutor> rExecutor =
      std::make_shared<CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("rcli"));
  shared_ptr<ReconnectingCli> rcli = ReconnectingCli::make(
      connectionParameters->id, rExecutor, move(cliFactory));
  // create keepalive cli
  shared_ptr<CPUThreadPoolExecutor> kaExecutor =
      std::make_shared<CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("kacli"));

  shared_ptr<KeepaliveCli> kaCli = KeepaliveCli::make(
      connectionParameters->id,
      rcli,
      kaExecutor,
      timekeeper,
      connectionParameters->kaTimeout);
  return kaCli;
}

shared_ptr<Cli> IoConfigurationBuilder::createSSH(
    shared_ptr<IOThreadPoolExecutor> executor,
    shared_ptr<ConnectionParameters> params) {
  MLOG(MDEBUG) << "Creating CLI ssh device for " << params->id
               << " (host: " << params->ip << ")";

  // create session
  const std::shared_ptr<SshSessionAsync>& session =
      std::make_shared<SshSessionAsync>(params->id, executor);
  // opening SSH connection
  session
      ->openShell(
          params->ip,
          params->port,
          params->username,
          params->password)
      .get(); // TODO make this async

  shared_ptr<CliFlavour> cl = params->flavour;

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
    shared_ptr<ConnectionParameters> params,
    shared_ptr<Cli> underlyingCliLayer,
    shared_ptr<folly::ThreadWheelTimekeeper> timekeeper,
    shared_ptr<CliCache> commandCache) {
  // create caching cli
  const shared_ptr<ReadCachingCli>& ccli = std::make_shared<ReadCachingCli>(
      params->id, underlyingCliLayer, commandCache);
  // create timeout tracker
  shared_ptr<TimeoutTrackingCli> ttcli = TimeoutTrackingCli::make(
      params->id,
      ccli,
      timekeeper,
      std::make_shared<folly::CPUThreadPoolExecutor>(
          5, std::make_shared<NamedThreadFactory>("ttcli")),
      params->cmdTimeout);
  // create Queued cli
  shared_ptr<QueuedCli> qcli = QueuedCli::make(
      params->id,
      ttcli,
      std::make_shared<folly::CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("qcli")));
  return qcli;
}

} // namespace cli
} // namespace channels
} // namespace devmand
