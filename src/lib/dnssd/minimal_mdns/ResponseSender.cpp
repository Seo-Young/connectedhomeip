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

#include "ResponseSender.h"

#include "QueryReplyFilter.h"

#include <system/SystemClock.h>

#define RETURN_IF_ERROR(err)                                                                                                       \
    do                                                                                                                             \
    {                                                                                                                              \
        if (err != CHIP_NO_ERROR)                                                                                                  \
        {                                                                                                                          \
            return;                                                                                                                \
        }                                                                                                                          \
    } while (false)

namespace mdns {
namespace Minimal {

namespace {

constexpr uint16_t kMdnsStandardPort = 5353;

// Restriction for UDP packets:  https://tools.ietf.org/html/rfc1035#section-4.2.1
//
//    Messages carried by UDP are restricted to 512 bytes (not counting the IP
//    or UDP headers).  Longer messages are truncated and the TC bit is set in
//    the header.
constexpr uint16_t kPacketSizeBytes = 512;

} // namespace
namespace Internal {

bool ResponseSendingState::SendUnicast() const
{
    return mQuery->RequestedUnicastAnswer() || (mSource->SrcPort != kMdnsStandardPort);
}

bool ResponseSendingState::IncludeQuery() const
{
    return (mSource->SrcPort != kMdnsStandardPort);
}

} // namespace Internal

CHIP_ERROR ResponseSender::AddQueryResponder(QueryResponderBase * queryResponder)
{
    for (size_t i = 0; i < kMaxQueryResponders; ++i)
    {
        if (mResponder[i] == nullptr || mResponder[i] == queryResponder)
        {
            mResponder[i] = queryResponder;
            return CHIP_NO_ERROR;
        }
    }
    return CHIP_ERROR_NO_MEMORY;
}

CHIP_ERROR ResponseSender::Respond(uint32_t messageId, const QueryData & query, const chip::Inet::IPPacketInfo * querySource)
{
    mSendState.Reset(messageId, query, querySource);

    // Responder has a stateful 'additional replies required' that is used within the response
    // loop. 'no additionals required' is set at the start and additionals are marked as the query
    // reply is built.
    for (size_t i = 0; i < kMaxQueryResponders; ++i)
    {
        if (mResponder[i] != nullptr)
        {
            mResponder[i]->ResetAdditionals();
        }
    }

    // send all 'Answer' replies
    {
        const chip::System::Clock::Timestamp kTimeNow = chip::System::SystemClock().GetMonotonicTimestamp();

        QueryReplyFilter queryReplyFilter(query);
        QueryResponderRecordFilter responseFilter;

        responseFilter.SetReplyFilter(&queryReplyFilter);

        if (!mSendState.SendUnicast())
        {
            // According to https://tools.ietf.org/html/rfc6762#section-6  we should multicast at most 1/sec
            //
            // TODO: the 'last sent' value does NOT track the interface we used to send, so this may cause
            //       broadcasts on one interface to throttle broadcasts on another interface.
            responseFilter.SetIncludeOnlyMulticastBeforeMS(kTimeNow - chip::System::Clock::Seconds32(1));
        }
        for (size_t i = 0; i < kMaxQueryResponders; ++i)
        {
            if (mResponder[i] == nullptr)
            {
                continue;
            }
            for (auto it = mResponder[i]->begin(&responseFilter); it != mResponder[i]->end(); it++)
            {
                it->responder->AddAllResponses(querySource, this);
                ReturnErrorOnFailure(mSendState.GetError());

                mResponder[i]->MarkAdditionalRepliesFor(it);

                if (!mSendState.SendUnicast())
                {
                    it->lastMulticastTime = kTimeNow;
                }
            }
        }
    }

    // send all 'Additional' replies
    {
        mSendState.SetResourceType(ResourceType::kAdditional);

        QueryReplyFilter queryReplyFilter(query);

        queryReplyFilter.SetIgnoreNameMatch(true).SetSendingAdditionalItems(true);

        QueryResponderRecordFilter responseFilter;
        responseFilter
            .SetReplyFilter(&queryReplyFilter) //
            .SetIncludeAdditionalRepliesOnly(true);
        for (size_t i = 0; i < kMaxQueryResponders; ++i)
        {
            if (mResponder[i] == nullptr)
            {
                continue;
            }
            for (auto it = mResponder[i]->begin(&responseFilter); it != mResponder[i]->end(); it++)
            {
                it->responder->AddAllResponses(querySource, this);
                ReturnErrorOnFailure(mSendState.GetError());
            }
        }
    }

    return FlushReply();
}

CHIP_ERROR ResponseSender::FlushReply()
{
    ReturnErrorCodeIf(!mResponseBuilder.HasPacketBuffer(), CHIP_NO_ERROR); // nothing to flush

    if (mResponseBuilder.HasResponseRecords())
    {
        char srcAddressString[chip::Inet::IPAddress::kMaxStringLength];
        VerifyOrDie(mSendState.GetSourceAddress().ToString(srcAddressString) != nullptr);

        if (mSendState.SendUnicast())
        {
            ChipLogDetail(Discovery, "Directly sending mDns reply to peer %s on port %d", srcAddressString,
                          mSendState.GetSourcePort());
            ReturnErrorOnFailure(mServer->DirectSend(mResponseBuilder.ReleasePacket(), mSendState.GetSourceAddress(),
                                                     mSendState.GetSourcePort(), mSendState.GetSourceInterfaceId()));
        }
        else
        {
            ChipLogDetail(Discovery, "Broadcasting mDns reply for query from %s", srcAddressString);
            ReturnErrorOnFailure(mServer->BroadcastSend(mResponseBuilder.ReleasePacket(), kMdnsStandardPort,
                                                        mSendState.GetSourceInterfaceId(), mSendState.GetSourceAddress().Type()));
        }
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR ResponseSender::PrepareNewReplyPacket()
{
    chip::System::PacketBufferHandle buffer = chip::System::PacketBufferHandle::New(kPacketSizeBytes);
    ReturnErrorCodeIf(buffer.IsNull(), CHIP_ERROR_NO_MEMORY);

    mResponseBuilder.Reset(std::move(buffer));
    mResponseBuilder.Header().SetMessageId(mSendState.GetMessageId());

    if (mSendState.IncludeQuery())
    {
        mResponseBuilder.AddQuery(*mSendState.GetQuery());
    }

    return CHIP_NO_ERROR;
}

void ResponseSender::AddResponse(const ResourceRecord & record)
{
    RETURN_IF_ERROR(mSendState.GetError());

    if (!mResponseBuilder.HasPacketBuffer())
    {
        mSendState.SetError(PrepareNewReplyPacket());
        RETURN_IF_ERROR(mSendState.GetError());
    }

    if (!mResponseBuilder.Ok())
    {
        mSendState.SetError(CHIP_ERROR_INCORRECT_STATE);
        return;
    }

    mResponseBuilder.AddRecord(mSendState.GetResourceType(), record);

    // ResponseBuilder AddRecord will only fail if insufficient space is available (or at least this is
    // the assumption here). It also guarantees that existing data and header are unchanged on
    // failure, hence we can flush and try again. This allows for split replies.
    if (!mResponseBuilder.Ok())
    {
        mResponseBuilder.Header().SetFlags(mResponseBuilder.Header().GetFlags().SetTruncated(true));

        RETURN_IF_ERROR(mSendState.SetError(FlushReply()));
        RETURN_IF_ERROR(mSendState.SetError(PrepareNewReplyPacket()));

        mResponseBuilder.AddRecord(mSendState.GetResourceType(), record);
        if (!mResponseBuilder.Ok())
        {
            // Very much unexpected: single record addtion should fit (our records should not be that big).
            ChipLogError(Discovery, "Failed to add single record to mDNS response.");
            mSendState.SetError(CHIP_ERROR_INTERNAL);
        }
    }
}

} // namespace Minimal
} // namespace mdns
