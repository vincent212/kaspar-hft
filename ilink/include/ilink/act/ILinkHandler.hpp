#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <cstdio>
#include "chutil/Macros.hpp"
#include "actors/Actor.hpp"
#include "actors/msg/Start.hpp"
#include "actors/msg/Shutdown.hpp"
#include "actors/msg/Continue.hpp"

#include "ilink/ILinkSnd.hpp"

#include "ilink/msg/BusinessReject.hpp"
#include "ilink/msg/ExecutionReport.hpp"
#include "ilink/msg/NotApplied.hpp"
#include "ilink/msg/NegotiationReseponse.hpp"
#include "ilink/msg/NegotiationReject.hpp"
#include "ilink/msg/EstablishmentAck.hpp"
#include "ilink/msg/EstablishementReject.hpp"
#include "ilink/msg/Retransmission.hpp"
#include "ilink/msg/RetransmissionReject.hpp"
#include "ilink/msg/CancelReject.hpp"
#include "ilink/msg/PartyDetailsAck.hpp"
#include "ilink/msg/RegisterPartyDetails.hpp"
#include "ilink/msg/Terminate.hpp"
#include "ilink/msg/Sequence.hpp"
#include "ilink/msg/DoInitialize.hpp"
#include "ilink/msg/DoBind.hpp"
#include "ilink/msg/Connected.hpp"
#include "ilink/msg/Disconnected.hpp"
#include "ilink/msg/DoTerminate.hpp"
#include "ilink/msg/Terminate.hpp"
#include "ilink/msg/StartReceiving.hpp"
#include "ilink/msg/ResetUUID.hpp"

#include "ilink_v8/TimeInForce.h"
#include "ilink_v8/OrderType.h"
#include "ilink_v8/SideReq.h"

#include "frame/som/msg/Order.hpp"
#include "frame/som/msg/Cancel.hpp"

#include "frame/som/msg/Ack.hpp"
#include "frame/som/msg/Reject.hpp"
#include "frame/som/msg/CancReject.hpp"
#include "frame/som/msg/Fill.hpp"
#include "frame/som/msg/CancAck.hpp"

#include "frame/ref/RefData.hpp"
#include "frame/ref/Price.hpp"

#include "chutil/Time.hpp"
#include "chutil/Table.hpp"

#include "logger/act/Logger.hpp"

#include "ilink_v8/OrderCancelReject535.h"
#include "ilink_v8/OrderCancelReplaceReject536.h"
#include "ilink_v8/ExecutionReportNew522.h"
#include "ilink_v8/ExecutionReportReject523.h"
#include "ilink_v8/ExecutionReportModify531.h"
#include "ilink_v8/ExecutionReportCancel534.h"
#include "ilink_v8/ExecutionReportStatus532.h"
#include "ilink_v8/ExecutionReportTradeOutright525.h"
#include "ilink_v8/ExecutionReportTradeSpread526.h"
#include "ilink_v8/ExecutionReportTradeAddendumOutright548.h"
#include "ilink_v8/ExecutionReportElimination524.h"
#include "ilink_v8/Sequence506.h"

#include "enum/e_names.hpp"

#include "frame/cons/msg/Get.hpp"
#include "frame/cons/msg/Page.hpp"

#include "actors/act/Timer.hpp"
#include "actors/msg/Timeout.hpp"

#include "ilink/audit.hpp"
#include "ilink/ilink_null.hpp"

#include "db/msg/AddTradeRecord.hpp"

#define MAXNUMREJECTS 250

namespace ilink
{

    struct ILinkHandler : public actors::Actor
    {

        const char* get_name() const override
        {
            return name;
        }

        std::unordered_map<std::string, int> cloid_to_coid;
        std::unordered_map<int, std::string> coid_to_cloid;
        std::unordered_map<std::string, uint64_t> cloid_to_xoid;
        std::unordered_map<uint64_t, std::string> xoid_to_cloid;
        std::unordered_map<uint64_t, int> xoid_to_coid;
        std::unordered_map<int, uint64_t> coid_to_xoid;
        std::unordered_map<int, frame::som::msg::Order> coid_to_order;
        std::set<uint32_t> seen_seqnums;
        bool pre_register;
        bool retransmit_on_establish_ack = false; // should be in config

        // for business reject
        std::unordered_map<uint32_t, int> nos_seqnum_to_coid;
        std::unordered_map<uint32_t, int> canc_seqnum_to_coid;

        // status
        bool negotiate = false;
        bool establish = false;
        bool is_primary = false;
        bool got_terminate = false;
        actor_ptr arbiter = 0;
        actor_ptr rec = 0;
        int timer_interval;
        chutil::Time last_time;
        int timeout_violations = 0;
        uint32_t lastseq = 0, lastseq_seen = 0;
        // todo: this should be initialized to something other than 1
        uint32_t last_seq_num_from_prev_uuid = 1;
        uint64_t KeepAliveInterval;

        // reject counts
        int bus_reject_cnt = 0;
        int exec_reject_cnt = 0;

        // must be set to true for BTEC
        bool cancel_required_x_orderid = false;

        // if this is false then we will send the xoid if available
        // if true then we will not send the xoid ever
        // for BTEC this must be false
        bool cancel_do_not_send_x_orderid = true;

        // retrasmission request
        typedef struct
        {
            uint32_t fromseqnum;
            uint32_t count;
        } retransmission_request_t;

        std::list<retransmission_request_t> retransmission_requests_sent;
        std::list<retransmission_request_t> retransmission_requests_waiting;

        uint32_t last_seq_num_requested() const
        {
            if (retransmission_requests_sent.empty())
                return 0;
            return retransmission_requests_sent.back().fromseqnum + retransmission_requests_sent.back().count;
        }

