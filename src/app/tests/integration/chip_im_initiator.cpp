/*
 *
 *    Copyright (c) 2020-2021 Project CHIP Authors
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
 *      This file implements a chip-im-initiator, for the
 *      CHIP Interaction Data Model Protocol.
 *
 *      Currently it provides simple command sender with sample cluster and command
 *
 */

#include <app/CommandHandler.h>
#include <app/CommandSender.h>
#include <app/ConcreteAttributePath.h>
#include <app/InteractionModelEngine.h>
#include <app/tests/integration/common.h>
#include <chrono>
#include <lib/core/CHIPCore.h>
#include <lib/support/ErrorStr.h>
#include <lib/support/TypeTraits.h>
#include <memory>
#include <mutex>
#include <platform/CHIPDeviceLayer.h>
#include <protocols/secure_channel/PASESession.h>
#include <system/SystemClock.h>
#include <system/SystemPacketBuffer.h>
#include <transport/SessionManager.h>
#include <transport/raw/UDP.h>

#define IM_CLIENT_PORT (CHIP_PORT + 1)

namespace {
// Max value for the number of message request sent.

constexpr size_t kMaxCommandMessageCount          = 3;
constexpr size_t kTotalFailureCommandMessageCount = 1;
constexpr size_t kMaxReadMessageCount             = 3;
constexpr size_t kMaxWriteMessageCount            = 3;
constexpr chip::FabricIndex gFabricIndex          = 0;
constexpr size_t kMaxSubMessageCount              = 1;
constexpr uint64_t gSubMaxReport                  = 5;

constexpr chip::System::Clock::Timeout gSubscribeRequestMessageTimeout = chip::System::Clock::Seconds16(1);
constexpr chip::System::Clock::Timeout gMessageInterval                = chip::System::Clock::Milliseconds32(1200);
constexpr chip::System::Clock::Timeout gMessageTimeout                 = chip::System::Clock::Milliseconds32(1000);

chip::TransportMgr<chip::Transport::UDP> gTransportManager;
chip::Inet::IPAddress gDestAddr;

// The last time a CHIP Command was attempted to be sent.
chip::System::Clock::Timestamp gLastMessageTime = chip::System::Clock::kZero;

// Count of the number of CommandRequests sent.
uint64_t gCommandCount = 0;

// Count of the number of CommandResponses received.
uint64_t gCommandRespCount = 0;

// Count of the number of ReadRequestMessages sent.
uint64_t gReadCount = 0;

// Count of the number of ReadResponses received.
uint64_t gReadRespCount = 0;

// Count of the number of WriteRequestMessages sent.
uint64_t gWriteCount = 0;

// Count of the number of WriteResponseMessages received.
uint64_t gWriteRespCount = 0;

// Count of the number of SubscribeRequestMessages sent.
uint64_t gSubCount = 0;

// Count of the number of SubscribeResponseMessages received.
uint64_t gSubRespCount = 0;

// Count of the number of reports for subscription.
uint64_t gSubReportCount = 0;

// Whether the last command successed.
enum class TestCommandResult : uint8_t
{
    kUndefined,
    kSuccess,
    kFailure
};

TestCommandResult gLastCommandResult = TestCommandResult::kUndefined;

void HandleReadComplete()
{
    auto respTime                                   = chip::System::SystemClock().GetMonotonicTimestamp();
    chip::System::Clock::Milliseconds64 transitTime = respTime - gLastMessageTime;

    gReadRespCount++;

    printf("Read Response: %" PRIu64 "/%" PRIu64 "(%.2f%%) time=%.3fs\n", gReadRespCount, gReadCount,
           static_cast<double>(gReadRespCount) * 100 / static_cast<double>(gReadCount),
           static_cast<double>(transitTime.count()) / 1000);
}

void HandleSubscribeReportComplete()
{
    auto respTime                                   = chip::System::SystemClock().GetMonotonicTimestamp();
    chip::System::Clock::Milliseconds64 transitTime = respTime - gLastMessageTime;

    gSubRespCount++;

    printf("Subscribe Complete: %" PRIu64 "/%" PRIu64 "(%.2f%%) time=%.3fs\n", gSubRespCount, gSubCount,
           static_cast<double>(gSubRespCount) * 100 / static_cast<double>(gSubCount),
           static_cast<double>(transitTime.count()) / 1000);
}

class MockInteractionModelApp : public chip::app::InteractionModelDelegate,
                                public ::chip::app::CommandSender::Callback,
                                public ::chip::app::WriteClient::Callback,
                                public ::chip::app::ReadClient::Callback
{
public:
    void OnEventData(const chip::app::ReadClient * apReadClient, const chip::app::EventHeader & aEventHeader,
                     chip::TLV::TLVReader * apData, const chip::app::StatusIB * apStatus) override
    {}
    void OnSubscriptionEstablished(const chip::app::ReadClient * apReadClient) override
    {
        if (apReadClient->IsSubscriptionType())
        {
            gSubReportCount++;
            if (gSubReportCount == gSubMaxReport)
            {
                HandleSubscribeReportComplete();
            }
        }
    }
    void OnAttributeData(const chip::app::ReadClient * apReadClient, const chip::app::ConcreteDataAttributePath & aPath,
                         chip::TLV::TLVReader * aData, const chip::app::StatusIB & status) override
    {}

    void OnError(const chip::app::ReadClient * apReadClient, CHIP_ERROR aError) override
    {
        printf("ReadError with err %" CHIP_ERROR_FORMAT, aError.Format());
    }
    void OnDone(chip::app::ReadClient * apReadClient) override
    {
        if (!apReadClient->IsSubscriptionType())
        {
            HandleReadComplete();
        }
    }

    void OnResponse(chip::app::CommandSender * apCommandSender, const chip::app::ConcreteCommandPath & aPath,
                    const chip::app::StatusIB & aStatus, chip::TLV::TLVReader * aData) override
    {
        printf("Command Response Success with EndpointId %d, ClusterId %d, CommandId %d", aPath.mEndpointId, aPath.mClusterId,
               aPath.mCommandId);

        gLastCommandResult                              = TestCommandResult::kSuccess;
        auto respTime                                   = chip::System::SystemClock().GetMonotonicTimestamp();
        chip::System::Clock::Milliseconds64 transitTime = respTime - gLastMessageTime;

        gCommandRespCount++;

        printf("Command Response: %" PRIu64 "/%" PRIu64 "(%.2f%%) time=%.3fs\n", gCommandRespCount, gCommandCount,
               static_cast<double>(gCommandRespCount) * 100 / static_cast<double>(gCommandCount),
               static_cast<double>(transitTime.count()) / 1000);
    }
    void OnError(const chip::app::CommandSender * apCommandSender, const chip::app::StatusIB & aStatus, CHIP_ERROR aError) override
    {
        gCommandRespCount += (aError == CHIP_ERROR_IM_STATUS_CODE_RECEIVED);
        gLastCommandResult = TestCommandResult::kFailure;
        printf("CommandResponseError happens with %" CHIP_ERROR_FORMAT, aError.Format());
    }
    void OnDone(chip::app::CommandSender * apCommandSender) override {}

    void OnResponse(const chip::app::WriteClient * apWriteClient, const chip::app::ConcreteAttributePath & path,
                    chip::app::StatusIB status) override
    {
        auto respTime                                   = chip::System::SystemClock().GetMonotonicTimestamp();
        chip::System::Clock::Milliseconds64 transitTime = respTime - gLastMessageTime;

        gWriteRespCount++;

        printf("Write Response: %" PRIu64 "/%" PRIu64 "(%.2f%%) time=%.3fs\n", gWriteRespCount, gWriteCount,
               static_cast<double>(gWriteRespCount) * 100 / static_cast<double>(gWriteCount),
               static_cast<double>(transitTime.count()) / 1000);
    }
    void OnError(const chip::app::WriteClient * apCommandSender, const chip::app::StatusIB &, CHIP_ERROR aError) override
    {
        printf("WriteClient::OnError happens with %" CHIP_ERROR_FORMAT, aError.Format());
    }
    void OnDone(chip::app::WriteClient * apWriteClient) override {}
};

MockInteractionModelApp gMockDelegate;

void CommandRequestTimerHandler(chip::System::Layer * systemLayer, void * appState);
void BadCommandRequestTimerHandler(chip::System::Layer * systemLayer, void * appState);
void ReadRequestTimerHandler(chip::System::Layer * systemLayer, void * appState);
void WriteRequestTimerHandler(chip::System::Layer * systemLayer, void * appState);
void SubscribeRequestTimerHandler(chip::System::Layer * systemLayer, void * appState);

CHIP_ERROR SendCommandRequest(std::unique_ptr<chip::app::CommandSender> && commandSender)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrReturnError(commandSender != nullptr, CHIP_ERROR_INCORRECT_STATE);

