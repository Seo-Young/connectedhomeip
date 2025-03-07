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

#pragma once

#include <app/OperationalDeviceProxy.h>
#include <lib/support/Pool.h>
#include <transport/SessionHandle.h>

namespace chip {

class OperationalDeviceProxyPoolDelegate
{
public:
    virtual OperationalDeviceProxy * Allocate(DeviceProxyInitParams & params, PeerId peerId) = 0;

    virtual OperationalDeviceProxy * Allocate(DeviceProxyInitParams & params, PeerId peerId,
                                              const Dnssd::ResolvedNodeData & nodeResolutionData) = 0;

    virtual void Release(OperationalDeviceProxy * device) = 0;

    virtual OperationalDeviceProxy * FindDevice(const SessionHandle & session) = 0;

    virtual OperationalDeviceProxy * FindDevice(NodeId id) = 0;

    virtual ~OperationalDeviceProxyPoolDelegate() {}
};

template <size_t N>
class OperationalDeviceProxyPool : public OperationalDeviceProxyPoolDelegate
{
public:
    ~OperationalDeviceProxyPool() { mDevicePool.ReleaseAll(); }

    OperationalDeviceProxy * Allocate(DeviceProxyInitParams & params, PeerId peerId) override
    {
        return mDevicePool.CreateObject(params, peerId);
    }

    OperationalDeviceProxy * Allocate(DeviceProxyInitParams & params, PeerId peerId,
                                      const Dnssd::ResolvedNodeData & nodeResolutionData) override
    {
        return mDevicePool.CreateObject(params, peerId, nodeResolutionData);
    }

    void Release(OperationalDeviceProxy * device) override { mDevicePool.ReleaseObject(device); }

    OperationalDeviceProxy * FindDevice(const SessionHandle & session) override
    {
        OperationalDeviceProxy * foundDevice = nullptr;
        mDevicePool.ForEachActiveObject([&](auto * activeDevice) {
            if (activeDevice->MatchesSession(session))
            {
                foundDevice = activeDevice;
                return Loop::Break;
            }
            return Loop::Continue;
        });

        return foundDevice;
    }

    OperationalDeviceProxy * FindDevice(NodeId id) override
    {
        OperationalDeviceProxy * foundDevice = nullptr;
        mDevicePool.ForEachActiveObject([&](auto * activeDevice) {
            if (activeDevice->GetDeviceId() == id)
            {
                foundDevice = activeDevice;
                return Loop::Break;
            }
            return Loop::Continue;
        });

        return foundDevice;
    }

private:
    BitMapObjectPool<OperationalDeviceProxy, N> mDevicePool;
};

}; // namespace chip
