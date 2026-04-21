//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#include "encode.hpp"
#include "task.hpp"
#include "utils.hpp"

#include <set>
#include <stdexcept>

#include <gnb/gtp/task.hpp>

#include <asn/ngap/ASN_NGAP_AssociatedQosFlowItem.h>
#include <asn/ngap/ASN_NGAP_AssociatedQosFlowList.h>
#include <asn/ngap/ASN_NGAP_GTPTunnel.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseCommand.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponse.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleaseResponseTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceReleasedItemRelRes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSUReq.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupItemSURes.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequest.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupRequestTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupResponse.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupResponseTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceToReleaseItemRelCmd.h>
#include <asn/ngap/ASN_NGAP_ProtocolIE-Field.h>
#include <asn/ngap/ASN_NGAP_QosFlowPerTNLInformationItem.h>
#include <asn/ngap/ASN_NGAP_QosFlowPerTNLInformationList.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestItem.h>
#include <asn/ngap/ASN_NGAP_QosFlowSetupRequestList.h>
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyRequest.h>          //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyListModReq.h>      //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyItemModReq.h>      //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyRequestTransfer.h> //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyResponse.h>        //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyListModRes.h>      //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyItemModRes.h>      //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceModifyResponseTransfer.h>//kassem
#include <asn/ngap/ASN_NGAP_QosFlowAddOrModifyRequestList.h>           //kassem
#include <asn/ngap/ASN_NGAP_QosFlowAddOrModifyRequestItem.h>           //kassem
#include <asn/ngap/ASN_NGAP_QosFlowAddOrModifyResponseList.h>          //kassem
#include <asn/ngap/ASN_NGAP_QosFlowAddOrModifyResponseItem.h>          //kassem
#include <asn/ngap/ASN_NGAP_QosFlowLevelQosParameters.h>               //kassem
#include <asn/ngap/ASN_NGAP_GBR-QosInformation.h>                     //kassem
#include <asn/ngap/ASN_NGAP_NotificationControl.h>                     //kassem
#include <asn/ngap/ASN_NGAP_AlternativeQoSParaSetList.h>               //kassem
#include <asn/ngap/ASN_NGAP_AlternativeQoSParaSetItem.h>               //kassem
#include <asn/ngap/ASN_NGAP_ProtocolExtensionContainer.h>              //kassem
#include <asn/ngap/ASN_NGAP_ProtocolExtensionField.h>  //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceNotify.h>                //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceNotifyItem.h>            //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceNotifyList.h>            //kassem
#include <asn/ngap/ASN_NGAP_PDUSessionResourceNotifyTransfer.h>        //kassem
#include <asn/ngap/ASN_NGAP_QosFlowNotifyItem.h>                       //kassem
#include <asn/ngap/ASN_NGAP_QosFlowNotifyList.h>                       //kassem
#include <asn/ngap/ASN_NGAP_NotificationCause.h>                       //kassem
namespace nr::gnb
{

void NgapTask::receiveSessionResourceSetupRequest(int amfId, ASN_NGAP_PDUSessionResourceSetupRequest *msg)
{
    std::vector<ASN_NGAP_PDUSessionResourceSetupItemSURes *> successList;
    std::vector<ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes *> failedList;

    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr)
        return;