    gLastMessageTime = chip::System::SystemClock().GetMonotonicTimestamp();

    printf("\nSend invoke command request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    chip::app::CommandPathParams commandPathParams = { kTestEndpointId, 0,
                                                       kTestClusterId, // ClusterId
                                                       kTestCommandId, // CommandId
                                                       chip::app::CommandPathFlags::kEndpointIdValid };

    // Add command data here

    uint8_t effectIdentifier = 1; // Dying light
    uint8_t effectVariant    = 1;
    chip::TLV::TLVWriter * writer;

    err = commandSender->PrepareCommand(commandPathParams);
    SuccessOrExit(err);

    writer = commandSender->GetCommandDataIBTLVWriter();
    err    = writer->Put(chip::TLV::ContextTag(1), effectIdentifier);
    SuccessOrExit(err);

    err = writer->Put(chip::TLV::ContextTag(2), effectVariant);
    SuccessOrExit(err);

    err = commandSender->FinishCommand();
    SuccessOrExit(err);

    err = commandSender->SendCommandRequest(gSession.Get(), gMessageTimeout);
    SuccessOrExit(err);

    gCommandCount++;
    commandSender.release();

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("Send invoke command request failed, err: %s\n", chip::ErrorStr(err));
    }
    return err;
}

CHIP_ERROR SendBadCommandRequest(std::unique_ptr<chip::app::CommandSender> && commandSender)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrReturnError(commandSender != nullptr, CHIP_ERROR_INCORRECT_STATE);

    gLastMessageTime = chip::System::SystemClock().GetMonotonicTimestamp();

    printf("\nSend invoke command request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    chip::app::CommandPathParams commandPathParams = { 0xDE,   // Bad Endpoint
                                                       0xADBE, // Bad GroupId
                                                       0xEFCA, // Bad ClusterId
                                                       0xFE,   // Bad CommandId
                                                       chip::app::CommandPathFlags::kEndpointIdValid };

    err = commandSender->PrepareCommand(commandPathParams);
    SuccessOrExit(err);

    err = commandSender->FinishCommand();
    SuccessOrExit(err);

    err = commandSender->SendCommandRequest(gSession.Get(), gMessageTimeout);
    SuccessOrExit(err);
    gCommandCount++;
    commandSender.release();

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("Send invoke command request failed, err: %s\n", chip::ErrorStr(err));
    }
    return err;
}

