// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_
#define FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_

#include <xtp_quote_api.h>
#include <xtp_trader_api.h>

#include <memory>
#include <string>

#include "Core/Gateway.h"
#include "Gateway/Xtp/XtpCommon.h"
#include "Gateway/Xtp/XtpMdApi.h"
#include "Gateway/Xtp/XtpTradeApi.h"

namespace ft {

class XtpGateway : public Gateway {
 public:
  explicit XtpGateway(TradingEngineInterface* engine);

  bool login(const Config& config) override;

  void logout() override;

  uint64_t send_order(const OrderReq* order) override;

  bool cancel_order(uint64_t order_id) override;

  bool query_contract(const std::string& ticker,
                      const std::string& exchange) override;

  bool query_contracts() override;

  bool query_account() override;

  bool query_position(const std::string& ticker) override;

  bool query_positions() override;

  bool query_trades() override;

  bool query_orders();

 private:
  TradingEngineInterface* engine_ = nullptr;
  std::unique_ptr<XtpTradeApi> trade_api_;
  std::unique_ptr<XtpMdApi> md_api_;
};

}  // namespace ft

#endif  // FT_SRC_GATEWAY_XTP_XTPGATEWAY_H_
