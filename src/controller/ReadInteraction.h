/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#pragma once

#include <app/AttributePathParams.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadPrepareParams.h>
#include <controller/TypedReadCallback.h>

namespace chip {
namespace Controller {
namespace detail {

template <typename DecodableAttributeType>
struct ReportAttributeParams : public app::ReadPrepareParams
{
    ReportAttributeParams(const SessionHandle & sessionHandle) : app::ReadPrepareParams(sessionHandle)
    {
        mKeepSubscriptions = false;
    }
    typename TypedReadAttributeCallback<DecodableAttributeType>::OnSuccessCallbackType mOnReportCb;
    typename TypedReadAttributeCallback<DecodableAttributeType>::OnErrorCallbackType mOnErrorCb;
    typename TypedReadAttributeCallback<DecodableAttributeType>::OnSubscriptionEstablishedCallbackType
        mOnSubscriptionEstablishedCb             = nullptr;
    app::ReadClient::InteractionType mReportType = app::ReadClient::InteractionType::Read;
};

template <typename DecodableAttributeType>
CHIP_ERROR ReportAttribute(Messaging::ExchangeManager * exchangeMgr, EndpointId endpointId, ClusterId clusterId,
                           AttributeId attributeId, ReportAttributeParams<DecodableAttributeType> && readParams)
{
    app::AttributePathParams attributePath(endpointId, clusterId, attributeId);
    app::ReadClient * readClient         = nullptr;
    app::InteractionModelEngine * engine = app::InteractionModelEngine::GetInstance();
    CHIP_ERROR err                       = CHIP_NO_ERROR;

    readParams.mpAttributePathParamsList    = &attributePath;
    readParams.mAttributePathParamsListSize = 1;

    auto onDone = [](app::ReadClient * apReadClient, TypedReadAttributeCallback<DecodableAttributeType> * callback) {
        chip::Platform::Delete(callback);
    };

    auto callback = chip::Platform::MakeUnique<TypedReadAttributeCallback<DecodableAttributeType>>(
        clusterId, attributeId, readParams.mOnReportCb, readParams.mOnErrorCb, onDone, readParams.mOnSubscriptionEstablishedCb);
    VerifyOrReturnError(callback != nullptr, CHIP_ERROR_NO_MEMORY);

    ReturnErrorOnFailure(engine->NewReadClient(&readClient, readParams.mReportType, &(callback->GetBufferedCallback())));

    err = readClient->SendRequest(readParams);

    if (err != CHIP_NO_ERROR)
    {
        readClient->Shutdown();
        return err;
    }

    //
    // At this point, we'll get a callback through the OnDone callback above regardless of success or failure
    // of the read operation to permit us to free up the callback object. So, release ownership of the callback
    // object now to prevent it from being reclaimed at the end of this scoped block.
    //
    callback.release();
    return err;
}

} // namespace detail

/**
 * To avoid instantiating all the complicated read code on a per-attribute
 * basis, we have a helper that's just templated on the type.
 */
template <typename DecodableAttributeType>
CHIP_ERROR ReadAttribute(Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
                         ClusterId clusterId, AttributeId attributeId,
                         typename TypedReadAttributeCallback<DecodableAttributeType>::OnSuccessCallbackType onSuccessCb,
                         typename TypedReadAttributeCallback<DecodableAttributeType>::OnErrorCallbackType onErrorCb)
{
    detail::ReportAttributeParams<DecodableAttributeType> params(sessionHandle);
    params.mOnReportCb = onSuccessCb;
    params.mOnErrorCb  = onErrorCb;
    return detail::ReportAttribute(exchangeMgr, endpointId, clusterId, attributeId, std::move(params));
}

/*
 * A typed read attribute function that takes as input a template parameter that encapsulates the type information
 * for a given attribute as well as callbacks for success and failure and either returns a decoded cluster-object representation
 * of the requested attribute through the provided success callback or calls the provided failure callback.
 *
 * The AttributeTypeInfo is generally expected to be a ClusterName::Attributes::AttributeName::TypeInfo struct, but any
 * object that contains type information exposed through a 'DecodableType' type declaration as well as GetClusterId() and
 * GetAttributeId() methods is expected to work.
 */
template <typename AttributeTypeInfo>
CHIP_ERROR
ReadAttribute(Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
              typename TypedReadAttributeCallback<typename AttributeTypeInfo::DecodableType>::OnSuccessCallbackType onSuccessCb,
              typename TypedReadAttributeCallback<typename AttributeTypeInfo::DecodableType>::OnErrorCallbackType onErrorCb)
{
    return ReadAttribute<typename AttributeTypeInfo::DecodableType>(exchangeMgr, sessionHandle, endpointId,
                                                                    AttributeTypeInfo::GetClusterId(),
                                                                    AttributeTypeInfo::GetAttributeId(), onSuccessCb, onErrorCb);
}

// Helper for SubscribeAttribute to reduce the amount of code generated.
template <typename DecodableAttributeType>
CHIP_ERROR SubscribeAttribute(Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
                              ClusterId clusterId, AttributeId attributeId,
                              typename TypedReadAttributeCallback<DecodableAttributeType>::OnSuccessCallbackType onReportCb,
                              typename TypedReadAttributeCallback<DecodableAttributeType>::OnErrorCallbackType onErrorCb,
                              uint16_t minIntervalFloorSeconds, uint16_t maxIntervalCeilingSeconds,
                              typename TypedReadAttributeCallback<DecodableAttributeType>::OnSubscriptionEstablishedCallbackType
                                  onSubscriptionEstablishedCb = nullptr)
{
    detail::ReportAttributeParams<DecodableAttributeType> params(sessionHandle);
    params.mOnReportCb                  = onReportCb;
    params.mOnErrorCb                   = onErrorCb;
    params.mOnSubscriptionEstablishedCb = onSubscriptionEstablishedCb;
    params.mMinIntervalFloorSeconds     = minIntervalFloorSeconds;
    params.mMaxIntervalCeilingSeconds   = maxIntervalCeilingSeconds;
    params.mReportType                  = app::ReadClient::InteractionType::Subscribe;
    return detail::ReportAttribute(exchangeMgr, endpointId, clusterId, attributeId, std::move(params));
}

/*
 * A typed way to subscribe to the value of a single attribute.  See
 * documentation for ReadAttribute above for details on how AttributeTypeInfo
 * works.
 */
template <typename AttributeTypeInfo>
CHIP_ERROR SubscribeAttribute(
    Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
    typename TypedReadAttributeCallback<typename AttributeTypeInfo::DecodableType>::OnSuccessCallbackType onReportCb,
    typename TypedReadAttributeCallback<typename AttributeTypeInfo::DecodableType>::OnErrorCallbackType onErrorCb,
    uint16_t aMinIntervalFloorSeconds, uint16_t aMaxIntervalCeilingSeconds,
    typename TypedReadAttributeCallback<typename AttributeTypeInfo::DecodableType>::OnSubscriptionEstablishedCallbackType
        onSubscriptionEstablishedCb = nullptr)
{
    return SubscribeAttribute<typename AttributeTypeInfo::DecodableType>(
        exchangeMgr, sessionHandle, endpointId, AttributeTypeInfo::GetClusterId(), AttributeTypeInfo::GetAttributeId(), onReportCb,
        onErrorCb, aMinIntervalFloorSeconds, aMaxIntervalCeilingSeconds, onSubscriptionEstablishedCb);
}

namespace detail {

template <typename DecodableEventType>
struct ReportEventParams : public app::ReadPrepareParams
{
    ReportEventParams(const SessionHandle & sessionHandle) : app::ReadPrepareParams(sessionHandle) { mKeepSubscriptions = false; }
    typename TypedReadEventCallback<DecodableEventType>::OnSuccessCallbackType mOnReportCb;
    typename TypedReadEventCallback<DecodableEventType>::OnErrorCallbackType mOnErrorCb;
    typename TypedReadEventCallback<DecodableEventType>::OnSubscriptionEstablishedCallbackType mOnSubscriptionEstablishedCb =
        nullptr;
    app::ReadClient::InteractionType mReportType = app::ReadClient::InteractionType::Read;
};

template <typename DecodableEventType>
CHIP_ERROR ReportEvent(Messaging::ExchangeManager * apExchangeMgr, EndpointId endpointId,
                       ReportEventParams<DecodableEventType> && readParams)
{
    ClusterId clusterId = DecodableEventType::GetClusterId();
    EventId eventId     = DecodableEventType::GetEventId();
    app::EventPathParams eventPath(endpointId, clusterId, eventId);
    app::ReadClient * readClient         = nullptr;
    app::InteractionModelEngine * engine = app::InteractionModelEngine::GetInstance();
    CHIP_ERROR err                       = CHIP_NO_ERROR;

    readParams.mpEventPathParamsList    = &eventPath;
    readParams.mEventPathParamsListSize = 1;

    auto onDone = [](app::ReadClient * apReadClient, TypedReadEventCallback<DecodableEventType> * callback) {
        chip::Platform::Delete(callback);
    };

    auto callback = chip::Platform::MakeUnique<TypedReadEventCallback<DecodableEventType>>(
        readParams.mOnReportCb, readParams.mOnErrorCb, onDone, readParams.mOnSubscriptionEstablishedCb);

    VerifyOrReturnError(callback != nullptr, CHIP_ERROR_NO_MEMORY);

    ReturnErrorOnFailure(engine->NewReadClient(&readClient, readParams.mReportType, callback.get()));

    err = readClient->SendRequest(readParams);
    if (err != CHIP_NO_ERROR)
    {
        readClient->Shutdown();
        return err;
    }

    //
    // At this point, we'll get a callback through the OnDone callback above regardless of success or failure
    // of the read operation to permit us to free up the callback object. So, release ownership of the callback
    // object now to prevent it from being reclaimed at the end of this scoped block.
    //
    callback.release();
    return err;
}

} // namespace detail

/*
 * A typed read event function that takes as input a template parameter that encapsulates the type information
 * for a given attribute as well as callbacks for success and failure and either returns a decoded cluster-object representation
 * of the requested attribute through the provided success callback or calls the provided failure callback.
 *
 * The DecodableEventType is generally expected to be a ClusterName::Events::EventName::DecodableEventType struct, but any
 * object that contains type information exposed through a 'DecodableType' type declaration as well as GetClusterId() and
 * GetEventId() methods is expected to work.
 */
template <typename DecodableEventType>
CHIP_ERROR ReadEvent(Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
                     typename TypedReadEventCallback<DecodableEventType>::OnSuccessCallbackType onSuccessCb,
                     typename TypedReadEventCallback<DecodableEventType>::OnErrorCallbackType onErrorCb)
{
    detail::ReportEventParams<DecodableEventType> params(sessionHandle);
    params.mOnReportCb = onSuccessCb;
    params.mOnErrorCb  = onErrorCb;
    return detail::ReportEvent(exchangeMgr, endpointId, std::move(params));
}

/**
 * A functon that allows subscribing to one particular event.  This works
 * similarly to ReadEvent but keeps reporting events as they are emitted.
 */
template <typename DecodableEventType>
CHIP_ERROR SubscribeEvent(Messaging::ExchangeManager * exchangeMgr, const SessionHandle & sessionHandle, EndpointId endpointId,
                          typename TypedReadEventCallback<DecodableEventType>::OnSuccessCallbackType onReportCb,
                          typename TypedReadEventCallback<DecodableEventType>::OnErrorCallbackType onErrorCb,
                          uint16_t minIntervalFloorSeconds, uint16_t maxIntervalCeilingSeconds,
                          typename TypedReadEventCallback<DecodableEventType>::OnSubscriptionEstablishedCallbackType
                              onSubscriptionEstablishedCb = nullptr)
{
    detail::ReportEventParams<DecodableEventType> params(sessionHandle);
    params.mOnReportCb                  = onReportCb;
    params.mOnErrorCb                   = onErrorCb;
    params.mOnSubscriptionEstablishedCb = onSubscriptionEstablishedCb;
    params.mMinIntervalFloorSeconds     = minIntervalFloorSeconds;
    params.mMaxIntervalCeilingSeconds   = maxIntervalCeilingSeconds;
    params.mReportType                  = app::ReadClient::InteractionType::Subscribe;
    return detail::ReportEvent(exchangeMgr, endpointId, std::move(params));
}

} // namespace Controller
} // namespace chip
