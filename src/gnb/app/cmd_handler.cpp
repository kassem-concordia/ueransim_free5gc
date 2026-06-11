//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "cmd_handler.hpp"

#include <gnb/app/task.hpp>
#include <gnb/gtp/task.hpp>
#include <gnb/ngap/task.hpp>
#include <gnb/rls/task.hpp>
#include <gnb/rrc/task.hpp>
#include <gnb/sctp/task.hpp>
#include <utils/common.hpp>
#include <utils/printer.hpp>

#define PAUSE_CONFIRM_TIMEOUT 3000
#define PAUSE_POLLING 10
#define QLOG(fmt, ...) \
    m_base->ngapTask->m_logger->err("[QNC-DEBUG %s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__)
namespace nr::gnb
{

void GnbCmdHandler::sendResult(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, false));
}

void GnbCmdHandler::sendError(const InetAddress &address, const std::string &output)
{
    m_base->cliCallbackTask->push(std::make_unique<app::NwCliSendResponse>(address, output, true));
}

void GnbCmdHandler::pauseTasks()
{
    m_base->gtpTask->requestPause();
    m_base->rlsTask->requestPause();
    m_base->ngapTask->requestPause();
    m_base->rrcTask->requestPause();
    m_base->sctpTask->requestPause();
}

void GnbCmdHandler::unpauseTasks()
{
    m_base->gtpTask->requestUnpause();
    m_base->rlsTask->requestUnpause();
    m_base->ngapTask->requestUnpause();
    m_base->rrcTask->requestUnpause();
    m_base->sctpTask->requestUnpause();
}

bool GnbCmdHandler::isAllPaused()
{
    if (!m_base->gtpTask->isPauseConfirmed())
        return false;
    if (!m_base->rlsTask->isPauseConfirmed())
        return false;
    if (!m_base->ngapTask->isPauseConfirmed())
        return false;
    if (!m_base->rrcTask->isPauseConfirmed())
        return false;
    if (!m_base->sctpTask->isPauseConfirmed())
        return false;
    return true;
}

void GnbCmdHandler::handleCmd(NmGnbCliCommand &msg)
{
    
    pauseTasks();

    uint64_t currentTime = utils::CurrentTimeMillis();
    uint64_t endTime = currentTime + PAUSE_CONFIRM_TIMEOUT;

    bool isPaused = false;
    while (currentTime < endTime)
    {
        currentTime = utils::CurrentTimeMillis();
        if (isAllPaused())
        {
            isPaused = true;
            break;
        }
        utils::Sleep(PAUSE_POLLING);
    }

    if (!isPaused)
    {
        sendError(msg.address, "gNB is unable process command due to pausing timeout");
    }
    else
    {
        handleCmdImpl(msg);
    }

    unpauseTasks();
}

