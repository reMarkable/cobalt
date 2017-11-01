// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "analyzer/report_master/auth_enforcer.h"

#include <memory>
#include <string>

#include "gflags/gflags.h"
#include "glog/logging.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {
namespace analyzer {

class GoogleEmailEnforcerTest : public testing::Test {
 protected:
  static grpc::Status GetEmailFromEncodedUserInfo(
      const std::string &encoded_user_info, std::string *email) {
    return GoogleEmailEnforcer::GetEmailFromEncodedUserInfo(encoded_user_info,
                                                            email);
  }

  static grpc::Status GetEmailFromServerContext(grpc::ServerContext *context,
                                                std::string *email) {
    return GoogleEmailEnforcer::GetEmailFromServerContext(context, email);
  }

  static bool CheckGoogleEmail(std::string email) {
    return GoogleEmailEnforcer::CheckGoogleEmail(email);
  }
};

TEST_F(GoogleEmailEnforcerTest, CheckGoogleEmail) {
  ASSERT_TRUE(CheckGoogleEmail("alex@google.com"));

  // Usernames are a maximum of 14 letters long.
  ASSERT_FALSE(CheckGoogleEmail("abcdefghiwqwera@google.com"));
  ASSERT_TRUE(CheckGoogleEmail("abcdefghiwqwer@google.com"));

  // Emails must include an @.
  ASSERT_FALSE(CheckGoogleEmail("alexgoogle.com"));

  // Emails must only contain lower case characters.
  ASSERT_FALSE(CheckGoogleEmail("alexA@google.com"));

  // Only accept google.com email addresses.
  ASSERT_FALSE(CheckGoogleEmail("alex@gmail.com"));
}

TEST_F(GoogleEmailEnforcerTest, GetEmailFromServerContextTest) {
  std::string email;
  std::unique_ptr<grpc::ServerContext> ctx(new grpc::ServerContext());

  // Test that GetEmailFromServerContext rejects an unauthenticated
  // ServerContext.
  ASSERT_EQ(grpc::StatusCode::UNAUTHENTICATED,
            GetEmailFromServerContext(ctx.get(), &email).error_code());
}

TEST_F(GoogleEmailEnforcerTest, GetEmailFromUserInfoTest) {
  std::string email;

  // Test that invalid base64 encoded strings are rejected.
  ASSERT_FALSE(GetEmailFromEncodedUserInfo("!!!!!!!!!", &email).ok());

  // Test that invalid json is rejected.
  // b64encode('hello world')
  ASSERT_FALSE(GetEmailFromEncodedUserInfo("aGVsbG8gd29ybGQ=", &email).ok());

  // Test that non-object json is rejected.
  // b64encode('[1,2,3]')
  ASSERT_FALSE(GetEmailFromEncodedUserInfo("WzEsMiwzXQ==", &email).ok());

  // Test that objects that do not contain an "email" field are rejected.
  // b64encode('{"hi": "there"}')
  ASSERT_FALSE(
      GetEmailFromEncodedUserInfo("eyJoaSI6ICJ0aGVyZSJ9", &email).ok());

  // Test that a non-string "email" field is rejected.
  // b64encode('{"email": 21}')
  ASSERT_FALSE(
      GetEmailFromEncodedUserInfo("eyJlbWFpbCI6IDIxfQ==", &email).ok());

  // Test that if all the requirements are met, the email field is properly set.
  // b64encode('{"email": "hello"}')
  ASSERT_TRUE(
      GetEmailFromEncodedUserInfo("eyJlbWFpbCI6ICJoZWxsbyJ9", &email).ok());
  ASSERT_EQ("hello", email);
}

// Check that the LogOnlyEnforcer only returns OK status.
TEST(LogOnlyEnforcerTest, AlwaysOK) {
  LogOnlyEnforcer null(std::shared_ptr<AuthEnforcer>(new NullEnforcer()));
  ASSERT_TRUE(null.CheckAuthorization(nullptr, 0, 0, 0).ok());

  LogOnlyEnforcer neg(std::shared_ptr<AuthEnforcer>(new NegativeEnforcer()));
  ASSERT_TRUE(neg.CheckAuthorization(nullptr, 0, 0, 0).ok());
}

}  // namespace analyzer
}  // namespace cobalt
