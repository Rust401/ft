// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#include "Gateway/Ctp/CtpTradeApi.h"

#include <ThostFtdcTraderApi.h>
#include <spdlog/spdlog.h>

#include "Utils/Misc.h"

namespace ft {

CtpTradeApi::CtpTradeApi(TradingEngineInterface *engine) : engine_(engine) {}

CtpTradeApi::~CtpTradeApi() {
  error();
  logout();
}

bool CtpTradeApi::login(const Config &config) {
  std::unique_lock<std::mutex> lock(query_mutex_);
  // 这里只是做个简单的警告，并不保证安全性，一般来说不会登录两次吧？
  if (is_logon_) {
    spdlog::error("[CtpTradeApi::login] Don't login twice");
    return false;
  }

  reset();
  trade_api_.reset(CThostFtdcTraderApi::CreateFtdcTraderApi());
  if (!trade_api_) {
    spdlog::error("[CtpTradeApi::login] Failed. Failed to CreateFtdcTraderApi");
    return false;
  }

  front_addr_ = config.trade_server_address;
  broker_id_ = config.broker_id;
  investor_id_ = config.investor_id;

  trade_api_->SubscribePrivateTopic(THOST_TERT_QUICK);
  trade_api_->RegisterSpi(this);
  trade_api_->RegisterFront(
      const_cast<char *>(config.trade_server_address.c_str()));
  trade_api_->Init();
  while (!is_connected_) {
    if (is_error_) {
      spdlog::error("[CtpTradeApi::login] Failed. Cannot connect to {}",
                    front_addr_);
      return false;
    }
  }

  if (!config.auth_code.empty()) {
    CThostFtdcReqAuthenticateField auth_req{};
    strncpy(auth_req.BrokerID, config.broker_id.c_str(),
            sizeof(auth_req.BrokerID));
    strncpy(auth_req.UserID, config.investor_id.c_str(),
            sizeof(auth_req.UserID));
    strncpy(auth_req.AuthCode, config.auth_code.c_str(),
            sizeof(auth_req.AuthCode));
    strncpy(auth_req.AppID, config.app_id.c_str(), sizeof(auth_req.AppID));

    if (trade_api_->ReqAuthenticate(&auth_req, next_req_id()) != 0) {
      spdlog::error("[CtpTradeApi::login] Failed. Failed to ReqAuthenticate");
      return false;
    }
    if (!wait_sync()) {
      spdlog::error("[CtpTradeApi::login] Failed. Failed to authenticate");
      return false;
    }
  }

  CThostFtdcReqUserLoginField login_req{};
  strncpy(login_req.BrokerID, config.broker_id.c_str(),
          sizeof(login_req.BrokerID));
  strncpy(login_req.UserID, config.investor_id.c_str(),
          sizeof(login_req.UserID));
  strncpy(login_req.Password, config.password.c_str(),
          sizeof(login_req.Password));

  if (trade_api_->ReqUserLogin(&login_req, next_req_id()) != 0) {
    spdlog::error("[CtpTradeApi::login] Failed. Failed to ReqUserLogin");
    return false;
  }
  if (!wait_sync()) {
    spdlog::error("[CtpTradeApi::login] Failed. Failed to login");
    return false;
  }

  CThostFtdcQrySettlementInfoField settlement_req{};
  strncpy(settlement_req.BrokerID, broker_id_.c_str(),
          sizeof(settlement_req.BrokerID));
  strncpy(settlement_req.InvestorID, investor_id_.c_str(),
          sizeof(settlement_req.InvestorID));

  if (trade_api_->ReqQrySettlementInfo(&settlement_req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::login] Failed. Failed to ReqQrySettlementInfo");
    return false;
  }
  if (!wait_sync()) {
    spdlog::error("[CtpTradeApi::login] Failed. Failed to query settlement");
    return false;
  }

  CThostFtdcSettlementInfoConfirmField confirm_req{};
  strncpy(confirm_req.BrokerID, broker_id_.c_str(),
          sizeof(confirm_req.BrokerID));
  strncpy(confirm_req.InvestorID, investor_id_.c_str(),
          sizeof(confirm_req.InvestorID));

  if (trade_api_->ReqSettlementInfoConfirm(&confirm_req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::login] Failed. Failed to ReqSettlementInfoConfirm");
    return false;
  }
  if (!wait_sync()) {
    spdlog::error(
        "[CtpTradeApi::login] Failed. Failed to confirm settlement info");
    return false;
  }

  if (config.cancel_outstanding_orders_on_startup) {
    spdlog::debug("[CtpTradeApi::login] Cancel outstanding orders on startup");

    CThostFtdcQryOrderField req{};
    strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
    strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));

