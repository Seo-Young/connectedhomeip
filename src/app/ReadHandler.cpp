/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file defines read handler for a CHIP Interaction Data model
 *
 */

#include <app/AppBuildConfig.h>
#include <app/InteractionModelEngine.h>
#include <app/MessageDef/EventPathIB.h>
#include <app/MessageDef/StatusResponseMessage.h>
#include <app/MessageDef/SubscribeRequestMessage.h>
#include <app/MessageDef/SubscribeResponseMessage.h>

#include <app/ReadHandler.h>
#include <app/reporting/Engine.h>

namespace chip {
namespace app {
CHIP_ERROR ReadHandler::Init(Messaging::ExchangeManager * apExchangeMgr, InteractionModelDelegate * apDelegate,
                             Messaging::ExchangeContext * apExchangeContext, InteractionType aInteractionType)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    // Error if already initialized.
    VerifyOrReturnError(IsFree(), err = CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mpExchangeCtx == nullptr, err = CHIP_ERROR_INCORRECT_STATE);
    mpExchangeMgr              = apExchangeMgr;
    mpExchangeCtx              = apExchangeContext;
    mSuppressResponse          = true;
    mpAttributeClusterInfoList = nullptr;
    mpEventClusterInfoList     = nullptr;
    mCurrentPriority           = PriorityLevel::Invalid;
    mEventMin                  = 0;
    mLastScheduledEventNumber  = 0;
    mIsPrimingReports          = true;
    MoveToState(HandlerState::Initialized);
    mpDelegate              = apDelegate;
    mSubscriptionId         = 0;
    mHoldReport             = false;
    mDirty                  = false;
    mActiveSubscription     = false;
    mIsChunkedReport        = false;
    mInteractionType        = aInteractionType;
    mInitiatorNodeId        = apExchangeContext->GetSessionHandle().GetPeerNodeId();
    mSubjectDescriptor      = apExchangeContext->GetSessionHandle().GetSubjectDescriptor();
    mHoldSync               = false;
    mLastWrittenEventsBytes = 0;
    if (apExchangeContext != nullptr)
    {
        apExchangeContext->SetDelegate(this);
    }