    auto *ieList = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListSUReq);
    if (ieList)
    {
        auto &list = ieList->PDUSessionResourceSetupListSUReq.list;
        for (int i = 0; i < list.count; i++)
        {
            auto &item = list.array[i];
            auto *transfer = ngap_encode::Decode<ASN_NGAP_PDUSessionResourceSetupRequestTransfer>(
                asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, item->pDUSessionResourceSetupRequestTransfer);
            if (transfer == nullptr)
            {
                m_logger->err(
                    "Unable to decode a PDU session resource setup request transfer. Ignoring the relevant item");
                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
                continue;
            }

            auto *resource = new PduSessionResource(ue->ctxId, static_cast<int>(item->pDUSessionID));

            auto *ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_PDUSessionAggregateMaximumBitRate);
            if (ie)
            {
                resource->sessionAmbr.dlAmbr =
                    asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateDL) /
                    8ull;
                resource->sessionAmbr.ulAmbr =
                    asn::GetUnsigned64(ie->PDUSessionAggregateMaximumBitRate.pDUSessionAggregateMaximumBitRateUL) /
                    8ull;
            }

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_DataForwardingNotPossible);
            if (ie)
                resource->dataForwardingNotPossible = true;

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_PDUSessionType);
            if (ie)
                resource->sessionType = ngap_utils::PduSessionTypeFromAsn(ie->PDUSessionType);

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_UL_NGU_UP_TNLInformation);
            if (ie)
            {
                resource->upTunnel.teid =
                    (uint32_t)asn::GetOctet4(ie->UPTransportLayerInformation.choice.gTPTunnel->gTP_TEID);

                resource->upTunnel.address =
                    asn::GetOctetString(ie->UPTransportLayerInformation.choice.gTPTunnel->transportLayerAddress);
            }

            ie = asn::ngap::GetProtocolIe(transfer, ASN_NGAP_ProtocolIE_ID_id_QosFlowSetupRequestList);
            if (ie)
            {
                auto *ptr = asn::New<ASN_NGAP_QosFlowSetupRequestList>();
                asn::DeepCopy(asn_DEF_ASN_NGAP_QosFlowSetupRequestList, ie->QosFlowSetupRequestList, ptr);

                resource->qosFlows = asn::WrapUnique(ptr, asn_DEF_ASN_NGAP_QosFlowSetupRequestList);
            }

            auto error = setupPduSessionResource(ue, resource);
            if (error.has_value())
            {
                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer>();
                ngap_utils::ToCauseAsn_Ref(error.value(), tr->cause);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupUnsuccessfulTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupUnsuccessfulTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceFailedToSetupItemSURes>();
                res->pDUSessionID = resource->psi;
                asn::SetOctetString(res->pDUSessionResourceSetupUnsuccessfulTransfer, encodedTr);

                failedList.push_back(res);
            }
            else
            {
                if (item->pDUSessionNAS_PDU)
                    deliverDownlinkNas(ue->ctxId, asn::GetOctetString(*item->pDUSessionNAS_PDU));

                auto *tr = asn::New<ASN_NGAP_PDUSessionResourceSetupResponseTransfer>();

                auto &qosList = resource->qosFlows->list;
                for (int iQos = 0; iQos < qosList.count; iQos++)
                {
                    auto *associatedQosFlowItem = asn::New<ASN_NGAP_AssociatedQosFlowItem>();
                    associatedQosFlowItem->qosFlowIdentifier = qosList.array[iQos]->qosFlowIdentifier;
                    asn::SequenceAdd(tr->dLQosFlowPerTNLInformation.associatedQosFlowList, associatedQosFlowItem);
                }

                auto &upInfo = tr->dLQosFlowPerTNLInformation.uPTransportLayerInformation;
                upInfo.present = ASN_NGAP_UPTransportLayerInformation_PR_gTPTunnel;
                upInfo.choice.gTPTunnel = asn::New<ASN_NGAP_GTPTunnel>();
                asn::SetBitString(upInfo.choice.gTPTunnel->transportLayerAddress, resource->downTunnel.address);
                asn::SetOctetString4(upInfo.choice.gTPTunnel->gTP_TEID, (octet4)resource->downTunnel.teid);

                OctetString encodedTr =
                    ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                if (encodedTr.length() == 0)
                    throw std::runtime_error("PDUSessionResourceSetupResponseTransfer encoding failed");

                asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupResponseTransfer, tr);

                auto *res = asn::New<ASN_NGAP_PDUSessionResourceSetupItemSURes>();
                res->pDUSessionID = resource->psi;
                asn::SetOctetString(res->pDUSessionResourceSetupResponseTransfer, encodedTr);

                successList.push_back(res);
            }

            asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceSetupRequestTransfer, transfer);
        }
    }

    auto *ieNasPdu = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NAS_PDU);
    if (ieNasPdu)
        deliverDownlinkNas(ue->ctxId, asn::GetOctetString(ieNasPdu->NAS_PDU));

    std::vector<ASN_NGAP_PDUSessionResourceSetupResponseIEs *> responseIes;

    if (!successList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_PDUSessionResourceSetupResponseIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceSetupListSURes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present = ASN_NGAP_PDUSessionResourceSetupResponseIEs__value_PR_PDUSessionResourceSetupListSURes;

        for (auto &item : successList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceSetupListSURes, item);

        responseIes.push_back(ie);
    }

    if (!failedList.empty())
    {
        auto *ie = asn::New<ASN_NGAP_PDUSessionResourceSetupResponseIEs>();
        ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceFailedToSetupListSURes;
        ie->criticality = ASN_NGAP_Criticality_ignore;
        ie->value.present =
            ASN_NGAP_PDUSessionResourceSetupResponseIEs__value_PR_PDUSessionResourceFailedToSetupListSURes;

        for (auto &item : failedList)
            asn::SequenceAdd(ie->value.choice.PDUSessionResourceFailedToSetupListSURes, item);

        responseIes.push_back(ie);
    }

    auto *respPdu = asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceSetupResponse>(responseIes);
    sendNgapUeAssociated(ue->ctxId, respPdu);

    if (failedList.empty())
        m_logger->info("PDU session resource(s) setup for UE[%d] count[%d]", ue->ctxId,
                       static_cast<int>(successList.size()));
    else if (successList.empty())
        m_logger->err("PDU session resource(s) setup was failed for UE[%d] count[%d]", ue->ctxId,
                      static_cast<int>(failedList.size()));
    else
        m_logger->err("PDU session establishment is partially successful for UE[%d], success[%d], failed[%d]",
                      static_cast<int>(successList.size()), static_cast<int>(failedList.size()));
}