CHIP_ERROR SendReadRequest()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::app::EventPathParams eventPathParams[2];
    eventPathParams[0].mEndpointId = kTestEndpointId;
    eventPathParams[0].mClusterId  = kTestClusterId;

    eventPathParams[1].mEndpointId = kTestEndpointId;
    eventPathParams[1].mClusterId  = kTestClusterId;
    eventPathParams[1].mEventId    = kTestChangeEvent2;

    chip::app::AttributePathParams attributePathParams(kTestEndpointId, kTestClusterId, 1);

    printf("\nSend read request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    chip::app::ReadPrepareParams readPrepareParams(gSession.Get());
    readPrepareParams.mTimeout                     = gMessageTimeout;
    readPrepareParams.mpAttributePathParamsList    = &attributePathParams;
    readPrepareParams.mAttributePathParamsListSize = 1;
    readPrepareParams.mpEventPathParamsList        = eventPathParams;
    readPrepareParams.mEventPathParamsListSize     = 2;
    err = chip::app::InteractionModelEngine::GetInstance()->SendReadRequest(readPrepareParams, &gMockDelegate);
    SuccessOrExit(err);

exit:
    if (err == CHIP_NO_ERROR)
    {
        gReadCount++;
    }
    else
    {
        printf("Send read request failed, err: %s\n", chip::ErrorStr(err));
    }
    return err;
}

