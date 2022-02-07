/**
 *  Copyright Notice:
 *  Copyright 2021 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libspdm/blob/main/LICENSE.md
 **/

#include "internal/libspdm_requester_lib.h"

#if LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP

/**
 * Process the SPDM encapsulated CHALLENGE request and return the response.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  request_size                  size in bytes of the request data.
 * @param  request                      A pointer to the request data.
 * @param  response_size                 size in bytes of the response data.
 *                                     On input, it means the size in bytes of response data buffer.
 *                                     On output, it means the size in bytes of copied response data buffer if RETURN_SUCCESS is returned,
 *                                     and means the size in bytes of desired response data buffer if RETURN_BUFFER_TOO_SMALL is returned.
 * @param  response                     A pointer to the response data.
 *
 * @retval RETURN_SUCCESS               The request is processed and the response is returned.
 * @retval RETURN_BUFFER_TOO_SMALL      The buffer is too small to hold the data.
 * @retval RETURN_DEVICE_ERROR          A device error occurs when communicates with the device.
 * @retval RETURN_SECURITY_VIOLATION    Any verification fails.
 **/
return_status spdm_get_encap_response_challenge_auth(
    IN void *context, IN uintn request_size, IN void *request,
    IN OUT uintn *response_size, OUT void *response)
{
    spdm_challenge_request_t *spdm_request;
    spdm_challenge_auth_response_t *spdm_response;
    bool result;
    uintn signature_size;
    uint8_t slot_id;
    uint32_t hash_size;
    uint32_t measurement_summary_hash_size;
    uint8_t *ptr;
    uintn total_size;
    spdm_context_t *spdm_context;
    uint8_t auth_attribute;
    return_status status;

    spdm_context = context;
    spdm_request = request;

    if (spdm_request->header.spdm_version != spdm_get_connection_version(spdm_context)) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_VERSION_MISMATCH,
            SPDM_CHALLENGE, response_size, response);
    }

    if (!spdm_is_capabilities_flag_supported(
            spdm_context, true,
            SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHAL_CAP, 0)) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSUPPORTED_REQUEST,
            SPDM_CHALLENGE, response_size, response);
    }

    if (request_size != sizeof(spdm_challenge_request_t)) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
            response_size, response);
    }

    slot_id = spdm_request->header.param1;

    if ((slot_id != 0xFF) &&
        (slot_id >= spdm_context->local_context.slot_count)) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
            response_size, response);
    }

    spdm_reset_message_buffer_via_request_code(spdm_context, NULL,
                                               spdm_request->header.request_response_code);

    signature_size = libspdm_get_req_asym_signature_size(
        spdm_context->connection_info.algorithm.req_base_asym_alg);
    hash_size = libspdm_get_hash_size(
        spdm_context->connection_info.algorithm.base_hash_algo);
    measurement_summary_hash_size = 0;

    total_size =
        sizeof(spdm_challenge_auth_response_t) + hash_size +
        SPDM_NONCE_SIZE + measurement_summary_hash_size +
        sizeof(uint16_t) +
        spdm_context->local_context.opaque_challenge_auth_rsp_size +
        signature_size;

    ASSERT(*response_size >= total_size);
    *response_size = total_size;
    zero_mem(response, *response_size);
    spdm_response = response;

    spdm_response->header.spdm_version = spdm_request->header.spdm_version;
    spdm_response->header.request_response_code = SPDM_CHALLENGE_AUTH;
    auth_attribute = (uint8_t)(slot_id & 0xF);
    spdm_response->header.param1 = auth_attribute;
    spdm_response->header.param2 = (1 << slot_id);
    if (slot_id == 0xFF) {
        spdm_response->header.param2 = 0;

        slot_id = spdm_context->local_context.provisioned_slot_id;
    }

    ptr = (void *)(spdm_response + 1);
    result = spdm_generate_cert_chain_hash(spdm_context, slot_id, ptr);
    if (!result) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSPECIFIED, 0,
            response_size, response);
    }
    ptr += hash_size;

    if(!libspdm_get_random_number(SPDM_NONCE_SIZE, ptr)) {
        return RETURN_DEVICE_ERROR;
    }
    ptr += SPDM_NONCE_SIZE;

    ptr += measurement_summary_hash_size;

    *(uint16_t *)ptr = (uint16_t)spdm_context->local_context
                       .opaque_challenge_auth_rsp_size;
    ptr += sizeof(uint16_t);
    if (spdm_context->local_context.opaque_challenge_auth_rsp != NULL) {
        copy_mem(ptr, spdm_context->local_context.opaque_challenge_auth_rsp,
                 spdm_context->local_context.opaque_challenge_auth_rsp_size);
        ptr += spdm_context->local_context.opaque_challenge_auth_rsp_size;
    }

    /* Calc Sign*/

    status = libspdm_append_message_mut_c(spdm_context, spdm_request,
                                          request_size);
    if (RETURN_ERROR(status)) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSPECIFIED, 0,
            response_size, response);
    }

    status = libspdm_append_message_mut_c(spdm_context, spdm_response,
                                          (uintn)ptr - (uintn)spdm_response);
    if (RETURN_ERROR(status)) {
        libspdm_reset_message_mut_c(spdm_context);
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSPECIFIED, 0,
            response_size, response);
    }
    result =
        spdm_generate_challenge_auth_signature(spdm_context, true, ptr);
    if (!result) {
        return libspdm_generate_encap_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSPECIFIED,
            0, response_size, response);
    }
    ptr += signature_size;

    return RETURN_SUCCESS;
}

#endif /* LIBSPDM_ENABLE_CAPABILITY_CHAL_CAP*/
