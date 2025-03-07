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

#include <crypto/CHIPCryptoPAL.h>
#include <lib/core/CHIPError.h>
#include <lib/core/CHIPVendorIdentifiers.hpp>
#include <lib/support/Span.h>

namespace chip {
namespace Credentials {

enum class AttestationVerificationResult : uint16_t
{
    kSuccess = 0,

    kPaaUntrusted        = 100,
    kPaaNotFound         = 101,
    kPaaExpired          = 102,
    kPaaSignatureInvalid = 103,
    kPaaRevoked          = 104,
    kPaaFormatInvalid    = 105,
    kPaaArgumentInvalid  = 106,

    kPaiExpired           = 200,
    kPaiSignatureInvalid  = 201,
    kPaiRevoked           = 202,
    kPaiFormatInvalid     = 203,
    kPaiArgumentInvalid   = 204,
    kPaiVendorIdMismatch  = 205,
    kPaiAuthorityNotFound = 206,

    kDacExpired           = 300,
    kDacSignatureInvalid  = 301,
    kDacRevoked           = 302,
    kDacFormatInvalid     = 303,
    kDacArgumentInvalid   = 304,
    kDacVendorIdMismatch  = 305,
    kDacProductIdMismatch = 306,
    kDacAuthorityNotFound = 307,

    kFirmwareInformationMismatch = 400,
    kFirmwareInformationMissing  = 401,

    kAttestationSignatureInvalid       = 500,
    kAttestationElementsMalformed      = 501,
    kAttestationNonceMismatch          = 502,
    kAttestationSignatureInvalidFormat = 503,

    kCertificationDeclarationNoKeyId            = 600,
    kCertificationDeclarationNoCertificateFound = 601,
    kCertificationDeclarationInvalidSignature   = 602,
    kCertificationDeclarationInvalidFormat      = 603,
    kCertificationDeclarationInvalidVendorId    = 604,
    kCertificationDeclarationInvalidProductId   = 605,

    kNoMemory = 700,

    kInvalidArgument = 800,

    kInternalError = 900,

    kNotImplemented = 0xFFFFU,

    // TODO: Add more attestation verification errors
};

enum CertificateType : uint8_t
{
    kUnknown = 0,
    kDAC     = 1,
    kPAI     = 2,
};

struct DeviceInfoForAttestation
{
    // Vendor ID reported by device in Basic Information cluster
    uint16_t vendorId = VendorId::NotSpecified;
    // Product ID reported by device in Basic Information cluster
    uint16_t productId = 0;
    // Vendor ID from  DAC
    uint16_t dacVendorId = VendorId::NotSpecified;
    // Product ID from DAC
    uint16_t dacProductId = 0;
    // Vendor ID from PAI cert
    uint16_t paiVendorId = VendorId::NotSpecified;
    // Product ID from PAI cert (0 if absent)
    uint16_t paiProductId = 0;
    // Vendor ID from  PAA cert
    uint16_t paaVendorId = VendorId::NotSpecified;
};

/**
 * @brief Helper utility to model a basic trust store usable for device attestation verifiers.
 *
 * API is synchronous. Real commissioner implementations may entirely
 * hide Product Attestation Authority cert lookup behind the DeviceAttestationVerifier and
 * never use this interface at all. It is provided as a utility to help build DeviceAttestationVerifier
 * implementations suitable for testing or examples.
 */
class AttestationTrustStore
{
public:
    AttestationTrustStore()          = default;
    virtual ~AttestationTrustStore() = default;

    // Not copyable
    AttestationTrustStore(const AttestationTrustStore &) = delete;
    AttestationTrustStore & operator=(const AttestationTrustStore &) = delete;

    /**
     * @brief Look-up a PAA cert by SKID
     *
     * The implementations of this interface must have access to a set of PAAs to trust.
     *
     * Interface is synchronous, and therefore this should not be used unless to expose a PAA
     * store that is both fully local and quick to access.
     *
     * @param[in] skid Buffer containing the subject key identifier (SKID) of the PAA to look-up
     * @param[inout] outPaaDerBuffer Buffer to receive the contents of the PAA root cert, if found.
     *                                  Size will be updated to match actual size.
     *
     * @returns CHIP_NO_ERROR on success, CHIP_INVALID_ARGUMENT if `skid` or `outPaaDerBuffer` arguments
     *          are not usable, CHIP_BUFFER_TOO_SMALL if certificate doesn't fit in `outPaaDerBuffer`
     *          span, CHIP_ERROR_CA_CERT_NOT_FOUND if no PAA found that matches `skid.
     *
     */
    virtual CHIP_ERROR GetProductAttestationAuthorityCert(const ByteSpan & skid, MutableByteSpan & outPaaDerBuffer) const = 0;
};

/**
 * @brief Basic AttestationTrustStore that holds all data within caller-owned memory.
 *
 * This is useful to wrap a fixed constant array of certificates into a trust store
 * implementation.
 */

class ArrayAttestationTrustStore : public AttestationTrustStore
{
public:
    ArrayAttestationTrustStore(const ByteSpan * derCerts, size_t numCerts) : mDerCerts(derCerts), mNumCerts(numCerts) {}