    return err;
}

void ReadHandler::Shutdown(ShutdownOptions aOptions)
{
    if (IsSubscriptionType())
    {
        InteractionModelEngine::GetInstance()->GetExchangeManager()->GetSessionManager()->SystemLayer()->CancelTimer(
            OnRefreshSubscribeTimerSyncCallback, this);
        if (mpDelegate != nullptr)
        {
            mpDelegate->SubscriptionTerminated(this);
        }
    }

    if (aOptions == ShutdownOptions::AbortCurrentExchange)
    {
        if (mpExchangeCtx != nullptr)
        {
            mpExchangeCtx->Abort();
            mpExchangeCtx = nullptr;
        }
    }

    if (IsAwaitingReportResponse())
    {
        InteractionModelEngine::GetInstance()->GetReportingEngine().OnReportConfirm();
    }
    InteractionModelEngine::GetInstance()->ReleaseClusterInfoList(mpAttributeClusterInfoList);
    InteractionModelEngine::GetInstance()->ReleaseClusterInfoList(mpEventClusterInfoList);
    mSubscriptionId            = 0;
    mMinIntervalFloorSeconds   = 0;
    mMaxIntervalCeilingSeconds = 0;
    mInteractionType           = InteractionType::Read;
    mpExchangeCtx              = nullptr;
    MoveToState(HandlerState::Uninitialized);
    mpAttributeClusterInfoList = nullptr;
    mpEventClusterInfoList     = nullptr;
    mCurrentPriority           = PriorityLevel::Invalid;
    mEventMin                  = 0;
    mLastScheduledEventNumber  = 0;
    mIsPrimingReports          = false;
    mpDelegate                 = nullptr;
    mHoldReport                = false;
    mDirty                     = false;
    mActiveSubscription        = false;
    mIsChunkedReport           = false;
    mInitiatorNodeId           = kUndefinedNodeId;
    mHoldSync                  = false;
    mLastWrittenEventsBytes    = 0;
}

CHIP_ERROR ReadHandler::OnReadInitialRequest(System::PacketBufferHandle && aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferHandle response;
    if (IsSubscriptionType())
    {
        err = ProcessSubscribeRequest(std::move(aPayload));
    }
    else
    {
        err = ProcessReadRequest(std::move(aPayload));
    }

    if (err != CHIP_NO_ERROR)
    {
        Shutdown();
    }

    return err;
}

CHIP_ERROR ReadHandler::OnStatusResponse(Messaging::ExchangeContext * apExchangeContext, System::PacketBufferHandle && aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    StatusIB status;
    err = StatusResponse::ProcessStatusResponse(std::move(aPayload), status);
    SuccessOrExit(err);
    switch (mState)
    {
    case HandlerState::AwaitingReportResponse:
        if (IsChunkedReport())
        {
            InteractionModelEngine::GetInstance()->GetReportingEngine().OnReportConfirm();
            MoveToState(HandlerState::GeneratingReports);
            if (mpExchangeCtx)
            {
                mpExchangeCtx->WillSendMessage();
            }
            // Trigger ReportingEngine run for sending next chunk of data.
            SuccessOrExit(err = InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun());
        }
        else if (IsSubscriptionType())
        {
            InteractionModelEngine::GetInstance()->GetReportingEngine().OnReportConfirm();
            if (IsPriming())
            {
                err           = SendSubscribeResponse();
                mpExchangeCtx = nullptr;
                SuccessOrExit(err);
                mActiveSubscription = true;
            }
            else
            {
                MoveToState(HandlerState::GeneratingReports);
                mpExchangeCtx = nullptr;
            }
        }
        else
        {
            Shutdown();
        }
        break;
    case HandlerState::GeneratingReports:
    case HandlerState::Initialized:
    case HandlerState::Uninitialized:
    default:
        err = CHIP_ERROR_INCORRECT_STATE;
        break;
    }
exit:
    if (err != CHIP_NO_ERROR)
    {
        Shutdown();
    }
    return err;
}

CHIP_ERROR ReadHandler::SendReportData(System::PacketBufferHandle && aPayload, bool aMoreChunks)
{
    VerifyOrReturnLogError(IsReportable(), CHIP_ERROR_INCORRECT_STATE);
    if (IsPriming() || IsChunkedReport())
    {
        mSessionHandle.SetValue(mpExchangeCtx->GetSessionHandle());
    }
    else
    {
        VerifyOrReturnLogError(mpExchangeCtx == nullptr, CHIP_ERROR_INCORRECT_STATE);
        mpExchangeCtx = mpExchangeMgr->NewContext(mSessionHandle.Value(), this);
        mpExchangeCtx->SetResponseTimeout(kImMessageTimeout);
    }
    VerifyOrReturnLogError(mpExchangeCtx != nullptr, CHIP_ERROR_INCORRECT_STATE);
    mIsChunkedReport        = aMoreChunks;
    bool noResponseExpected = IsReadType() && !mIsChunkedReport;
    if (!noResponseExpected)
    {
        MoveToState(HandlerState::AwaitingReportResponse);
    }
    CHIP_ERROR err =
        mpExchangeCtx->SendMessage(Protocols::InteractionModel::MsgType::ReportData, std::move(aPayload),
                                   Messaging::SendFlags(noResponseExpected ? Messaging::SendMessageFlags::kNone
                                                                           : Messaging::SendMessageFlags::kExpectResponse));
    if (err == CHIP_NO_ERROR && noResponseExpected)
    {
        mpExchangeCtx = nullptr;
        InteractionModelEngine::GetInstance()->GetReportingEngine().OnReportConfirm();
    }

    if (err == CHIP_NO_ERROR)
    {
        if (IsSubscriptionType() && !IsPriming())
        {
            err = RefreshSubscribeSyncTimer();
        }
    }
    if (!aMoreChunks)
    {
        ClearDirty();
    }
    return err;
}

CHIP_ERROR ReadHandler::OnMessageReceived(Messaging::ExchangeContext * apExchangeContext, const PayloadHeader & aPayloadHeader,
                                          System::PacketBufferHandle && aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (aPayloadHeader.HasMessageType(Protocols::InteractionModel::MsgType::StatusResponse))
    {
        err = OnStatusResponse(apExchangeContext, std::move(aPayload));
    }
    else
    {
        err = OnUnknownMsgType(apExchangeContext, aPayloadHeader, std::move(aPayload));
    }
    return err;
}

CHIP_ERROR ReadHandler::OnUnknownMsgType(Messaging::ExchangeContext * apExchangeContext, const PayloadHeader & aPayloadHeader,
                                         System::PacketBufferHandle && aPayload)
{
    ChipLogDetail(DataManagement, "Msg type %d not supported", aPayloadHeader.GetMessageType());
    Shutdown();
    return CHIP_ERROR_INVALID_MESSAGE_TYPE;
}

void ReadHandler::OnResponseTimeout(Messaging::ExchangeContext * apExchangeContext)
{
    ChipLogProgress(DataManagement, "Time out! failed to receive status response from Exchange: " ChipLogFormatExchange,
                    ChipLogValueExchange(apExchangeContext));
    Shutdown();
}

CHIP_ERROR ReadHandler::ProcessReadRequest(System::PacketBufferHandle && aPayload)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    System::PacketBufferTLVReader reader;

