// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#include "Gateway/Virtual/VirtualGateway.h"

#include <spdlog/spdlog.h>

#include "Core/ContractTable.h"

namespace ft {

VirtualGateway::VirtualGateway(TradingEngineInterface* engine)
    : engine_(engine) {
  virtual_api_.set_spi(this);
  virtual_api_.start_quote_server();
  virtual_api_.start_trade_server();
}

bool VirtualGateway::login(const Config& config) {
  spdlog::info("[VirtualGateway::login] Virtual API v" VIRTUAL_GATEWAY_VERSION);
  return true;
}

void VirtualGateway::logout() {}

uint64_t VirtualGateway::send_order(const OrderReq* order) {
  VirtualOrderReq req{};
  req.ticker_index = order->ticker_index;
  req.direction = order->direction;
  req.offset = order->offset;
  req.type = order->type;
  req.volume = order->volume;
  req.price = order->price;

  return virtual_api_.insert_order(&req);
}

bool VirtualGateway::cancel_order(uint64_t order_id) {
  return virtual_api_.cancel_order(order_id);
}

bool VirtualGateway::query_contract(const std::string& ticker,
                                    const std::string& exchange) {
  return true;
}

bool VirtualGateway::query_contracts() { return true; }

bool VirtualGateway::query_position(const std::string& ticker) { return true; }

bool VirtualGateway::query_positions() { return true; }

bool VirtualGateway::query_account() {
  Account account{};
  account.account_id = 1234;
  account.balance = 100000000;
  engine_->on_query_account(&account);
  return true;
}

bool VirtualGateway::query_trades() { return true; }

bool VirtualGateway::query_margin_rate(const std::string& ticker) {
  return true;
}

bool VirtualGateway::query_commision_rate(const std::string& ticker) {
  return true;
}

void VirtualGateway::on_order_accepted(uint64_t order_id) {
  engine_->on_order_accepted(order_id);
}

void VirtualGateway::on_order_traded(uint64_t order_id, int traded,
                                     double price) {
  engine_->on_order_traded(order_id, traded, price);
}

void VirtualGateway::on_order_canceled(uint64_t order_id, int canceled) {
  engine_->on_order_canceled(order_id, canceled);
}

void VirtualGateway::on_tick(const TickData* tick) { engine_->on_tick(tick); }

}  // namespace ft
