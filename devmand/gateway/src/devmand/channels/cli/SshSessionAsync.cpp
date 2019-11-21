// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <ErrorHandler.h>
#include <devmand/channels/cli/SshSession.h>
#include <devmand/channels/cli/SshSessionAsync.h>
#include <folly/executors/IOExecutor.h>
#include <folly/futures/Future.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace devmand {
namespace channels {
namespace cli {
namespace sshsession {

using boost::lockfree::spsc_queue;
using devmand::ErrorHandler;
using devmand::channels::cli::sshsession::SshSession;
using devmand::channels::cli::sshsession::SshSessionAsync;
using std::lock_guard;
using std::unique_lock;

SshSessionAsync::SshSessionAsync(string _id, shared_ptr<IOExecutor> _executor)
    : executor(SerialExecutor::create(
          folly::Executor::getKeepAliveToken(_executor.get()))),
      session(_id),
      reading(false),
      matchingExpectedOutput(false) {}

SshSessionAsync::~SshSessionAsync() {
  MLOG(MDEBUG) << "~SshSessionAsync started";
  if (this->sessionEvent != nullptr &&
      event_get_base(this->sessionEvent) != nullptr) {
    event_free(this->sessionEvent);
  }
  this->session.close();

  while (reading.load()) {
    // waiting for any pending read to run out
  }
  MLOG(MDEBUG) << "~SshSessionAsync finished";
}

Future<string> SshSessionAsync::read(int timeoutMillis) {
  return via(executor.get(), [this, timeoutMillis] {
    return session.read(timeoutMillis);
  });
}

Future<Unit> SshSessionAsync::openShell(
    const string& ip,
    int port,
    const string& username,
    const string& password) {
  return via(executor.get(), [this, ip, port, username, password] {
    session.openShell(ip, port, username, password);
  });
}

Future<Unit> SshSessionAsync::write(const string& command) {
  return via(executor.get(), [this, command] { session.write(command); });
}

Future<string> SshSessionAsync::readUntilOutput(const string& lastOutput) {
  this->readingState.currentLastOutput = lastOutput;
  this->readingState.promise = std::make_shared<Promise<string>>();
  this->readingState.outputSoFar = "";
  matchingExpectedOutput.store(true);
  processDataInBuffer(); // we could have had something already waiting in the
                         // queue
  return this->readingState.promise->getFuture();
}

void SshSessionAsync::setEvent(event* event) {
  this->sessionEvent = event;
}

void readCallback(evutil_socket_t fd, short what, void* ptr) {
  (void)fd;
  (void)what;
  ((SshSessionAsync*)ptr)->readSshDataToBuffer();
  ((SshSessionAsync*)ptr)->processDataInBuffer();
}

socket_t SshSessionAsync::getSshFd() {
  return this->session.getSshFd();
}

void SshSessionAsync::readSshDataToBuffer() {
  ErrorHandler::executeWithCatch([this]() {
    const string& output = this->session.read();
    if (!output.empty()) {
      readQueue.push(output);
    }
  });
}

using namespace std::chrono_literals;

void SshSessionAsync::matchExpectedOutput() {
  if (not this->session.isOpen()) {
    throw std::runtime_error("Session is closed");
  }

  if (not matchingExpectedOutput) { // we are not allowed to match unless
                                    // readUntilOutput is called because we
                                    // don't know against what to match
    return;
  }
  reading.store(true);
  while (this->readQueue.read_available() != 0) {
    string output;
    readQueue.pop(output);
    this->readingState.outputSoFar.append(output);
    std::size_t found = this->readingState.outputSoFar.find(
        this->readingState.currentLastOutput);
    if (found != std::string::npos) {
      // TODO check for any additional output after lastOutput
      string final = this->readingState.outputSoFar.substr(0, found);
      matchingExpectedOutput.store(false);
      this->readingState.promise->setValue(final);
    }
  }
  reading.store(false);
}

void SshSessionAsync::processDataInBuffer() {
  via(executor.get(), [this] { this->matchExpectedOutput(); });
}

} // namespace sshsession
} // namespace cli
} // namespace channels
} // namespace devmand
