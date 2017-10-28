// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_ANALYZER_REPORT_MASTER_AUTH_ENFORCER_H_
#define COBALT_ANALYZER_REPORT_MASTER_AUTH_ENFORCER_H_

#include <memory>

#include "grpc++/grpc++.h"

namespace cobalt {
namespace analyzer {

// AuthEnforcer describes an interface to enforce authorization rules for
// requests to the report master API.
//
// Calls to CheckAuthorization return grpc::Status::OK if the call being checked
// is authorized and PERMISSION_DENIED or UNAUTHENTICATED otherwise.
class AuthEnforcer {
public:
  virtual grpc::Status CheckAuthorization(grpc::ServerContext *context,
                                          uint32_t customer_id,
                                          uint32_t project_id,
                                          uint32_t report_config_id) = 0;
  virtual ~AuthEnforcer() = default;

  static std::shared_ptr<AuthEnforcer> CreateFromFlagsOrDie();
};

// NullEnforcer allows all requests.
class NullEnforcer final : public AuthEnforcer {
public:
  grpc::Status CheckAuthorization(grpc::ServerContext *context,
                                  uint32_t customer_id, uint32_t project_id,
                                  uint32_t report_config_id) override;
  virtual ~NullEnforcer() = default;
};

// NegativeEnforcer always denies permission. It is used for testing.
class NegativeEnforcer final : public AuthEnforcer {
public:
  grpc::Status CheckAuthorization(grpc::ServerContext *context,
                                  uint32_t customer_id, uint32_t project_id,
                                  uint32_t report_config_id) override;
  virtual ~NegativeEnforcer() = default;
};

// GoogleEmailEnforcer assumes requests were initially authenticated by the
// endpoints service. This enforcer then checks that the authenticated user
// is a google.com account.
class GoogleEmailEnforcer final : public AuthEnforcer {
public:
  grpc::Status CheckAuthorization(grpc::ServerContext *context,
                                  uint32_t customer_id, uint32_t project_id,
                                  uint32_t report_config_id) override;
  virtual ~GoogleEmailEnforcer() = default;

private:
  friend class GoogleEmailEnforcerTest;

  static grpc::Status GetEmailFromEncodedUserInfo(
      const std::string &encoded_user_info, std::string *email);
  static grpc::Status GetEmailFromServerContext(
      grpc::ServerContext *context, std::string *email);
  static bool CheckGoogleEmail(std::string email);
};

// LogOnlyEnforcer calls its underlying enforcer, logs any error the underlying
// enforcer returns and then returns an OK status.
// The purpose of LogOnlyEnforcer is to be able to see what would be the effect
// of turning on authorization.
class LogOnlyEnforcer final : public AuthEnforcer {
public:
  grpc::Status CheckAuthorization(grpc::ServerContext *context,
                                  uint32_t customer_id, uint32_t project_id,
                                  uint32_t report_config_id) override;
  virtual ~LogOnlyEnforcer() = default;
  LogOnlyEnforcer(std::shared_ptr<AuthEnforcer> auth_enforcer);

private:
  std::shared_ptr<AuthEnforcer> enforcer_;
};

} // namespace analyzer
} // namespace cobalt

#endif // COBALT_ANALYZER_REPORT_MASTER_AUTH_ENFORCER_H_
