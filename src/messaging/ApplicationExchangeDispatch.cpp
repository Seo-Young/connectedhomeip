/*
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

/**
 *    @file
 *      This file provides implementation of Application Channel class.
 */

#include <messaging/ApplicationExchangeDispatch.h>
#include <protocols/secure_channel/Constants.h>

namespace chip {
namespace Messaging {

CHIP_ERROR ApplicationExchangeDispatch::PrepareMessage(const SessionHandle & session, PayloadHeader & payloadHeader,
                                                       System::PacketBufferHandle && message,
                                                       EncryptedPacketBufferHandle & preparedMessage)
{
    return mSessionManager->PrepareMessage(session, payloadHeader, std::move(message), preparedMessage);
}

CHIP_ERROR ApplicationExchangeDispatch::SendPreparedMessage(const SessionHandle & session,
                                                            const EncryptedPacketBufferHandle & preparedMessage) const
{
    return mSessionManager->SendPreparedMessage(session, preparedMessage);
}

bool ApplicationExchangeDispatch::MessagePermitted(uint16_t protocol, uint8_t type)
{
    // TODO: Change this check to only include the protocol and message types that are allowed
    switch (protocol)
    {
    case Protocols::SecureChannel::Id.GetProtocolId():
        switch (type)
        {
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PBKDFParamRequest):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PBKDFParamResponse):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PASE_Pake1):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PASE_Pake2):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PASE_Pake3):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::PASE_PakeError):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::CASE_Sigma1):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::CASE_Sigma2):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::CASE_Sigma3):
        case static_cast<uint8_t>(Protocols::SecureChannel::MsgType::CASE_Sigma2Resume):
            return false;

        default:
            break;
        }
        break;

    default:
        break;
    }
    return true;
}

} // namespace Messaging
} // namespace chip