    ReadRequestMessage::Parser readRequestParser;
    EventPathIBs::Parser eventPathListParser;
    EventFilterIBs::Parser eventFilterIBsParser;
    AttributePathIBs::Parser attributePathListParser;

    reader.Init(std::move(aPayload));

    ReturnErrorOnFailure(reader.Next());
    ReturnErrorOnFailure(readRequestParser.Init(reader));
#if CHIP_CONFIG_IM_ENABLE_SCHEMA_CHECK
    ReturnErrorOnFailure(readRequestParser.CheckSchemaValidity());
#endif

    err = readRequestParser.GetAttributeRequests(&attributePathListParser);
    if (err == CHIP_END_OF_TLV)
    {
        err = CHIP_NO_ERROR;
    }
    else if (err == CHIP_NO_ERROR)
    {
        ReturnErrorOnFailure(ProcessAttributePathList(attributePathListParser));
    }
    ReturnErrorOnFailure(err);
    err = readRequestParser.GetEventRequests(&eventPathListParser);
    if (err == CHIP_END_OF_TLV)
    {
        err = CHIP_NO_ERROR;
    }
    else if (err == CHIP_NO_ERROR)
    {
        ReturnErrorOnFailure(err);
        ReturnErrorOnFailure(ProcessEventPaths(eventPathListParser));
        err = readRequestParser.GetEventFilters(&eventFilterIBsParser);
        if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }
        else if (err == CHIP_NO_ERROR)
        {
            ReturnErrorOnFailure(ProcessEventFilters(eventFilterIBsParser));
        }
    }
    ReturnErrorOnFailure(err);

    MoveToState(HandlerState::GeneratingReports);

    ReturnErrorOnFailure(InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun());

    // mpExchangeCtx can be null here due to
    // https://github.com/project-chip/connectedhomeip/issues/8031
    if (mpExchangeCtx)
    {
        mpExchangeCtx->WillSendMessage();
    }

    // There must be no code after the WillSendMessage() call that can cause
    // this method to return a failure.

    return CHIP_NO_ERROR;
}

