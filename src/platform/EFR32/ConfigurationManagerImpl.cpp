/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    Copyright (c) 2019 Nest Labs, Inc.
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
 *          Provides the implementation of the Device Layer ConfigurationManager object
 *          for EFR32 platforms using the Silicon Labs SDK.
 */
/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/internal/GenericConfigurationManagerImpl.cpp>

#include <platform/ConfigurationManager.h>
#include <platform/DiagnosticDataProvider.h>
#include <platform/EFR32/EFR32Config.h>

#include "em_rmu.h"

namespace chip {
namespace DeviceLayer {

using namespace ::chip::DeviceLayer::Internal;

ConfigurationManagerImpl & ConfigurationManagerImpl::GetDefaultInstance()
{
    static ConfigurationManagerImpl sInstance;
    return sInstance;
}

CHIP_ERROR ConfigurationManagerImpl::Init()
{
    CHIP_ERROR err;
    bool failSafeArmed;

    // Initialize the generic implementation base class.
    err = Internal::GenericConfigurationManagerImpl<EFR32Config>::Init();
    SuccessOrExit(err);

    // TODO: Initialize the global GroupKeyStore object here (#1626)

    IncreaseBootCount();
    // It is possible to configure the possible reset sources with RMU_ResetControl
    // In this case, we keep Reset control at default setting
    rebootCause = RMU_ResetCauseGet();
    RMU_ResetCauseClear();

    // If the fail-safe was armed when the device last shutdown, initiate a factory reset.
    if (GetFailSafeArmed(failSafeArmed) == CHIP_NO_ERROR && failSafeArmed)
    {
        ChipLogProgress(DeviceLayer, "Detected fail-safe armed on reboot; initiating factory reset");
        InitiateFactoryReset();
    }
    err = CHIP_NO_ERROR;

exit:
    return err;
}

bool ConfigurationManagerImpl::CanFactoryReset()
{
    // TODO: query the application to determine if factory reset is allowed.
    return true;
}

void ConfigurationManagerImpl::InitiateFactoryReset()
{
    PlatformMgr().ScheduleWork(DoFactoryReset);
}

CHIP_ERROR ConfigurationManagerImpl::GetRebootCount(uint32_t & rebootCount)
{
    return EFR32Config::ReadConfigValue(EFR32Config::kConfigKey_BootCount, rebootCount);
}

CHIP_ERROR ConfigurationManagerImpl::IncreaseBootCount(void)
{
    uint32_t bootCount = 0;

    if (EFR32Config::ConfigValueExists(EFR32Config::kConfigKey_BootCount))
    {
        GetRebootCount(bootCount);
    }

    return EFR32Config::WriteConfigValue(EFR32Config::kConfigKey_BootCount, bootCount + 1);
}

uint32_t ConfigurationManagerImpl::GetBootReason(void)
{
    // rebootCause is obtained at bootup.
    uint32_t matterBootCause;
#if defined(_SILICON_LABS_32B_SERIES_1)
    if (rebootCause & RMU_RSTCAUSE_PORST || rebootCause & RMU_RSTCAUSE_EXTRST) // PowerOn or External pin reset
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::PowerOnReboot;
    }
    else if (rebootCause & RMU_RSTCAUSE_AVDDBOD || rebootCause & RMU_RSTCAUSE_DVDDBOD || rebootCause & RMU_RSTCAUSE_DECBOD)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::BrownOutReset;
    }
    else if (rebootCause & RMU_RSTCAUSE_SYSREQRST)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::SoftwareReset;
    }
    else if (rebootCause & RMU_RSTCAUSE_WDOGRST)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::SoftwareWatchdogReset;
    }
    else
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::Unspecified;
    }
    // Not tracked HARDWARE_WATCHDOG_RESET && SOFTWARE_UPDATE_COMPLETED
#elif defined(_SILICON_LABS_32B_SERIES_2)
    if (rebootCause & EMU_RSTCAUSE_POR || rebootCause & EMU_RSTCAUSE_PIN) // PowerOn or External pin reset
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::PowerOnReboot;
    }
    else if (rebootCause & EMU_RSTCAUSE_AVDDBOD || rebootCause & EMU_RSTCAUSE_DVDDBOD || rebootCause & EMU_RSTCAUSE_DECBOD ||
             rebootCause & EMU_RSTCAUSE_VREGIN || rebootCause & EMU_RSTCAUSE_IOVDD0BOD || rebootCause & EMU_RSTCAUSE_DVDDLEBOD)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::BrownOutReset;
    }
    else if (rebootCause & EMU_RSTCAUSE_SYSREQ)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::SoftwareReset;
    }
    else if (rebootCause & EMU_RSTCAUSE_WDOG0 || rebootCause & EMU_RSTCAUSE_WDOG1)
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::SoftwareWatchdogReset;
    }
    else
    {
        matterBootCause = DiagnosticDataProvider::BootReasonType::Unspecified;
    }
    // Not tracked HARDWARE_WATCHDOG_RESET && SOFTWARE_UPDATE_COMPLETED
