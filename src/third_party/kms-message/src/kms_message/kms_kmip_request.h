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

#ifndef KMS_KMIP_REQUEST_H
#define KMS_KMIP_REQUEST_H

#include "kms_message_defines.h"
#include "kms_request.h"
#include "kms_request_opt.h"
#include "kms_response_parser.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Constructs a encrypt request for KMIP.
 *
 * Parameters:
 * All parameters must be NULL terminated strings.
 * - id: The id of the key to use
 * - plaintext: The plaintext key to encrypt.
 * - plaintext_len: The number of bytes of plaintext.
 * - opt: Additional options. This must have the KMIP provider set via
 * kms_request_opt_set_provider.
 */

KMS_MSG_EXPORT (kms_request_t *)
kms_kmip_request_encrypt_new (const char *id,
                              const uint8_t *plaintext,
                              size_t plaintext_len,
                              const kms_request_opt_t *opt);


KMS_MSG_EXPORT (kms_request_t *)
kms_kmip_request_parse_encrypt_resp (
                              const uint8_t *resp,
                              size_t resp_len,
                              const kms_request_opt_t *opt);



/* Constructs an decrypt request for KMIP.
 *
 * Parameters:
 * All parameters must be NULL terminated strings.
 * - id: The id of the key to use
 * - ciphertext: The ciphertext key to decrypt.
 * - ciphertext_len: The number of bytes of ciphertext.
 * - opt: Additional options. This must have the KMIP provider set via
 * kms_request_opt_set_provider.
 */

KMS_MSG_EXPORT (kms_request_t *)
kms_kmip_request_unwrapkey_new (const char *id,
                                const uint8_t *ciphertext,
                                size_t ciphertext_len,
                                const kms_request_opt_t *opt);


typedef struct _kms_kmip_response_parser_t kms_kmip_response_parser_t;


KMS_MSG_EXPORT (kms_kmip_response_parser_t *)
kms_kmip_response_parser_new (void);


KMS_MSG_EXPORT (void)
kms_kmip_response_parser_destroy (kms_kmip_response_parser_t *parser);

KMS_MSG_EXPORT (size_t)
kms_kmip_response_parser_wants_bytes (kms_kmip_response_parser_t *parser, int32_t max);

KMS_MSG_EXPORT (size_t)
kms_kmip_response_parser_feed (kms_kmip_response_parser_t *parser,
                               uint8_t *buf,
                               uint32_t len);


KMS_MSG_EXPORT (const char*)
kms_kmip_response_parser_error (kms_kmip_response_parser_t *parser);

KMS_MSG_EXPORT (void)
kms_kmip_response_get_response (kms_kmip_response_parser_t *parser, uint8_t** buffer, size_t* length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KMS_KMIP_REQUEST_H */