std::optional<NgapCause> NgapTask::setupPduSessionResource(NgapUeContext *ue, PduSessionResource *resource)
{
    if (resource->sessionType != PduSessionType::IPv4)
    {
        m_logger->err("PDU session resource could not setup: Only IPv4 is supported");
        return NgapCause::RadioNetwork_unspecified;
    }

    if (resource->upTunnel.address.length() == 0)
    {
        m_logger->err("PDU session resource could not setup: Uplink TNL information is missing");
        return NgapCause::Protocol_transfer_syntax_error;
    }

    if (resource->qosFlows == nullptr || resource->qosFlows->list.count == 0)
    {
        m_logger->err("PDU session resource could not setup: QoS flow list is null or empty");
        return NgapCause::Protocol_semantic_error;
    }

    std::string gtpIp = m_base->config->gtpAdvertiseIp.value_or(m_base->config->gtpIp);

    resource->downTunnel.address = utils::IpToOctetString(gtpIp);
    resource->downTunnel.teid = ++m_downlinkTeidCounter;

    auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_CREATE);
    w->resource = resource;
    m_base->gtpTask->push(std::move(w));

    ue->pduSessions.insert(resource->psi);
    m_pduSessions[ue->ctxId][resource->psi] = resource; //kassem

    return {};
}

void NgapTask::receiveSessionResourceReleaseCommand(int amfId, ASN_NGAP_PDUSessionResourceReleaseCommand *msg)
{
    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg));
    if (ue == nullptr)
        return;

    std::set<int> psIds{};

    auto *ieReq = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceToReleaseListRelCmd);
    if (ieReq)
    {
        auto &list = ieReq->PDUSessionResourceToReleaseListRelCmd.list;

        for (int i = 0; i < list.count; i++)
        {
            auto &item = list.array[i];
            if (item)
                psIds.insert(static_cast<int>(item->pDUSessionID));
        }
    }

    ieReq = asn::ngap::GetProtocolIe(msg, ASN_NGAP_ProtocolIE_ID_id_NAS_PDU);
    if (ieReq)
        deliverDownlinkNas(ue->ctxId, asn::GetOctetString(ieReq->NAS_PDU));

    auto *ieResp = asn::New<ASN_NGAP_PDUSessionResourceReleaseResponseIEs>();
    ieResp->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceReleasedListRelRes;
    ieResp->criticality = ASN_NGAP_Criticality_ignore;
    ieResp->value.present =
        ASN_NGAP_PDUSessionResourceReleaseResponseIEs__value_PR_PDUSessionResourceReleasedListRelRes;

    // Perform release
    for (auto &psi : psIds)
    {
        auto w = std::make_unique<NmGnbNgapToGtp>(NmGnbNgapToGtp::SESSION_RELEASE);
        w->ueId = ue->ctxId;
        w->psi = psi;
        m_base->gtpTask->push(std::move(w));

        ue->pduSessions.erase(psi);
    }

    for (auto &psi : psIds)
    {
        auto *tr = asn::New<ASN_NGAP_PDUSessionResourceReleaseResponseTransfer>();

        OctetString encodedTr = ngap_encode::EncodeS(asn_DEF_ASN_NGAP_PDUSessionResourceReleaseResponseTransfer, tr);

        if (encodedTr.length() == 0)
            throw std::runtime_error("PDUSessionResourceReleaseResponseTransfer encoding failed");

        asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceReleaseResponseTransfer, tr);

        auto *item = asn::New<ASN_NGAP_PDUSessionResourceReleasedItemRelRes>();
        item->pDUSessionID = static_cast<ASN_NGAP_PDUSessionID_t>(psi);
        asn::SetOctetString(item->pDUSessionResourceReleaseResponseTransfer, encodedTr);

        asn::SequenceAdd(ieResp->value.choice.PDUSessionResourceReleasedListRelRes, item);
    }

    auto *respPdu = asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceReleaseResponse>({ieResp});
    sendNgapUeAssociated(ue->ctxId, respPdu);

    m_logger->info("PDU session resource(s) released for UE[%d] count[%d]", ue->ctxId, static_cast<int>(psIds.size()));
}