    if (trade_api_->ReqQryOrder(&req, next_req_id()) != 0) {
      spdlog::error("[CtpTradeApi::login] Failed. Failed to ReqQryOrder");
      return false;
    }
    if (!wait_sync()) {
      spdlog::error("[CtpTradeApi::login] Failed to query orders");
      return false;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  is_logon_ = true;
  return true;
}

void CtpTradeApi::logout() {
  if (is_logon_) {
    CThostFtdcUserLogoutField req{};
    strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
    strncpy(req.UserID, investor_id_.c_str(), sizeof(req.UserID));
    if (trade_api_->ReqUserLogout(&req, next_req_id()) != 0) return;

    while (is_logon_) continue;
  }
}

void CtpTradeApi::OnFrontConnected() {
  spdlog::debug("[CtpTradeApi::OnFrontConnected] Success. Connected to {}",
                front_addr_);
  is_error_ = false;
  is_connected_ = true;
}

void CtpTradeApi::OnFrontDisconnected(int reason) {
  spdlog::error("[CtpTradeApi::OnFrontDisconnected] . Disconnected from {}",
                front_addr_);
  error();
  is_connected_ = false;
}

void CtpTradeApi::OnHeartBeatWarning(int time_lapse) {
  spdlog::warn(
      "[CtpTradeApi::OnHeartBeatWarning] No packet received for some time");
}

void CtpTradeApi::OnRspAuthenticate(
    CThostFtdcRspAuthenticateField *rsp_authenticate_field,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspAuthenticate] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  spdlog::debug("[CTP::OnRspAuthenticate] Success. Investor ID: {}",
                investor_id_);
  done();
}

void CtpTradeApi::OnRspUserLogin(CThostFtdcRspUserLoginField *rsp_user_login,
                                 CThostFtdcRspInfoField *rsp_info, int req_id,
                                 bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspUserLogin] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  front_id_ = rsp_user_login->FrontID;
  session_id_ = rsp_user_login->SessionID;
  int max_order_ref = std::stoi(rsp_user_login->MaxOrderRef);
  next_order_ref_ = max_order_ref + 1;

  spdlog::debug(
      "[CtpTradeApi::OnRspUserLogin] Success. Login as {}. "
      "Front ID: {}, Session ID: {}, Max OrderRef: {}",
      investor_id_, front_id_, session_id_, max_order_ref);
  done();
}

void CtpTradeApi::OnRspQrySettlementInfo(
    CThostFtdcSettlementInfoField *settlement_info,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CTP::OnRspQrySettlementInfo] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  spdlog::debug("[CTP::OnRspQrySettlementInfo] Success");
  done();
}