CHIP_ERROR SendWriteRequest(chip::app::WriteClientHandle & apWriteClient)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    chip::TLV::TLVWriter * writer;
    gLastMessageTime = chip::System::SystemClock().GetMonotonicTimestamp();
    chip::app::AttributePathParams attributePathParams;

    printf("\nSend write request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    attributePathParams.mEndpointId  = 2;
    attributePathParams.mClusterId   = 3;
    attributePathParams.mAttributeId = 4;

    SuccessOrExit(err = apWriteClient->PrepareAttribute(attributePathParams));

    writer = apWriteClient->GetAttributeDataIBTLVWriter();

    SuccessOrExit(err =
                      writer->PutBoolean(chip::TLV::ContextTag(chip::to_underlying(chip::app::AttributeDataIB::Tag::kData)), true));
    SuccessOrExit(err = apWriteClient->FinishAttribute());
    SuccessOrExit(err = apWriteClient.SendWriteRequest(gSession.Get(), gMessageTimeout));

    gWriteCount++;

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("Send read request failed, err: %s\n", chip::ErrorStr(err));
    }
    return err;
}

CHIP_ERROR SendSubscribeRequest()
{
    CHIP_ERROR err   = CHIP_NO_ERROR;
    gLastMessageTime = chip::System::SystemClock().GetMonotonicTimestamp();

    chip::app::ReadPrepareParams readPrepareParams(gSession.Get());
    chip::app::EventPathParams eventPathParams[2];
    chip::app::AttributePathParams attributePathParams[1];
    readPrepareParams.mpEventPathParamsList                = eventPathParams;
    readPrepareParams.mpEventPathParamsList[0].mEndpointId = kTestEndpointId;
    readPrepareParams.mpEventPathParamsList[0].mClusterId  = kTestClusterId;
    readPrepareParams.mpEventPathParamsList[0].mEventId    = kTestChangeEvent1;

    readPrepareParams.mpEventPathParamsList[1].mEndpointId = kTestEndpointId;
    readPrepareParams.mpEventPathParamsList[1].mClusterId  = kTestClusterId;
    readPrepareParams.mpEventPathParamsList[1].mEventId    = kTestChangeEvent2;

    readPrepareParams.mEventPathParamsListSize = 2;

    readPrepareParams.mpAttributePathParamsList                 = attributePathParams;
    readPrepareParams.mpAttributePathParamsList[0].mEndpointId  = kTestEndpointId;
    readPrepareParams.mpAttributePathParamsList[0].mClusterId   = kTestClusterId;
    readPrepareParams.mpAttributePathParamsList[0].mAttributeId = 1;

    readPrepareParams.mAttributePathParamsListSize = 1;

    readPrepareParams.mMinIntervalFloorSeconds   = 5;
    readPrepareParams.mMaxIntervalCeilingSeconds = 5;
    printf("\nSend subscribe request message to Node: %" PRIu64 "\n", chip::kTestDeviceNodeId);

    err = chip::app::InteractionModelEngine::GetInstance()->SendSubscribeRequest(readPrepareParams, &gMockDelegate);
    SuccessOrExit(err);

    gSubCount++;

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("Send subscribe request failed, err: %s\n", chip::ErrorStr(err));
    }
    return err;
}

CHIP_ERROR EstablishSecureSession()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    chip::SecurePairingUsingTestSecret * testSecurePairingSecret = chip::Platform::New<chip::SecurePairingUsingTestSecret>();
    VerifyOrExit(testSecurePairingSecret != nullptr, err = CHIP_ERROR_NO_MEMORY);

    // Attempt to connect to the peer.
    err = gSessionManager.NewPairing(gSession,
                                     chip::Optional<chip::Transport::PeerAddress>::Value(
                                         chip::Transport::PeerAddress::UDP(gDestAddr, CHIP_PORT, chip::Inet::InterfaceId::Null())),
                                     chip::kTestDeviceNodeId, testSecurePairingSecret, chip::CryptoContext::SessionRole::kInitiator,
                                     gFabricIndex);

exit:
    if (err != CHIP_NO_ERROR)
    {
        printf("Establish secure session failed, err: %s\n", chip::ErrorStr(err));
        gLastMessageTime = chip::System::SystemClock().GetMonotonicTimestamp();
    }
    else
    {
        printf("Establish secure session succeeded\n");
    }

    return err;
}

void CommandRequestTimerHandler(chip::System::Layer * systemLayer, void * appState)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (gCommandRespCount != gCommandCount)
    {
        printf("No response received\n");

        // Set gCommandRespCount to gCommandCount to start next iteration if there is any.
        gCommandRespCount = gCommandCount;
    }

    if (gCommandRespCount < kMaxCommandMessageCount)
    {
        auto commandSender = std::make_unique<chip::app::CommandSender>(&gMockDelegate, &gExchangeManager);
        VerifyOrExit(commandSender != nullptr, err = CHIP_ERROR_NO_MEMORY);

        err = SendCommandRequest(std::move(commandSender));
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to send command request with error: %s\n", chip::ErrorStr(err)));

        err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, CommandRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }
    else
    {
        err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, BadCommandRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }
}

