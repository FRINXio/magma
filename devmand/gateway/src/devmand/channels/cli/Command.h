// Copyright (c) 2019-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include <folly/futures/Future.h>
#include <iostream>

using std::string;
using std::vector;

namespace devmand {
namespace channels {
namespace cli {
/*
 * Command struct encapsulating a string to be executed on a device.
 */

struct Command {
 public:
  Command() = delete;

 public:
 protected:
  explicit Command(std::string _command, bool skipCache);

 public:
  bool isMultiCommand();
  vector<Command> splitMultiCommand();

  std::string toString() const {
    return command; // TODO serialize so that newlines are escaped
  }

  bool skipCache() const {
    return skipCache_;
  }

  friend std::ostream& operator<<(std::ostream& _stream, Command const& c) {
    _stream << c.toString();
    return _stream;
  }

  Command operator=(Command other) {
    return Command(other.toString(), other.skipCache());
  }

 private:
  const string command;
  const bool skipCache_;
};

class WriteCommand : public Command {
 public:
  static WriteCommand create(const std::string& cmd, bool skipCache = false);

  static WriteCommand create(const Command& cmd);

 private:
  WriteCommand(const string& command, bool skipCache);
};

class ReadCommand : public Command {
 public:
  static ReadCommand create(const std::string& cmd, bool skipCache = false);

  static ReadCommand create(const Command& cmd);

 private:
  ReadCommand(const string& command, bool skipCache);
};

} // namespace cli
} // namespace channels
} // namespace devmand