void CtpTradeApi::OnRspSettlementInfoConfirm(
    CThostFtdcSettlementInfoConfirmField *settlement_info_confirm,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::debug(
        "[CtpTradeApi::OnRspSettlementInfoConfirm] Failed. ErrorMsg: {}",
        gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  spdlog::debug(
      "[CtpTradeApi::OnRspSettlementInfoConfirm] Success. Settlement "
      "confirmed");
  done();
}

void CtpTradeApi::OnRspUserLogout(CThostFtdcUserLogoutField *user_logout,
                                  CThostFtdcRspInfoField *rsp_info, int req_id,
                                  bool is_last) {
  spdlog::debug(
      "[CtpTradeApi::OnRspUserLogout] Success. Broker ID: {}, Investor ID: {}",
      user_logout->BrokerID, user_logout->UserID);
  reset();
}

uint64_t CtpTradeApi::send_order(const OrderReq *order) {
  if (!is_logon_) {
    spdlog::error("[CtpTradeApi::send_order] Failed. Not logon");
    return false;
  }

  const auto *contract = ContractTable::get_by_index(order->ticker_index);
  if (!contract) {
    spdlog::error("[CtpTradeApi::send_order] Contract not found. Index: {}",
                  order->ticker_index);
    return false;
  }

  int order_ref = next_order_ref();
  CThostFtdcInputOrderField req{};
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));
  strncpy(req.InstrumentID, contract->ticker.c_str(), sizeof(req.InstrumentID));
  strncpy(req.ExchangeID, contract->exchange.c_str(), sizeof(req.ExchangeID));
  snprintf(req.OrderRef, sizeof(req.OrderRef), "%d", order_ref);
  req.OrderPriceType = order_type(order->type);
  req.Direction = direction(order->direction);
  req.CombOffsetFlag[0] = offset(order->offset);
  req.LimitPrice = order->price;
  req.VolumeTotalOriginal = order->volume;
  req.CombHedgeFlag[0] = THOST_FTDC_HF_Speculation;
  req.ContingentCondition = THOST_FTDC_CC_Immediately;
  req.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
  req.MinVolume = 1;
  req.IsAutoSuspend = 0;
  req.UserForceClose = 0;

  if (order->type == OrderType::FAK) {
    req.TimeCondition = THOST_FTDC_TC_IOC;
    req.VolumeCondition = THOST_FTDC_VC_AV;
  } else if (order->type == OrderType::FOK) {
    req.TimeCondition = THOST_FTDC_TC_IOC;
    req.VolumeCondition = THOST_FTDC_VC_CV;
  } else {
    req.TimeCondition = THOST_FTDC_TC_GFD;
    req.VolumeCondition = THOST_FTDC_VC_AV;
  }

  std::unique_lock<std::mutex> lock(order_mutex_);
  if (trade_api_->ReqOrderInsert(&req, next_req_id()) != 0) {
    spdlog::error("[CtpTradeApi::send_order] Failed to call ReqOrderInsert");
    return false;
  }

  OrderDetail detail;
  detail.contract = contract;
  order_details_.emplace(order_ref, detail);
  lock.unlock();

  spdlog::debug(
      "[CtpTradeApi::send_order] [{}-{}-{}{}] 订单发送成功. "
      "Volume:{}, Price:{:.3f}",
      order_ref, contract->ticker, direction_str(order->direction),
      offset_str(order->offset), order->volume, order->price);
  return static_cast<uint64_t>(order_ref);
}

void CtpTradeApi::OnRspOrderInsert(CThostFtdcInputOrderField *order,
                                   CThostFtdcRspInfoField *rsp_info, int req_id,
                                   bool is_last) {
  if (!order) {
    spdlog::warn("[CtpTradeApi::OnRspOrderInsert] nullptr");
    return;
  }

  if (order->InvestorID != investor_id_) {
    spdlog::warn(
        "[CtpTradeApi::OnRspOrderInsert] Failed. "
        "= =#Receive RspOrderInsert of other investor");
    return;
  }

  int order_ref;
  try {
    order_ref = std::stoi(order->OrderRef);
  } catch (...) {
    spdlog::error("[CtpTradeApi::OnRspOrderInsert] Invalid order ref");
    return;
  }

  spdlog::error(
      "[CtpTradeApi::OnRspOrderInsert] Rejected. OrderRef: {}, Status: "
      "Rejected, ErrorMsg: {}",
      order->OrderRef, gb2312_to_utf8(rsp_info->ErrorMsg));

  std::unique_lock<std::mutex> lock(order_mutex_);
  auto iter = order_details_.find(order_ref);
  if (iter == order_details_.end()) {
    spdlog::error(
        "[CtpTradeApi::OnRspOrderInsert] Order not found. OrderRef: {}",
        order_ref);
    return;
  }
  order_details_.erase(iter);
  lock.unlock();

  engine_->on_order_rejected(order_ref);
}

