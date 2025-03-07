/**
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <app/AppBuildConfig.h>
#include <app/util/basic-types.h>
#include <lib/core/CHIPCore.h>
#include <lib/core/CHIPTLV.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

#include "InvokeResponseIBs.h"
#include "StructBuilder.h"
#include "StructParser.h"

namespace chip {
namespace app {
namespace InvokeResponseMessage {
enum class Tag : uint8_t
{
    kSuppressResponse = 0,
    kInvokeResponses  = 1,
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
     *
     *  @return #CHIP_NO_ERROR on success
     */
    CHIP_ERROR CheckSchemaValidity() const;
#endif

    /**
     *  @brief Get SuppressResponse. Next() must be called before accessing them.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetSuppressResponse(bool * const apSuppressResponse) const;

    /**
     *  @brief Get a parser for a InvokeResponse.
     *
     *  @param [in] apInvokeResponses    A pointer to the invoke response list parser.
     *
     *  @return #CHIP_NO_ERROR on success
     *          #CHIP_END_OF_TLV if there is no such element
     */
    CHIP_ERROR GetInvokeResponses(InvokeResponseIBs::Parser * const apInvokeResponses) const;
};

class Builder : public StructBuilder
{
public:
    /**
     *  @brief This is set to 'true' by the subscriber to indicate preservation of previous subscriptions. If omitted, it implies
     * 'false' as a value.
     */
    InvokeResponseMessage::Builder & SuppressResponse(const bool aSuppressResponse);

    /**
     *  @brief Initialize a InvokeResponseIBs::Builder for writing into the TLV stream
     *
     *  @return A reference to InvokeResponseIBs::Builder
     */
    InvokeResponseIBs::Builder & CreateInvokeResponses();

    /**
     *  @brief Get reference to InvokeResponseIBs::Builder
     *
     *  @return A reference to InvokeResponseIBs::Builder
     */
    InvokeResponseIBs::Builder & GetInvokeResponses() { return mInvokeResponses; }

    /**
     *  @brief Mark the end of this InvokeResponseMessage
     *
     *  @return A reference to *this
     */
    InvokeResponseMessage::Builder & EndOfInvokeResponseMessage();

private:
    InvokeResponseIBs::Builder mInvokeResponses;
};
} // namespace InvokeResponseMessage
} // namespace app
} // namespace chip
