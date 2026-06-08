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
    case app::GnbCliCommand::QNC_NOTIFY: { //kassem
        int ueId       = msg.cmd->ueId; //kassem
        int psi        = msg.cmd->psi; //kassem
        int qfi        = msg.cmd->qfi; //kassem
        bool fulfilled = msg.cmd->fulfilled; //kassem

        if (m_base->ngapTask->m_ueCtx.count(ueId) == 0) { //kassem
            sendError(msg.address, "UE not found with given ID"); //kassem
            break; //kassem
        } //kassem

        auto &pduSessions = m_base->ngapTask->m_pduSessions; //kassem
        if (!pduSessions.count(ueId) || !pduSessions.at(ueId).count(psi)) { //kassem
            sendError(msg.address, "PDU session not found for given UE/PSI"); //kassem
            break; //kassem
        } //kassem

        auto *resource = pduSessions.at(ueId).at(psi); //kassem
        if (!resource) { //kassem
            sendError(msg.address, "PDU session resource is null"); //kassem
            break; //kassem
        } //kassem

        bool qncFound = false; //kassem
        for (auto &flow : resource->qncFlows) { //kassem
            if (flow.qfi == qfi && flow.qncEnabled) { //kassem
                qncFound = true; //kassem
                if (!fulfilled && !flow.altProfiles.empty()) { //kassem
                    flow.activeProfileIndex = //kassem
                        (flow.activeProfileIndex + 1) % //kassem
                        (static_cast<int>(flow.altProfiles.size()) + 1); //kassem
                } //kassem
                break; //kassem
            } //kassem
        } //kassem

        if (!qncFound) { //kassem
            sendError(msg.address, "QFI not found or QNC not enabled for this flow"); //kassem
            break; //kassem
        } //kassem

        m_base->ngapTask->sendQosFlowNotify(ueId, psi, qfi, fulfilled); //kassem
        sendResult(msg.address, std::string("Sent QoS flow notify: ") + //kassem
            (fulfilled ? "fulfilled" : "not-fulfilled")); //kassem
        break; //kassem
    } //kassem
    case app::GnbCliCommand::QNC_NOTIFY_BATCH: { //kassem
        int psi          = msg.cmd->psi; //kassem
        int qfi          = msg.cmd->qfi; //kassem
        bool fulfilled   = msg.cmd->fulfilled; //kassem
        int nbUes        = msg.cmd->nbUes; //kassem
        int nbNotif      = msg.cmd->nbNotif; //kassem
        int hysteresisMs = msg.cmd->hysteresisMs; //kassem

        /* Collect UE IDs that have a valid QNC flow for this PSI/QFI */ //kassem
        std::vector<int> targets; //kassem
        for (auto &ue : m_base->ngapTask->m_ueCtx) { //kassem
            int ueId = ue.first; //kassem
            auto &pduSessions = m_base->ngapTask->m_pduSessions; //kassem
            if (!pduSessions.count(ueId) || !pduSessions.at(ueId).count(psi)) //kassem
                continue; //kassem
            auto *resource = pduSessions.at(ueId).at(psi); //kassem
            if (!resource) //kassem
                continue; //kassem
            for (auto &flow : resource->qncFlows) { //kassem
                if (flow.qfi == qfi && flow.qncEnabled) { //kassem
                    targets.push_back(ueId); //kassem
                    break; //kassem
                } //kassem
            } //kassem
            if ((int)targets.size() >= nbUes) //kassem
                break; //kassem
        } //kassem

        if (targets.empty()) { //kassem
            sendError(msg.address, "No UEs with QNC-enabled flow found for given PSI/QFI"); //kassem
            break; //kassem
        } //kassem

        int actualUes = (int)targets.size(); //kassem

        /* Send nbNotif notifications to each UE */ //kassem
        for (int n = 0; n < nbNotif; n++) { //kassem
            for (int ueId : targets) { //kassem
                auto &pduSessions = m_base->ngapTask->m_pduSessions; //kassem
                /* Guard: UE may have released PDU session since targets was built */ //kassem
                if (!pduSessions.count(ueId) || !pduSessions.at(ueId).count(psi)) //kassem
                    continue; //kassem
                auto *resource = pduSessions.at(ueId).at(psi); //kassem
                if (!resource) //kassem
                    continue; //kassem
                bool qncFound = false; //kassem
                for (auto &flow : resource->qncFlows) { //kassem
                    if (flow.qfi == qfi && flow.qncEnabled) { //kassem
                        qncFound = true; //kassem
                        if (!fulfilled && !flow.altProfiles.empty()) { //kassem
                            flow.activeProfileIndex = //kassem
                                (flow.activeProfileIndex + 1) % //kassem
                                (static_cast<int>(flow.altProfiles.size()) + 1); //kassem
                        } //kassem
                        break; //kassem
                    } //kassem
                } //kassem
                if (!qncFound) //kassem
                    continue; //kassem
                m_base->ngapTask->sendQosFlowNotify(ueId, psi, qfi, fulfilled); //kassem
            } //kassem
            /* Hysteresis between notification bursts */ //kassem
            if (hysteresisMs > 0 && n < nbNotif - 1) //kassem
                std::this_thread::sleep_for(std::chrono::milliseconds(hysteresisMs)); //kassem
        } //kassem

        std::string resultMsg = "Sent " + std::to_string(nbNotif) + //kassem
            " x " + std::string(fulfilled ? "fulfilled" : "not-fulfilled") + //kassem
            " to " + std::to_string(actualUes) + " UEs" + //kassem
            " (PSI=" + std::to_string(psi) + " QFI=" + std::to_string(qfi) + //kassem
            " hysteresis=" + std::to_string(hysteresisMs) + "ms)"; //kassem
        sendResult(msg.address, resultMsg); //kassem
        break; //kassem
    } //kassem
    }
}

} // namespace nr::gnb