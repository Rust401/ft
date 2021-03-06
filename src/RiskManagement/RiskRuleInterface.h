// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_RISKMANAGEMENT_RISKRULEINTERFACE_H_
#define FT_SRC_RISKMANAGEMENT_RISKRULEINTERFACE_H_

#include <string>

#include "Core/ErrorCode.h"
#include "Core/Gateway.h"
#include "Core/Protocol.h"

namespace ft {

class RiskRuleInterface {
 public:
  virtual ~RiskRuleInterface() {}

  virtual int check_order_req(const OrderReq* req) { return NO_ERROR; }

  virtual void on_order_sent(uint64_t engine_order_id) {}

  virtual void on_order_traded(uint64_t engine_order_id, int this_traded,
                               double traded_price) {}

  virtual void on_order_completed(uint64_t engine_order_id, int error_code) {}
};

}  // namespace ft

#endif  // FT_SRC_RISKMANAGEMENT_RISKRULEINTERFACE_H_