        // check sequence
        // do not request retransmission if we just got a sequence message
        void check_seq(uint32_t sn, bool busmessage = true)
        {

            // check if there are any retransmission requests waiting
            if (busmessage && !retransmission_requests_waiting.empty())
            {
                auto &req = retransmission_requests_waiting.front();
                if (sn == req.fromseqnum - 1)
                {
                    log_inf("check_seq: found waiting retransmission request: sn: %d, from: %d count: %d", sn, req.fromseqnum, req.count);
                    snd->send_retransmission_request(sock, req.fromseqnum, req.count);
                    retransmission_requests_sent.push_back(req);
                    retransmission_requests_waiting.pop_front();
                }
            }

            if (sn > lastseq + 1)
            {
                auto hasprev = seen_seqnums.find(sn - 1) != seen_seqnums.end();

                if (hasprev)
                {
                    log_inf("not a gap cause sn: %d, has prev", sn);
                }
                else if (busmessage)
                {
                    auto last_req = last_seq_num_requested();
                    if (last_req >= sn && hasprev)
                    {
                        log_inf("not a gap because last_req: %d, sn: %d, hasprev: %d", last_req, sn, hasprev);
                    }
                    else
                    {
                        auto lastseq_adj = std::max(lastseq, last_req);
                        if (sn <= lastseq_adj + 1)
                        {
                            log_err("gap within a gap: sn: %d, lastseq_adj: %d, lastseq: %d, last_req: %d", sn, lastseq_adj, lastseq, last_req);

                            // this is a gap in retrasmission
                            // find previous smaller entry in seen_seqnums
                            auto it = seen_seqnums.lower_bound(sn);
                            if (it != seen_seqnums.begin())
                            {
                                --it; // Now 'it' points to the largest element less than 'sn'
                            }
                            else
                            {
                                ERRF(boost::format("there should be something smaller than sn: %d") % sn);
                            }
                            auto lastseq = *it;
                            log_inf("lastseq: %d", lastseq);
                            auto from = lastseq + 1;
                            auto count = sn - from;
                            ASSERT(count > 0, "count must be greater than 0");
                            log_inf("sending retransmission request: from: %d count: %d", from, count);
                            snd->send_retransmission_request(sock, from, count);
                        }
                        else
                        {
                            auto count = sn - lastseq_adj - 1;
                            ASSERT(count > 0, "count must be greater than 0");
                            auto from_seq_num = lastseq_adj + 1;
                            log_inf("lastseq_adj: %d, lastseq: %d, last_req: %d, sn: %d, count: %d, from_seq_num: %d",
                                    lastseq_adj, lastseq, last_req, sn, count, from_seq_num);
                            log_err("check_seq: failed sending or queing retransmission request: sequence gap sn: %d, lastseq: %d, from: %d count: %d",
                                    sn, lastseq_adj, from_seq_num, count);
                            auto total_count = count;
                            auto first = true;
                            while (total_count > 0)
                            {
                                auto this_count = std::min(2500U, total_count);
                                if (first)
                                {
                                    log_inf("check_seq: sending retransmission request: from: %d count: %d", from_seq_num, this_count);
                                    snd->send_retransmission_request(sock, from_seq_num, this_count);
                                    retransmission_requests_sent.push_back({from_seq_num, this_count});
                                    first = false;
                                }
                                else
                                {
                                    log_inf("check_seq: queing retransmission request: from: %d count: %d", from_seq_num, this_count);
                                    retransmission_requests_waiting.push_back({from_seq_num, this_count});
                                }
                                from_seq_num += this_count;
                                total_count -= this_count;
                            }
                        }
                    }
                }
            }
            else
            {
                log_inf("check_seq: sequence ok: %d", sn);
                lastseq = sn;
            }
            seen_seqnums.insert(sn);
            log_inf("check_seq: sn: %d", sn);
        }

        void set_rec(actor_ptr _rec)
        {
            log_inf("set_rec %s", _rec->get_name());
            rec = _rec;
        }

        std::string FirmID, SessionID, OperatorID;
        en::x exch;
        char name[256];

        actor_ptr db;

        int sock;
        m2::ilink::ILinkSnd *snd;
        // constructor
        ILinkHandler(
            actor_ptr db,
            en::x _exch,
            const std::string &_FirmID,
            const std::string &_SessionID,
            const std::string &_operator_id,
            uint64_t _KeepAliveInterval,
            int _sock,
            m2::ilink::ILinkSnd *_snd,
            bool _pre_register,
            bool _is_primary)
            : db(db),
              sock(_sock),
              snd(_snd),
              pre_register(_pre_register),
              is_primary(_is_primary),
              KeepAliveInterval(_KeepAliveInterval),
              FirmID(_FirmID), SessionID(_SessionID),
              OperatorID(_operator_id),
              exch(_exch)
        {
            snprintf(name, sizeof(name), "ILinkHandler_%d_%s", _is_primary ? 1 : 0, en::to_string(_exch));
            MESSAGE_HANDLER(actors::msg::Start, start_handler);
            MESSAGE_HANDLER(actors::msg::Shutdown, shutdown_handler);
            MESSAGE_HANDLER(actors::msg::Continue, continue_handler);
            MESSAGE_HANDLER(ilink::msg::BusinessReject, businessReject_handler);
            MESSAGE_HANDLER(ilink::msg::ExecutionReport, executionReport_handler);
            MESSAGE_HANDLER(ilink::msg::NotApplied, notApplied_handler);
            MESSAGE_HANDLER(ilink::msg::NegotiationResponse, negotiationResponse_handler);
            MESSAGE_HANDLER(ilink::msg::NegotiationReject, negotiationReject_handler);
            MESSAGE_HANDLER(ilink::msg::EstablishmentAck, establishmentAck_handler);
            MESSAGE_HANDLER(ilink::msg::EstablishmentReject, establishmentReject_handler);
            MESSAGE_HANDLER(ilink::msg::Retransmission, retransmission_handler);
            MESSAGE_HANDLER(ilink::msg::RetransmissionReject, retransmissionReject_handler);
            MESSAGE_HANDLER(ilink::msg::CancelReject, cancelReject_handler);
            MESSAGE_HANDLER(ilink::msg::Terminate, terminate_handler);
            MESSAGE_HANDLER(frame::som::msg::Order, order_handler);
            MESSAGE_HANDLER(frame::som::msg::Cancel, cancel_handler);
            MESSAGE_HANDLER(ilink::msg::PartyDetailsAck, partyDetailsAck_handler);
            MESSAGE_HANDLER(ilink::msg::RegisterPartyDetails, registerPartyDetails_handler);
            MESSAGE_HANDLER(frame::cons::msg::Get, get_handler);
            MESSAGE_HANDLER(actors::msg::Timeout, timeout_handler);
            MESSAGE_HANDLER(ilink::msg::Sequence, sequence_handler);
            MESSAGE_HANDLER(ilink::msg::DoInitialize, do_initialize_handler);
            MESSAGE_HANDLER(ilink::msg::DoTerminate, doTerminate_handler);
            MESSAGE_HANDLER(ilink::msg::DoBind, doBind_handler);
            MESSAGE_HANDLER(ilink::msg::StartReceiving, startreceiving_handler);
            MESSAGE_HANDLER(ilink::msg::ResetUUID, resetUUID_handler);
            last_time = chutil::Time::now_local();
            timer_interval = KeepAliveInterval / 2;
        }

