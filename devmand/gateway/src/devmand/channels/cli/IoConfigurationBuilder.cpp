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

  connectionParameters = makeConnectionParameters(
      deviceConfig.id,
      deviceConfig.ip,
      plaintextCliKv.at("username"),
      plaintextCliKv.at("password"),
      plaintextCliKv.find("flavour") != plaintextCliKv.end()
          ? plaintextCliKv.at("flavour")
          : "",
      std::stoi(plaintextCliKv.at("port")),
      loadTimeout(
          plaintextCliKv,
          configKeepAliveIntervalSeconds,
          defaultKeepaliveInterval),
      loadTimeout(
          plaintextCliKv,
          configMaxCommandTimeoutSeconds,
          defaultCommandTimeout));
}

IoConfigurationBuilder::~IoConfigurationBuilder() {
  MLOG(MDEBUG) << "~IoConfigurationBuilder";
}

shared_ptr<Cli> IoConfigurationBuilder::createAll(
    shared_ptr<CliCache> commandCache) {
  return createAllUsingFactory(commandCache);
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

shared_ptr<Cli> IoConfigurationBuilder::createAllUsingFactory(
    shared_ptr<CliCache> commandCache) {
  shared_ptr<folly::ThreadWheelTimekeeper> timekeeper =
      make_shared<folly::ThreadWheelTimekeeper>(); // TODO use singleton when
                                                   // folly is initialized

  // TODO make Executor sharing configurable
  shared_ptr<IOThreadPoolExecutor> executor = make_shared<IOThreadPoolExecutor>(
      10, std::make_shared<NamedThreadFactory>("persession"));

  shared_ptr<IOThreadPoolExecutor> rccliExecutor =
      make_shared<IOThreadPoolExecutor>(
          1, std::make_shared<NamedThreadFactory>("rccli"));

  function<SemiFuture<shared_ptr<Cli>>()> cliFactory =
      [executor,
       params = connectionParameters,
       timekeeper,
       commandCache,
       rccliExecutor]() {
        return createPromptAwareCli(executor, params)
            .via(executor.get())
            .thenValue(
                [executor, params, commandCache, timekeeper, rccliExecutor](
                    shared_ptr<Cli> sshCli) -> shared_ptr<Cli> {
                  MLOG(MDEBUG) << "[" << params->id
                               << "] "
                                  "Creating cli layers rcclli, ttcli, qcli";
                  // create caching cli
                  const shared_ptr<ReadCachingCli>& rccli =
                      std::make_shared<ReadCachingCli>(
                          params->id, sshCli, commandCache, rccliExecutor);
                  // create timeout tracker
                  shared_ptr<TimeoutTrackingCli> ttcli =
                      TimeoutTrackingCli::make(
                          params->id,
                          rccli,
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
                });
      };

  // create reconnecting cli that uses cliFactory to establish ssh connection
  shared_ptr<CPUThreadPoolExecutor> rExecutor =
      std::make_shared<CPUThreadPoolExecutor>(
          2, std::make_shared<NamedThreadFactory>("rcli"));
  shared_ptr<ReconnectingCli> rcli = ReconnectingCli::make(
      connectionParameters->id, rExecutor, move(cliFactory), timekeeper);
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

Future<shared_ptr<Cli>> IoConfigurationBuilder::createPromptAwareCli(
    shared_ptr<folly::Executor> executor,
    shared_ptr<ConnectionParameters> params) {
  MLOG(MDEBUG) << "Creating CLI ssh device for " << params->id << " ("
               << params->ip << ":" << params->port << ")";

  // create session
  std::shared_ptr<SshSessionAsync> session =
      std::make_shared<SshSessionAsync>(params->id, executor);
  // open SSH connection

  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "Opening shell";
  // TODO: do this using future chaining
  session
      ->openShell(params->ip, params->port, params->username, params->password)
      .get();

  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "Setting flavour";
  shared_ptr<CliFlavour> cl = params->flavour;
  const shared_ptr<IOThreadPoolExecutor>& pacli =
      std::make_shared<IOThreadPoolExecutor>(
          1, std::make_shared<NamedThreadFactory>("pacli"));
  // create CLI
  shared_ptr<PromptAwareCli> cli =
      PromptAwareCli::make(params->id, session, cl, pacli);
  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "Initializing cli";
  // initialize CLI
  // TODO: do this using future chaining:
  //  via(executor.get())
  //      .thenValue([params, cli, session](...) -> SemiFuture<shared_ptr<Cli>>
  //      {

  cli->initializeCli(params->password).get();
  // resolve prompt needs to happen
  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "Resolving prompt";
  // TODO: do this using future chaining
  cli->resolvePrompt().get();

  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "Creating async data reader";
  event* sessionEvent = SshSocketReader::getInstance().addSshReader(
      readCallback, session->getSshFd(), session.get());
  session->setEvent(sessionEvent);
  MLOG(MDEBUG) << "[" << params->id
               << "] "
                  "SSH layer configured";
  return makeFuture(cli);
}

shared_ptr<IoConfigurationBuilder::ConnectionParameters>
IoConfigurationBuilder::makeConnectionParameters(
    string id,
    string hostname,
    string username,
    string password,
    string flavour,
    int port,
    chrono::seconds kaTimeout,
    chrono::seconds cmdTimeout) {
  shared_ptr<IoConfigurationBuilder::ConnectionParameters>
      connectionParameters =
          make_shared<IoConfigurationBuilder::ConnectionParameters>();
  connectionParameters->id = id;
  connectionParameters->ip = hostname;
  connectionParameters->username = username;
  connectionParameters->password = password;
  connectionParameters->port = port;
  connectionParameters->flavour = CliFlavour::create(flavour);
  connectionParameters->kaTimeout = kaTimeout;
  connectionParameters->cmdTimeout = cmdTimeout;
  return connectionParameters;
}

} // namespace cli
} // namespace channels
} // namespace devmand