void CtpTradeApi::OnRtnOrder(CThostFtdcOrderField *order) {
  if (!order) {
    spdlog::warn("[CtpTradeApi::OnRtnOrder] nullptr");
    return;
  }

  // 听说CTP会收到别人的订单回报？判断一下
  if (order->InvestorID != investor_id_) {
    spdlog::warn("[CtpTradeApi::OnRtnOrder] Failed. Unknown order");
    return;
  }

  // CTP返回的OrderRef不会有问题吧？
  int order_ref = std::stoi(order->OrderRef);

  std::unique_lock<std::mutex> lock(order_mutex_);
  auto iter = order_details_.find(order_ref);
  if (order_details_.find(order_ref) == order_details_.end()) {
    // 出现这种情况应该是启动时撤销之前的未完成订单导致的，否则是bug
    if (order->OrderStatus == THOST_FTDC_OST_PartTradedNotQueueing ||
        order->OrderStatus == THOST_FTDC_OST_Canceled) {
      spdlog::warn(
          "[CtpTradeApi::OnRtnOrder] Outstanding order canceled. Ticker: {}, "
          "OrderRef: {}, Traded/Total: {}/{}",
          order->InstrumentID, order->OrderRef, order->VolumeTraded,
          order->VolumeTotalOriginal);
    } else {
      spdlog::warn("[CtpTradeApi::OnRtnOrder] Order not found. OrderRef: {}",
                   order->OrderRef);
    }

    return;
  }
  auto &detail = iter->second;

  // 被拒单或撤销被拒，回调相应函数
  if (order->OrderSubmitStatus == THOST_FTDC_OSS_InsertRejected) {
    order_details_.erase(iter);
    lock.unlock();
    engine_->on_order_rejected(order_ref);
    return;
  } else if (order->OrderSubmitStatus == THOST_FTDC_OSS_CancelRejected) {
    lock.unlock();
    engine_->on_order_cancel_rejected(order_ref);
    return;
  }

  // 如果只是被CTP接收，则直接返回，只能撤被交易所接受的单
  if (order->OrderStatus == THOST_FTDC_OST_Unknown ||
      order->OrderStatus == THOST_FTDC_OST_NoTradeNotQueueing)
    return;

  // 被交易所接收，则回调on_order_accepted
  if (!detail.accepted_ack) {
    engine_->on_order_accepted(order_ref);
    detail.accepted_ack = true;
  }

  // 处理撤单
  if (order->OrderStatus == THOST_FTDC_OST_PartTradedNotQueueing ||
      order->OrderStatus == THOST_FTDC_OST_Canceled) {
    // 撤单都是一次性撤销所有未成交订单
    // 这里是为了防止重复收到撤单回执
    if (detail.canceled_vol == 0) {
      detail.canceled_vol = order->VolumeTotalOriginal - order->VolumeTraded;
      engine_->on_order_canceled(order_ref, detail.canceled_vol);
    }

    // 这里是处理撤单比回调先到的情况，如果撤单比成交回执先到，则继续等待成交回执到来
    if (detail.canceled_vol + detail.traded_vol == detail.original_vol)
      order_details_.erase(iter);
  }
}

void CtpTradeApi::OnRtnTrade(CThostFtdcTradeField *trade) {
  if (!trade) {
    spdlog::warn("[CtpTradeApi::OnRtnTrade] nullptr");
    return;
  }

  if (trade->InvestorID != investor_id_) {
    spdlog::warn("[CtpTradeApi::OnRtnTrade] Failed. Recv unknown trade");
    return;
  }

  int order_ref = std::stoi(trade->OrderRef);

  std::unique_lock<std::mutex> lock(order_mutex_);
  auto iter = order_details_.find(order_ref);
  if (iter == order_details_.end()) {
    spdlog::error("[CtpTradeApi::OnRtnTrade] Order not found. OrderRef: {}",
                  order_ref);
    return;
  }
  auto &detail = iter->second;

  if (!detail.accepted_ack) engine_->on_order_accepted(order_ref);

  detail.traded_vol += trade->Volume;
  engine_->on_order_traded(order_ref, trade->Volume, trade->Price);

  if (detail.traded_vol + detail.canceled_vol == detail.original_vol)
    order_details_.erase(iter);
}

bool CtpTradeApi::cancel_order(uint64_t order_id) {
  if (!is_logon_) return false;

  int order_ref = static_cast<int>(order_id);
  std::unique_lock<std::mutex> lock(order_mutex_);
  auto iter = order_details_.find(order_ref);
  if (!iter->second.accepted_ack) {
    spdlog::error("[CtpTradeApi::cancel_order] 未被交易所接受的订单不可撤");
    return false;
  }

  auto contract = iter->second.contract;
  lock.unlock();

  CThostFtdcInputOrderActionField req{};
  strncpy(req.InstrumentID, contract->ticker.c_str(), sizeof(req.InstrumentID));
  strncpy(req.ExchangeID, contract->exchange.c_str(), sizeof(req.ExchangeID));
  snprintf(req.OrderRef, sizeof(req.OrderRef), "%d", order_ref);
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));
  req.ActionFlag = THOST_FTDC_AF_Delete;
  req.FrontID = front_id_;
  req.SessionID = session_id_;

  if (trade_api_->ReqOrderAction(&req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::cancel_order] Failed. Failed to ReqOrderAction");
    return false;
  }

  return true;
}