#else
    matterBootCause = DiagnosticDataProvider::BootReasonType::Unspecified;
#endif

    return matterBootCause;
}

CHIP_ERROR ConfigurationManagerImpl::GetTotalOperationalHours(uint32_t & totalOperationalHours)
{
    if (!EFR32Config::ConfigValueExists(EFR32Config::kConfigKey_TotalOperationalHours))
    {
        totalOperationalHours = 0;
        return CHIP_NO_ERROR;
    }

    return EFR32Config::ReadConfigValue(EFR32Config::kConfigKey_TotalOperationalHours, totalOperationalHours);
}

CHIP_ERROR ConfigurationManagerImpl::StoreTotalOperationalHours(uint32_t totalOperationalHours)
{
    return EFR32Config::WriteConfigValue(EFR32Config::kConfigKey_TotalOperationalHours, totalOperationalHours);
}

CHIP_ERROR ConfigurationManagerImpl::ReadPersistedStorageValue(::chip::Platform::PersistedStorage::Key persistedStorageKey,
                                                               uint32_t & value)
{
    // This method reads CHIP Persisted Counter type nvm3 objects.
    // (where persistedStorageKey represents an index to the counter).
    CHIP_ERROR err;

    err = EFR32Config::ReadConfigValueCounter(persistedStorageKey, value);
    if (err == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND)
    {
        err = CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }
    SuccessOrExit(err);

exit:
    return err;
}

CHIP_ERROR ConfigurationManagerImpl::WritePersistedStorageValue(::chip::Platform::PersistedStorage::Key persistedStorageKey,
                                                                uint32_t value)
{
    // This method reads CHIP Persisted Counter type nvm3 objects.
    // (where persistedStorageKey represents an index to the counter).
    CHIP_ERROR err;

    err = EFR32Config::WriteConfigValueCounter(persistedStorageKey, value);
    if (err == CHIP_DEVICE_ERROR_CONFIG_NOT_FOUND)
    {
        err = CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND;
    }
    SuccessOrExit(err);

exit:
    return err;
}

CHIP_ERROR ConfigurationManagerImpl::ReadConfigValue(Key key, bool & val)
{
    return EFR32Config::ReadConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::ReadConfigValue(Key key, uint32_t & val)
{
    return EFR32Config::ReadConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::ReadConfigValue(Key key, uint64_t & val)
{
    return EFR32Config::ReadConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::ReadConfigValueStr(Key key, char * buf, size_t bufSize, size_t & outLen)
{
    return EFR32Config::ReadConfigValueStr(key, buf, bufSize, outLen);
}

CHIP_ERROR ConfigurationManagerImpl::ReadConfigValueBin(Key key, uint8_t * buf, size_t bufSize, size_t & outLen)
{
    return EFR32Config::ReadConfigValueBin(key, buf, bufSize, outLen);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValue(Key key, bool val)
{
    return EFR32Config::WriteConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValue(Key key, uint32_t val)
{
    return EFR32Config::WriteConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValue(Key key, uint64_t val)
{
    return EFR32Config::WriteConfigValue(key, val);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValueStr(Key key, const char * str)
{
    return EFR32Config::WriteConfigValueStr(key, str);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValueStr(Key key, const char * str, size_t strLen)
{
    return EFR32Config::WriteConfigValueStr(key, str, strLen);
}

CHIP_ERROR ConfigurationManagerImpl::WriteConfigValueBin(Key key, const uint8_t * data, size_t dataLen)
{
    return EFR32Config::WriteConfigValueBin(key, data, dataLen);
}

void ConfigurationManagerImpl::RunConfigUnitTest(void)
{
    EFR32Config::RunConfigUnitTest();
}

void ConfigurationManagerImpl::DoFactoryReset(intptr_t arg)
{
    CHIP_ERROR err;

    ChipLogProgress(DeviceLayer, "Performing factory reset");

    err = EFR32Config::FactoryResetConfig();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "FactoryResetConfig() failed: %s", chip::ErrorStr(err));
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD

    ChipLogProgress(DeviceLayer, "Clearing Thread provision");
    ThreadStackMgr().ErasePersistentInfo();

#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD

    // Restart the system.
    ChipLogProgress(DeviceLayer, "System restarting");
    NVIC_SystemReset();
}

} // namespace DeviceLayer
} // namespace chip