        // The Sequence, Establish, Establishment Acknowledgment and Retransmission messages are sequence forming
        // since they contain the NextSeqNo which indicates the sequence number of subsequent business messages

        // resetUUID handler
        void resetUUID_handler(const ilink::msg::ResetUUID *)
        {
            log_inf("resetUUID_handler");
            snd->reset_uuid();
        }

        // startreceiving handler
        void startreceiving_handler(const ilink::msg::StartReceiving *)
        {
            log_inf("startreceiving_handler");
            rec->send(new ilink::msg::StartReceiving(), this);
        }

        // dobind handler
        // this sends the establish message
        void doBind_handler(const ilink::msg::DoBind *m)
        {
            log_inf("doBind_handler nextseq_from_us: %d, lastseq_from_cme: %d",
                    m->nextseq_from_us,
                    m->lastseq_from_cme);
            arbiter = m->sender;
            ASSERT(arbiter, "no arbiter");
            if (m->uuid)
                snd->reset_uuid(m->uuid, m->nextseq_from_us);
            snd->send_establish_message(sock);
            rec->send(new ilink::msg::StartReceiving(), this);
            last_time = chutil::Time::now_local();
            lastseq = m->lastseq_from_cme;
        }

        // doterminate handler
        void doTerminate_handler(const ilink::msg::DoTerminate *)
        {
            log_inf("doTerminate_handler");
            snd->send_terminate(sock);
        }

        // connect handler
        // this handler sends a nogotiate message
        //
        // Initialization = Negotiate (SBE ID =500)
        // Binding = Establish (SBE ID = 503)
        // The CME Globex requires customer to Negotiate (initialize) only once per week for each MSGW.
        // Once completed for the week, when reconnecting to the MSGW, only an Establish message is required.
        void do_initialize_handler(const ilink::msg::DoInitialize *m)
        {
            log_inf("do_initialize_handler");
            arbiter = m->sender;
            ASSERT(arbiter, "no arbiter");
            snd->send_nogotiate_message(sock);
            rec->send(new ilink::msg::StartReceiving(), this);
            last_time = chutil::Time::now_local();
        }

        // sequence handler
        void sequence_handler(const ilink::msg::Sequence *m)
        {
            log_inf("sequence_handler next seq num: %d, lastseq: %d", m->NextSeqNo, lastseq);
            if (m->KeepAliveLapsed == sbe::KeepAliveLapsed::Lapsed)
            {
                log_err("got sequence and keep alive lapsed");
                snd->send_sequence(sock);
            }
            last_time = chutil::Time::now_local();
            check_seq(m->NextSeqNo - 1, false); // non business message
        }

        // terminate_handler
        void terminate_handler(const ilink::msg::Terminate *m)
        {
            log_err("terminate_handler code: %d, reason: %s", m->ErrorCodes, m->Reason);
            std::cerr << "terminate_handler code: " << m->ErrorCodes << ", reason: " << m->Reason << std::endl;
            sleep(1);
            got_terminate = true;
        }

        // timeout handler
        void timeout_handler(const actors::msg::Timeout *)
        {
            log_inf("timeout_handler");
            if (got_terminate)
                return;
            actors::act::Timer::wake_up_in(this, 0, timer_interval);
            if (last_time.date > 0)
            {
                auto now = chutil::Time::now_local();
                auto diff = now - last_time;
                if (diff > KeepAliveInterval * 1e6) // timer_interval is in milliseconds
                {
                    log_dbg("timeout_handler: no message for %d seconds", diff / 1e9);
                    // std::cerr << "timeout_handler: no message for " << diff / 1e9 << " seconds"
                    //           << "timer_interval: " << timer_interval / 1e3
                    //           << "KeepAliveInterval: " << KeepAliveInterval / 1e3
                    //           << std::endl;
                    timeout_violations++;
                    snd->send_sequence(sock, true);
                }
                else
                    timeout_violations = 0;

                if (timeout_violations > 2)
                {
                    ERR("timeout_handler: too many violations");
                }
                else if (timeout_violations > 1)
                {
                    log_err("timeout_handler: sending terminate too many keep alive violations");
                    snd->send_terminate(sock, 20); // 20 is timeout lapsed
                }
                else if (timeout_violations == 1)
                    snd->send_sequence(sock, true);
                else
                    snd->send_sequence(sock);
            }
            else
            {
                snd->send_sequence(sock);
            }
        }

        void get_handler(const frame::cons::msg::Get *m)
        {
            if (m->what == "status")
            {
                std::vector<std::string> cols = {"name", "value"};
                chutil::Table t("status", cols);
                t.add_row();
                t.set_value("negotiate");
                t.set_value(negotiate);
                t.add_row();
                t.set_value("establish");
                t.set_value(establish);
                t.add_row();
                t.set_value("is_primary");
                t.set_value(is_primary);
                reply(new frame::cons::msg::Page(t.to_string()));
            }
        }