void CtpTradeApi::OnRspOrderAction(CThostFtdcInputOrderActionField *action,
                                   CThostFtdcRspInfoField *rsp_info, int req_id,
                                   bool is_last) {
  if (!action) {
    spdlog::warn("[CtpTradeApi::OnRspOrderAction] nullptr");
  }

  if (action->InvestorID != investor_id_) {
    spdlog::warn("[CtpTradeApi::OnRspOrderAction] Failed. Recv unknown action");
    return;
  }

  spdlog::error("[CtpTradeApi::OnRspOrderAction] Failed. Rejected. Reason: {}",
                gb2312_to_utf8(rsp_info->ErrorMsg));

  int order_ref;
  try {
    order_ref = std::stoi(action->OrderRef);
  } catch (...) {
    spdlog::error("[CtpTradeApi::OnRspOrderAction] Invalid OrderRef");
    return;
  }

  std::unique_lock<std::mutex> lock(order_mutex_);
  auto iter = order_details_.find(order_ref);
  if (iter == order_details_.end()) {
    spdlog::error(
        "[CtpTradeApi::OnRspOrderAction] Order not found. OrderRef: {}",
        order_ref);
    return;
  }
  engine_->on_order_cancel_rejected(order_ref);
}

bool CtpTradeApi::query_contract(const std::string &ticker,
                                 const std::string &exchange) {
  if (!is_logon_) return false;

  CThostFtdcQryInstrumentField req{};
  strncpy(req.InstrumentID, ticker.c_str(), sizeof(req.InstrumentID));
  strncpy(req.ExchangeID, exchange.c_str(), sizeof(req.ExchangeID));

  std::unique_lock<std::mutex> lock(query_mutex_);
  if (trade_api_->ReqQryInstrument(&req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::query_contract] Failed. Failed to ReqQryInstrument");
    return false;
  }

  return wait_sync();
}

bool CtpTradeApi::query_contracts() { return query_contract("", ""); }

void CtpTradeApi::OnRspQryInstrument(CThostFtdcInstrumentField *instrument,
                                     CThostFtdcRspInfoField *rsp_info,
                                     int req_id, bool is_last) {
  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspQryInstrument] Failed. Error Msg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  if (!instrument) {
    spdlog::error(
        "[CtpTradeApi::OnRspQryInstrument] Failed. instrument is nullptr");
    error();
    return;
  }

  spdlog::debug(
      "[CtpTradeApi::OnRspQryInstrument] Success. Instrument: {}, Exchange: {}",
      instrument->InstrumentID, instrument->ExchangeID);

  Contract contract;
  contract.product_type = product_type(instrument->ProductClass);
  contract.ticker = instrument->InstrumentID;
  contract.exchange = instrument->ExchangeID;
  contract.name = gb2312_to_utf8(instrument->InstrumentName);
  contract.product_type = product_type(instrument->ProductClass);
  contract.size = instrument->VolumeMultiple;
  contract.price_tick = instrument->PriceTick;
  contract.max_market_order_volume = instrument->MaxMarketOrderVolume;
  contract.min_market_order_volume = instrument->MinMarketOrderVolume;
  contract.max_limit_order_volume = instrument->MaxLimitOrderVolume;
  contract.min_limit_order_volume = instrument->MinLimitOrderVolume;
  contract.delivery_year = instrument->DeliveryYear;
  contract.delivery_month = instrument->DeliveryMonth;

  engine_->on_query_contract(&contract);

  if (is_last) done();
}

bool CtpTradeApi::query_position(const std::string &ticker) {
  if (!is_logon_) return false;

  CThostFtdcQryInvestorPositionField req{};
  if (!ticker.empty()) {
    auto contract = ContractTable::get_by_ticker(ticker);
    if (!contract) {
      spdlog::error("[CtpTradeApi::query_position] Contract not found: {}",
                    ticker);
      return false;
    }
    strncpy(req.InstrumentID, contract->ticker.c_str(),
            sizeof(req.InstrumentID));
    strncpy(req.ExchangeID, contract->exchange.c_str(), sizeof(req.ExchangeID));
  }
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));

  std::unique_lock<std::mutex> lock(query_mutex_);
  if (trade_api_->ReqQryInvestorPosition(&req, next_req_id()) != 0) {
    spdlog::error("[CtpTradeApi::query_position] Failed to send query req");
    return false;
  }

  return wait_sync();
}

