/*
 * Copyright 2020-present MongoDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "kms_message/kms_kmip_request.h"

#include "kms_message/kms_b64.h"
#include "kms_message_private.h"
#include "kms_request_opt_private.h"
#include "kms_request_str.h"
#include <stdint.h>


#define BSON_BIG_ENDIAN 4321
#define BSON_LITTLE_ENDIAN 1234

#if defined(__sun)
#define BSON_UINT16_SWAP_LE_BE(v) BSWAP_16 ((uint16_t) v)
#define BSON_UINT32_SWAP_LE_BE(v) BSWAP_32 ((uint32_t) v)
#define BSON_UINT64_SWAP_LE_BE(v) BSWAP_64 ((uint64_t) v)
#elif defined(__clang__) && defined(__clang_major__) &&  \
   defined(__clang_minor__) && (__clang_major__ >= 3) && \
   (__clang_minor__ >= 1)
#if __has_builtin(__builtin_bswap16)
#define BSON_UINT16_SWAP_LE_BE(v) __builtin_bswap16 (v)
#endif
#if __has_builtin(__builtin_bswap32)
#define BSON_UINT32_SWAP_LE_BE(v) __builtin_bswap32 (v)
#endif
#if __has_builtin(__builtin_bswap64)
#define BSON_UINT64_SWAP_LE_BE(v) __builtin_bswap64 (v)
#endif
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#if __GNUC__ > 4 || (defined(__GNUC_MINOR__) && __GNUC_MINOR__ >= 3)
#define BSON_UINT32_SWAP_LE_BE(v) __builtin_bswap32 ((uint32_t) v)
#define BSON_UINT64_SWAP_LE_BE(v) __builtin_bswap64 ((uint64_t) v)
#endif
#if __GNUC__ > 4 || (defined(__GNUC_MINOR__) && __GNUC_MINOR__ >= 8)
#define BSON_UINT16_SWAP_LE_BE(v) __builtin_bswap16 ((uint32_t) v)
#endif
#endif


#if 1 // BSON_BYTE_ORDER == BSON_LITTLE_ENDIAN
#define BSON_UINT16_FROM_LE(v) ((uint16_t) v)
#define BSON_UINT16_TO_LE(v) ((uint16_t) v)
#define BSON_UINT16_FROM_BE(v) BSON_UINT16_SWAP_LE_BE (v)
#define BSON_UINT16_TO_BE(v) BSON_UINT16_SWAP_LE_BE (v)
#define BSON_UINT32_FROM_LE(v) ((uint32_t) v)
#define BSON_UINT32_TO_LE(v) ((uint32_t) v)
#define BSON_UINT32_FROM_BE(v) BSON_UINT32_SWAP_LE_BE (v)
#define BSON_UINT32_TO_BE(v) BSON_UINT32_SWAP_LE_BE (v)
#define BSON_UINT64_FROM_LE(v) ((uint64_t) v)
#define BSON_UINT64_TO_LE(v) ((uint64_t) v)
#define BSON_UINT64_FROM_BE(v) BSON_UINT64_SWAP_LE_BE (v)
#define BSON_UINT64_TO_BE(v) BSON_UINT64_SWAP_LE_BE (v)
#define BSON_DOUBLE_FROM_LE(v) ((double) v)
#define BSON_DOUBLE_TO_LE(v) ((double) v)
#elif BSON_BYTE_ORDER == BSON_BIG_ENDIAN
#define BSON_UINT16_FROM_LE(v) BSON_UINT16_SWAP_LE_BE (v)
#define BSON_UINT16_TO_LE(v) BSON_UINT16_SWAP_LE_BE (v)
#define BSON_UINT16_FROM_BE(v) ((uint16_t) v)
#define BSON_UINT16_TO_BE(v) ((uint16_t) v)
#define BSON_UINT32_FROM_LE(v) BSON_UINT32_SWAP_LE_BE (v)
#define BSON_UINT32_TO_LE(v) BSON_UINT32_SWAP_LE_BE (v)
#define BSON_UINT32_FROM_BE(v) ((uint32_t) v)
#define BSON_UINT32_TO_BE(v) ((uint32_t) v)
#define BSON_UINT64_FROM_LE(v) BSON_UINT64_SWAP_LE_BE (v)
#define BSON_UINT64_TO_LE(v) BSON_UINT64_SWAP_LE_BE (v)
#define BSON_UINT64_FROM_BE(v) ((uint64_t) v)
#define BSON_UINT64_TO_BE(v) ((uint64_t) v)
#define BSON_DOUBLE_FROM_LE(v) (__bson_double_swap_slow (v))
#define BSON_DOUBLE_TO_LE(v) (__bson_double_swap_slow (v))
#else
#error "The endianness of target architecture is unknown."
#endif


enum ITEM_TYPE {
   ITEM_TYPE_Structure = 0x01,
   ITEM_TYPE_Integer = 0x02,
   ITEM_TYPE_LongInteger = 0x03,
   ITEM_TYPE_BigInteger = 0x04,
   ITEM_TYPE_Enumeration = 0x05,
   ITEM_TYPE_Boolean = 0x06,
   ITEM_TYPE_TextString = 0x07,
   ITEM_TYPE_ByteString = 0x08,
   ITEM_TYPE_DateTime = 0x09,
   ITEM_TYPE_Interval = 0x0A,
};

enum TAG_TYPE {
   TAG_ActivationDate = 0x420001,
   TAG_ApplicationData = 0x420002,
   TAG_ApplicationNamespace = 0x420003,
   TAG_ApplicationSpecificInformation = 0x420004,
   TAG_ArchiveDate = 0x420005,
   TAG_AsynchronousCorrelationValue = 0x420006,
   TAG_AsynchronousIndicator = 0x420007,
   TAG_Attribute = 0x420008,
   TAG_AttributeIndex = 0x420009,
   TAG_AttributeName = 0x42000A,
   TAG_AttributeValue = 0x42000B,
   TAG_Authentication = 0x42000C,
   TAG_BatchCount = 0x42000D,
   TAG_BatchErrorContinuationOption = 0x42000E,
   TAG_BatchItem = 0x42000F,
   TAG_BatchOrderOption = 0x420010,
   TAG_BlockCipherMode = 0x420011,
   TAG_CancellationResult = 0x420012,
   TAG_Certificate = 0x420013,
   TAG_CertificateIdentifier = 0x420014, //(deprecatedasofvers=ion1.1),
   TAG_CertificateIssuer = 0x420015,     //(deprecatedasofvers=ion1.1),
   TAG_CertificateIssuerAlternativeName =
      0x420016, //(deprecatedasofvers=ion1.1),
   TAG_CertificateIssuerDistinguishedName =
      0x420017, //(deprecatedasofvers=ion1.1),
   TAG_CertificateRequest = 0x420018,
   TAG_CertificateRequestType = 0x420019,
   TAG_CertificateSubject = 0x42001A, //(deprecatedasofvers=ion1.1),
   TAG_CertificateSubjectAlternativeName =
      0x42001B, //(deprecatedasofvers=ion1.1),
   TAG_CertificateSubjectDistinguishedName =
      0x42001C, //(deprecatedasofvers=ion1.1),
   TAG_CertificateType = 0x42001D,
   TAG_CertificateValue = 0x42001E,
   TAG_CommonTemplateAttribute = 0x42001F,
   TAG_CompromiseDate = 0x420020,
   TAG_CompromiseOccurrenceDate = 0x420021,
   TAG_ContactInformation = 0x420022,
   TAG_Credential = 0x420023,
   TAG_CredentialType = 0x420024,
   TAG_CredentialValue = 0x420025,
   TAG_CriticalityIndicator = 0x420026,
   TAG_CRTCoefficient = 0x420027,
   TAG_CryptographicAlgorithm = 0x420028,
   TAG_CryptographicDomainParameters = 0x420029,
   TAG_CryptographicLength = 0x42002A,
   TAG_CryptographicParameters = 0x42002B,
   TAG_CryptographicUsageMask = 0x42002C,
   TAG_CustomAttribute = 0x42002D,
   TAG_D = 0x42002E,
   TAG_DeactivationDate = 0x42002F,
   TAG_DerivationData = 0x420030,
   TAG_DerivationMethod = 0x420031,
   TAG_DerivationParameters = 0x420032,
   TAG_DestroyDate = 0x420033,
   TAG_Digest = 0x420034,
   TAG_DigestValue = 0x420035,
   TAG_EncryptionKeyInformation = 0x420036,
   TAG_G = 0x420037,
   TAG_HashingAlgorithm = 0x420038,
   TAG_InitialDate = 0x420039,
   TAG_InitializationVector = 0x42003A,
   TAG_Issuer = 0x42003B, //(deprecatedasofvers=ion1.1),
   TAG_IterationCount = 0x42003C,
   TAG_IVCounterNonce = 0x42003D,
   TAG_J = 0x42003E,
   TAG_Key = 0x42003F,
   TAG_KeyBlock = 0x420040,
   TAG_KeyCompressionType = 0x420041,
   TAG_KeyFormatType = 0x420042,
   TAG_KeyMaterial = 0x420043,
   TAG_KeyPartIdentifier = 0x420044,
   TAG_KeyValue = 0x420045,
   TAG_KeyWrappingData = 0x420046,
   TAG_KeyWrappingSpecification = 0x420047,
   TAG_LastChangeDate = 0x420048,
   TAG_LeaseTime = 0x420049,
   TAG_Link = 0x42004A,
   TAG_LinkType = 0x42004B,
   TAG_LinkedObjectIdentifier = 0x42004C,
   TAG_MACSignature = 0x42004D,
   TAG_MACSignatureKeyInformation = 0x42004E,
   TAG_MaximumItems = 0x42004F,
   TAG_MaximumResponseSize = 0x420050,
   TAG_MessageExtension = 0x420051,
   TAG_Modulus = 0x420052,
   TAG_Name = 0x420053,
   TAG_NameType = 0x420054,
   TAG_NameValue = 0x420055,
   TAG_ObjectGroup = 0x420056,
   TAG_ObjectType = 0x420057,
   TAG_Offset = 0x420058,
   TAG_OpaqueDataType = 0x420059,
   TAG_OpaqueDataValue = 0x42005A,
   TAG_OpaqueObject = 0x42005B,
   TAG_Operation = 0x42005C,
   TAG_OperationPolicyName = 0x42005D, //(deprecated),
   TAG_P = 0x42005E,
   TAG_PaddingMethod = 0x42005F,
   TAG_PrimeExponentP = 0x420060,
   TAG_PrimeExponentQ = 0x420061,
   TAG_PrimeFieldSize = 0x420062,
   TAG_PrivateExponent = 0x420063,
   TAG_PrivateKey = 0x420064,
   TAG_PrivateKeyTemplateAttribute = 0x420065,
   TAG_PrivateKeyUniqueIdentifier = 0x420066,
   TAG_ProcessStartDate = 0x420067,
   TAG_ProtectStopDate = 0x420068,
   TAG_ProtocolVersion = 0x420069,
   TAG_ProtocolVersionMajor = 0x42006A,
   TAG_ProtocolVersionMinor = 0x42006B,
   TAG_PublicExponent = 0x42006C,
   TAG_PublicKey = 0x42006D,
   TAG_PublicKeyTemplateAttribute = 0x42006E,
   TAG_PublicKeyUniqueIdentifier = 0x42006F,
   TAG_PutFunction = 0x420070,
   TAG_Q = 0x420071,
   TAG_QString = 0x420072,
   TAG_Qlength = 0x420073,
   TAG_QueryFunction = 0x420074,
   TAG_RecommendedCurve = 0x420075,
   TAG_ReplacedUniqueIdentifier = 0x420076,
   TAG_RequestHeader = 0x420077,
   TAG_RequestMessage = 0x420078,
   TAG_RequestPayload = 0x420079,
   TAG_ResponseHeader = 0x42007A,
   TAG_ResponseMessage = 0x42007B,
   TAG_ResponsePayload = 0x42007C,
   TAG_ResultMessage = 0x42007D,
   TAG_ResultReason = 0x42007E,
   TAG_ResultStatus = 0x42007F,
   TAG_RevocationMessage = 0x420080,
   TAG_RevocationReason = 0x420081,
   TAG_RevocationReasonCode = 0x420082,
   TAG_KeyRoleType = 0x420083,
   TAG_Salt = 0x420084,
   TAG_SecretData = 0x420085,
   TAG_SecretDataType = 0x420086,
   TAG_SerialNumber = 0x420087, //(deprecatedasofvers=ion1.1),
   TAG_ServerInformation = 0x420088,
   TAG_SplitKey = 0x420089,
   TAG_SplitKeyMethod = 0x42008A,
   TAG_SplitKeyParts = 0x42008B,
   TAG_SplitKeyThreshold = 0x42008C,
   TAG_State = 0x42008D,
   TAG_StorageStatusMask = 0x42008E,
   TAG_SymmetricKey = 0x42008F,
   TAG_Template = 0x420090,
   TAG_TemplateAttribute = 0x420091,
   TAG_TimeStamp = 0x420092,
   TAG_UniqueBatchItemID = 0x420093,
   TAG_UniqueIdentifier = 0x420094,
   TAG_UsageLimits = 0x420095,
   TAG_UsageLimitsCount = 0x420096,
   TAG_UsageLimitsTotal = 0x420097,
   TAG_UsageLimitsUnit = 0x420098,
   TAG_Username = 0x420099,
   TAG_ValidityDate = 0x42009A,
   TAG_ValidityIndicator = 0x42009B,
   TAG_VendorExtension = 0x42009C,
   TAG_VendorIdentification = 0x42009D,
   TAG_WrappingMethod = 0x42009E,
   TAG_X = 0x42009F,
   TAG_Y = 0x4200A0,
   TAG_Password = 0x4200A1,
   TAG_DeviceIdentifier = 0x4200A2,
   TAG_EncodingOption = 0x4200A3,
   TAG_ExtensionInformation = 0x4200A4,
   TAG_ExtensionName = 0x4200A5,
   TAG_ExtensionTag = 0x4200A6,
   TAG_ExtensionType = 0x4200A7,
   TAG_Fresh = 0x4200A8,
   TAG_MachineIdentifier = 0x4200A9,
   TAG_MediaIdentifier = 0x4200AA,
   TAG_NetworkIdentifier = 0x4200AB,
   TAG_ObjectGroupMember = 0x4200AC,
   TAG_CertificateLength = 0x4200AD,
   TAG_DigitalSignatureAlgorithm = 0x4200AE,
   TAG_CertificateSerialNumber = 0x4200AF,
   TAG_DeviceSerialNumber = 0x4200B0,
   TAG_IssuerAlternativeName = 0x4200B1,
   TAG_IssuerDistinguishedName = 0x4200B2,
   TAG_SubjectAlternativeName = 0x4200B3,
   TAG_SubjectDistinguishedName = 0x4200B4,
   TAG_X509CertificateIdentifier = 0x4200B5,
   TAG_X509CertificateIssuer = 0x4200B6,
   TAG_X509CertificateSubject = 0x4200B7,
   TAG_KeyValueLocation = 0x4200B8,
   TAG_KeyValueLocationValue = 0x4200B9,
   TAG_KeyValueLocationType = 0x4200BA,
   TAG_KeyValuePresent = 0x4200BB,
   TAG_OriginalCreationDate = 0x4200BC,
   TAG_PGPKey = 0x4200BD,
   TAG_PGPKeyVersion = 0x4200BE,
   TAG_AlternativeName = 0x4200BF,
   TAG_AlternativeNameValue = 0x4200C0,
   TAG_AlternativeNameType = 0x4200C1,
   TAG_Data = 0x4200C2,
   TAG_SignatureData = 0x4200C3,
   TAG_DataLength = 0x4200C4,
   TAG_RandomIV = 0x4200C5,
   TAG_MACData = 0x4200C6,
   TAG_AttestationType = 0x4200C7,
   TAG_Nonce = 0x4200C8,
   TAG_NonceID = 0x4200C9,
   TAG_NonceValue = 0x4200CA,
   TAG_AttestationMeasurement = 0x4200CB,
   TAG_AttestationAssertion = 0x4200CC,
   TAG_IVLength = 0x4200CD,
   TAG_TagLength = 0x4200CE,
   TAG_FixedFieldLength = 0x4200CF,
   TAG_CounterLength = 0x4200D0,
   TAG_InitialCounterValue = 0x4200D1,
   TAG_InvocationFieldLength = 0x4200D2,
   TAG_AttestationCapableIndicator = 0x4200D3,
   TAG_OffsetItems = 0x4200D4,
   TAG_LocatedItems = 0x4200D5,
   TAG_CorrelationValue = 0x4200D6,
   TAG_InitIndicator = 0x4200D7,
   TAG_FinalIndicator = 0x4200D8,
   TAG_RNGParameters = 0x4200D9,
   TAG_RNGAlgorithm = 0x4200DA,
   TAG_DRBGAlgorithm = 0x4200DB,
   TAG_FIPS186Variation = 0x4200DC,
   TAG_PredictionResistance = 0x4200DD,
   TAG_RandomNumberGenerator = 0x4200DE,
   TAG_ValidationInformation = 0x4200DF,
   TAG_ValidationAuthorityType = 0x4200E0,
   TAG_ValidationAuthorityCountry = 0x4200E1,
   TAG_ValidationAuthorityURI = 0x4200E2,
   TAG_ValidationVersionMajor = 0x4200E3,
   TAG_ValidationVersionMinor = 0x4200E4,
   TAG_ValidationType = 0x4200E5,
   TAG_ValidationLevel = 0x4200E6,
   TAG_ValidationCertificateIdentifier = 0x4200E7,
   TAG_ValidationCertificateURI = 0x4200E8,
   TAG_ValidationVendorURI = 0x4200E9,
   TAG_ValidationProfile = 0x4200EA,
   TAG_ProfileInformation = 0x4200EB,
   TAG_ProfileName = 0x4200EC,
   TAG_ServerURI = 0x4200ED,
   TAG_ServerPort = 0x4200EE,
   TAG_StreamingCapability = 0x4200EF,
   TAG_AsynchronousCapability = 0x4200F0,
   TAG_AttestationCapability = 0x4200F1,
   TAG_UnwrapMode = 0x4200F2,
   TAG_DestroyAction = 0x4200F3,
   TAG_ShreddingAlgorithm = 0x4200F4,
   TAG_RNGMode = 0x4200F5,
   TAG_ClientRegistrationMethod = 0x4200F6,
   TAG_CapabilityInformation = 0x4200F7,
   TAG_KeyWrapType = 0x4200F8,
   TAG_BatchUndoCapability = 0x4200F9,
   TAG_BatchContinueCapability = 0x4200FA,
   TAG_PKCS12FriendlyName = 0x4200FB,
   TAG_Description = 0x4200FC,
   TAG_Comment = 0x4200FD,
   TAG_AuthenticatedEncryptionAdditionalData = 0x4200FE,
   TAG_AuthenticatedEncryptionTag = 0x4200FF,
   TAG_SaltLength = 0x420100,
   TAG_MaskGenerator = 0x420101,
   TAG_MaskGeneratorHashingAlgorithm = 0x420102,
   TAG_PSource = 0x420103,
   TAG_TrailerField = 0x420104,
   TAG_ClientCorrelationValue = 0x420105,
   TAG_ServerCorrelationValue = 0x420106,
   TAG_DigestedData = 0x420107,
   TAG_CertificateSubjectCN = 0x420108,
   TAG_CertificateSubjectO = 0x420109,
   TAG_CertificateSubjectOU = 0x42010A,
   TAG_CertificateSubjectEmail = 0x42010B,
   TAG_CertificateSubjectC = 0x42010C,
   TAG_CertificateSubjectST = 0x42010D,
   TAG_CertificateSubjectL = 0x42010E,
   TAG_CertificateSubjectUID = 0x42010F,
   TAG_CertificateSubjectSerialNumber = 0x420110,
   TAG_CertificateSubjectTitle = 0x420111,
   TAG_CertificateSubjectDC = 0x420112,
   TAG_CertificateSubjectDNQualifier = 0x420113,
   TAG_CertificateIssuerCN = 0x420114,
   TAG_CertificateIssuerO = 0x420115,
   TAG_CertificateIssuerOU = 0x420116,
   TAG_CertificateIssuerEmail = 0x420117,
   TAG_CertificateIssuerC = 0x420118,
   TAG_CertificateIssuerST = 0x420119,
   TAG_CertificateIssuerL = 0x42011A,
   TAG_CertificateIssuerUID = 0x42011B,
   TAG_CertificateIssuerSerialNumber = 0x42011C,
   TAG_CertificateIssuerTitle = 0x42011D,
   TAG_CertificateIssuerDC = 0x42011E,
   TAG_CertificateIssuerDNQualifier = 0x42011F,
   TAG_Sensitive = 0x420120,
   TAG_AlwaysSensitive = 0x420121,
   TAG_Extractable = 0x420122,
   TAG_NeverExtractable = 0x420123,
   TAG_ReplaceExisting = 0x420124,

   // Used by tests, named x-ID, just takes a string
   // Could not find it documented in Tag list
   // TODO
   TAG_XID = 0x540000,
};


#define MAX_POSITIONS 10
typedef struct {
   kms_request_str_t *buffer;

   size_t positions[MAX_POSITIONS];
   size_t cur_pos;
} kmip_writer_t;


void
write_u8 (kmip_writer_t *writer, uint8_t value)
{
   char *c = (char *) &value;

   kms_request_str_append_chars (writer->buffer, c, 1);
}

void
write_u16 (kmip_writer_t *writer, uint16_t value)
{
   uint16_t v = BSON_UINT16_TO_BE (value);
   char *c = (char *) &v;

   kms_request_str_append_chars (writer->buffer, c, 2);
}

void
write_u32 (kmip_writer_t *writer, uint32_t value)
{
   uint32_t v = BSON_UINT32_TO_BE (value);
   char *c = (char *) &v;

   kms_request_str_append_chars (writer->buffer, c, 4);
}

void
write_u64 (kmip_writer_t *writer, uint64_t value)
{
   uint64_t v = BSON_UINT64_TO_BE (value);
   char *c = (char *) &v;

   kms_request_str_append_chars (writer->buffer, c, 8);
}


void
write_tag_enum (kmip_writer_t *writer, int32_t tag)
{
   // println!("write_Tag");
   // 0x42 for tags built into the protocol
   // 0x54 for extension tags
   write_u8 (writer, 0x42);
   write_u16 (writer, tag);
}

int
compute_padding (int len)
{
   if (len % 8 == 0) {
      return len;
   }

   int padding = 8 - (len % 8);
   return len + padding;
}

void
write_string (kmip_writer_t *writer, int32_t tag, const char *str, int len)
{
   // println!("write_string");
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_TextString);
   write_u32 (writer, len);

   int i;
   for (i = 0; i < len; i++) {
      write_u8 (writer, str[i]);
   }

   int padded_length = compute_padding (len);
   for (i = 0; i < padded_length - len; i++) {
      write_u8 (writer, 0);
   }
}

void
write_bytes (kmip_writer_t *writer, int32_t tag, const char *str, int len)
{
   // println!("write_bytes");
   write_tag_enum (writer, tag);

   write_u8 (writer, ITEM_TYPE_ByteString);
   write_u32 (writer, len);

   int i;
   for (i = 0; i < len; i++) {
      write_u8 (writer, str[i]);
   }

   int padded_length = compute_padding (len);
   for (i = 0; i < padded_length - len; i++) {
      write_u8 (writer, 0);
   }
}

void
write_i32 (kmip_writer_t *writer, int32_t tag, int32_t value)
{
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_Integer);
   write_u32 (writer, 4);
   write_u32 (writer, value);
   write_u32 (writer, 0);
}

void
write_i64 (kmip_writer_t *writer, int32_t tag, int64_t value)
{
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_LongInteger);
   write_u32 (writer, 8);
   write_u64 (writer, value);
}

void
write_enumeration (kmip_writer_t *writer, int32_t tag, int32_t value)
{
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_Enumeration);
   write_u32 (writer, 4);
   write_u32 (writer, value);
   write_u32 (writer, 0);
}

void
write_i64_datetime (kmip_writer_t *writer, int32_t tag, int64_t value)
{
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_DateTime);
   write_u32 (writer, 8);
   write_u64 (writer, value);
}

void
begin_struct (kmip_writer_t *writer, int32_t tag)
{
   write_tag_enum (writer, tag);
   write_u8 (writer, ITEM_TYPE_Structure);

   // println!("write_innter");
   // println!(" begin inner");
   size_t pos = writer->buffer->len;

   write_u32 (writer, 0);
   if (writer->cur_pos == MAX_POSITIONS) {
      abort ();
   }
   writer->cur_pos++;
   writer->positions[writer->cur_pos] = pos;
}

void
close_struct (kmip_writer_t *writer)
{
   // println!(" close inner");
   size_t current_pos = writer->buffer->len;
   if (writer->cur_pos == 0) {
      abort ();
   }
   size_t start_pos = writer->positions[writer->cur_pos];
   writer->cur_pos--;
   // offset by 4
   size_t len = current_pos - start_pos - 4;

   uint32_t v = BSON_UINT32_TO_BE (len);
   char *c = (char *) &v;
   memcpy (writer->buffer->str + start_pos, c, 4);
}


kmip_writer_t *
kmip_writer_new ()
{
   kmip_writer_t *writer = calloc (1, sizeof (kmip_writer_t));
   writer->buffer = kms_request_str_new ();
   return writer;
}


void
kmip_writer_destroy (kmip_writer_t *writer)
{
   kms_request_str_destroy (writer->buffer);
   free (writer);
}

kms_request_t *
kms_kmip_request_encrypt_new (const char *id,
                              const uint8_t *plaintext,
                              size_t plaintext_len,
                              const kms_request_opt_t *opt)
{
   kms_request_t *req;
   kmip_writer_t *writer;
   req = kms_request_new ("POST", "IGNORE", opt);
   writer = kmip_writer_new ();

   if (opt->provider != KMS_REQUEST_PROVIDER_KMIP) {
      KMS_ERROR (req, "Expected KMS request with provider type: KMIP");
      goto done;
   }


   // Write an encyption request

   // <RequestMessage>
   //   <RequestHeader>
   //     <ProtocolVersion>
   //       <ProtocolVersionMajor type="Integer" value="1"/>
   //       <ProtocolVersionMinor type="Integer" value="4"/>
   //     </ProtocolVersion>
   //     <BatchCount type="Integer" value="1"/>
   //   </RequestHeader>
   //   <BatchItem>
   //     <Operation type="Enumeration" value="Encrypt"/>
   //     <RequestPayload>
   //       <UniqueIdentifier type="TextString" value="1"/>
   //       <CryptographicParameters>
   //         <BlockCipherMode type="Enumeration" value="ECB"/>
   //       </CryptographicParameters>
   //       <Data type="ByteString" value="01020304050607080910111213141516"/>
   //     </RequestPayload>
   //   </BatchItem>
   // </RequestMessage>
   // clang-format: off
   begin_struct (writer, TAG_RequestMessage);
   begin_struct (writer, TAG_RequestHeader);
   begin_struct (writer, TAG_ProtocolVersion);
   write_i32 (writer, TAG_ProtocolVersionMajor, 1);
   write_i32 (writer, TAG_ProtocolVersionMinor, 2);
   close_struct (writer);
   write_i32 (writer, TAG_BatchCount, 1);
   close_struct (writer);
   begin_struct (writer, TAG_BatchItem);
   write_enumeration (writer, TAG_Operation, 0x1F);
   begin_struct (writer, TAG_RequestPayload);
   write_string (writer, TAG_UniqueIdentifier, id, strlen (id));
   begin_struct (writer, TAG_CryptographicParameters);
   write_enumeration (writer, TAG_BlockCipherMode, 2); // ECB
   close_struct (writer);
   write_bytes (writer, TAG_Data, (const char *) plaintext, plaintext_len);
   close_struct (writer);
   close_struct (writer);
   close_struct (writer);
   // clang-format: on


   // Dump into payload
   if (!kms_request_append_payload (
          req, writer->buffer->str, writer->buffer->len)) {
      goto done;
   }

done:
   kmip_writer_destroy (writer);

   return req;
}

kms_request_t *
kms_kmip_request_unwrapkey_new (const char *id,
                                const uint8_t *ciphertext,
                                size_t ciphertext_len,
                                const kms_request_opt_t *opt)
{
   //        if (opt->provider != KMS_REQUEST_PROVIDER_KMIP) {
   //       KMS_ERROR (req, "Expected KMS request with provider type: KMIP");
   //       goto done;
   //    }
   return NULL;
}


typedef struct {
   uint8_t *ptr;
   size_t pos;
   size_t len;
} kmip_reader_t;


kmip_reader_t *
kmip_reader_new (uint8_t *ptr, size_t len)
{
   kmip_reader_t *reader = calloc (1, sizeof (kmip_reader_t));
   reader->ptr = ptr;
   reader->len = len;
   return reader;
}


void
kmip_reader_destroy (kmip_reader_t *reader)
{
   free (reader);
}

bool
kmip_reader_in_place (kmip_reader_t *reader, size_t pos, size_t len, kmip_reader_t *out_reader)
{
   // Everything should be padding to 8 byte boundaries
   len = compute_padding(len);
   if((pos + len) > reader->len){
      return false;
   }

   memset(out_reader, 0, sizeof(kmip_reader_t));
   out_reader->ptr = reader->ptr + reader->pos;
   out_reader->len = len;
   return true;
}


size_t
kmip_reader_save_position(kmip_reader_t *reader){
   return reader->pos;
}

void
kmip_reader_restore_position(kmip_reader_t *reader, size_t pos){
   reader->pos = pos;
}

bool
kmip_reader_has_data(kmip_reader_t *reader){
   return reader->pos < reader->len;
}



#define CHECK_REMAINING_BUFFER_AND_RET(read_size)   \
   if ((reader->pos + (read_size)) > reader->len) { \
      return false;                                 \
   }

bool
_kmip_reader_read_u8 (kmip_reader_t *reader, uint8_t *value)
{
   CHECK_REMAINING_BUFFER_AND_RET (sizeof (uint8_t));

   *value = *(reader->ptr+reader->pos);
   reader->pos += sizeof (uint8_t);

   return true;
}

bool
_kmip_reader_read_u16 (kmip_reader_t *reader, uint16_t *value)
{
   CHECK_REMAINING_BUFFER_AND_RET (sizeof (uint16_t));

   uint16_t temp;
   memcpy (&temp, reader->ptr+reader->pos, sizeof (uint16_t));
   *value = BSON_UINT16_FROM_BE (temp);
   reader->pos += sizeof (uint16_t);

   return true;
}

bool
_kmip_reader_read_u32 (kmip_reader_t *reader, uint32_t *value)
{
   CHECK_REMAINING_BUFFER_AND_RET (sizeof (uint32_t));

   uint32_t temp;
   memcpy (&temp, reader->ptr+reader->pos, sizeof (uint32_t));
   *value = BSON_UINT32_FROM_BE (temp);
   reader->pos += sizeof (uint32_t);

   return true;
}

bool
_kmip_reader_read_u64 (kmip_reader_t *reader, uint64_t *value)
{
   CHECK_REMAINING_BUFFER_AND_RET (sizeof (uint64_t));

   uint64_t temp;
   memcpy (&temp, reader->ptr+reader->pos, sizeof (uint64_t));
   *value = BSON_UINT64_FROM_BE (temp);
   reader->pos += sizeof (uint64_t);

   return true;
}

bool
_kmip_reader_read_bytes (kmip_reader_t *reader, uint8_t** ptr, size_t length)
{
   size_t advance_length = compute_padding(length);
   CHECK_REMAINING_BUFFER_AND_RET (advance_length);

   *ptr = reader->ptr+reader->pos;
   reader->pos += advance_length;

   return true;
}


#define READER_CHECK_AND_RET(x) \
   if (!(x)) {                  \
      return false;             \
   }

bool
kmip_reader_read_tag (kmip_reader_t *reader, uint32_t *tag)
{
   uint8_t tag_first;

   READER_CHECK_AND_RET (_kmip_reader_read_u8 (reader, &tag_first));

   if (tag_first != 0x42) {
      return false;
   }

   uint16_t tag_second;
   READER_CHECK_AND_RET (_kmip_reader_read_u16 (reader, &tag_second));

   *tag = (0x420000 + tag_second);
   return true;
}

bool
kmip_reader_read_length (kmip_reader_t *reader, uint32_t *length)
{
   return _kmip_reader_read_u32 (reader, length);
}

bool
kmip_reader_read_type (kmip_reader_t *reader, uint8_t *type)
{
   return _kmip_reader_read_u8 (reader, type);
}


bool
kmip_reader_read_enumeration (kmip_reader_t *reader, uint32_t *enum_value)
{
    READER_CHECK_AND_RET (_kmip_reader_read_u32 (reader, enum_value));

   // Skip 4 bytes becase enums are padded
   uint32_t ignored;

   return _kmip_reader_read_u32 (reader, &ignored);
}


bool
kmip_reader_read_integer (kmip_reader_t *reader, uint32_t *value)
{
    READER_CHECK_AND_RET (_kmip_reader_read_u32 (reader, value));

   // Skip 4 bytes becase integers are padded
   uint32_t ignored;

   return _kmip_reader_read_u32 (reader, &ignored);
}


bool
kmip_reader_read_long_integer (kmip_reader_t *reader, uint64_t *value)
{
   return _kmip_reader_read_u64 (reader, value);
}


bool
kmip_reader_read_bytes (kmip_reader_t *reader, uint8_t** ptr, size_t length)
{
   return _kmip_reader_read_bytes (reader, ptr, length);
}

bool
kmip_reader_read_string (kmip_reader_t *reader, uint8_t** ptr, size_t length)
{
   return _kmip_reader_read_bytes (reader, ptr, length);
}


// void
// kmip_reader_get_struct_reader (kmip_reader_t *reader, size_t count, ...)
// {
//    va_list ap;

//    va_start (ap, count);

//    in
//    va_end (ap);
// }

#define FIND_CHECK_AND_RET(x) \
   if (!(x)) {  \
      kmip_reader_restore_position(reader, saved_pos);     \
      return false;             \
   }

// Note: does not descend structures
bool
kmip_reader_find (kmip_reader_t *reader, size_t search_tag, uint8_t type, size_t* pos, size_t* length)
{
   size_t saved_pos = kmip_reader_save_position(reader);
   
   while(kmip_reader_has_data(reader)) {
      uint32_t read_tag;
      FIND_CHECK_AND_RET(kmip_reader_read_tag(reader, &read_tag));

      uint8_t read_type;
      FIND_CHECK_AND_RET(kmip_reader_read_type(reader, &read_type));

      uint32_t read_length;
      FIND_CHECK_AND_RET(kmip_reader_read_length(reader, &read_length));



      if(read_tag == search_tag && read_type == type) {
         *pos = reader->pos;
         *length = read_length;
         return true;
      }

      size_t advance_length = read_length;
      if(read_type == ITEM_TYPE_ByteString || read_type == ITEM_TYPE_TextString ) {
         advance_length = compute_padding(advance_length);
      }

      CHECK_REMAINING_BUFFER_AND_RET(advance_length);

      // Skip to the next type, 
      reader->pos += advance_length;
   }

   return false;
}



kmip_reader_t*
kmip_reader_find_and_get_struct_reader (kmip_reader_t *reader, size_t tag)
{
   size_t pos;
   size_t length;

   if(!kmip_reader_find(reader, tag, ITEM_TYPE_Structure, &pos, &length)) {
      return NULL;
   }

   return kmip_reader_new (reader->ptr + pos, length);
}


bool
kmip_reader_find_and_read_enum (kmip_reader_t *reader, size_t tag, uint32_t *value)
{
   size_t pos;
   size_t length;

   if(!kmip_reader_find(reader, tag, ITEM_TYPE_Enumeration, &pos, &length)) {
      return NULL;
   }

   kmip_reader_t temp_reader;
   if(!kmip_reader_in_place(reader, pos, length, &temp_reader)) {
      return false;
   }

   return kmip_reader_read_enumeration(&temp_reader, value);
}

bool
kmip_reader_find_and_read_bytes (kmip_reader_t *reader, size_t tag, uint8_t** out_ptr, size_t* out_len)
{
   size_t pos;

   if(!kmip_reader_find(reader, tag, ITEM_TYPE_ByteString, &pos, out_len)) {
      return NULL;
   }

   kmip_reader_t temp_reader;
   if(!kmip_reader_in_place(reader, pos, *out_len, &temp_reader)) {
      return false;
   }

   return kmip_reader_read_bytes(&temp_reader, out_ptr, *out_len);
}

// size_t
// write_u8_unsafe (uint8_t *ptr, size_t len, uint32_t *out)
// {
//    if (len < 4) {
//       return 0;
//    }

//    uint32_t value;
//    memcpy (&value, ptr, 4);

//    uint32_t v2 = BSON_UINT32_FROM_BE (value);
//    *out = v2;

//    return 1;
// }


// size_t
// read_u32_unsafe (uint8_t *ptr, size_t len, uint32_t *out)
// {
//    if (len < 4) {
//       return 0;
//    }

//    uint32_t value;
//    memcpy (&value, ptr, 4);

//    uint32_t v2 = BSON_UINT32_FROM_BE (value);
//    *out = v2;

//    return 1;
// }

//////////////////////////////////////////
struct _kms_kmip_response_parser_t {
   char error[512];
   bool failed;
   kms_response_t *response;
   kms_request_str_t *raw_response;
   uint32_t content_length;
};


kms_kmip_response_parser_t *
kms_kmip_response_parser_new (void)
{
   kms_kmip_response_parser_t *parser =
      calloc (1, sizeof (kms_kmip_response_parser_t));

   parser->raw_response = kms_request_str_new ();
   return parser;
}


void
kms_kmip_response_parser_destroy (kms_kmip_response_parser_t *parser)
{
   kms_request_str_destroy (parser->raw_response);

   free (parser);
}

size_t
kms_kmip_response_parser_wants_bytes (kms_kmip_response_parser_t *parser,
                                      int32_t max)
{
   if (parser->content_length == 0) {
      return max > 512 ? max : 512;
   }

   return parser->content_length - parser->raw_response->len;
}

const char *
kms_kmip_response_parser_error (kms_kmip_response_parser_t *parser)
{
   if (!parser) {
      return NULL;
   }

   return parser->error;
}

size_t
kms_kmip_response_parser_feed (kms_kmip_response_parser_t *parser,
                               uint8_t *buf,
                               uint32_t len)
{
   // First read
   kms_request_str_append_chars (parser->raw_response, (char *) buf, len);

   if (parser->raw_response->len > 8) {
      // Need to reat tag, type and length
      kmip_reader_t *reader = kmip_reader_new (
         (uint8_t *) parser->raw_response->str, parser->raw_response->len);
      uint32_t tag = 0;
      uint8_t kmip_type = 0;
      if (!kmip_reader_read_tag (reader, &tag) ||
          !kmip_reader_read_type (reader, &kmip_type) ||
          !kmip_reader_read_length (reader, &parser->content_length)) {
         kmip_reader_destroy (reader);

         return 0;
      }

      kmip_reader_destroy (reader);
   }

   return 1;
}

void
kms_kmip_response_get_response (kms_kmip_response_parser_t *parser,
                                uint8_t **buf,
                                size_t *len)
{
   *buf = (uint8_t *) parser->raw_response->str;
   *len = parser->raw_response->len;
}



kms_request_t *
kms_kmip_request_parse_encrypt_resp (
                              const uint8_t *resp,
                              size_t resp_len,
                              const kms_request_opt_t *opt){

                                    kms_request_t *req;
   req = kms_request_new ("POST", "IGNORE", opt);

      kmip_reader_t *reader = kmip_reader_new (
         (uint8_t *) resp, resp_len);
kmip_reader_t* response_message;
      kmip_reader_t* batch_item ;
kmip_reader_t* response_payload;

    response_message = kmip_reader_find_and_get_struct_reader(reader, TAG_ResponseMessage);
    if(!response_message){
         KMS_ERROR (req, "TAG_ResponseMessage not found");
         goto done;
      }
      batch_item = kmip_reader_find_and_get_struct_reader(response_message,TAG_BatchItem);

      uint32_t result_status;
      if(!kmip_reader_find_and_read_enum(batch_item, TAG_ResultStatus, &result_status)) {
         KMS_ERROR (req, "Expected KMS request with provider type: KMIP");
         goto done;
      }

      if(result_status != 0) {
         // TODO - better error
                  KMS_ERROR (req, "Bad result status from KMIP");
         goto done;
      }

       response_payload = kmip_reader_find_and_get_struct_reader(batch_item, TAG_ResponsePayload);


      uint8_t *ptr;
      size_t len;

      if(!kmip_reader_find_and_read_bytes(response_payload, TAG_Data, &ptr, &len)){
                  KMS_ERROR (req, "Failed to read Data in response");
         goto done;
      }


done:
      kmip_reader_destroy (reader);

return req;
                              }