        // partyDetailsAck_handler
        void partyDetailsAck_handler(const ilink::msg::PartyDetailsAck *m)
        {

            log_inf("partyDetailsAck_handler: uuid: %d, partydetailslistreqid: %d, partyrequeststatus: %d",
                    m->UUID,
                    m->PartyDetailsListReqID,
                    m->PartyRequestStatus);

            auto l = m->partyDetailID.size();
            for (size_t i = 0; i < l; i++)
            {
                log_inf("partyDetailsAck_handler: partyDetailID: %s, partyDetailSource: %s, partyDetailRole: %d",
                        m->partyDetailID[i],
                        m->partyDetailSource[i],
                        int(m->partyDetailRole[i]));
            }
            last_time = chutil::Time::now_local();
            lastseq = m->SeqNum;
            check_seq(m->SeqNum);

            // send party details list request
            snd->send_party_details_list_request(
                sock,
                m->PartyDetailsListReqID,
                FirmID);
        }

        // registerPartyDetails_handler
        void registerPartyDetails_handler(const ilink::msg::RegisterPartyDetails *)
        {
            log_inf("registerPartyDetails_handler");
            snd->send_party_details_definition(
                sock,
                sbe::ListUpdAct::Value::Add,
                FirmID,
                OperatorID);
        }

        static std::string gen_cloid(const std::string &prefix, int oid)
        {
            ASSERTF(prefix == "STR" || prefix == "RFQ" || prefix == "SMA", (boost::format("prefix is not STR, RFQ or SMA: %s") % prefix));
            auto t = chutil::Time::now_local();
            auto hms = t.get_hour_minute_sec();
            auto hmsi = std::get<0>(hms) * 10000 + std::get<1>(hms) * 100 + std::get<2>(hms);
            auto hmss = boost::lexical_cast<std::string>(hmsi);
            auto cloid = prefix + hmss + "X";
            // HHMMSSX is 10 long
            auto oids = boost::lexical_cast<std::string>(oid);
            if (oids.size() > 10)
                oids = oids.substr(oids.size() - 10);
            cloid += oids;
            ASSERT(cloid.size() <= 20, "cloid size is greater than 20");
            return cloid;
        }

        // order handler
        void order_handler(const frame::som::msg::Order *m)
        {
            ASSERT(m->still_to_be_filled > 0, "order handler: still to be filled is zero");
            ASSERT(m->sz > 0, "order handler: size is zero");
            auto a = frame::ref::RefData::inst().asset(m->sym);
            auto price = frame::ref::Price(m->px, m->sym);
            const auto &cloid = gen_cloid(m->prefix, m->oid);
            sbe::SideReq::Value side;
            if (m->side == en::bs::BUY)
                side = sbe::SideReq::Value::Buy;
            else if (m->side == en::bs::SEL)
                side = sbe::SideReq::Value::Sell;
            else
                SNGH;

            log_inf("order_handler: oid: %d, oid_to_cancel: %d, cloid: %s, sym: %d, side: %d, px: %f, sz: %d, still_to_be_filled: %d",
                    m->oid,
                    m->oid_to_cancel,
                    cloid,
                    m->sym,
                    int(m->side),
                    price.to_double(),
                    m->sz,
                    m->still_to_be_filled);

            if (m->oid_to_cancel != -1)
            {
                log_inf("order handler: cancel replace");
                ERR("cancel replace not implemented");
                auto it = coid_to_cloid.find(m->oid_to_cancel);
                if (it != coid_to_cloid.end())
                {
                    uint64_t xoid;
                    auto &cloid_to_cancel = it->second;
                    auto it2 = cloid_to_xoid.find(cloid_to_cancel);
                    log_err("cloid to cancel: %s not found", cloid_to_cancel);
                    return;
                    // ASSERT(it2 != cloid_to_xoid.end(), "cloid to cancel not found");
                    xoid = it2->second;

                    // send cancel replace
                    auto seqnum = snd->send_cancel_replace(
                        sock,
                        price.to_double(),
                        m->sz,
                        a->sec_id,
                        side,
                        cloid, // should this be cloid or cloid_to_xoid?
                        xoid,  // xoid to cancel
                        0,     // stop px
                        0,     // miqty
                        0,     // display qty
                        sbe::OrderTypeReq::Value::Limit,
                        sbe::TimeInForce::Value::Day);
                    canc_seqnum_to_coid[seqnum] = m->oid;
                }
                else
                    ERR("order to cancel not found");
            }
            else
            {
                auto a = frame::ref::RefData::inst().asset(m->sym);
                auto digits = a->n_rounding_digits;
                ASSERT(digits > 0, "rounding digits must be greater than 0");
                auto seqnum = snd->send_new_order_single(
                    sock,
                    price.to_double(),
                    digits,
                    int(m->sz + .0001),
                    a->sec_id,
                    side,
                    cloid,
                    0, // stop px
                    0, // miqty
                    0, // display qty
                    sbe::OrderTypeReq::Value::Limit,
                    sbe::TimeInForce::Value::Day);
                nos_seqnum_to_coid[seqnum] = m->oid;
                log_inf("order handler: new order seqnum: %d, oid: %d", seqnum, m->oid);
            }
            cloid_to_coid[cloid] = m->oid;
            coid_to_order[m->oid] = *m;
            coid_to_cloid[m->oid] = cloid;
        }

        // cancel handler
        void cancel_handler(const frame::som::msg::Cancel *m)
        {
            log_inf("cancel handler");
            auto p = coid_to_order.find(m->id);
            ASSERT(p != coid_to_order.end(), "order not found");
            auto p2 = coid_to_cloid.find(m->id);
            ASSERT(p2 != coid_to_cloid.end(), "cloid not found");
            auto &cloid = p2->second;
            uint64_t xoid = 0; // leave at 0 if not available
            if (!cancel_do_not_send_x_orderid)
            {
                // try to use xoid if available
                bool cancel_has_x_order_id;
                auto p3 = cloid_to_xoid.find(p2->second);
                if (p3 == cloid_to_xoid.end() && cancel_required_x_orderid)
                {
                    // must have xoid (for BTEC)
                    log_inf("cl order id: %d, not acked but required", m->id);
                    m->sender->send(new frame::som::msg::CancReject(
                        m->id,
                        frame::som::msg::CancReject::NOTACKED), this);
                    return;
                }
                else if (p3 != cloid_to_xoid.end())
                    cancel_has_x_order_id = true;
                else
                    cancel_has_x_order_id = false;

                if (cancel_has_x_order_id)
                {
                    log_inf("have xoid: %d", p3->second);
                    xoid = p3->second;
                }
                else
                {
                    log_inf("do not have xoid");
                    xoid = 0;
                }
            }

            auto a = frame::ref::RefData::inst().asset(p->second.sym);
            sbe::SideReq::Value side;
            if (p->second.side == en::bs::BUY)
                side = sbe::SideReq::Value::Buy;
            else if (p->second.side == en::bs::SEL)
                side = sbe::SideReq::Value::Sell;
            else
                SNGH;

            auto seqnum = snd->send_cancel(
                sock,
                xoid,
                cloid,
                a->sec_id,
                side);
            canc_seqnum_to_coid[seqnum] = m->id;
        }