void NgapTask::receiveSessionResourceModifyRequest( //kassem
    int amfId, ASN_NGAP_PDUSessionResourceModifyRequest *msg) //kassem
{ //kassem
    // Find which UE this message is for //kassem
     m_logger->err("[QNC] *****************************kassem Enter");
    auto *ue = findUeByNgapIdPair(amfId, ngap_utils::FindNgapIdPair(msg)); //kassem
    if (ue == nullptr) //kassem
        return; //kassem
 
    // The response will collect one item per successfully modified session //kassem
    std::vector<ASN_NGAP_PDUSessionResourceModifyItemModRes *> responseItems; //kassem
 
    // Get the list of PDU sessions to modify //kassem
    auto *ieList = asn::ngap::GetProtocolIe( //kassem
        msg, ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceModifyListModReq); //kassem
    if (!ieList) //kassem
    { //kassem
        m_logger->err("[QNC] ModifyRequest: no session list IE found"); //kassem
        return; //kassem
    } //kassem
 
    auto &sessionList = ieList->PDUSessionResourceModifyListModReq.list; //kassem
    for (int i = 0; i < sessionList.count; i++) //kassem
    { //kassem
        m_logger->err("[QNC] *****************************kassem Enter for %d", i);
        auto *sessionItem = sessionList.array[i]; //kassem
        if (!sessionItem) continue; //kassem
 
        int psi = static_cast<int>(sessionItem->pDUSessionID); //kassem
 
        // The transfer is encoded bytes inside the item - decode them //kassem
        auto *transfer = //kassem
            ngap_encode::Decode<ASN_NGAP_PDUSessionResourceModifyRequestTransfer>( //kassem
                asn_DEF_ASN_NGAP_PDUSessionResourceModifyRequestTransfer, //kassem
                sessionItem->pDUSessionResourceModifyRequestTransfer); //kassem
        if (!transfer) //kassem
        { //kassem
            m_logger->err("[QNC] ModifyRequest: failed to decode transfer for PSI=%d", psi); //kassem
            continue; //kassem
        } //kassem
 
        // ── Parse QNC and alt profiles ────────────────────────────────── //kassem
 
        // Get or create the PduSessionResource for this session //kassem
        PduSessionResource *resource = nullptr; //kassem
        if (m_pduSessions.count(ue->ctxId) && //kassem
            m_pduSessions[ue->ctxId].count(psi)) //kassem
        { //kassem
            resource = m_pduSessions[ue->ctxId][psi]; //kassem
        } //kassem
 
        // Get the QosFlowAddOrModifyRequestList from the transfer IEs //kassem
        auto *ieFlowList = asn::ngap::GetProtocolIe( //kassem
            transfer, ASN_NGAP_ProtocolIE_ID_id_QosFlowAddOrModifyRequestList); //kassem
        m_logger->err("[QNC] ieFlowList=%p resource=%p psi=%d ueId=%d",
        (void*)ieFlowList, (void*)resource, psi, ue->ctxId);
        if (ieFlowList && resource) //kassem
        { //kassem
            auto &flowList = ieFlowList->QosFlowAddOrModifyRequestList.list; //kassem
            for (int iFlow = 0; iFlow < flowList.count; iFlow++) //kassem
            { //kassem
                auto *flowItem = flowList.array[iFlow]; //kassem
                if (!flowItem) continue; //kassem
 
                // qosFlowLevelQosParameters is OPTIONAL in modify requests //kassem
                // (it is absent if only the flow mapping changes) //kassem
                auto *qosParams = flowItem->qosFlowLevelQosParameters; //kassem
                if (!qosParams){ 
                    m_logger->err("[QNC] QFI=%d SKIP: no qosFlowLevelQosParameters", //kassem
                            (int)flowItem->qosFlowIdentifier);
                    continue;
                } //kassem
 
                // Only GBR flows have gBR_QosInformation //kassem
                auto *gbrInfo = qosParams->gBR_QosInformation; //kassem
                if (!gbrInfo){  m_logger->err("[QNC] QFI=%d SKIP: no gBR_QosInformation", //kassem
        (int)flowItem->qosFlowIdentifier);
        continue;} //kassem
 
                // Only process if NotificationControl is set //kassem
                if (!gbrInfo->notificationControl){ m_logger->err("[QNC] QFI=%d SKIP: no notificationControl", //kassem
        (int)flowItem->qosFlowIdentifier);
        continue; } //kassem
                if (*gbrInfo->notificationControl != //kassem
                    ASN_NGAP_NotificationControl_notification_requested) {m_logger->err("[QNC] QFI=%d SKIP: notificationControl value=%d", //kassem
        (int)flowItem->qosFlowIdentifier, //kassem
        (int)*gbrInfo->notificationControl);  continue; }//kassem
 
                int qfi = static_cast<int>(flowItem->qosFlowIdentifier); //kassem
 
                // Build the QNC state for this flow //kassem
                QosFlowQncState qncState{}; //kassem
                qncState.qfi = qfi; //kassem
                qncState.qncEnabled = true; //kassem
                qncState.activeProfileIndex = 0; // start on primary profile //kassem
 
                // ── Parse AlternativeQoSParaSetList ────────────────────── //kassem
                // Full struct definition is in ASN_NGAP_ProtocolExtensionField.h //kassem
                // which we now include — direct field access is safe. //kassem
                if (gbrInfo->iE_Extensions) //kassem
                { //kassem
                    m_logger->err("[QNC] *****************************kassem Enter ie_extensions");
                    auto *extC = reinterpret_cast< //kassem
                        ASN_NGAP_ProtocolExtensionContainer_174P96_t *>( //kassem
                            gbrInfo->iE_Extensions); //kassem
                    for (int iD = 0; iD < extC->list.count; iD++) //kassem
                    { //kassem
                        auto *extIE = extC->list.array[iD]; //kassem
                        if (!extIE || extIE->id != 220) continue; //kassem
                        auto *altList = //kassem
                            &extIE->extensionValue //kassem
                                 .choice.AlternativeQoSParaSetList; //kassem
                        for (int iAlt = 0; iAlt < altList->list.count; iAlt++) //kassem
                        { //kassem
                            m_logger->err("[QNC] *****************************kassem Enter for alternartive %d", iAlt);
                            auto *altItem = altList->list.array[iAlt]; //kassem
                            if (!altItem) continue; //kassem
                            AltQosProfile prof{}; //kassem
                            prof.index = static_cast<int>( //kassem
                                altItem->alternativeQoSParaSetIndex); //kassem
                            if (altItem->guaranteedFlowBitRateDL) //kassem
                                prof.gfbrDl = asn::GetUnsigned64( //kassem
                                    *altItem->guaranteedFlowBitRateDL); //kassem
                            if (altItem->guaranteedFlowBitRateUL) //kassem
                                prof.gfbrUl = asn::GetUnsigned64( //kassem
                                    *altItem->guaranteedFlowBitRateUL); //kassem
                            qncState.altProfiles.push_back(prof); //kassem
                            m_logger->info( //kassem
                                "[QNC] PSI=%d QFI=%d alt[%d] " //kassem
                                "index=%d gfbrDL=%lu gfbrUL=%lu", //kassem
                                psi, qfi, iAlt, prof.index, //kassem
                                (unsigned long)prof.gfbrDl, //kassem
                                (unsigned long)prof.gfbrUl); //kassem
                        } //kassem
                        break; //kassem
                    } //kassem
                } //kassem
                m_logger->info( //kassem
                    "[QNC] PSI=%d QFI=%d qnc=true altProfiles=%d stored", //kassem
                    psi, qfi, //kassem
                    static_cast<int>(qncState.altProfiles.size())); //kassem
 
                // Remove any existing QNC state for this QFI then add new //kassem
                resource->qncFlows.erase( //kassem
                    std::remove_if(resource->qncFlows.begin(), //kassem
                                   resource->qncFlows.end(), //kassem
                                   [qfi](const QosFlowQncState &s) { //kassem
                                       return s.qfi == qfi; //kassem
                                   }), //kassem
                    resource->qncFlows.end()); //kassem
                resource->qncFlows.push_back(std::move(qncState)); //kassem
            } //kassem
        } //kassem
 
        // ── Build response for this PDU session ───────────────────────── //kassem
 
        // The response transfer: confirm which flows were added/modified. //kassem
        // We echo back every flow that was in the request. //kassem
        auto *respTransfer = //kassem
            asn::New<ASN_NGAP_PDUSessionResourceModifyResponseTransfer>(); //kassem
 
        if (ieFlowList) //kassem
        { //kassem
            auto &flowList = ieFlowList->QosFlowAddOrModifyRequestList.list; //kassem
            for (int iFlow = 0; iFlow < flowList.count; iFlow++) //kassem
            { //kassem
                auto *flowItem = flowList.array[iFlow]; //kassem
                if (!flowItem) continue; //kassem
 
                auto *respFlowItem = //kassem
                    asn::New<ASN_NGAP_QosFlowAddOrModifyResponseItem>(); //kassem
                respFlowItem->qosFlowIdentifier = //kassem
                    flowItem->qosFlowIdentifier; //kassem
                asn::SequenceAdd( //kassem
                    *respTransfer->qosFlowAddOrModifyResponseList, //kassem
                    respFlowItem); //kassem
            } //kassem
        } //kassem
 
        OctetString encodedResp = ngap_encode::EncodeS( //kassem
            asn_DEF_ASN_NGAP_PDUSessionResourceModifyResponseTransfer, //kassem
            respTransfer); //kassem
        asn::Free( //kassem
            asn_DEF_ASN_NGAP_PDUSessionResourceModifyResponseTransfer, //kassem
            respTransfer); //kassem
 
        if (encodedResp.length() == 0) //kassem
        { //kassem
            m_logger->err("[QNC] ModifyResponse transfer encoding failed"); //kassem
            asn::Free( //kassem
                asn_DEF_ASN_NGAP_PDUSessionResourceModifyRequestTransfer, //kassem
                transfer); //kassem
            continue; //kassem
        } //kassem
 
        auto *resItem = //kassem
            asn::New<ASN_NGAP_PDUSessionResourceModifyItemModRes>(); //kassem
        resItem->pDUSessionID = //kassem
            static_cast<ASN_NGAP_PDUSessionID_t>(psi); //kassem
        asn::SetOctetString( //kassem
            resItem->pDUSessionResourceModifyResponseTransfer, //kassem
            encodedResp); //kassem
        responseItems.push_back(resItem); //kassem
 
        asn::Free( //kassem
            asn_DEF_ASN_NGAP_PDUSessionResourceModifyRequestTransfer, //kassem
            transfer); //kassem
    } //kassem
 
    // ── Send the PDUSessionResourceModifyResponse ──────────────────────── //kassem
    if (!responseItems.empty()) //kassem
    { //kassem
        auto *ie = //kassem
            asn::New<ASN_NGAP_PDUSessionResourceModifyResponseIEs>(); //kassem
        ie->id = //kassem
            ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceModifyListModRes; //kassem
        ie->criticality = ASN_NGAP_Criticality_ignore; //kassem
        ie->value.present = //kassem
            ASN_NGAP_PDUSessionResourceModifyResponseIEs__value_PR_PDUSessionResourceModifyListModRes; //kassem
 
        for (auto *item : responseItems) //kassem
            asn::SequenceAdd( //kassem
                ie->value.choice.PDUSessionResourceModifyListModRes, //kassem
                item); //kassem
 
        auto *respPdu = //kassem
            asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceModifyResponse>( //kassem
                {ie}); //kassem
        sendNgapUeAssociated(ue->ctxId, respPdu); //kassem
 
        m_logger->info( //kassem
            "[QNC] PDU session resource(s) modified for UE[%d] count[%d]", //kassem
            ue->ctxId, static_cast<int>(responseItems.size())); //kassem
    } //kassem
} //kassem
 