void BadCommandRequestTimerHandler(chip::System::Layer * systemLayer, void * appState)
{
    // Test with invalid endpoint / cluster / command combination.
    CHIP_ERROR err     = CHIP_NO_ERROR;
    auto commandSender = std::make_unique<chip::app::CommandSender>(&gMockDelegate, &gExchangeManager);
    VerifyOrExit(commandSender != nullptr, err = CHIP_ERROR_NO_MEMORY);

    err = SendBadCommandRequest(std::move(commandSender));
    VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to send bad command request with error: %s\n", chip::ErrorStr(err)));

    err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, ReadRequestTimerHandler, NULL);
    VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));

exit:
    if (err != CHIP_NO_ERROR)
    {
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }
}

void ReadRequestTimerHandler(chip::System::Layer * systemLayer, void * appState)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (gReadRespCount != gReadCount)
    {
        printf("No response received\n");

        // Set gReadRespCount to gReadCount to start next iteration if there is any.
        gReadRespCount = gReadCount;
    }

    if (gReadRespCount < kMaxReadMessageCount)
    {
        err = SendReadRequest();
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to send read request with error: %s\n", chip::ErrorStr(err)));

        err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, ReadRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }
    else
    {
        err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, WriteRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }
}

void WriteRequestTimerHandler(chip::System::Layer * systemLayer, void * appState)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (gWriteRespCount != gWriteCount)
    {
        printf("No response received\n");

        // Set gWriteRespCount to gWriteCount to start next iteration if there is any.
        gWriteRespCount = gWriteCount;
    }

    if (gWriteRespCount < kMaxWriteMessageCount)
    {
        chip::app::WriteClientHandle writeClient;
        err = chip::app::InteractionModelEngine::GetInstance()->NewWriteClient(writeClient, &gMockDelegate);
        SuccessOrExit(err);

        err = SendWriteRequest(writeClient);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to send write request with error: %s\n", chip::ErrorStr(err)));

        err = chip::DeviceLayer::SystemLayer().StartTimer(gMessageInterval, WriteRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }
    else
    {
        err = chip::DeviceLayer::SystemLayer().StartTimer(gSubscribeRequestMessageTimeout, SubscribeRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }
}

void SubscribeRequestTimerHandler(chip::System::Layer * systemLayer, void * appState)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    if (gSubRespCount != gSubCount)
    {
        printf("No response received\n");

        // Set gSubRespCount to gSubCount to start next iteration if there is any.
        gSubRespCount = gSubCount;
    }

    if (gSubRespCount < kMaxSubMessageCount)
    {
        err = SendSubscribeRequest();
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to send write request with error: %s\n", chip::ErrorStr(err)));

        err = chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Seconds16(20), SubscribeRequestTimerHandler, NULL);
        VerifyOrExit(err == CHIP_NO_ERROR, printf("Failed to schedule timer with error: %s\n", chip::ErrorStr(err)));
    }
    else
    {
        // Complete all tests.
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
    }
}
} // namespace