        // notification about upcoming retransmission
        void retransmission_handler(const ilink::msg::Retransmission *m)
        {
            log_inf("retransmission handler uuid: %d, lastuuid: %d, msgcnt: %d", m->UUID, m->LastUUID, m->MsgCount);
        }

        // retransmission reject
        void retransmissionReject_handler(const ilink::msg::RetransmissionReject *m)
        {
            log_err("retransmissionReject_handler reason: %s, code: %d", m->Reason, m->errorCodes);
            if (m->errorCodes == 3)
            {
                log_err("retransmissionReject_handler: retransmission already in progress");
            }
            else
            {
                // std::cerr << "retransmissionReject_handler reason: " << m->Reason << ", code: " << m->errorCodes << std::endl;
                // snd->send_terminate(sock);
            }
        }

        // cancel reject
        void cancelReject_handler(const ilink::msg::CancelReject *m)
        {
            const auto &param = m->cancel_reject_param;
            log_inf("cancelReject_handler cloid: %s, xoid: %d, reason: %d",
                    param.ClOrdID, param.OrderID, param.CxlRejReason);

            auto p = cloid_to_coid.find(param.ClOrdID);

            auto coid = p->second;
            const auto &p2 = coid_to_order.find(coid);
            ASSERT(p2 != coid_to_order.end(), "order not found");
            auto &order = p2->second;
            if (param.templateId == sbe::OrderCancelReject535::sbeTemplateId())
            {
                order.sender->send(new frame::som::msg::CancReject(
                    coid,
                    frame::som::msg::CancReject::FROMEXCHANGE), this);
            }
            else if (param.templateId == sbe::OrderCancelReplaceReject536::sbeTemplateId())
            {
                order.sender->send(new frame::som::msg::Reject(
                    coid), this);
                order.sender->send(new frame::som::msg::CancReject(
                    coid,
                    frame::som::msg::CancReject::FROMEXCHANGE), this);
            }
            else
                SNGH;
            last_time = chutil::Time::now_local();
            check_seq(m->cancel_reject_param.SeqNum);
        }

        // business reject
        void businessReject_handler(const ilink::msg::BusinessReject *m)
        {
            log_err("businessReject_handler reason: %d, refid: %d, refmsgtype: %s, refseqnum: %d, reftagid: %d, text: %s",
                    m->BusinessRejectReason,
                    m->BusinessRejectRefID,
                    m->RefMsgType,
                    m->RefSeqNum,
                    m->TagId,
                    m->Text);
            last_time = chutil::Time::now_local();
            check_seq(m->SeqNum);

            if (bus_reject_cnt++ > MAXNUMREJECTS)
            {
                std::cerr << "too many business rejects" << std::endl;
                ERR("too many business rejects");
            }

            // BusinessRejectRefID is the seq number of the message that was rejected

            auto p0 = nos_seqnum_to_coid.find(m->BusinessRejectRefID);
            if (p0 != nos_seqnum_to_coid.end())
            {
                auto coid = p0->second;
                auto p1 = coid_to_order.find(coid);
                ASSERT(p1 != coid_to_order.end(), "order not found");
                auto &order = p1->second;
                if (m->BusinessRejectReason == 8)
                {
                    order.sender->send(new frame::som::msg::Reject(
                        coid,
                        frame::som::msg::Reject::THROTTLE), this);
                }
                else if (m->BusinessRejectReason == 107)
                {
                    order.sender->send(new frame::som::msg::Reject(
                        coid,
                        frame::som::msg::Reject::EXCHLIMIT), this);
                }
                else
                {
                    order.sender->send(new frame::som::msg::Reject(
                        coid,
                        frame::som::msg::Reject::THROTTLE // should be something else
                        ), this);
                }
            }
            else
            {
                log_inf("BusinessRejectRefID: %d, is not in nos_seqnum_to_coid", m->BusinessRejectRefID);
            }

            auto p1 = canc_seqnum_to_coid.find(m->BusinessRejectRefID);
            if (p1 != canc_seqnum_to_coid.end())
            {
                auto coid = p1->second;
                auto p2 = coid_to_order.find(coid);
                if (p2 == coid_to_order.end())
                {
                    log_err("order not found");
                    return;
                }
                // ASSERT(p2 != coid_to_order.end(), "order not found");
                auto &order = p2->second;
                if (m->BusinessRejectReason == 8)
                {
                    order.sender->send(new frame::som::msg::CancReject(
                        coid,
                        frame::som::msg::CancReject::THROTTLE), this);
                }
                else
                {
                    order.sender->send(new frame::som::msg::CancReject(
                        coid,
                        frame::som::msg::CancReject::FROMEXCHANGE_BUSINESSREJECT), this);
                }
            }
            else
            {
                log_inf("BusinessRejectRefID: %d, is not in canc_seqnum_to_coid", m->BusinessRejectRefID);
            }

            std::vector<std::any> vals;
            vals.resize(size_t(m2::ilink::Audit::END));
            vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(m->SendingTime);
            vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
            vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
            vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
            vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
            vals[size_t(m2::ilink::Audit::MessageType)] = "j";
            vals[size_t(m2::ilink::Audit::OrderFlowID)] = m->BusinessRejectRefID;
            vals[size_t(m2::ilink::Audit::RejectReason)] = m->BusinessRejectReason;
            m2::ilink::send_audit_msg(vals);
        }

