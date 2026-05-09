#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "sock_help.hpp"

#include "ilink_v8/Negotiate500.h"
#include "ilink_v8/Establish503.h"
#include "ilink_v8/Sequence506.h"
#include "ilink_v8/Terminate507.h"
#include "ilink_v8/NewOrderSingle514.h"
#include "ilink_v8/PRICENULL9.h"
#include "ilink_v8/SideReq.h"
#include "ilink_v8/OrderCancelReplaceRequest515.h"
#include "ilink_v8/OrderCancelRequest516.h"
#include "ilink_v8/PartyDetailsDefinitionRequest518.h"
#include "ilink_v8/PartyDetailsListRequest537.h"
#include "ilink_v8/ListUpdAct.h"
#include "ilink_v8/RetransmitRequest508.h"
#include "ilink_v8/Decimal64NULL.h"
#include "ilink_v8/ExecMode.h"
#include "ilink_v8/ExecInst.h"

#include "sign.hpp"
#include "logger/act/Logger.hpp"
#include "ilink/audit.hpp"

#include "ilink/ilink_null.hpp"

/***************************************************************
 *
 * For message definitions see:
 * https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Binary+Order+Entry
 *
 * We only implement new UUID negotiation. No reuse of UUID from previous session
 *
 * Option specific functionality is not implemented
 *
 * *************************************************************/

namespace m2::ilink
{

  class ILinkSnd
  {

  public:
    ILinkSnd(
        uint16_t _KeepAliveInterval,
        const std::string &_Account,
        const std::string &_AccessKeyId,
        const std::string &_SecretKey,
        const std::string &_SessionID,
        const std::string &_FirmID,
        const std::string &_OperatorID,
        const std::string &_TradingSystemName,
        const std::string &_TradingSystemVersion,
        const std::string &_TradingSystemVendor,
        const std::string &_SenderId, // typically same as firm id
        const std::string &_Location,
        uint64_t _PartyDetailsListReqID, // set to pre-registered value
        char self_match_prevention,
        uint64_t &_SelfMatchPreventionID
        )
        : Account(_Account),
          AccessKeyId(_AccessKeyId),
          SecretKey(_SecretKey),
          SessionID(_SessionID),
          FirmID(_FirmID),
          OperatorID(_OperatorID),
          TradingSystemName(_TradingSystemName),
          TradingSystemVersion(_TradingSystemVersion),
          TradingSystemVendor(_TradingSystemVendor),
          SenderId(_SenderId),
          Location(_Location),
          PartyDetailsListReqID(_PartyDetailsListReqID),
          KeepAliveInterval(_KeepAliveInterval),
          SelfMatchPreventionID(_SelfMatchPreventionID)
    {
      UUID = chutil::Time::epoch() / 1000;
      ASSERT(self_match_prevention == 'N' || self_match_prevention == 'O', "self_match_prevention must be 'O' or 'N'");
      SelfMatchPrevention = self_match_prevention;
      ASSERT(SelfMatchPrevention == sbe::SMPI::CancelNewest || SelfMatchPrevention == sbe::SMPI::CancelOldest, "SelfMatchPrevention must be 'N' or 'O' (2)");
    }

  private:
    const char* get_name() const { return "ILinkSnd"; }

    const bool debug = false;
    std::string Account;
    std::string AccessKeyId;
    std::string SecretKey;
    std::string SessionID;
    std::string OperatorID;
    std::string FirmID;
    std::string TradingSystemName;
    std::string TradingSystemVersion;
    std::string TradingSystemVendor;
    uint32_t NextSeqNo = 1;
    uint16_t KeepAliveInterval; // in ms
    uint64_t UUID;
    uint64_t OrderRequestID = 0;
    std::string SenderId;
    uint64_t PartyDetailsListReqID;
    std::string Location;
    const std::string New_Line = "\n";
    char SelfMatchPrevention;
    uint64_t SelfMatchPreventionID;