void GnbCmdHandler::handleCmdImpl(NmGnbCliCommand &msg)
{
    switch (msg.cmd->present)
    {
    case app::GnbCliCommand::STATUS: {
        sendResult(msg.address, ToJson(m_base->appTask->m_statusInfo).dumpYaml());
        break;
    }
    case app::GnbCliCommand::INFO: {
        sendResult(msg.address, ToJson(*m_base->config).dumpYaml());
        break;
    }
    case app::GnbCliCommand::AMF_LIST: {
        Json json = Json::Arr({});
        for (auto &amf : m_base->ngapTask->m_amfCtx)
            json.push(Json::Obj({{"id", amf.first}}));
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::GnbCliCommand::AMF_INFO: {
        if (m_base->ngapTask->m_amfCtx.count(msg.cmd->amfId) == 0)
            sendError(msg.address, "AMF not found with given ID");
        else
        {
            auto amf = m_base->ngapTask->m_amfCtx[msg.cmd->amfId];
            sendResult(msg.address, ToJson(*amf).dumpYaml());
        }
        break;
    }
    case app::GnbCliCommand::UE_LIST: {
        Json json = Json::Arr({});
        for (auto &ue : m_base->ngapTask->m_ueCtx)
        {
            json.push(Json::Obj({
                {"ue-id", ue.first},
                {"ran-ngap-id", ue.second->ranUeNgapId},
                {"amf-ngap-id", ue.second->amfUeNgapId},
            }));
        }
        sendResult(msg.address, json.dumpYaml());
        break;
    }
    case app::GnbCliCommand::UE_COUNT: {
        sendResult(msg.address, std::to_string(m_base->ngapTask->m_ueCtx.size()));
        break;
    }
    case app::GnbCliCommand::UE_RANGE: { //kassem
        if (m_base->ngapTask->m_ueCtx.empty()) {
            sendResult(msg.address, "0 0");
        } else {
            int minId = m_base->ngapTask->m_ueCtx.begin()->first;
            int maxId = minId;
            for (auto &ue : m_base->ngapTask->m_ueCtx) {
                if (ue.first < minId) minId = ue.first;
                if (ue.first > maxId) maxId = ue.first;
            }
            sendResult(msg.address,
                std::to_string(minId) + " " + std::to_string(maxId));
        }
        break;
    }
    case app::GnbCliCommand::UE_RELEASE_REQ: {
        if (m_base->ngapTask->m_ueCtx.count(msg.cmd->ueId) == 0)
            sendError(msg.address, "UE not found with given ID");
        else
        {
            auto ue = m_base->ngapTask->m_ueCtx[msg.cmd->ueId];
            m_base->ngapTask->sendContextRelease(ue->ctxId, NgapCause::RadioNetwork_unspecified);
            sendResult(msg.address, "Requesting UE context release");
        }
        break;
    }
    case app::GnbCliCommand::QNC_NOTIFY: {
        int ueId       = msg.cmd->ueId;
        int psi        = msg.cmd->psi;
        int qfi        = msg.cmd->qfi;
        bool fulfilled = msg.cmd->fulfilled;
 
        QLOG("QNC_NOTIFY ENTER ueId=%d psi=%d qfi=%d fulfilled=%d",
             ueId, psi, qfi, (int)fulfilled);
 
        if (m_base->ngapTask->m_ueCtx.count(ueId) == 0) {
            QLOG("QNC_NOTIFY ABORT: UE=%d not found", ueId);
            sendError(msg.address, "UE not found with given ID");
            break;
        }
        QLOG("QNC_NOTIFY STEP1 OK: UE=%d found", ueId);
 
        auto &pduSessions = m_base->ngapTask->m_pduSessions;
        if (!pduSessions.count(ueId) || !pduSessions.at(ueId).count(psi)) {
            QLOG("QNC_NOTIFY ABORT: no PDU session UE=%d PSI=%d", ueId, psi);
            sendError(msg.address, "PDU session not found for given UE/PSI");
            break;
        }
        QLOG("QNC_NOTIFY STEP2 OK: PDU session found UE=%d PSI=%d", ueId, psi);
 
        auto *resource = pduSessions.at(ueId).at(psi);
        if (!resource) {
            QLOG("QNC_NOTIFY ABORT: resource null UE=%d PSI=%d", ueId, psi);
            sendError(msg.address, "PDU session resource is null");
            break;
        }
        QLOG("QNC_NOTIFY STEP3 OK: resource valid, qncFlows count=%d",
             (int)resource->qncFlows.size());
 
        bool qncFound = false;
        for (auto &flow : resource->qncFlows) {
            if (flow.qfi == qfi && flow.qncEnabled) {
                qncFound = true;
                if (!fulfilled && !flow.altProfiles.empty()) {
                    flow.activeProfileIndex =
                        (flow.activeProfileIndex + 1) %
                        (static_cast<int>(flow.altProfiles.size()) + 1);
                    QLOG("QNC_NOTIFY profile rotated -> activeIndex=%d",
                         flow.activeProfileIndex);
                }
                break;
            }
        }
 
        if (!qncFound) {
            QLOG("QNC_NOTIFY ABORT: QFI=%d not found or QNC not enabled", qfi);
            sendError(msg.address, "QFI not found or QNC not enabled for this flow");
            break;
        }
        QLOG("QNC_NOTIFY STEP4 OK: QNC flow found, calling sendQosFlowNotify");
 
        m_base->ngapTask->sendQosFlowNotify(ueId, psi, qfi, fulfilled);
 
        QLOG("QNC_NOTIFY STEP5 OK: sendQosFlowNotify returned");
        sendResult(msg.address, std::string("Sent QoS flow notify: ") +
            (fulfilled ? "fulfilled" : "not-fulfilled"));
        break;
    }
 
    case app::GnbCliCommand::QNC_NOTIFY_BATCH: {
        int firstUeId    = msg.cmd->firstUeId;   // ← new: start of consecutive range
        int nbUes        = msg.cmd->nbUes;
        int psi          = msg.cmd->psi;
        int qfi          = msg.cmd->qfi;
        bool fulfilled   = msg.cmd->fulfilled;
        int nbNotif      = msg.cmd->nbNotif;
        int hysteresisMs = msg.cmd->hysteresisMs;
 
        QLOG("BATCH ENTER firstUeId=%d nbUes=%d psi=%d qfi=%d fulfilled=%d nbNotif=%d hysteresis=%dms",
             firstUeId, nbUes, psi, qfi, (int)fulfilled, nbNotif, hysteresisMs);
 
        // ── STEP 1: build target list from consecutive range ──────────
        // No m_ueCtx iteration — IDs are explicit, no race condition
        int sentCount    = 0;
        int skippedCount = 0;
        int noQncCount   = 0;
 
        for (int n = 0; n < nbNotif; n++)
        {
            QLOG("BATCH burst %d/%d START", n + 1, nbNotif);
 
            for (int i = 0; i < nbUes; i++)
            {
                int ueId = firstUeId + i;
 
                // guard 1: UE exists
                if (m_base->ngapTask->m_ueCtx.count(ueId) == 0)
                {
                    QLOG("BATCH burst=%d UE=%d SKIP — not registered", n + 1, ueId);
                    skippedCount++;
                    continue;
                }
 
                // guard 2: PDU session exists
                auto &pduSessions = m_base->ngapTask->m_pduSessions;
                if (!pduSessions.count(ueId) || !pduSessions.at(ueId).count(psi))
                {
                    QLOG("BATCH burst=%d UE=%d SKIP — no PDU session PSI=%d", n + 1, ueId, psi);
                    skippedCount++;
                    continue;
                }
 
                auto *resource = pduSessions.at(ueId).at(psi);
                if (!resource)
                {
                    QLOG("BATCH burst=%d UE=%d SKIP — resource null", n + 1, ueId);
                    skippedCount++;
                    continue;
                }
 
                // guard 3: QNC flow active
                bool qncFound = false;
                for (auto &flow : resource->qncFlows)
                {
                    if (flow.qfi == qfi && flow.qncEnabled)
                    {
                        qncFound = true;
                        if (!fulfilled && !flow.altProfiles.empty())
                        {
                            flow.activeProfileIndex =
                                (flow.activeProfileIndex + 1) %
                                (static_cast<int>(flow.altProfiles.size()) + 1);
                            QLOG("BATCH burst=%d UE=%d profile rotated -> idx=%d",
                                 n + 1, ueId, flow.activeProfileIndex);
                        }
                        break;
                    }
                }
 
                if (!qncFound)
                {
                    // print but don't abort — skip silently with a log
                    QLOG("BATCH burst=%d UE=%d SKIP — no QNC flow QFI=%d (qncFlows=%d)",
                         n + 1, ueId, qfi, (int)resource->qncFlows.size());
                    noQncCount++;
                    continue;
                }
 
                // all guards passed — send
                m_base->ngapTask->sendQosFlowNotify(ueId, psi, qfi, fulfilled);
                sentCount++;
                QLOG("BATCH burst=%d UE=%d SENT OK (total sent=%d)", n + 1, ueId, sentCount);
            }
 
            QLOG("BATCH burst %d/%d END (sent=%d skipped=%d noQnc=%d)",
                 n + 1, nbNotif, sentCount, skippedCount, noQncCount);

            if (hysteresisMs > 0 && n < nbNotif - 1)
            {
                // Release pause during sleep so SCTP/NGAP can process heartbeats
                // and keep the AMF connection alive.  Re-acquire before next burst.
                unpauseTasks();
                std::this_thread::sleep_for(std::chrono::milliseconds(hysteresisMs));
                pauseTasks();

                uint64_t repauseEnd = utils::CurrentTimeMillis() + PAUSE_CONFIRM_TIMEOUT;
                bool repaused = false;
                while (utils::CurrentTimeMillis() < repauseEnd)
                {
                    if (isAllPaused()) { repaused = true; break; }
                    utils::Sleep(PAUSE_POLLING);
                }
                if (!repaused)
                {
                    QLOG("BATCH burst %d: re-pause timed out, aborting", n + 1);
                    sendError(msg.address, "Batch aborted: re-pause timed out after burst " +
                                          std::to_string(n + 1));
                    return;
                }
            }
        }
 
        // ── summary ───────────────────────────────────────────────────
        QLOG("BATCH DONE sent=%d skipped=%d noQnc=%d firstUeId=%d nbUes=%d bursts=%d",
             sentCount, skippedCount, noQncCount, firstUeId, nbUes, nbNotif);
 
        std::string resultMsg =
            "Sent " + std::to_string(sentCount) +
            "/" + std::to_string(nbNotif * nbUes) +
            (fulfilled ? " fulfilled" : " not-fulfilled") +
            " (firstUeId=" + std::to_string(firstUeId) +
            " nbUes=" + std::to_string(nbUes) +
            " PSI=" + std::to_string(psi) +
            " QFI=" + std::to_string(qfi) +
            " skipped=" + std::to_string(skippedCount) +
            " noQnc=" + std::to_string(noQncCount) + ")";
        sendResult(msg.address, resultMsg);
        break;
    }
 
    } // end switch
}

} // namespace nr::gnb