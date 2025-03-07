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

#include "OTARequestorDriverImpl.h"
#include "esp_log.h"

#define TAG "OTARequestorDriver"

namespace chip {

bool OTARequestorDriverImpl::CheckImageDownloadAllowed()
{
    return true;
}

void OTARequestorDriverImpl::ImageDownloadComplete()
{
    ESP_LOGI(TAG, "Image download complete");
}

UserConsentAction OTARequestorDriverImpl::RequestUserConsent()
{
    return ImmediateYes;
}

} // namespace chip