    /**
     * @brief genrate ts in nanoseconds
     *
     */
    uint64_t generate_time_stamp_nanoseconds() const noexcept
    {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      return ts.tv_sec * 1000000000 + ts.tv_nsec;
    }

    uint16_t generate_days_since_epoch() const noexcept
    {
      return static_cast<uint16_t>(std::chrono::duration_cast<std::chrono::hours>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count() /
                                   24);
    }

    /**
     * @brief Create a canonical negotiate message
     *
     * @param RequestTimeStamp
     * @return std::string
     */
    auto create_canonical_negotiate_message(
        uint64_t RequestTimeStamp) const noexcept
    {
      std::string canonicalMsg;
      canonicalMsg.append(std::to_string(RequestTimeStamp));
      canonicalMsg += '\n';
      canonicalMsg.append(std::to_string(UUID));
      canonicalMsg += '\n';
      canonicalMsg.append(SessionID);
      canonicalMsg += '\n';
      canonicalMsg.append(FirmID);
      return canonicalMsg;
    }

    /**
     * @brief Create a canonical establish message string
     *
     * @param RequestTimeStamp
     * @return auto
     */
    auto create_canonical_establish_message(uint64_t RequestTimeStamp) const noexcept
    {
      std::string canonicalMsg;
      canonicalMsg.append(std::to_string(RequestTimeStamp)).append(New_Line);
      canonicalMsg.append(std::to_string(UUID)).append(New_Line);
      canonicalMsg.append(SessionID).append(New_Line);
      canonicalMsg.append(FirmID).append(New_Line);
      canonicalMsg.append(TradingSystemName).append(New_Line);
      canonicalMsg.append(TradingSystemVersion).append(New_Line);
      canonicalMsg.append(TradingSystemVendor).append(New_Line);
      canonicalMsg.append(std::to_string(NextSeqNo)).append(New_Line);
      canonicalMsg.append(std::to_string(KeepAliveInterval));
      return canonicalMsg;
    }

  public:
    void reset_uuid(u_int64_t _uuid = 0, uint32_t _next_seq_no = 1)
    {
      if (_uuid)
      {
        UUID = _uuid;
      }
      else
      {
        UUID = chutil::Time::epoch() / 1000;
      }
      NextSeqNo = _next_seq_no;
    }

    /**
     * @brief create negotiate message and send it
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Negotiate
     *
     */
    void send_nogotiate_message(int sock) const noexcept
    {
      log_inf("sending negotiate message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::Negotiate500 negotiate;
      auto msg = negotiate.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.putAccessKeyID(AccessKeyId);
      msg.putFirm(FirmID);
      msg.putSession(SessionID);
      msg.requestTimestamp(RequestTimeStamp);
      auto canonicalMsg = create_canonical_negotiate_message(RequestTimeStamp);
      auto signature = calculateHMAC(SecretKey, canonicalMsg, debug);
      msg.putHMACSignature(signature);
      msg.uUID(UUID);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }
      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());
      sockhelp::send_message(sock, buffer, msg.encodedLength(), true);
    }

