/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include <memory>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "RuleStore.h"
#include "SessionID.h"
#include "SessionState.h"
#include "MemorySessionStore.h"
#include "magma_logging.h"

using ::testing::Test;

namespace magma {

class SessionStoreTest : public ::testing::Test {
 protected:
  SessionIDGenerator id_gen_;
};

/**
 * End to end test of the MemorySessionStore.
 * 1) Create MemorySessionStore
 * 2) Request grant1 for subscriber IMSI1
 * 3) Create bare-bones session for IMSI1 and IMSI2
 * 4) Write and commit session state for IMSI1 and IMSI2
 * 5) Request grant2 for subscriber IMSI1 and IMSI2
 * 6) Verify that state was written for IMSI1 and has been retrieved.
 * 7) Verify that state has not been written for IMSI2.
 *
 * Session state for IMSI2 has not been written to the store because grant1
 * was not created IMSI2 being a granted subscriber.
 */
TEST_F(SessionStoreTest, test_read_and_write)
{
  std::string hardware_addr_bytes = {0x0f, 0x10, 0x2e, 0x12, 0x3a, 0x55};
  std::string imsi = "IMSI1";
  std::string imsi2 = "IMSI2";
  std::string msisdn = "5100001234";
  std::string radius_session_id =
    "AA-AA-AA-AA-AA-AA:TESTAP__"
    "0F-10-2E-12-3A-55";
  auto sid = id_gen_.gen_session_id(imsi);
  auto sid2 = id_gen_.gen_session_id(imsi2);
  std::string core_session_id = "asdf";
  SessionState::Config cfg = {.ue_ipv4 = "",
                              .spgw_ipv4 = "",
                              .msisdn = msisdn,
                              .apn = "",
                              .imei = "",
                              .plmn_id = "",
                              .imsi_plmn_id = "",
                              .user_location = "",
                              .rat_type = RATType::TGPP_WLAN,
                              .mac_addr = "0f:10:2e:12:3a:55",
                              .hardware_addr = hardware_addr_bytes,
                              .radius_session_id = radius_session_id};
  auto rule_store = std::make_shared<StaticRuleStore>();
  auto tgpp_context = TgppContext{};

  auto session_store = new MemorySessionStore();

  // Emulate CreateSession, which needs to create a new session for a subscriber
  CallBackOnAccess cb = [=] (SessionMap session_map) mutable -> void {
    auto session = std::make_unique<SessionState>(imsi, sid, core_session_id, cfg, *rule_store, tgpp_context);
    auto session2 = std::make_unique<SessionState>(imsi2, sid2, core_session_id, cfg, *rule_store, tgpp_context);
    EXPECT_EQ(session->get_session_id(), sid);
    EXPECT_EQ(session2->get_session_id(), sid2);

    EXPECT_EQ(session_map.size(), 0);
    session_map[imsi] = std::vector<std::unique_ptr<SessionState>>();
    EXPECT_EQ(session_map.size(), 1);
    EXPECT_EQ(session_map[imsi].size(), 0);
    session_map[imsi].push_back(std::move(session));
    EXPECT_EQ(session_map[imsi].size(), 1);

    // Since the grant was not given with R/W permission for subscriber IMSI2,
    // The session for IMSI2 should not be saved into the store
    session_map[imsi2] = std::vector<std::unique_ptr<SessionState>>();
    session_map[imsi2].push_back(std::move(session2));
    EXPECT_EQ(session_map.size(), 2);
    EXPECT_EQ(session_map[imsi2].size(), 1);

    // And now commit back to MemorySessionStore
    session_store->commit_sessions(
      std::move(session_map),
      [](bool success) -> void {});
  };

  std::vector<std::string> requested_ids{imsi};
  std::vector<std::string>& requested_ids_ref = requested_ids;
  session_store->operate_on_sessions(
    requested_ids_ref,
    cb);

  // Get a new grant for the same subscriber, and expect to see the data that
  // was written to it earlier
  CallBackOnAccess cb2 = [=] (SessionMap session_map) mutable -> void {
      EXPECT_EQ(session_map.size(), 1);
      EXPECT_EQ(session_map[imsi].size(), 1);
      EXPECT_EQ(session_map[imsi].front()->get_session_id(), sid);
  };
  requested_ids = {imsi, imsi2};
  session_store->operate_on_sessions(
    requested_ids_ref,
    cb2);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

} // namespace magma