        // execution report
        void executionReport_handler(const ilink::msg::ExecutionReport *m)
        {
            last_time = chutil::Time::now_local();

            const auto &param = m->exec_report_param;
            // find chopin id
            auto p0 = cloid_to_coid.find(std::string(param.ClOrdID));

            if (p0 == cloid_to_coid.end())
            {

                std::string msg_typ = "unknonwn";
                switch (param.templateId)
                {
                case sbe::ExecutionReportReject523::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportReject523";
                    break;
                }
                case sbe::ExecutionReportNew522::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportNew522";
                    break;
                }
                case sbe::ExecutionReportModify531::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportModify531";
                    break;
                }
                case sbe::ExecutionReportCancel534::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportCancel534";
                    break;
                }
                case sbe::ExecutionReportStatus532::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportStatus532";
                    break;
                }
                case sbe::ExecutionReportTradeOutright525::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportTradeOutright525";

                    std::vector<std::any> vals;
                    vals.resize(size_t(m2::ilink::Audit::END));
                    vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                    vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                    vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                    vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                    vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                    vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)param.ManualOrderIndicator;
                    vals[size_t(m2::ilink::Audit::MessageType)] = "8--1";
                    vals[size_t(m2::ilink::Audit::Instrument)] = param.SecurityID;
                    vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                    vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                    vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                    vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                    vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)param.Side;
                    vals[size_t(m2::ilink::Audit::FillPrice)] = param.Price_mantissa * pow(10, param.Price_exponent);
                    vals[size_t(m2::ilink::Audit::FillQuantity)] = param.LastQty;
                    vals[size_t(m2::ilink::Audit::CumulativeQuantity)] = param.CumQty;
                    vals[size_t(m2::ilink::Audit::RemainingQuantity)] = param.LeavesQty;
                    vals[size_t(m2::ilink::Audit::AggressorFlag)] = param.AggressorIndicator;
                    vals[size_t(m2::ilink::Audit::CMEGlobexMessageID)] = param.SideTradeID;
                    vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = param.PartyDetailsListReqID;

                    m2::ilink::send_audit_msg(vals);

                    break;
                }
                case sbe::ExecutionReportElimination524::sbeTemplateId():
                {
                    msg_typ = "ExecutionReportElimination524";
                    break;
                }
                }

                log_err("executionReport_handler: got message: %s, templateid: %d, but cloid not found as this order was not placed by the system"
                        "exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d, last: %d, leaves: %d, orderqty: %d, cum: %d",
                        msg_typ,
                        int(param.templateId),
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID,
                        param.LastQty,
                        param.LeavesQty,
                        param.OrderQty,
                        param.CumQty);
                check_seq(param.SeqNum);
                return;
            }
            auto coid = p0->second;
            // find order
            auto p2 = coid_to_order.find(coid);
            ASSERT(p2 != coid_to_order.end(), "order not found");
            auto &order = p2->second;
            switch (param.templateId)
            {
            case sbe::ExecutionReportReject523::sbeTemplateId():
            {
                // this is a reject
                log_inf("executionReport_handler: reject exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d",
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID);
                order.sender->send(new frame::som::msg::Reject(
                    coid), this);

                if (exec_reject_cnt++ > MAXNUMREJECTS)
                {
                    std::cerr << "too many execution rejects" << std::endl;
                    ERR("too many execution rejects");
                }

                std::vector<std::any> vals;
                vals.resize(size_t(m2::ilink::Audit::END));
                vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                vals[size_t(m2::ilink::Audit::MessageType)] = "8--8";
                vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                m2::ilink::send_audit_msg(vals);
                break;
            }

            case sbe::ExecutionReportNew522::sbeTemplateId():
            {
                // this is an ack
                log_inf("executionReport_handler: new exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d",
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID);
                order.sender->send(new frame::som::msg::Ack(
                    coid,
                    param.OrderID), this);
                cloid_to_xoid[std::string(param.ClOrdID)] = param.OrderID;

                std::vector<std::any> vals;
                vals.resize(size_t(m2::ilink::Audit::END));
                vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                vals[size_t(m2::ilink::Audit::MessageType)] = "8--0";
                vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)param.Side;
                vals[size_t(m2::ilink::Audit::Quantity)] = param.OrderQty;
                vals[size_t(m2::ilink::Audit::LimitPrice)] = param.Price_mantissa * pow(10, param.Price_exponent);
                vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)param.ManualOrderIndicator;
                std::string order_type;
                order_type = (char)param.OrdType;
                vals[size_t(m2::ilink::Audit::OrderType)] = order_type;
                vals[size_t(m2::ilink::Audit::OrderQualifier)] = (uint8_t)param.TimeInForce;
                if (param.DispQty != UINT32_NULL)
                    vals[size_t(m2::ilink::Audit::DisplayQuantity)] = param.DispQty;
                vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
                vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = param.PartyDetailsListReqID;
                vals[size_t(m2::ilink::Audit::Instrument)] = param.SecurityID;
                m2::ilink::send_audit_msg(vals);

                break;
            }

            case sbe::ExecutionReportModify531::sbeTemplateId():
            {
                // can repl ack
                log_inf("executionReport_handler: modify exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d",
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID);
                order.sender->send(new frame::som::msg::CancAck(
                    order.oid_to_cancel), this);
                order.sender->send(new frame::som::msg::Ack(
                    coid,
                    param.OrderID), this);
                cloid_to_xoid[std::string(param.ClOrdID)] = param.OrderID;

                std::vector<std::any> vals;
                vals.resize(size_t(m2::ilink::Audit::END));
                vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                vals[size_t(m2::ilink::Audit::MessageType)] = "8--5";
                vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (char)param.Side;
                vals[size_t(m2::ilink::Audit::Quantity)] = param.OrderQty;
                vals[size_t(m2::ilink::Audit::LimitPrice)] = param.Price_mantissa * pow(10, param.Price_exponent);
                std::string order_type;
                order_type = (char)param.OrdType;
                vals[size_t(m2::ilink::Audit::OrderType)] = order_type;
                vals[size_t(m2::ilink::Audit::OrderQualifier)] = (uint8_t)param.TimeInForce;
                vals[size_t(m2::ilink::Audit::DisplayQuantity)] = param.DispQty;
                vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
                vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = param.PartyDetailsListReqID;
                vals[size_t(m2::ilink::Audit::Instrument)] = param.SecurityID;
                m2::ilink::send_audit_msg(vals);
                break;
            }

            case sbe::ExecutionReportCancel534::sbeTemplateId():
            {
                // cancel ack
                log_inf("executionReport_handler: cancel exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d",
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID);
                order.sender->send(new frame::som::msg::CancAck(
                    coid), this);

                std::vector<std::any> vals;
                vals.resize(size_t(m2::ilink::Audit::END));
                vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                vals[size_t(m2::ilink::Audit::MessageType)] = "8--4";
                vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)param.Side;
                vals[size_t(m2::ilink::Audit::Quantity)] = param.OrderQty;
                vals[size_t(m2::ilink::Audit::LimitPrice)] = param.Price_mantissa * pow(10, param.Price_exponent);
                std::string order_type;
                order_type = (char)param.OrdType;
                vals[size_t(m2::ilink::Audit::OrderType)] = order_type;
                vals[size_t(m2::ilink::Audit::OrderQualifier)] = (uint8_t)param.TimeInForce;
                if (param.DispQty != UINT32_NULL)
                    vals[size_t(m2::ilink::Audit::DisplayQuantity)] = param.DispQty;
                vals[size_t(m2::ilink::Audit::CountryofOrigin)] = "US";
                vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = param.PartyDetailsListReqID;
                vals[size_t(m2::ilink::Audit::Instrument)] = param.SecurityID;
                vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)param.ManualOrderIndicator;
                m2::ilink::send_audit_msg(vals);
                break;
            }

            case sbe::ExecutionReportStatus532::sbeTemplateId():
            {
                // status
                ERR("status not supported");
                break;
            }

            case sbe::ExecutionReportTradeOutright525::sbeTemplateId():
            {
                // fill
                auto px = param.lastPx_mantissa * pow(10, param.lastPx_exponent);
                bool overfill = false;
                if (order.still_to_be_filled < int(param.LeavesQty))
                {
                    log_err("order overfilled still_to_be_filled: %d, LeavesQty: %d",
                            order.still_to_be_filled,
                            param.LeavesQty);
                    std::cerr << "order overfilled still_to_be_filled: " << order.still_to_be_filled
                              << ", LeavesQty: " << param.LeavesQty << std::endl;
                    overfill = true;
                }
                auto amt_filled = order.still_to_be_filled - param.LeavesQty;

                if (amt_filled != param.LastQty)
                    log_err("amt_filled != param.LastQty, amt_filled: %d, LastQty: %d",
                            amt_filled,
                            param.LastQty);

                order.still_to_be_filled = param.LeavesQty;

                if (!amt_filled)
                {
                    log_err("amt_filled is zero");
                    std::cerr << "amt_filled is zero" << std::endl;
                    overfill = true;
                }
                log_inf("executionReport_handler: coid: %d, trade outright exectype: %s, ordstatus: %s, orderid: %d, cloid: %s, secid: %d, CumQty: %d, LeavesQty: %d, DispQty: %d, OrderQty: %d, px: %f, amt_filled: %d, still_to_be_filled: %d",
                        order.oid,
                        param.ExecType,
                        param.OrdStatus,
                        param.OrderID,
                        param.ClOrdID,
                        param.SecurityID,
                        param.CumQty,
                        param.LeavesQty,
                        param.DispQty,
                        param.OrderQty,
                        px,
                        amt_filled,
                        order.still_to_be_filled);
                if (!overfill)
                    order.sender->send(new frame::som::msg::Fill(
                        order.oid,
                        order.venue,
                        order.sym,
                        frame::ref::Price((long double)px, order.sym),
                        order.side,
                        amt_filled,
                        order.still_to_be_filled,
                        order.owner, param.TransactTime), this);

                if (db)
                {
                    auto a = frame::ref::RefData::get_asset(order.sym);
                    ASSERT(a, "no such asset");

                    db->send(new postrade::msg::AddTradeRecord(
                        param.TransactTime,
                        "ILINK",
                        a->name,
                        a->mnemonic,
                        std::to_string(param.OrderID),
                        param.ExecID,
                        param.ClOrdID,
                        en::to_string(order.side),
                        amt_filled,
                        en::to_string(order.owner),
                        "", "", "",
                        px,
                        order.oid), this);  // chid = system order ID
                }

                std::vector<std::any> vals;
                vals.resize(size_t(m2::ilink::Audit::END));
                vals[size_t(m2::ilink::Audit::SendingTimestamps)] = chutil::Time::from_epoch(param.SendingTime);
                vals[size_t(m2::ilink::Audit::MessageDirection)] = m2::ilink::FROM_CME;
                vals[size_t(m2::ilink::Audit::OperatorID)] = OperatorID;
                vals[size_t(m2::ilink::Audit::ExecutingFirmID)] = FirmID;
                vals[size_t(m2::ilink::Audit::SessionID)] = SessionID;
                vals[size_t(m2::ilink::Audit::ManualOrderIndicator)] = (uint8_t)param.ManualOrderIndicator;
                vals[size_t(m2::ilink::Audit::MessageType)] = "8--1";
                vals[size_t(m2::ilink::Audit::Instrument)] = param.SecurityID;
                vals[size_t(m2::ilink::Audit::OrderFlowID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::CMEGlobexOrderID)] = param.OrderID;
                vals[size_t(m2::ilink::Audit::ClientOrderID)] = param.ClOrdID;
                vals[size_t(m2::ilink::Audit::OrderRequestID)] = param.OrderRequestID;
                vals[size_t(m2::ilink::Audit::BuySellIndicator)] = (uint8_t)param.Side;
                vals[size_t(m2::ilink::Audit::FillPrice)] = param.Price_mantissa * pow(10, param.Price_exponent);
                vals[size_t(m2::ilink::Audit::FillQuantity)] = amt_filled;
                vals[size_t(m2::ilink::Audit::CumulativeQuantity)] = param.CumQty;
                vals[size_t(m2::ilink::Audit::RemainingQuantity)] = param.LeavesQty;
                vals[size_t(m2::ilink::Audit::AggressorFlag)] = param.AggressorIndicator;
                vals[size_t(m2::ilink::Audit::CMEGlobexMessageID)] = param.SideTradeID;
                vals[size_t(m2::ilink::Audit::PartyDetailsListRequestID)] = param.PartyDetailsListReqID;

                m2::ilink::send_audit_msg(vals);
                break;
            }

            case sbe::ExecutionReportTradeSpread526::sbeTemplateId():
            {
                ERR("spread not supported");
                break;
            }

            case sbe::ExecutionReportTradeAddendumOutright548::sbeTemplateId():
            {
                log_err("ExecutionReportTradeAddendumOutright548 not supported");
                break;
            }

            case sbe::ExecutionReportElimination524::sbeTemplateId():
            {
                // typically a forced cancel at end of trading day
                log_wrn("ExecutionReportElimination524 exectype: %s, ordstat: %s secid: %d, cloid: %s, orderid: %d",
                        param.ExecType,
                        param.OrdStatus,
                        param.SecurityID,
                        param.ClOrdID,
                        param.OrderID);

                order.sender->send(new frame::som::msg::CancAck(
                    coid), this);

                break;
            }

            default:
                ERR("unknown template id");
            }

            check_seq(m->exec_report_param.SeqNum);
        }

        // not applied
        void notApplied_handler(const ilink::msg::NotApplied *m)
        {
            std::cerr << "IlinkHandler::notApplied_handler "
                      << m->NextSeqNo << std::endl;

            log_err("NotApplied NextSeqNo: %d", m->NextSeqNo);

            // If the exchange detects a sequence gap from customer, then a Not Applied message will be sent and the customer can take action as needed
            // This can include sending a Sequence message instructing the exchange to ignore the gap and proceed ahead with the NextSeqNo (recommended behavior)
            // This can include trying to replay the messages missed by the exchange (although this is not recommended behavior)

            // send sequence
            snd->send_sequence(sock);
            check_seq(m->NextSeqNo);
        }

        // negotiation response
        void negotiationResponse_handler(const ilink::msg::NegotiationResponse *m)
        {
            log_inf("negotiationResponse_handler UUID: %d, PreviousSeqNo: %d, PreviousUUID: %d",
                    m->UUID,
                    m->PreviousSeqNo,
                    m->PreviousUUID);
            // send establishment request
            send(new ilink::msg::DoBind(), arbiter);
            negotiate = true;
            last_time = chutil::Time::now_local();
        }

        // negotiation reject
        void negotiationReject_handler(const ilink::msg::NegotiationReject *m)
        {
            std::cerr << "IlinkHandler::negotiationReject_handler" << std::endl;
            std::cerr << "negotiationReject_handler reason: " << m->Reason << ", code: " << m->errorCodes << std::endl;
            arbiter->send(new ilink::msg::Disconnected(is_primary), this);
            last_time = chutil::Time::now_local();
        }

        // establishment ack
        void establishmentAck_handler(const ilink::msg::EstablishmentAck *m)
        {
            log_inf("establishmentAck_handler UUID: %d, PreviousSeqNo: %d, PreviousUUID: %d, NextSeqNo: %d, lastseq: %d, last_seq_num_from_prev_uuid: %d",
                    m->UUID,
                    m->PreviousSeqNo,
                    m->PreviousUUID,
                    m->NextSeqNo,
                    lastseq,
                    last_seq_num_from_prev_uuid);
            if (pre_register)
            {
                log_inf("registering party details definition");
                snd->send_party_details_definition(sock, sbe::ListUpdAct::Add, FirmID, OperatorID);
            }
            else
            {
                log_inf("pre-register not set");
            }
            establish = true;
            arbiter->send(new ilink::msg::Connected(is_primary), this);
            actors::act::Timer::wake_up_in(this, 1);
            last_time = chutil::Time::now_local();
            log_inf("previous seqno: %d, last_seq_num_from_prev_uuid: %d", m->PreviousSeqNo, last_seq_num_from_prev_uuid);
            if (m->PreviousSeqNo > last_seq_num_from_prev_uuid)
            {
                log_inf("have gap on establish ack lastseq: %d, NextSeqNo: %d, PrevSeqNo: %d",
                        lastseq,
                        m->NextSeqNo,
                        m->PreviousSeqNo);
                auto count = m->PreviousSeqNo - last_seq_num_from_prev_uuid;
                auto from = last_seq_num_from_prev_uuid + 1;
                if (retransmit_on_establish_ack)
                {
                    log_inf("sending retransmission request from 1 count: %d",
                            count);
                    snd->send_retransmission_request(
                        sock,
                        from,
                        count,
                        m->PreviousUUID);
                    lastseq = m->PreviousSeqNo;
                }
                else
                {
                    log_wrn("retransmit_on_establish_ack not set");
                }
            }
        }

        // establishment reject
        void establishmentReject_handler(const ilink::msg::EstablishmentReject *m)
        {
            std::cout << "IlinkHandler::establishmentReject_handler " << m->Reason << std::endl;
            arbiter->send(new ilink::msg::Disconnected(is_primary), this);
            last_time = chutil::Time::now_local();
        }

        // start handler
        void start_handler(const actors::msg::Start *)
        {
            log_inf("start handler sock: %d", sock);
        }

        // shutdown handler
        void shutdown_handler(const actors::msg::Shutdown *)
        {
            std::cout << "IlinkHandler::shutdown_handler" << std::endl;
        }

        // continue handler
        void continue_handler(const actors::msg::Continue *)
        {
            std::cout << "IlinkHandler::continue_handler" << std::endl;
        }
    };

}