    /**
     * @brief send establish message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Establish
     *
     */
    void send_establish_message(int sock) noexcept
    {
      log_inf("sending establish message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::Establish503 establish;
      auto msg = establish.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.putAccessKeyID(AccessKeyId);
      msg.putFirm(FirmID);
      msg.putSession(SessionID);
      msg.requestTimestamp(RequestTimeStamp);
      auto canonicalMsg = create_canonical_establish_message(RequestTimeStamp);
      auto sig = calculateHMAC(SecretKey, canonicalMsg, debug);
      msg.putHMACSignature(sig);
      msg.uUID(UUID);
      msg.putTradingSystemName(TradingSystemName);
      msg.putTradingSystemVersion(TradingSystemVersion);
      msg.putTradingSystemVendor(TradingSystemVendor);
      msg.nextSeqNo(NextSeqNo); // Next expected message sequence number from us
      msg.keepAliveInterval(KeepAliveInterval);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }
      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());
      sockhelp::send_message(sock, buffer, msg.encodedLength(), true);
    }

    /**
     * @brief send heartbeat message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Sequence
     * @return void
     */
    void send_sequence(int sock, bool lapsed = false) const noexcept
    {
      log_inf("sending sequence message %d", sock);
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::Sequence506 sequence;
      auto msg = sequence.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.nextSeqNo(NextSeqNo);
      msg.uUID(UUID);
      msg.faultToleranceIndicator(sbe::FTI::Primary);
      if (lapsed)
        msg.keepAliveIntervalLapsed(sbe::KeepAliveLapsed::Lapsed);
      else
        msg.keepAliveIntervalLapsed(sbe::KeepAliveLapsed::NotLapsed);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }
      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());
      sockhelp::send_message(sock, buffer, msg.encodedLength());
    }

    /**
     * @brief send terminate message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Terminate
     */
    void send_terminate(int sock, uint16_t errorCodes = 0) const noexcept
    {
      log_inf("sending terminate message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::Terminate507 terminate;
      auto msg = terminate.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.uUID(UUID);
      msg.requestTimestamp(RequestTimeStamp);
      msg.errorCodes(errorCodes);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }
      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());
      sockhelp::send_message(sock, buffer, msg.encodedLength());
    }


    double round_to_n_digits(double value, int n) {
      double multiplier = std::pow(10.0, n);
      return std::round(value * multiplier) / multiplier;
    }

    double round_to_n_digits(long double value, int n) {
      double multiplier = std::pow(10.0, n);
      return std::round(value * multiplier) / multiplier;
    }

    /**
     * @brief send new order single message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+New+Order+-+Single
     */
    uint32_t send_new_order_single(
        int sock,
        long double price,
        int num_digits,
        uint32_t qty,
        int32_t securityID,
        sbe::SideReq::Value side,
        const std::string &cloid,
        double stop_px,
        uint32_t min_qty,
        uint32_t display_qty,
        sbe::OrderTypeReq::Value ord_type,
        sbe::TimeInForce::Value time_in_force) noexcept
    {
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      log_inf("sending new order single message sock: %d, qty: %d, securityid: %d, side: %d, cloid: %s, price: %f, stoppx: %f, min_qty: %d, display_qty: %d, ord_type: %d, time_in_force: %d, RequestTimeStamp: %d, OrderRequestID: %d",
              sock,
              qty,
              securityID,
              int(side),
              cloid,
              price,
              stop_px,
              min_qty,
              display_qty,
              int(ord_type),
              int(time_in_force),
              RequestTimeStamp,
              OrderRequestID
              );
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::NewOrderSingle514 newOrderSingle;
      auto msg = newOrderSingle.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      auto &p = msg.price();

      double price_rounded = round_to_n_digits(price, num_digits);
      auto price9 = int64_t(round(price_rounded * 1e9));
      //
      // test this carefully for any rounding issues
      // originally was:       
      // auto price9 = int64_t(round(price * 1e9));
      //

      log_inf("price_rounded: %f, setting price9: %ld, num_digits: %d", price_rounded, price9, num_digits);
      p.mantissa(price9);

      msg.orderQty(qty);
      msg.securityID(securityID);
      msg.side(side);
      msg.seqNum(NextSeqNo++);
      msg.putSenderID(SenderId);
      msg.putClOrdID(cloid);
      msg.partyDetailsListReqID(PartyDetailsListReqID);
      msg.orderRequestID(OrderRequestID++);
      msg.sendingTimeEpoch(RequestTimeStamp);
      msg.expireDate(UINT16_NULL);
      auto &rp = msg.reservationPrice();
      rp.mantissa(sbe::PRICENULL9::mantissaNullValue());
      auto &dp = msg.discretionPrice();
      dp.mantissa(sbe::PRICENULL9::mantissaNullValue());

      if (ord_type == sbe::OrderTypeReq::Value::StopLimit || ord_type == sbe::OrderTypeReq::Value::StopwithProtection)
      {
        char price_buffer[32];
        memset(price_buffer, 0, sizeof price_buffer);
        sbe::PRICENULL9 price9(price_buffer, sizeof price_buffer);
        price9.mantissa(stop_px * 1e9);
        msg.stopPx().wrap(price_buffer, 0, sbe::NewOrderSingle514::sbeSchemaVersion(), sizeof price_buffer);
      }
      else
      {
        auto &p = msg.stopPx();
        p.mantissa(sbe::PRICENULL9::mantissaNullValue());
      }
      msg.putLocation(Location);
      if (min_qty)
      {
        msg.minQty(min_qty);
      }
      else
      {
        msg.minQty(UINT32_NULL);
      }
      if (display_qty)
      {
        msg.displayQty(display_qty);
      }
      else
      {
        msg.displayQty(UINT32_NULL);
      }
      msg.ordType(ord_type);
      msg.timeInForce(time_in_force);
      msg.manualOrderIndicator(sbe::ManualOrdIndReq::Automated);
      msg.shortSaleType(sbe::ShortSaleType::NULL_VALUE);
      msg.liquidityFlag(sbe::BooleanNULL::NULL_VALUE);
      msg.managedOrder(sbe::BooleanNULL::NULL_VALUE);
      // msg.executionMode(sbe::ExecMode::NULL_VALUE);
      //
      // setting execution mode to NULL_VALUE causes a reject
      // so we set it to 0 instead
      // this should generate an exception in << below unless
      // modifications are made to generated sbe code in ExcecMode.h
      union
      {
        sbe::ExecMode::Value em;
        uint8_t u8;
      } em;
      em.u8 = 0;
      msg.executionMode(em.em);

      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());

      std::vector<std::any> vals;
      vals.resize(size_t(m2::ilink::Audit::END));
      vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(msg.sendingTimeEpoch());
      vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::TO_CME;
      vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
      vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
      vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
      vals[size_t(m2::ilink::Audit::MessageType)] = "D";
      vals[size_t(m2::ilink::Audit::Instrument)] = msg.securityID();
      vals[size_t(m2::ilink::Audit::OrderFlowID)] = std::string(msg.clOrdID());
      vals[size_t(m2::ilink::Audit::OrderRequestID)] = msg.orderRequestID();
      vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)msg.side();
      vals[size_t(m2::ilink::Audit::Quantity)] = msg.orderQty();
      vals[size_t(m2::ilink::Audit::LimitPrice)] = msg.price().mantissa() / 1e9;
      vals[size_t(m2::ilink::Audit::ClientOrderID)] = std::string(msg.clOrdID());
      std::string order_type;
      order_type = (char)msg.ordType();
      vals[size_t(m2::ilink::Audit::OrderType)] = order_type;
      vals[size_t(m2::ilink::Audit::OrderQualifier)] = (uint8_t)msg.timeInForce();
      vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)msg.manualOrderIndicator();

      if (display_qty)
      {
        vals[size_t(m2::ilink::Audit::DisplayQuantity)] = msg.displayQty();
      }
      vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
      vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = msg.partyDetailsListReqID();
      m2::ilink::send_audit_msg(vals);

      return msg.seqNum();
    }

    /**
     * @brief sendn cancel replace request message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Order+Cancel+Replace+Request
     */
    uint32_t send_cancel_replace(
        int sock,
        long double price,
        uint32_t qty,
        int32_t securityID,
        sbe::SideReq::Value side,
        const std::string &cloid,
        uint64_t ord_id,
        double stop_px,
        uint32_t min_qty,
        uint32_t display_qty,
        sbe::OrderTypeReq::Value ord_type,
        sbe::TimeInForce::Value time_in_force) noexcept
    {
      log_inf("sending cancel replace message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::OrderCancelReplaceRequest515 cancelReplace;
      auto msg = cancelReplace.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      auto &p = msg.price();

      //
      // test this carefully for any rounding issues
      // originally was:       
      // auto price9 = int64_t(round(price * 1e9));
      //
      auto price9 = int64_t(round(price * 1e8) * 10);
      log_inf("setting price9: %ld", price9);
      p.mantissa(price9);

      msg.orderQty(qty);
      msg.securityID(securityID);
      msg.side(side);
      msg.seqNum(NextSeqNo++);
      msg.putSenderID(SenderId);
      msg.putClOrdID(cloid);
      msg.partyDetailsListReqID(PartyDetailsListReqID);
      msg.orderRequestID(OrderRequestID++);
      msg.sendingTimeEpoch(RequestTimeStamp);
      msg.expireDate(UINT16_NULL);
      if (ord_type == sbe::OrderTypeReq::Value::StopLimit || ord_type == sbe::OrderTypeReq::Value::StopwithProtection)
      {
        char price_buffer[32];
        memset(price_buffer, 0, sizeof price_buffer);
        sbe::PRICENULL9 price9(price_buffer, sizeof price_buffer);
        price9.mantissa(stop_px * 1e9);
        msg.stopPx().wrap(price_buffer, 0, sbe::NewOrderSingle514::sbeSchemaVersion(), sizeof price_buffer);
      }
      else
      {
        auto &p = msg.stopPx();
        p.mantissa(sbe::PRICENULL9::mantissaNullValue());
      }
      msg.putLocation(Location);

      if (min_qty)
      {
        msg.minQty(min_qty);
      }
      else
      {
        msg.minQty(UINT32_NULL);
      }
      if (display_qty)
      {
        msg.displayQty(display_qty);
      }

      msg.ordType(ord_type);
      msg.timeInForce(time_in_force);
      msg.manualOrderIndicator(sbe::ManualOrdIndReq::Automated);
      msg.oFMOverride(sbe::OFMOverrideReq::Enabled);
      if (ord_id)
        msg.orderID(ord_id);
      else
        msg.orderID(UINT64_NULL);
      msg.shortSaleType(sbe::ShortSaleType::NULL_VALUE);
      msg.liquidityFlag(sbe::BooleanNULL::NULL_VALUE);
      msg.managedOrder(sbe::BooleanNULL::NULL_VALUE);
      // msg.executionMode(sbe::ExecMode::NULL_VALUE);
      //
      // see NOS
      union
      {
        sbe::ExecMode::Value em;
        uint8_t u8;
      } em;
      em.u8 = 0;
      msg.executionMode(em.em);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());

      std::vector<std::any> vals;
      vals.resize(size_t(m2::ilink::Audit::END));
      vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(msg.sendingTimeEpoch());
      vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::TO_CME;
      vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
      vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
      vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
      vals[size_t(m2::ilink::Audit::MessageType)] = "G";
      vals[size_t(m2::ilink::Audit::Instrument)] = msg.securityID();
      vals[size_t(m2::ilink::Audit::OrderFlowID)] = msg.clOrdID();
      vals[size_t(m2::ilink::Audit::OrderRequestID)] = msg.orderRequestID();
      vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)msg.side();
      vals[size_t(m2::ilink::Audit::Quantity)] = msg.orderQty();
      vals[size_t(m2::ilink::Audit::LimitPrice)] = msg.price().mantissa() / 1e9;
      std::string order_type;
      order_type = (char)msg.ordType();
      vals[size_t(m2::ilink::Audit::OrderType)] = order_type;
      vals[size_t(m2::ilink::Audit::OrderQualifier)] = (uint8_t)msg.timeInForce();
      vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)msg.manualOrderIndicator();

      if (display_qty)
      {
        vals[size_t(m2::ilink::Audit::DisplayQuantity)] = msg.displayQty();
      }
      vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
      vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = msg.partyDetailsListReqID();

      m2::ilink::send_audit_msg(vals);

      return msg.seqNum();
    }

    /**
     * @brief send cancel request message
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Order+Cancel+Request
     */
    uint32_t send_cancel(
        int sock,
        uint64_t orig_ordid, // can be 0 if not available for CME & EBS
        std::string cloid,
        int32_t securityID,
        sbe::SideReq::Value side) noexcept
    {
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();

      log_inf("sending cancel message %d, orig_ordid: %d, cloid: %s, RequestTimeStamp: %d, OrderRequestID: %d", 
        sock, orig_ordid, cloid, RequestTimeStamp, OrderRequestID);

      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::OrderCancelRequest516 cancel;
      auto msg = cancel.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.seqNum(NextSeqNo++);
      msg.putSenderID(SenderId);
      msg.putClOrdID(cloid);
      msg.partyDetailsListReqID(PartyDetailsListReqID);
      msg.orderRequestID(OrderRequestID++);
      msg.sendingTimeEpoch(RequestTimeStamp);
      msg.putLocation(Location);
      if (orig_ordid)
        msg.orderID(orig_ordid);
      else
        msg.orderID(UINT64_NULL);
      msg.securityID(securityID);
      msg.side(side);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());

      std::vector<std::any> vals;
      vals.resize(size_t(m2::ilink::Audit::END));
      vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(msg.sendingTimeEpoch());
      vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::TO_CME;
      vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID ;
      vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
      vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
      vals[size_t(m2::ilink::Audit::MessageType)] = "F";
      vals[size_t(m2::ilink::Audit::Instrument)] = msg.securityID();
      vals[size_t(m2::ilink::Audit::OrderFlowID)] = std::string(msg.clOrdID());
      vals[size_t(m2::ilink::Audit::OrderRequestID)] = msg.orderRequestID();
      vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)msg.side();
      vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)msg.manualOrderIndicator();
      vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
      vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = msg.partyDetailsListReqID();
      vals[size_t(m2::ilink::Audit::ClientOrderID)] = std::string(msg.clOrdID());

      m2::ilink::send_audit_msg(vals);

      return msg.seqNum();
    }

    /**
     * @brief send party details request message
     * For message definitions:
     * @see https://www.cmegroup.com/confluence/display/EPI CSANDBOX/iLink+3+Party+Details+Definition+Request
     * iLink responds with
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Party+Details+Definition+Request+Acknowledgment
     */
    void send_party_details_definition(
        int sock,
        sbe::ListUpdAct::Value list_update_action,
        const std::string &party_detail_id, // redundant
        const std::string &oper // redundant
        ) noexcept
    {
      log_inf("sending party details definition message %d", sock);
      ASSERT(party_detail_id == FirmID, " party_detail_id != FirmID");
      ASSERT(oper == OperatorID, " oper != OperatorID");
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::PartyDetailsDefinitionRequest518 partyDetailsDefinitionRequest;
      auto msg = partyDetailsDefinitionRequest.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.partyDetailsListReqID(RequestTimeStamp);
      msg.sendingTimeEpoch(RequestTimeStamp);
      msg.listUpdateAction(list_update_action);
      msg.seqNum(NextSeqNo++);
      msg.custOrderHandlingInst(sbe::CustOrdHandlInst::ClientElectronic);
      msg.custOrderCapacity(sbe::CustOrderCapacity::Value::Membertradingfortheirownaccount);
      msg.clearingTradePriceType(sbe::SLEDS::TradeClearingatExecutionPrice);
      msg.clearingAccountType(sbe::ClearingAcctType::Value::Customer);
      msg.cmtaGiveupCD(sbe::CmtaGiveUpCD::Value(0));
      msg.selfMatchPreventionInstruction((sbe::SMPI::Value)SelfMatchPrevention);
      msg.avgPxIndicator(sbe::AvgPxInd::NULL_VALUE);
      msg.selfMatchPreventionID(SelfMatchPreventionID);
      msg.executor(UINT64_NULL);
      msg.iDMShortCode(UINT64_NULL);
      sbe::PartyDetailsDefinitionRequest518::NoPartyDetails noPartyDetails = msg.noPartyDetailsCount(3);
      noPartyDetails.next();
      noPartyDetails.putPartyDetailID(Account);
      noPartyDetails.partyDetailRole(sbe::PartyDetailRole::CustomerAccount);
      noPartyDetails.next();
      noPartyDetails.putPartyDetailID(oper);
      noPartyDetails.partyDetailRole(sbe::PartyDetailRole::Operator);
      noPartyDetails.next();
      noPartyDetails.putPartyDetailID(party_detail_id);
      noPartyDetails.partyDetailRole(sbe::PartyDetailRole::ExecutingFirm);

      if (debug)
      {
        std::cerr << "len:" << msg.encodedLength() << " sending " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());

      std::vector<std::any> vals;
      vals.resize(size_t(m2::ilink::Audit::END));
      vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(msg.sendingTimeEpoch());
      vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::TO_CME;
      vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
      vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
      vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
      vals[size_t(m2::ilink::Audit::MessageType)] = "CX";
      vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = msg.partyDetailsListReqID();
      vals[size_t(m2::ilink::Audit::ListUpdateAction)] = 'A';
      vals[size_t(m2::ilink::Audit::CustomerTypeIndicatorCapacity)] = (uint8_t)msg.custOrderCapacity();
      std::string giveupcd;
      giveupcd = (char)msg.cmtaGiveupCD();
      vals[size_t(m2::ilink::Audit::CmtaGiveUpCD)] = giveupcd;
      vals[size_t(m2::ilink::Audit::Origin)] = (uint8_t)msg.clearingAccountType();
      vals[size_t(m2::ilink::Audit::ClearingTradePriceType)] = (uint8_t)msg.clearingTradePriceType();
      vals[size_t(m2::ilink::Audit::CustomerHandlingInstr)] = (char)msg.custOrderHandlingInst();
      vals[size_t(m2::ilink::Audit::TakeUpFirm)] = party_detail_id;
      vals[size_t(m2::ilink::Audit::TakeupAccountIdentifier)] = party_detail_id;

      m2::ilink::send_audit_msg(vals);
    }

    /**
     * @brief send retransmission request
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/Retransmission
     */
    void send_retransmission_request(
        int sock,
        uint32_t from_seq_no,
        uint16_t msg_count,
        uint64_t last_uuid = UINT64_NULL
        ) noexcept
    {
      log_inf("sending retransmission request message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::RetransmitRequest508 retransmitRequest;
      auto msg = retransmitRequest.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.uUID(UUID);
      msg.requestTimestamp(RequestTimeStamp);
      msg.fromSeqNo(from_seq_no);
      msg.msgCount(msg_count);
      msg.lastUUID(last_uuid);
      if (debug)
      {
        std::cerr << "sending: " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());
    }

    /**
     * @brief send party details list request
     * @see https://www.cmegroup.com/confluence/display/EPICSANDBOX/iLink+3+Party+Details+List+Request
     */
    void send_party_details_list_request(
        int sock,
        uint64_t reqid,
        const std::string &partyId)
    {
      log_inf("sending party details list request message %d", sock);
      auto RequestTimeStamp = generate_time_stamp_nanoseconds();
      char buffer[1024];
      memset(buffer, 0, sizeof buffer);
      sbe::PartyDetailsListRequest537 partyDetailsListRequest;
      auto msg = partyDetailsListRequest.wrapAndApplyHeader(buffer, sockhelp::SOFH_HEADER_SIZE, sizeof buffer);
      msg.partyDetailsListReqID(reqid);
      msg.sendingTimeEpoch(RequestTimeStamp);
      msg.seqNum(NextSeqNo++);
      sbe::PartyDetailsListRequest537::NoRequestingPartyIDs noReqPartyIds = msg.noRequestingPartyIDsCount(1);
      noReqPartyIds.next();
      noReqPartyIds.putRequestingPartyID(partyId);
      noReqPartyIds.requestingPartyIDSource('C');
      noReqPartyIds.requestingPartyRole('1');
      if (debug)
      {
        std::cerr << "len:" << msg.encodedLength() << " sending " << msg << std::endl;
      }

      std::stringstream ss;
      ss << msg;
      log_inf("msg: %s", ss.str());

      sockhelp::send_message(sock, buffer, msg.encodedLength());
    }
  };
}