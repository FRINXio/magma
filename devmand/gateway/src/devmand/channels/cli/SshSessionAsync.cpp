// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <devmand/channels/cli/SshSessionAsync.h>
#include <folly/futures/Future.h>
#include <folly/executors/IOThreadPoolExecutor.h>

namespace devmand {
namespace channels {
namespace cli {
namespace sshsession {

    using devmand::channels::cli::sshsession::SshSessionAsync;

    SshSessionAsync::SshSessionAsync(folly::IOThreadPoolExecutor &_executor) : executor(_executor) {}

    SshSessionAsync::~SshSessionAsync() {
        session.close();
    }

    Future<string> SshSessionAsync::read(int timeoutMillis) {
        return via(&executor, [this, timeoutMillis] { return session.read(timeoutMillis); });
    }

    Future<Unit>
    SshSessionAsync::openShell(const string &ip, int port, const string &username, const string &password) {
        return via(&executor, [this, ip, port, username, password] { session.openShell(ip, port, username, password); });
    }

    Future<Unit> SshSessionAsync::write(const string &command) {
        return via(&executor, [this, command] { session.write(command); });
    }

    Future<Unit> SshSessionAsync::close() {
        return via(&executor, [this] { session.close(); });
    }

    Future<string> SshSessionAsync::readUntilOutput(string lastOutput) {
        return via(&executor, [this, lastOutput] { return session.readUntilOutput(lastOutput); });
    }

}
}
}
}