void NgapTask::sendQosFlowNotify(int ueId, int psi, int qfi, bool fulfilled) //kassem
{ //kassem
    auto *ue = findUeContext(ueId); //kassem
    if (!ue) { //kassem
        m_logger->err("[QNC] sendQosFlowNotify: UE %d not found", ueId); //kassem
        return; //kassem
    } //kassem
 
    auto *notifyItem = asn::New<ASN_NGAP_QosFlowNotifyItem>(); //kassem
    notifyItem->qosFlowIdentifier = //kassem
        static_cast<ASN_NGAP_QosFlowIdentifier_t>(qfi); //kassem
    notifyItem->notificationCause = fulfilled //kassem
        ? ASN_NGAP_NotificationCause_fulfilled //kassem
        : ASN_NGAP_NotificationCause_not_fulfilled; //kassem
 
    auto *notifyTransfer = //kassem
        asn::New<ASN_NGAP_PDUSessionResourceNotifyTransfer>(); //kassem
    notifyTransfer->qosFlowNotifyList = asn::New<ASN_NGAP_QosFlowNotifyList>();
    asn::SequenceAdd(*notifyTransfer->qosFlowNotifyList, notifyItem); //kassem //kassem
 
    OctetString encodedTransfer = ngap_encode::EncodeS( //kassem
        asn_DEF_ASN_NGAP_PDUSessionResourceNotifyTransfer, notifyTransfer); //kassem
    if (encodedTransfer.length() == 0) { //kassem
        m_logger->err("[QNC] PDUSessionResourceNotifyTransfer encoding failed"); //kassem
        asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceNotifyTransfer, //kassem
                  notifyTransfer); //kassem
        return; //kassem
    } //kassem
    asn::Free(asn_DEF_ASN_NGAP_PDUSessionResourceNotifyTransfer, notifyTransfer); //kassem
 
    auto *notifyListItem = asn::New<ASN_NGAP_PDUSessionResourceNotifyItem>(); //kassem
    notifyListItem->pDUSessionID = //kassem
        static_cast<ASN_NGAP_PDUSessionID_t>(psi); //kassem
    asn::SetOctetString( //kassem
        notifyListItem->pDUSessionResourceNotifyTransfer, encodedTransfer); //kassem
 
    auto *ie = asn::New<ASN_NGAP_PDUSessionResourceNotifyIEs>(); //kassem
    ie->id = ASN_NGAP_ProtocolIE_ID_id_PDUSessionResourceNotifyList; //kassem
    ie->criticality = ASN_NGAP_Criticality_ignore; //kassem
    ie->value.present = //kassem
        ASN_NGAP_PDUSessionResourceNotifyIEs__value_PR_PDUSessionResourceNotifyList; //kassem
    asn::SequenceAdd( //kassem
        ie->value.choice.PDUSessionResourceNotifyList, notifyListItem); //kassem
 
    auto *pdu = asn::ngap::NewMessagePdu<ASN_NGAP_PDUSessionResourceNotify>( //kassem
        {ie}); //kassem
    sendNgapUeAssociated(ue->ctxId, pdu); //kassem
 
    m_logger->info( //kassem
        "[QNC] Sent PDUSessionResourceNotify UE=%d PSI=%d QFI=%d cause=%s", //kassem
        ueId, psi, qfi, fulfilled ? "fulfilled" : "not-fulfilled"); //kassem
} //kassem

} // namespace nr::gnb