CHIP_ERROR ReadHandler::ProcessAttributePathList(AttributePathIBs::Parser & aAttributePathListParser)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    TLV::TLVReader reader;
    aAttributePathListParser.GetReader(&reader);

    while (CHIP_NO_ERROR == (err = reader.Next()))
    {
        VerifyOrExit(TLV::AnonymousTag == reader.GetTag(), err = CHIP_ERROR_INVALID_TLV_TAG);
        VerifyOrExit(TLV::kTLVType_List == reader.GetType(), err = CHIP_ERROR_WRONG_TLV_TYPE);
        ClusterInfo clusterInfo;
        AttributePathIB::Parser path;
        err = path.Init(reader);
        SuccessOrExit(err);
        // TODO: MEIs (ClusterId and AttributeId) have a invalid pattern instead of a single invalid value, need to add separate
        // functions for checking if we have received valid values.
        // TODO: Wildcard cluster id with non-global attributes or wildcard attribute paths should be rejected.
        err = path.GetEndpoint(&(clusterInfo.mEndpointId));
        if (err == CHIP_NO_ERROR)
        {
            VerifyOrExit(!clusterInfo.HasWildcardEndpointId(), err = CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH);
        }
        else if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }
        SuccessOrExit(err);
        err = path.GetCluster(&(clusterInfo.mClusterId));
        if (err == CHIP_NO_ERROR)
        {
            VerifyOrExit(!clusterInfo.HasWildcardClusterId(), err = CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH);
        }
        else if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }

        SuccessOrExit(err);
        err = path.GetAttribute(&(clusterInfo.mAttributeId));
        if (CHIP_END_OF_TLV == err)
        {
            err = CHIP_NO_ERROR;
        }
        else if (err == CHIP_NO_ERROR)
        {
            VerifyOrExit(!clusterInfo.HasWildcardAttributeId(), err = CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH);
        }
        SuccessOrExit(err);

        err = path.GetListIndex(&(clusterInfo.mListIndex));
        if (CHIP_NO_ERROR == err)
        {
            VerifyOrExit(!clusterInfo.HasWildcardAttributeId() && !clusterInfo.HasWildcardListIndex(),
                         err = CHIP_ERROR_IM_MALFORMED_ATTRIBUTE_PATH);
        }
        else if (CHIP_END_OF_TLV == err)
        {
            err = CHIP_NO_ERROR;
        }
        SuccessOrExit(err);
        err = InteractionModelEngine::GetInstance()->PushFront(mpAttributeClusterInfoList, clusterInfo);
        SuccessOrExit(err);
        mIsPrimingReports = true;
    }
    // if we have exhausted this container
    if (CHIP_END_OF_TLV == err)
    {
        mAttributePathExpandIterator = AttributePathExpandIterator(mpAttributeClusterInfoList);
        err                          = CHIP_NO_ERROR;
    }

exit:
    return err;
}

CHIP_ERROR ReadHandler::ProcessEventPaths(EventPathIBs::Parser & aEventPathsParser)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    TLV::TLVReader reader;
    aEventPathsParser.GetReader(&reader);

    while (CHIP_NO_ERROR == (err = reader.Next()))
    {
        VerifyOrReturnError(TLV::AnonymousTag == reader.GetTag(), CHIP_ERROR_INVALID_TLV_TAG);
        ClusterInfo clusterInfo;
        EventPathIB::Parser path;
        ReturnErrorOnFailure(path.Init(reader));

        err = path.GetEndpoint(&(clusterInfo.mEndpointId));
        if (err == CHIP_NO_ERROR)
        {
            VerifyOrReturnError(!clusterInfo.HasWildcardEndpointId(), err = CHIP_ERROR_IM_MALFORMED_EVENT_PATH);
        }
        else if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }
        ReturnErrorOnFailure(err);

        err = path.GetCluster(&(clusterInfo.mClusterId));
        if (err == CHIP_NO_ERROR)
        {
            VerifyOrReturnError(!clusterInfo.HasWildcardClusterId(), err = CHIP_ERROR_IM_MALFORMED_EVENT_PATH);
        }
        else if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }
        ReturnErrorOnFailure(err);

        err = path.GetEvent(&(clusterInfo.mEventId));
        if (CHIP_END_OF_TLV == err)
        {
            err = CHIP_NO_ERROR;
        }
        else if (err == CHIP_NO_ERROR)
        {
            VerifyOrReturnError(!clusterInfo.HasWildcardEventId(), err = CHIP_ERROR_IM_MALFORMED_EVENT_PATH);
        }
        ReturnErrorOnFailure(err);

        ReturnErrorOnFailure(InteractionModelEngine::GetInstance()->PushFront(mpEventClusterInfoList, clusterInfo));
    }

    // if we have exhausted this container
    if (CHIP_END_OF_TLV == err)
    {
        err = CHIP_NO_ERROR;
    }
    return err;
}

CHIP_ERROR ReadHandler::ProcessEventFilters(EventFilterIBs::Parser & aEventFiltersParser)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    TLV::TLVReader reader;
    aEventFiltersParser.GetReader(&reader);

    while (CHIP_NO_ERROR == (err = reader.Next()))
    {
        VerifyOrReturnError(TLV::AnonymousTag == reader.GetTag(), CHIP_ERROR_INVALID_TLV_TAG);
        EventFilterIB::Parser filter;
        ReturnErrorOnFailure(filter.Init(reader));
        // this is for current node, and would have only one event filter.
        ReturnErrorOnFailure(filter.GetEventMin(&(mEventMin)));
    }
    if (CHIP_END_OF_TLV == err)
    {
        err = CHIP_NO_ERROR;
    }
    return err;
}