namespace chip {
namespace app {
bool ServerClusterCommandExists(const ConcreteCommandPath & aCommandPath)
{
    // Always return true in test.
    return true;
}

void DispatchSingleClusterCommand(const ConcreteCommandPath & aCommandPath, chip::TLV::TLVReader & aReader,
                                  CommandHandler * apCommandObj)
{
    // Nothing todo.
}

void DispatchSingleClusterResponseCommand(const ConcreteCommandPath & aCommandPath, chip::TLV::TLVReader & aReader,
                                          CommandSender * apCommandObj)
{
    if (aCommandPath.mClusterId != kTestClusterId || aCommandPath.mCommandId != kTestCommandId ||
        aCommandPath.mEndpointId != kTestEndpointId)
    {
        return;
    }

    if (aReader.GetLength() != 0)
    {
        chip::TLV::Debug::Dump(aReader, TLVPrettyPrinter);
    }

    gLastCommandResult = TestCommandResult::kSuccess;
}

CHIP_ERROR ReadSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, const ConcreteReadAttributePath & aPath,
                                 AttributeReportIBs::Builder & aAttributeReports,
                                 AttributeValueEncoder::AttributeEncodeState * apEncoderState)
{
    AttributeReportIB::Builder & attributeReport = aAttributeReports.CreateAttributeReport();
    ReturnErrorOnFailure(aAttributeReports.GetError());
    AttributeStatusIB::Builder & attributeStatus = attributeReport.CreateAttributeStatus();
    ReturnErrorOnFailure(attributeReport.GetError());
    AttributePathIB::Builder & attributePath = attributeStatus.CreatePath();
    ReturnErrorOnFailure(attributeStatus.GetError());
    attributePath.Endpoint(aPath.mEndpointId).Cluster(aPath.mClusterId).Attribute(aPath.mAttributeId).EndOfAttributePathIB();
    ReturnErrorOnFailure(attributePath.GetError());
    StatusIB::Builder & errorStatus = attributeStatus.CreateErrorStatus();
    errorStatus.EncodeStatusIB(StatusIB(Protocols::InteractionModel::Status::UnsupportedAttribute));
    ReturnErrorOnFailure(errorStatus.GetError());
    attributeStatus.EndOfAttributeStatusIB();
    ReturnErrorOnFailure(attributeStatus.GetError());
    return attributeReport.EndOfAttributeReportIB().GetError();
}

CHIP_ERROR WriteSingleClusterData(const Access::SubjectDescriptor & aSubjectDescriptor, ClusterInfo & aClusterInfo,
                                  TLV::TLVReader & aReader, WriteHandler *)
{
    if (aClusterInfo.mClusterId != kTestClusterId || aClusterInfo.mEndpointId != kTestEndpointId)
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    if (aReader.GetLength() != 0)
    {
        chip::TLV::Debug::Dump(aReader, TLVPrettyPrinter);
    }
    return CHIP_NO_ERROR;
}
} // namespace app
} // namespace chip

int main(int argc, char * argv[])
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);

    if (argc <= 1)
    {
        printf("Missing Command Server IP address\n");
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    if (!chip::Inet::IPAddress::FromString(argv[1], gDestAddr))
    {
        printf("Invalid Command Server IP address: %s\n", argv[1]);
        ExitNow(err = CHIP_ERROR_INVALID_ARGUMENT);
    }

    static_assert(gMessageInterval > gMessageTimeout, "Interval period too small");

    InitializeChip();

    err = gTransportManager.Init(chip::Transport::UdpListenParameters(chip::DeviceLayer::UDPEndPointManager())
                                     .SetAddressType(chip::Inet::IPAddressType::kIPv6)
                                     .SetListenPort(IM_CLIENT_PORT));
    SuccessOrExit(err);

    err = gSessionManager.Init(&chip::DeviceLayer::SystemLayer(), &gTransportManager, &gMessageCounterManager);
    SuccessOrExit(err);

    err = gExchangeManager.Init(&gSessionManager);
    SuccessOrExit(err);

    err = gMessageCounterManager.Init(&gExchangeManager);
    SuccessOrExit(err);

    err = chip::app::InteractionModelEngine::GetInstance()->Init(&gExchangeManager, &gMockDelegate);
    SuccessOrExit(err);

    // Start the CHIP connection to the CHIP im responder.
    err = EstablishSecureSession();
    SuccessOrExit(err);

    err = chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::kZero, CommandRequestTimerHandler, NULL);
    SuccessOrExit(err);

    chip::DeviceLayer::PlatformMgr().RunEventLoop();

    chip::app::InteractionModelEngine::GetInstance()->Shutdown();
    gTransportManager.Close();
    ShutdownChip();
exit:
    if (err != CHIP_NO_ERROR || (gCommandRespCount != kMaxCommandMessageCount + kTotalFailureCommandMessageCount))
    {
        printf("ChipCommandSender failed: %s\n", chip::ErrorStr(err));
        exit(EXIT_FAILURE);
    }

    if (err != CHIP_NO_ERROR || (gReadRespCount != kMaxReadMessageCount))
    {
        printf("ChipReadClient failed: %s\n", chip::ErrorStr(err));
        exit(EXIT_FAILURE);
    }

    if (err != CHIP_NO_ERROR || (gWriteRespCount != kMaxWriteMessageCount))
    {
        printf("ChipWriteClient failed: %s\n", chip::ErrorStr(err));
        exit(EXIT_FAILURE);
    }

    printf("Test success \n");
    return EXIT_SUCCESS;
}