bool CtpTradeApi::query_positions() { return query_position(""); }

void CtpTradeApi::OnRspQryInvestorPosition(
    CThostFtdcInvestorPositionField *position, CThostFtdcRspInfoField *rsp_info,
    int req_id, bool is_last) {
  if (is_error_rsp(rsp_info)) {
    spdlog::error(
        "[CtpTradeApi::OnRspQryInvestorPosition] Failed. Error Msg: {}",
        gb2312_to_utf8(rsp_info->ErrorMsg));
    pos_cache_.clear();
    error();
    return;
  }

  if (position) {
    auto contract = ContractTable::get_by_ticker(position->InstrumentID);
    if (!contract) {
      spdlog::error(
          "[CtpTradeApi::OnRspQryInvestorPosition] Contract not found: {}",
          position->InstrumentID);
      goto check_last;
    }

    auto &pos = pos_cache_[contract->index];
    pos.ticker_index = contract->index;

    bool is_long_pos = position->PosiDirection == THOST_FTDC_PD_Long;
    auto &pos_detail = is_long_pos ? pos.long_pos : pos.short_pos;
    pos_detail.holdings = position->Position;
    pos_detail.yd_holdings = position->Position - position->TodayPosition;
    pos_detail.float_pnl = position->PositionProfit;

    if (is_long_pos)
      pos_detail.frozen = position->LongFrozen;
    else
      pos_detail.frozen = position->ShortFrozen;

    if (pos_detail.holdings > 0 && contract->size > 0)
      pos_detail.cost_price =
          position->PositionCost / (pos_detail.holdings * contract->size);

    spdlog::debug(
        "[CtpTradeApi::OnRspQryInvestorPosition] {}, long:{}, short:{}",
        contract->ticker, pos.long_pos.holdings, pos.short_pos.holdings);
  }

check_last:
  if (is_last) {
    for (auto &[ticker_index, pos] : pos_cache_) {
      UNUSED(ticker_index);
      engine_->on_query_position(&pos);
    }
    pos_cache_.clear();
    done();
  }
}

bool CtpTradeApi::query_account() {
  if (!is_logon_) return false;

  CThostFtdcQryTradingAccountField req{};
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));

  std::unique_lock<std::mutex> lock(query_mutex_);
  if (trade_api_->ReqQryTradingAccount(&req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::query_account] Failed. Failed to "
        "ReqQryTradingAccount");
    return false;
  }

  return wait_sync();
}

void CtpTradeApi::OnRspQryTradingAccount(
    CThostFtdcTradingAccountField *trading_account,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspQryTradingAccount] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  spdlog::debug(
      "[CtpTradeApi::OnRspQryTradingAccount] Success. "
      "Account ID: {}, Balance: {}, Frozen: {}",
      trading_account->AccountID, trading_account->Balance,
      trading_account->FrozenMargin);

  Account account;
  account.account_id = std::stoul(trading_account->AccountID);
  account.balance = trading_account->Balance;
  account.frozen = trading_account->FrozenCash + trading_account->FrozenMargin +
                   trading_account->FrozenCommission;

  engine_->on_query_account(&account);
  done();
}

void CtpTradeApi::OnRspQryOrder(CThostFtdcOrderField *order,
                                CThostFtdcRspInfoField *rsp_info, int req_id,
                                bool is_last) {
  // TODO(kevin)

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspQryOrder] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  if (order && (order->OrderStatus == THOST_FTDC_OST_NoTradeQueueing ||
                order->OrderStatus == THOST_FTDC_OST_PartTradedQueueing)) {
    spdlog::info(
        "[CtpTradeApi::OnRspQryOrder] Cancel all orders on startup. Ticker: "
        "{}.{}, "
        "OrderSysID: {}, OriginalVolume: {}, Traded: {}, StatusMsg: {}",
        order->InstrumentID, order->ExchangeID, order->OrderSysID,
        order->VolumeTotalOriginal, order->VolumeTraded,
        gb2312_to_utf8(order->StatusMsg));

    CThostFtdcInputOrderActionField req{};
    strncpy(req.InstrumentID, order->InstrumentID, sizeof(req.InstrumentID));
    strncpy(req.ExchangeID, order->ExchangeID, sizeof(req.ExchangeID));
    strncpy(req.OrderSysID, order->OrderSysID, sizeof(req.OrderSysID));
    strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
    strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));
    req.ActionFlag = THOST_FTDC_AF_Delete;

    if (trade_api_->ReqOrderAction(&req, next_req_id()) != 0)
      spdlog::error(
          "[CtpTradeApi::OnRspQryOrder] Failed to call ReqOrderAction");
  }

  if (is_last) done();
}