const char * ReadHandler::GetStateStr() const
{
#if CHIP_DETAIL_LOGGING
    switch (mState)
    {
    case HandlerState::Uninitialized:
        return "Uninitialized";

    case HandlerState::Initialized:
        return "Initialized";

    case HandlerState::GeneratingReports:
        return "GeneratingReports";

    case HandlerState::AwaitingReportResponse:
        return "AwaitingReportResponse";
    }
#endif // CHIP_DETAIL_LOGGING
    return "N/A";
}

void ReadHandler::MoveToState(const HandlerState aTargetState)
{
    mState = aTargetState;
    ChipLogDetail(DataManagement, "IM RH moving to [%s]", GetStateStr());
}

bool ReadHandler::CheckEventClean(EventManagement & aEventManager)
{
    if (mIsChunkedReport)
    {
        if ((mLastScheduledEventNumber != 0) && (mEventMin <= mLastScheduledEventNumber))
        {
            return false;
        }
    }
    else
    {
        EventNumber lastEventNumber = aEventManager.GetLastEventNumber();
        if ((lastEventNumber != 0) && (mEventMin <= lastEventNumber))
        {
            // We have more events. snapshot last event number
            aEventManager.SetScheduledEventInfo(mLastScheduledEventNumber, mLastWrittenEventsBytes);
            return false;
        }
    }
    return true;
}

CHIP_ERROR ReadHandler::SendSubscribeResponse()
{
    System::PacketBufferHandle packet = System::PacketBufferHandle::New(chip::app::kMaxSecureSduLengthBytes);
    VerifyOrReturnLogError(!packet.IsNull(), CHIP_ERROR_NO_MEMORY);

    System::PacketBufferTLVWriter writer;
    writer.Init(std::move(packet));

    SubscribeResponseMessage::Builder response;
    ReturnErrorOnFailure(response.Init(&writer));
    response.SubscriptionId(mSubscriptionId)
        .MinIntervalFloorSeconds(mMinIntervalFloorSeconds)
        .MaxIntervalCeilingSeconds(mMaxIntervalCeilingSeconds)
        .EndOfSubscribeResponseMessage();
    ReturnErrorOnFailure(response.GetError());

    ReturnErrorOnFailure(writer.Finalize(&packet));
    VerifyOrReturnLogError(mpExchangeCtx != nullptr, CHIP_ERROR_INCORRECT_STATE);

    ReturnErrorOnFailure(RefreshSubscribeSyncTimer());
    mIsPrimingReports = false;
    MoveToState(HandlerState::GeneratingReports);
    if (mpDelegate != nullptr)
    {
        mpDelegate->SubscriptionEstablished(this);
    }
    return mpExchangeCtx->SendMessage(Protocols::InteractionModel::MsgType::SubscribeResponse, std::move(packet));
}

