/**
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include "AttributePathIBs.h"
#include "DataVersionFilterIBs.h"
#include "EventFilterIBs.h"
#include "EventPathIBs.h"
#include "StructBuilder.h"
#include "StructParser.h"
#include <app/AppBuildConfig.h>
#include <app/util/basic-types.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/CHIPTLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

namespace chip {
namespace app {
namespace SubscribeRequestMessage {
enum class Tag : uint8_t
{
    kKeepSubscriptions         = 0,
    kMinIntervalFloorSeconds   = 1,
    kMaxIntervalCeilingSeconds = 2,
    kAttributeRequests         = 3,
    kDataVersionFilters        = 4,
    kEventRequests             = 5,
    kEventFilters              = 6,
    kIsProxy                   = 7,
    kIsFabricFiltered          = 8,
};

class Parser : public StructParser
{
public:
#if CHIP_CONFIG_IM_ENABLE_SCHEMA_CHECK
    /**
     *  @brief Roughly verify the message is correctly formed
     *   1) all mandatory tags are present
     *   2) all elements have expected data type
     *   3) any tag can only appear once
     *   4) At the top level of the structure, unknown tags are ignored for forward compatibility
     *  @note The main use of this function is to print out what we're
     *    receiving during protocol development and debugging.
     *    The encoding rule has changed in IM encoding spec so this
     *    check is only "roughly" conformant now.
     */
    CHIP_ERROR CheckSchemaValidity() const;
#endif

    /**
     *  @brief Check if subscription is kept.
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetKeepSubscriptions(bool * const apKeepExistingSubscription) const;

    /**
     *  @brief Get MinIntervalFloorSeconds. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetMinIntervalFloorSeconds(uint16_t * const apMinIntervalFloorSeconds) const;

    /**
     *  @brief Get MaxIntervalCeilingSeconds. Next() must be called before accessing them.
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetMaxIntervalCeilingSeconds(uint16_t * const apMaxIntervalCeilingSeconds) const;

    /**
     *  @brief Get a TLVReader for the AttributePathIBs. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetAttributeRequests(AttributePathIBs::Parser * const apAttributeRequests) const;

    /**
     *  @brief Get a TLVReader for the DataVersionFilterIBs. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetDataVersionFilters(DataVersionFilterIBs::Parser * const apDataVersionFilters) const;

    /**
     *  @brief Get a TLVReader for the EventPathIBs. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetEventRequests(EventPathIBs::Parser * const apEventRequests) const;

    /**
     *  @brief Get a TLVReader for the EventFilterIBs. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetEventFilters(EventFilterIBs::Parser * const apEventFilters) const;

    /**
     *  @brief Get GetIsProxy boolean .
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetIsProxy(bool * const apIsProxy) const;

    /**
     *  @brief Get IsFabricFiltered boolean
     *
     *  @param [in] apIsFabricFiltered    A pointer to apIsFabricFiltered
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetIsFabricFiltered(bool * const apIsFabricFiltered) const;
};

class Builder : public StructBuilder
{
public:
    /**
     *  @brief This is set to 'true' by the subscriber to indicate preservation of previous subscriptions. If omitted, it implies
     * 'false' as a value.
     */
    SubscribeRequestMessage::Builder & KeepSubscriptions(const bool aKeepSubscriptions);
    SubscribeRequestMessage::Builder & MinIntervalFloorSeconds(const uint16_t aMinIntervalFloorSeconds);
    SubscribeRequestMessage::Builder & MaxIntervalCeilingSeconds(const uint16_t aMinIntervalFloorSeconds);
    AttributePathIBs::Builder & CreateAttributeRequests();
    DataVersionFilterIBs::Builder & CreateDataVersionFilters();
    EventPathIBs::Builder & CreateEventRequests();
    EventFilterIBs::Builder & CreateEventFilters();

    /**
     *  @brief This is set to true by the subscriber if it is a proxy-type device proxying for another client. This
     *  confers it special privileges on the publisher that might result in evictions of other non-proxy subscriptions
     *  to make way for the proxy.
     */
    SubscribeRequestMessage::Builder & IsProxy(const bool aIsProxy);

    /**
     *  @brief  limits the data written within fabric-scoped lists to the accessing fabric
     *  @return A reference to *this
     */
    SubscribeRequestMessage::Builder & IsFabricFiltered(const bool aIsFabricFiltered);

    /**
     *  @brief Mark the end of this SubscribeRequestMessage
     */
    SubscribeRequestMessage::Builder & EndOfSubscribeRequestMessage();

private:
    AttributePathIBs::Builder mAttributeRequests;
    DataVersionFilterIBs::Builder mDataVersionFilters;
    EventPathIBs::Builder mEventRequests;
    EventFilterIBs::Builder mEventFilters;
};
} // namespace SubscribeRequestMessage
} // namespace app
} // namespace chip