bool CtpTradeApi::query_trades() {
  if (!is_logon_) return false;

  CThostFtdcQryTradeField req{};
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));

  std::unique_lock<std::mutex> lock(query_mutex_);
  if (trade_api_->ReqQryTrade(&req, next_req_id()) != 0) {
    spdlog::error("[CtpTradeApi::query_trades] Failed. Failed to ReqQryTrade");
    return false;
  }

  return wait_sync();
}

void CtpTradeApi::OnRspQryTrade(CThostFtdcTradeField *trade,
                                CThostFtdcRspInfoField *rsp_info, int req_id,
                                bool is_last) {
  // TODO(kevin)
  if (!is_last) return;

  if (is_error_rsp(rsp_info)) {
    spdlog::error("[CtpTradeApi::OnRspQryTrade] Failed. ErrorMsg: {}",
                  gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  if (trade) {
    auto contract = ContractTable::get_by_ticker(trade->InstrumentID);
    assert(contract);

    Trade td{};
    td.ticker_index = contract->index;
    td.volume = trade->Volume;
    td.price = trade->Price;
    td.direction = direction(trade->Direction);
    td.offset = offset(trade->OffsetFlag);
    engine_->on_query_trade(&td);
  }

  if (is_last) done();
}

bool CtpTradeApi::query_margin_rate(const std::string &ticker) {
  CThostFtdcQryInstrumentMarginRateField req{};

  if (!ticker.empty()) {
    auto contract = ContractTable::get_by_ticker(ticker);
    if (!contract) {
      spdlog::error(
          "[CtpTradeApi::query_margin_rate] Contract not found. Ticker: {}",
          ticker);
      return false;
    }
    strncpy(req.InstrumentID, contract->ticker.c_str(),
            sizeof(req.InstrumentID));
    strncpy(req.ExchangeID, contract->exchange.c_str(), sizeof(req.ExchangeID));
  }

  req.HedgeFlag = THOST_FTDC_HF_Speculation;
  strncpy(req.BrokerID, broker_id_.c_str(), sizeof(req.BrokerID));
  strncpy(req.InvestorID, investor_id_.c_str(), sizeof(req.InvestorID));

  std::unique_lock<std::mutex> lock(query_mutex_);
  if (trade_api_->ReqQryInstrumentMarginRate(&req, next_req_id()) != 0) {
    spdlog::error(
        "[CtpTradeApi::query_margin_rate] Failed. "
        "Failed to call ReqQryInstrumentMarginRate");
    return false;
  }

  return wait_sync();
}

void CtpTradeApi::OnRspQryInstrumentMarginRate(
    CThostFtdcInstrumentMarginRateField *margin_rate,
    CThostFtdcRspInfoField *rsp_info, int req_id, bool is_last) {
  if (is_error_rsp(rsp_info)) {
    spdlog::error(
        "[CtpTradeApi::OnRspQryInstrumentMarginRate] Failed. ErrorMsg: {}",
        gb2312_to_utf8(rsp_info->ErrorMsg));
    error();
    return;
  }

  if (margin_rate) {
    // TODO(kevin)
    auto __c = ContractTable::get_by_ticker(margin_rate->InstrumentID);
    if (!__c) {
      spdlog::error(
          "[CtpTradeApi::OnRspQryInstrumentMarginRate] Contract not found: {}",
          margin_rate->InstrumentID);
    }

    spdlog::info("Margin Rate. {}, {}, {}", margin_rate->InstrumentID,
                 margin_rate->LongMarginRatioByMoney,
                 margin_rate->ShortMarginRatioByMoney);

    auto contract = const_cast<Contract *>(__c);
    contract->long_margin_rate = margin_rate->LongMarginRatioByMoney;
    contract->short_margin_rate = margin_rate->ShortMarginRatioByMoney;
  }

  if (is_last) done();
}

}  // namespace ft
