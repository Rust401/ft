// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#include "RiskManagement/NoSelfTrade.h"

#include "Core/ContractTable.h"

namespace ft {

int NoSelfTradeRule::check_order_req(const OrderReq* order) {
  const auto* contract = ContractTable::get_by_index(order->ticker_index);
  assert(contract);

  uint64_t opp_d = opp_direction(order->direction);  // 对手方
  const OrderReq* pending_order;
  for (auto& o : orders_) {
    pending_order = &o;
    if (pending_order->direction != opp_d) continue;

    // 存在市价单直接拒绝
    if (pending_order->type == OrderType::MARKET) goto catch_order;

    if (order->direction == Direction::BUY) {
      if (order->price > pending_order->price - 1e-5) goto catch_order;
    } else {
      if (order->price < pending_order->price + 1e-5) goto catch_order;
    }
  }

  orders_.emplace_back(*order);
  return NO_ERROR;

catch_order:
  spdlog::error(
      "[RiskMgr] Self trade! Ticker: {}. This Order: "
      "[Direction: {}, Type: {}, Price: {:.2f}]. "
      "Pending Order: [Direction: {}, Type: {}, Price: {:.2f}]",
      contract->ticker, direction_str(order->direction),
      ordertype_str(order->type), order->price,
      direction_str(pending_order->direction),
      ordertype_str(pending_order->type), pending_order->price);
  return ERR_SELF_TRADE;
}

void NoSelfTradeRule::on_order_completed(uint64_t engine_order_id,
                                         int error_code) {
  for (auto iter = orders_.begin(); iter != orders_.end(); ++iter) {
    if (iter->engine_order_id == engine_order_id) {
      orders_.erase(iter);
      return;
    }
  }
}

}  // namespace ft