CHIP_ERROR ReadHandler::ProcessSubscribeRequest(System::PacketBufferHandle && aPayload)
{
    System::PacketBufferTLVReader reader;
    reader.Init(std::move(aPayload));

    ReturnErrorOnFailure(reader.Next());
    SubscribeRequestMessage::Parser subscribeRequestParser;
    ReturnErrorOnFailure(subscribeRequestParser.Init(reader));
#if CHIP_CONFIG_IM_ENABLE_SCHEMA_CHECK
    ReturnErrorOnFailure(subscribeRequestParser.CheckSchemaValidity());
#endif

    AttributePathIBs::Parser attributePathListParser;
    CHIP_ERROR err = subscribeRequestParser.GetAttributeRequests(&attributePathListParser);
    if (err == CHIP_END_OF_TLV)
    {
        err = CHIP_NO_ERROR;
    }
    else if (err == CHIP_NO_ERROR)
    {
        ReturnErrorOnFailure(ProcessAttributePathList(attributePathListParser));
    }
    ReturnErrorOnFailure(err);

    EventPathIBs::Parser eventPathListParser;
    err = subscribeRequestParser.GetEventRequests(&eventPathListParser);
    if (err == CHIP_END_OF_TLV)
    {
        err = CHIP_NO_ERROR;
    }
    else if (err == CHIP_NO_ERROR)
    {
        ReturnErrorOnFailure(ProcessEventPaths(eventPathListParser));
        EventFilterIBs::Parser eventFilterIBsParser;
        err = subscribeRequestParser.GetEventFilters(&eventFilterIBsParser);
        if (err == CHIP_END_OF_TLV)
        {
            err = CHIP_NO_ERROR;
        }
        else if (err == CHIP_NO_ERROR)
        {
            ReturnErrorOnFailure(ProcessEventFilters(eventFilterIBsParser));
        }
    }
    ReturnErrorOnFailure(err);

    ReturnErrorOnFailure(subscribeRequestParser.GetMinIntervalFloorSeconds(&mMinIntervalFloorSeconds));
    ReturnErrorOnFailure(subscribeRequestParser.GetMaxIntervalCeilingSeconds(&mMaxIntervalCeilingSeconds));
    VerifyOrReturnError(mMinIntervalFloorSeconds <= mMaxIntervalCeilingSeconds, CHIP_ERROR_INVALID_ARGUMENT);
    ReturnErrorOnFailure(subscribeRequestParser.GetIsFabricFiltered(&mIsFabricFiltered));
    ReturnErrorOnFailure(Crypto::DRBG_get_bytes(reinterpret_cast<uint8_t *>(&mSubscriptionId), sizeof(mSubscriptionId)));

    MoveToState(HandlerState::GeneratingReports);

    InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun();
    // mpExchangeCtx can be null here due to
    // https://github.com/project-chip/connectedhomeip/issues/8031
    if (mpExchangeCtx)
    {
        mpExchangeCtx->WillSendMessage();
    }
    return CHIP_NO_ERROR;
}

void ReadHandler::OnUnblockHoldReportCallback(System::Layer * apSystemLayer, void * apAppState)
{
    VerifyOrReturn(apAppState != nullptr);
    ReadHandler * readHandler = static_cast<ReadHandler *>(apAppState);
    ChipLogProgress(DataManagement, "Unblock report hold after min %d seconds", readHandler->mMinIntervalFloorSeconds);
    readHandler->mHoldReport = false;
    if (readHandler->mDirty)
    {
        InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun();
    }
    InteractionModelEngine::GetInstance()->GetExchangeManager()->GetSessionManager()->SystemLayer()->StartTimer(
        System::Clock::Seconds16(readHandler->mMaxIntervalCeilingSeconds - readHandler->mMinIntervalFloorSeconds),
        OnRefreshSubscribeTimerSyncCallback, readHandler);
}

void ReadHandler::OnRefreshSubscribeTimerSyncCallback(System::Layer * apSystemLayer, void * apAppState)
{
    VerifyOrReturn(apAppState != nullptr);
    ReadHandler * readHandler = static_cast<ReadHandler *>(apAppState);
    readHandler->mHoldSync    = false;
    ChipLogProgress(DataManagement, "Refresh subscribe timer sync after %d seconds",
                    readHandler->mMaxIntervalCeilingSeconds - readHandler->mMinIntervalFloorSeconds);
    InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun();
}

CHIP_ERROR ReadHandler::RefreshSubscribeSyncTimer()
{
    ChipLogProgress(DataManagement, "Refresh Subscribe Sync Timer with max %d seconds", mMaxIntervalCeilingSeconds);
    InteractionModelEngine::GetInstance()->GetExchangeManager()->GetSessionManager()->SystemLayer()->CancelTimer(
        OnUnblockHoldReportCallback, this);
    InteractionModelEngine::GetInstance()->GetExchangeManager()->GetSessionManager()->SystemLayer()->CancelTimer(
        OnRefreshSubscribeTimerSyncCallback, this);
    mHoldReport = true;
    mHoldSync   = true;
    ReturnErrorOnFailure(
        InteractionModelEngine::GetInstance()->GetExchangeManager()->GetSessionManager()->SystemLayer()->StartTimer(
            System::Clock::Seconds16(mMinIntervalFloorSeconds), OnUnblockHoldReportCallback, this));

    return CHIP_NO_ERROR;
}
} // namespace app
} // namespace chip
