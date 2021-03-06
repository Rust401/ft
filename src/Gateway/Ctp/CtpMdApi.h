// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_GATEWAY_CTP_CTPMDAPI_H_
#define FT_SRC_GATEWAY_CTP_CTPMDAPI_H_

#include <ThostFtdcMdApi.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Core/Config.h"
#include "Core/TickData.h"
#include "Core/TradingEngineInterface.h"
#include "Gateway/Ctp/CtpCommon.h"

namespace ft {

class CtpMdApi : public CThostFtdcMdSpi {
 public:
  explicit CtpMdApi(TradingEngineInterface *engine);

  ~CtpMdApi();

  bool login(const Config &config);

  void logout();

  void OnFrontConnected() override;

  void OnFrontDisconnected(int reason) override;

  void OnHeartBeatWarning(int time_lapse) override;

  void OnRspUserLogin(CThostFtdcRspUserLoginField *login_rsp,
                      CThostFtdcRspInfoField *rsp_info, int req_id,
                      bool is_last) override;

  void OnRspUserLogout(CThostFtdcUserLogoutField *logout_rsp,
                       CThostFtdcRspInfoField *rsp_info, int req_id,
                       bool is_last) override;

  void OnRspError(CThostFtdcRspInfoField *rsp_info, int req_id,
                  bool is_last) override;

  void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                          CThostFtdcRspInfoField *rsp_info, int req_id,
                          bool is_last) override;

  void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                            CThostFtdcRspInfoField *rsp_info, int req_id,
                            bool is_last) override;

  void OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                           CThostFtdcRspInfoField *rsp_info, int req_id,
                           bool is_last) override;

  void OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                             CThostFtdcRspInfoField *rsp_info, int req_id,
                             bool is_last) override;

  void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *md) override;

  void OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *for_quote_rsp) override;

 private:
  int next_req_id() { return next_req_id_++; }

 private:
  TradingEngineInterface *engine_;
  std::unique_ptr<CThostFtdcMdApi, CtpApiDeleter> md_api_;

  std::string server_addr_;
  std::string broker_id_;
  std::string investor_id_;
  std::string passwd_;

  std::atomic<int> next_req_id_ = 0;

  std::atomic<bool> is_error_ = false;
  std::atomic<bool> is_connected_ = false;
  std::atomic<bool> is_logon_ = false;

  std::vector<std::string> sub_list_;
  std::map<std::string, const Contract *> symbol2contract_;
};

}  // namespace ft

#endif  // FT_SRC_GATEWAY_CTP_CTPMDAPI_H_