    CHIP_ERROR GetProductAttestationAuthorityCert(const ByteSpan & skid, MutableByteSpan & outPaaDerBuffer) const override
    {
        VerifyOrReturnError(!skid.empty() && (skid.data() != nullptr), CHIP_ERROR_INVALID_ARGUMENT);
        VerifyOrReturnError(skid.size() == Crypto::kSubjectKeyIdentifierLength, CHIP_ERROR_INVALID_ARGUMENT);

        size_t paaIdx;
        ByteSpan candidate;

        for (paaIdx = 0; paaIdx < mNumCerts; ++paaIdx)
        {
            uint8_t skidBuf[Crypto::kSubjectKeyIdentifierLength] = { 0 };
            candidate                                            = mDerCerts[paaIdx];
            MutableByteSpan candidateSkidSpan{ skidBuf };
            VerifyOrReturnError(CHIP_NO_ERROR == Crypto::ExtractSKIDFromX509Cert(candidate, candidateSkidSpan),
                                CHIP_ERROR_INTERNAL);

            if (skid.data_equal(candidateSkidSpan))
            {
                // Found a match
                return CopySpanToMutableSpan(candidate, outPaaDerBuffer);
            }
        }

        return CHIP_ERROR_CA_CERT_NOT_FOUND;
    }

protected:
    const ByteSpan * mDerCerts;
    const size_t mNumCerts;
};

class DeviceAttestationVerifier
{
public:
    DeviceAttestationVerifier()          = default;
    virtual ~DeviceAttestationVerifier() = default;

    // Not copyable
    DeviceAttestationVerifier(const DeviceAttestationVerifier &) = delete;
    DeviceAttestationVerifier & operator=(const DeviceAttestationVerifier &) = delete;

    /**
     * @brief Verify an attestation information payload against a DAC/PAI chain.
     *
     * @param[in] attestationInfoBuffer Buffer containing attestation information portion of Attestation Response (raw TLV)
     * @param[in] attestationChallengeBuffer Buffer containing the attestation challenge from the secure session
     * @param[in] attestationSignatureBuffer Buffer the signature portion of Attestation Response
     * @param[in] paiDerBuffer Buffer containing the PAI certificate from device in DER format.
     *                                If length zero, there was no PAI certificate.
     * @param[in] dacDerBuffer Buffer containing the DAC certificate from device in DER format.
     * @param[in] attestationNonce Buffer containing attestation nonce.
     *
     * @returns AttestationVerificationResult::kSuccess on success or another specific
     *          value from AttestationVerificationResult enum on failure.
     */
    virtual AttestationVerificationResult VerifyAttestationInformation(const ByteSpan & attestationInfoBuffer,
                                                                       const ByteSpan & attestationChallengeBuffer,
                                                                       const ByteSpan & attestationSignatureBuffer,
                                                                       const ByteSpan & paiDerBuffer, const ByteSpan & dacDerBuffer,
                                                                       const ByteSpan & attestationNonce) = 0;

    /**
     * @brief Verify a CMS Signed Data signature against the CSA certificate of Subject Key Identifier that matches
     *        the subjectKeyIdentifier field of cmsEnvelopeBuffer.
     *
     * @param[in]  cmsEnvelopeBuffer A ByteSpan with a CMS signed message.
     * @param[out] certDeclBuffer    A ByteSpan to hold the CD content extracted from the CMS signed message.
     *
     * @returns AttestationVerificationResult::kSuccess on success or another specific
     *          value from AttestationVerificationResult enum on failure.
     */
    virtual AttestationVerificationResult ValidateCertificationDeclarationSignature(const ByteSpan & cmsEnvelopeBuffer,
                                                                                    ByteSpan & certDeclBuffer) = 0;

    /**
     * @brief Verify a CMS Signed Data Payload against the Basic Information Cluster and DAC/PAI's Vendor and Product IDs
     *
     * @param[in] certDeclBuffer   A ByteSpan with the Certification Declaration content.
     * @param[in] firmwareInfo     A ByteSpan with the Firmware Information content.
     * @param[in] deviceInfo
     *
     * @returns AttestationVerificationResult::kSuccess on success or another specific
     *          value from AttestationVerificationResult enum on failure.
     */
    virtual AttestationVerificationResult ValidateCertificateDeclarationPayload(const ByteSpan & certDeclBuffer,
                                                                                const ByteSpan & firmwareInfo,
                                                                                const DeviceInfoForAttestation & deviceInfo) = 0;

    // TODO: Validate Firmware Information

protected:
    CHIP_ERROR ValidateAttestationSignature(const chip::Crypto::P256PublicKey & pubkey, const ByteSpan & attestationElements,
                                            const ByteSpan & attestationChallenge,
                                            const chip::Crypto::P256ECDSASignature & signature);
};

/**
 * Instance getter for the global DeviceAttestationVerifier.
 *
 * Callers have to externally synchronize usage of this function.
 *
 * @return The global device attestation verifier. Assume never null.
 */
DeviceAttestationVerifier * GetDeviceAttestationVerifier();

/**
 * Instance setter for the global DeviceAttestationVerifier.
 *
 * Callers have to externally synchronize usage of this function.
 *
 * If the `verifier` is nullptr, no change is done.
 *
 * @param[in] verifier the DeviceAttestationVerifier to start returning with the getter
 */
void SetDeviceAttestationVerifier(DeviceAttestationVerifier * verifier);

} // namespace Credentials
} // namespace chip
