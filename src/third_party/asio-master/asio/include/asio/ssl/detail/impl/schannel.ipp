/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>

#include "asio/detail/assert.hpp"

/**
 * Start or continue SSL handshake.
 *
 * Must be called until HandshakeState::Done is returned.
 *
 * Return status code to indicate whether it needs more data or if data needs to be sent to the
 * other side.
 */
ssl_want SSLHandshakeManager::nextHandshake(asio::error_code& ec, HandshakeState* pHandshakeState) {
    ASIO_ASSERT(_mode != HandshakeMode::Unknown);
    ec = asio::error_code();
    *pHandshakeState = HandshakeState::Continue;

    if (_state == State::HandshakeStart) {
        ssl_want want;

        if (_mode == HandshakeMode::Server) {
            // ASIO will ask for the handshake to start when the input buffer is empty
            // but we want data first so tell ASIO to give us data
            if (_pInBuffer->empty()) {
                return ssl_want::want_input_and_retry;
            }

            startServerHandshake(ec);
            if (ec) {
                return ssl_want::want_nothing;
            }

            want = doServerHandshake(true, ec, pHandshakeState);
            if (ec) {
                return want;
            }

        } else {
            startClientHandshake(ec);
            if (ec) {
                return ssl_want::want_nothing;
            }

            want = doClientHandshake(ec);
            if (ec) {
                return want;
            }
        }

        setState(State::NeedMoreHandshakeData);

        return want;
    } else if (_state == State::NeedMoreHandshakeData) {
        return ssl_want::want_input_and_retry;
    } else {
        ssl_want want;

        if (_mode == HandshakeMode::Server) {
            want = doServerHandshake(false, ec, pHandshakeState);
        } else {
            want = doClientHandshake(ec);
        }

        if (ec) {
            return want;
        }

        if (want == ssl_want::want_nothing || *pHandshakeState == HandshakeState::Done) {
            setState(State::Done);
        } else {
            setState(State::NeedMoreHandshakeData);
        }

        return want;
    }
}

/**
 * Begin graceful SSL shutdown. Either:
 * - respond to already received alert signalling connection shutdown on remote side
 * - start SSL shutdown by signalling remote side
 */
ssl_want SSLHandshakeManager::beginShutdown(asio::error_code& ec) {
    ASIO_ASSERT(_mode != HandshakeMode::Unknown);
    _state = State::HandshakeStart;

    return startShutdown(ec);
}

/*
 * Injest data from ASIO that has been received.
 */
void SSLHandshakeManager::writeEncryptedData(const void* data, std::size_t length) {
    // We have more data, it may not be enough to decode. We will decide if we have enough on
    // the next nextHandshake call.
    if (_state != State::HandshakeStart) {
        setState(State::HaveEncryptedData);
    }

    _pInBuffer->append(data, length);
}


void SSLHandshakeManager::startServerHandshake(asio::error_code& ec) {
    TimeStamp lifetime;
    SECURITY_STATUS ss = AcquireCredentialsHandleW(
        NULL, (LPWSTR)UNISP_NAME, SECPKG_CRED_INBOUND, NULL, _cred, NULL, NULL, _phcred, &lifetime);
    if (ss != SEC_E_OK) {
        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return;
    }
}

void SSLHandshakeManager::startClientHandshake(asio::error_code& ec) {
    TimeStamp lifetime;
    SECURITY_STATUS ss = AcquireCredentialsHandleW(NULL,
                                                   (LPWSTR)UNISP_NAME,
                                                   SECPKG_CRED_OUTBOUND,
                                                   NULL,
                                                   _cred,
                                                   NULL,
                                                   NULL,
                                                   _phcred,
                                                   &lifetime);

    if (ss != SEC_E_OK) {
        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return;
    }
}

ssl_want SSLHandshakeManager::startShutdown(asio::error_code& ec) {
    DWORD shutdownCode = SCHANNEL_SHUTDOWN;

    SecBuffer inputBuffers[1];
    inputBuffers[0].cbBuffer = sizeof(shutdownCode);
    inputBuffers[0].BufferType = SECBUFFER_TOKEN;
    inputBuffers[0].pvBuffer = &shutdownCode;

    SecBufferDesc inputBufferDesc;
    inputBufferDesc.ulVersion = SECBUFFER_VERSION;
    inputBufferDesc.cBuffers = 1;
    inputBufferDesc.pBuffers = inputBuffers;

    SECURITY_STATUS ss = ApplyControlToken(_phctxt, &inputBufferDesc);

    if (ss != SEC_E_OK) {
        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return ssl_want::want_nothing;
    }

    TimeStamp lifetime;

    SecBuffer outputBuffer;
    outputBuffer.cbBuffer = 0;
    outputBuffer.BufferType = SECBUFFER_TOKEN;
    outputBuffer.pvBuffer = NULL;
    ContextBufferDeleter deleter(&outputBuffer.pvBuffer);

    SecBufferDesc outputBufferDesc;
    outputBufferDesc.ulVersion = SECBUFFER_VERSION;
    outputBufferDesc.cBuffers = 1;
    outputBufferDesc.pBuffers = &outputBuffer;

    if (_mode == HandshakeMode::Server) {
        ULONG attribs = getServerFlags() | ASC_REQ_ALLOCATE_MEMORY;

        SECURITY_STATUS ss = AcceptSecurityContext(_phcred,
                                                   _phctxt,
                                                   NULL,
                                                   attribs,
                                                   SECURITY_NATIVE_DREP,
                                                   _phctxt,
                                                   &outputBufferDesc,
                                                   &attribs,
                                                   &lifetime);

        if (ss != SEC_E_OK) {
            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }

        _pOutBuffer->reset();
        _pOutBuffer->append(outputBuffer.pvBuffer, outputBuffer.cbBuffer);

        if (SEC_E_OK == ss && outputBuffer.cbBuffer != 0) {
            ec = asio::error::eof;
            return ssl_want::want_output;
        } else {
            return ssl_want::want_nothing;
        }
    } else {
        ULONG ContextAttributes;
        DWORD sspiFlags = getClientFlags() | ISC_REQ_ALLOCATE_MEMORY;

        ss = InitializeSecurityContextA(_phcred,
                                        _phctxt,
                                        const_cast<SEC_CHAR*>(_serverName.c_str()),
                                        sspiFlags,
                                        0,
                                        0,
                                        NULL,
                                        0,
                                        _phctxt,
                                        &outputBufferDesc,
                                        &ContextAttributes,
                                        &lifetime);

        if (ss != SEC_E_OK) {
            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }

        // TODO - I have not found a way to hit this code path
        ASIO_ASSERT(false);
    }

    return ssl_want::want_nothing;
}

ssl_want SSLHandshakeManager::doServerHandshake(bool newConversation,
                                                asio::error_code& ec,
                                                HandshakeState* pHandshakeState) {
    TimeStamp lifetime;

    _pOutBuffer->resize(kDefaultBufferSize);

    SecBuffer outputBuffer;
    outputBuffer.cbBuffer = _pOutBuffer->size();
    outputBuffer.BufferType = SECBUFFER_TOKEN;
    outputBuffer.pvBuffer = _pOutBuffer->data();

    SecBufferDesc outputBufferDesc;
    outputBufferDesc.ulVersion = SECBUFFER_VERSION;
    outputBufferDesc.cBuffers = 1;
    outputBufferDesc.pBuffers = &outputBuffer;

    SecBuffer inputBuffers[2];
    inputBuffers[0].cbBuffer = _pInBuffer->size();
    inputBuffers[0].BufferType = SECBUFFER_TOKEN;
    inputBuffers[0].pvBuffer = _pInBuffer->data();

    inputBuffers[1].cbBuffer = 0;
    inputBuffers[1].BufferType = SECBUFFER_EMPTY;
    inputBuffers[1].pvBuffer = NULL;

    SecBufferDesc inputBufferDesc;
    inputBufferDesc.ulVersion = SECBUFFER_VERSION;
    inputBufferDesc.cBuffers = 2;
    inputBufferDesc.pBuffers = inputBuffers;

    ULONG attribs = getServerFlags();

    SECURITY_STATUS ss = AcceptSecurityContext(_phcred,
                                               newConversation ? NULL : _phctxt,
                                               &inputBufferDesc,
                                               attribs,
                                               SECURITY_NATIVE_DREP,
                                               _phctxt,
                                               &outputBufferDesc,
                                               &attribs,
                                               &lifetime);

    if (ss < SEC_E_OK) {
        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            // TODO: consider using SECBUFFER_MISSING and approriate optimizations
            return ssl_want::want_input_and_retry;
        }
        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return ssl_want::want_nothing;
    }

    SecBuffer* extraBuffer = NULL;

    for (int i = 0; i < 2; i++) {
        if (inputBuffers[i].BufferType == SECBUFFER_EXTRA) {
            extraBuffer = &inputBuffers[i];
        }
    }

    if (extraBuffer != NULL && extraBuffer->cbBuffer > 0) {
        _extraEncryptedBuffer.clear();
        std::copy(reinterpret_cast<unsigned char*>(extraBuffer->pvBuffer),
                  reinterpret_cast<unsigned char*>(extraBuffer->pvBuffer) + extraBuffer->cbBuffer,
                  std::back_inserter(_extraEncryptedBuffer));
    }

    // Next, figure out if we need to send any data out
    bool needOutput{false};

    // Did AcceptSecurityContext say we need to continue or is it done but left data in the
    // output buffer then we need to sent the data out.
    if (SEC_I_CONTINUE_NEEDED == ss || SEC_I_COMPLETE_AND_CONTINUE == ss ||
        (SEC_E_OK == ss && outputBuffer.cbBuffer != 0)) {
        needOutput = true;
    }

    // Tell the reusable buffer size of the data written.
    _pOutBuffer->resize(outputBuffer.cbBuffer);

    // Reset the input buffer
    _pInBuffer->reset();

    // Check if we have any additional encrypted data
    if (!_extraEncryptedBuffer.empty()) {
        _pInBuffer->fill(_extraEncryptedBuffer);
        _extraEncryptedBuffer.clear();

        setState(State::HaveEncryptedData);
    }

    if (needOutput) {
        // If AcceptSecurityContext returns SEC_E_OK, then the handshake is done
        if (SEC_E_OK == ss && outputBuffer.cbBuffer != 0) {
            *pHandshakeState = HandshakeState::Done;

            // We have output, but no need to retry anymore
            return ssl_want::want_output;
        }

        return ssl_want::want_output_and_retry;
    }

    return ssl_want::want_nothing;
}

ssl_want SSLHandshakeManager::doClientHandshake(asio::error_code& ec) {
    DWORD sspiFlags = getClientFlags() | ISC_REQ_ALLOCATE_MEMORY;

    SecBuffer outputBuffers[3];

    outputBuffers[0].cbBuffer = 0;
    outputBuffers[0].BufferType = SECBUFFER_TOKEN;
    outputBuffers[0].pvBuffer = NULL;
    ContextBufferDeleter deleter(&outputBuffers[0].pvBuffer);

    outputBuffers[1].cbBuffer = 0;
    outputBuffers[1].BufferType = SECBUFFER_ALERT;
    outputBuffers[1].pvBuffer = NULL;

    outputBuffers[2].cbBuffer = 0;
    outputBuffers[2].BufferType = SECBUFFER_EMPTY;
    outputBuffers[2].pvBuffer = NULL;

    SecBufferDesc outputBufferDesc;
    outputBufferDesc.ulVersion = SECBUFFER_VERSION;
    outputBufferDesc.cBuffers = 3;
    outputBufferDesc.pBuffers = &outputBuffers[0];

    SecBuffer inputBuffers[2];

    SECURITY_STATUS ss;
    TimeStamp lifetime;
    ULONG ContextAttributes;

    // If the input buffer is empty, this is the start of the client handshake.
    if (!_pInBuffer->empty()) {
        inputBuffers[0].cbBuffer = _pInBuffer->size();
        inputBuffers[0].BufferType = SECBUFFER_TOKEN;
        inputBuffers[0].pvBuffer = _pInBuffer->data();

        inputBuffers[1].cbBuffer = 0;
        inputBuffers[1].BufferType = SECBUFFER_EMPTY;
        inputBuffers[1].pvBuffer = NULL;

        SecBufferDesc inputBufferDesc;
        inputBufferDesc.ulVersion = SECBUFFER_VERSION;
        inputBufferDesc.cBuffers = 2;
        inputBufferDesc.pBuffers = &inputBuffers[0];

        ss = InitializeSecurityContextA(_phcred,
                                        _phctxt,
                                        const_cast<SEC_CHAR*>(_serverName.c_str()),
                                        sspiFlags,
                                        0,
                                        0,
                                        &inputBufferDesc,
                                        0,
                                        _phctxt,
                                        &outputBufferDesc,
                                        &ContextAttributes,
                                        &lifetime);
    } else {
        ss = InitializeSecurityContextA(_phcred,
                                        NULL,
                                        const_cast<SEC_CHAR*>(_serverName.c_str()),
                                        sspiFlags,
                                        0,
                                        0,
                                        NULL,
                                        0,
                                        _phctxt,
                                        &outputBufferDesc,
                                        &ContextAttributes,
                                        &lifetime);
    }

    if (ss < SEC_E_OK) {
        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            return ssl_want::want_input_and_retry;
        }

        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return ssl_want::want_nothing;
    }

    if (_pInBuffer->size()) {
        // Locate (optional) extra buffers.
        SecBuffer* extraBuffer = NULL;

        for (int i = 0; i < 2; i++) {
            if (inputBuffers[i].BufferType == SECBUFFER_EXTRA) {
                extraBuffer = &inputBuffers[i];
            }
        }

        if (extraBuffer != NULL && extraBuffer->cbBuffer > 0) {
            _extraEncryptedBuffer.clear();
            std::copy(reinterpret_cast<unsigned char*>(extraBuffer->pvBuffer),
                      reinterpret_cast<unsigned char*>(extraBuffer->pvBuffer) +
                          extraBuffer->cbBuffer,
                      std::back_inserter(_extraEncryptedBuffer));
        }
    }

    // Next, figure out if we need to send any data out
    bool needOutput{false};

    // Did AcceptSecurityContext say we need to continue or is it done but left data in the
    // output buffer then we need to sent the data out.
    if (SEC_I_CONTINUE_NEEDED == ss || SEC_I_COMPLETE_AND_CONTINUE == ss ||
        (SEC_E_OK == ss && outputBuffers[0].cbBuffer != 0)) {
        needOutput = true;
    }

    //_pOutBuffer->reset();
    _pOutBuffer->append(outputBuffers[0].pvBuffer, outputBuffers[0].cbBuffer);

    // Reset the input buffer
    _pInBuffer->reset();

    // Check if we have any additional encrypted data
    if (!_extraEncryptedBuffer.empty()) {
        _pInBuffer->fill(_extraEncryptedBuffer);
        _extraEncryptedBuffer.clear();

        setState(State::HaveEncryptedData);
    }

    if (needOutput) {
        return ssl_want::want_output_and_retry;
    }

    return ssl_want::want_nothing;
}

/**
 * Read decrypted data if encrypted data was provided via writeData and succesfully decrypted.
 */
ssl_want SSLReadManager::readDecryptedData(void* data,
                                           std::size_t length,
                                           asio::error_code& ec,
                                           std::size_t& bytes_transferred,
                                           DecryptState* pDecryptState) {
    bytes_transferred = 0;
    ec = asio::error_code();
    *pDecryptState = DecryptState::Continue;

    // Our last state was that we needed more encrypted data, so tell ASIO we still want some
    if (_state == State::NeedMoreEncryptedData) {
        return ssl_want::want_input_and_retry;
    }

    // If we have encrypted data, try to decrypt it
    if (_state == State::HaveEncryptedData) {
        ssl_want wantState = decryptBuffer(ec, pDecryptState);
        if (ec) {
            return wantState;
        }

        // If remote side started shutdown, bail
        if (*pDecryptState != DecryptState::Continue) {
            return ssl_want::want_nothing;
        }

        if (wantState == ssl_want::want_input_and_retry) {
            setState(State::NeedMoreEncryptedData);
        }

        if (wantState != ssl_want::want_nothing) {
            return wantState;
        }
    }

    // We decrypted data in the past, hand it back to ASIO until we are out of decrypted data
    ASIO_ASSERT(_state == State::HaveDecryptedData);

    _pInBuffer->read(data, length, bytes_transferred);

    // Have we read all the decrypted data?
    if (_pInBuffer->empty()) {
        // If we have some extra encrypted data, it needs to be checked if it is at least a
        // valid SSL packet, so set the state machine to reflect that we have some encrypted
        // data.
        if (!_extraEncryptedBuffer.empty()) {
            _pInBuffer->fill(_extraEncryptedBuffer);
            _extraEncryptedBuffer.clear();
            setState(State::HaveEncryptedData);
        } else {
            // We are empty so reset our state to need encrypted data for the next call
            setState(State::NeedMoreEncryptedData);
        }
    }

    return ssl_want::want_nothing;
}

ssl_want SSLReadManager::decryptBuffer(asio::error_code& ec, DecryptState* pDecryptState) {
    SecBuffer securityBuffers[4];
    securityBuffers[0].cbBuffer = _pInBuffer->size();
    securityBuffers[0].BufferType = SECBUFFER_DATA;
    securityBuffers[0].pvBuffer = _pInBuffer->data();

    securityBuffers[1].cbBuffer = 0;
    securityBuffers[1].BufferType = SECBUFFER_EMPTY;
    securityBuffers[1].pvBuffer = 0;

    securityBuffers[2].cbBuffer = 0;
    securityBuffers[2].BufferType = SECBUFFER_EMPTY;
    securityBuffers[2].pvBuffer = 0;

    securityBuffers[3].cbBuffer = 0;
    securityBuffers[3].BufferType = SECBUFFER_EMPTY;
    securityBuffers[3].pvBuffer = 0;

    SecBufferDesc bufferDesc;
    bufferDesc.ulVersion = SECBUFFER_VERSION;
    bufferDesc.cBuffers = 4;
    bufferDesc.pBuffers = securityBuffers;

    SECURITY_STATUS ss = DecryptMessage(_phctxt, &bufferDesc, 0, NULL);

    if (ss < SEC_E_OK) {
        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            return ssl_want::want_input_and_retry;
        } else {
            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }
    }

    // Shutdown has been initiated at the client side
    if (ss == SEC_I_CONTEXT_EXPIRED) {
        *pDecryptState = DecryptState::Shutdown;
    } else if (ss == SEC_I_RENEGOTIATE) {
        *pDecryptState = DecryptState::Renegotiate;

        // Fail the connection on SSL renegotiations
        ec = asio::ssl::error::stream_truncated;
        return ssl_want::want_nothing;
    }

    // Locate data and (optional) extra buffers.
    SecBuffer* pDataBuffer = NULL;
    SecBuffer* pExtraBuffer = NULL;

    for (int i = 0; i < 4; i++) {
        if (securityBuffers[i].BufferType == SECBUFFER_DATA) {
            pDataBuffer = &securityBuffers[i];
        }

        if (securityBuffers[i].BufferType == SECBUFFER_EXTRA) {
            ASIO_ASSERT(pExtraBuffer == NULL);
            pExtraBuffer = &securityBuffers[i];
        }
    }

    _pInBuffer->resetPos(pDataBuffer->pvBuffer, pDataBuffer->cbBuffer);

    // The network layer may have read more then 1 SSL packet so remember the extra data.
    if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
        ASIO_ASSERT(_extraEncryptedBuffer.empty());
        _extraEncryptedBuffer.clear();
        std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer),
                  reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) + pExtraBuffer->cbBuffer,
                  std::back_inserter(_extraEncryptedBuffer));
    }

    setState(State::HaveDecryptedData);

    return ssl_want::want_nothing;
}


/**
 * Encrypts data to be sent to the remote side.
 *
 * If the message is >= max packet side, it will return want_output_and_retry, and expects
 * callers to continue to call it with the same parameters until want_output is returned.
 */
ssl_want SSLWriteManager::writeUnecryptedData(const void* pMessage,
                                              std::size_t messageLength,
                                              std::size_t& bytes_transferred,
                                              asio::error_code& ec) {
    ec = asio::error_code();

    if (_securityTrailerLength == ULONG_MAX) {
        SecPkgContext_StreamSizes SecPkgContextStreamSizes;

        SECURITY_STATUS ss =
            QueryContextAttributes(_phctxt, SECPKG_ATTR_STREAM_SIZES, &SecPkgContextStreamSizes);

        if (ss < SEC_E_OK) {
            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }

        _securityTrailerLength = SecPkgContextStreamSizes.cbTrailer;
        _securityMaxMessageLength = SecPkgContextStreamSizes.cbMaximumMessage;
        _securityHeaderLength = SecPkgContextStreamSizes.cbHeader;
    }

    // Do we need to fragment the message out?
    if (messageLength > _securityMaxMessageLength) {
        // Since the message is too large for SSL, we have to write out fragments. We rely on
        // the fact that ASIO will keep giving us the same buffer back as long as it is asked to
        // retry.
        std::size_t fragmentLength =
            std::min(_securityMaxMessageLength, messageLength - lastWriteOffset);
        ssl_want want = encryptMessage(reinterpret_cast<const char*>(pMessage) + lastWriteOffset,
                                       fragmentLength,
                                       bytes_transferred,
                                       ec);
        if (ec) {
            return want;
        }

        lastWriteOffset += fragmentLength;

        // We have more data to give ASIO after this fragment
        if (lastWriteOffset < messageLength) {
            return ssl_want::want_output_and_retry;
        }

        // We have consumed all the data given to us over multiple consecutive calls, reset
        // position.
        lastWriteOffset = 0;

        // ASIO's buffering of engine calls assumes that bytes_transfered refers to all the
        // bytes we transfered total when want_output is returned. It ignores bytes_transfered
        // when want_output_and_retry is returned;
        bytes_transferred = messageLength;

        return ssl_want::want_output;
    } else {
        // Reset fragmentation position
        lastWriteOffset = 0;

        // Send message as is without fragmentation
        return encryptMessage(pMessage, messageLength, bytes_transferred, ec);
    }
}
ssl_want SSLWriteManager::encryptMessage(const void* pMessage,
                                         std::size_t messageLength,
                                         std::size_t& bytes_transferred,
                                         asio::error_code& ec) {

    _pOutBuffer->resize(_securityTrailerLength + _securityHeaderLength + messageLength);

    SecBuffer securityBuffers[4];

    securityBuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
    securityBuffers[0].cbBuffer = _securityHeaderLength;
    securityBuffers[0].pvBuffer = _pOutBuffer->data();

    memcpy_s(_pOutBuffer->data() + _securityHeaderLength,
             _pOutBuffer->size() - _securityHeaderLength,
             pMessage,
             messageLength);

    securityBuffers[1].BufferType = SECBUFFER_DATA;
    securityBuffers[1].cbBuffer = messageLength;
    securityBuffers[1].pvBuffer = _pOutBuffer->data() + _securityHeaderLength;

    securityBuffers[2].cbBuffer = _securityTrailerLength;
    securityBuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
    securityBuffers[2].pvBuffer = _pOutBuffer->data() + _securityHeaderLength + messageLength;

    securityBuffers[3].cbBuffer = 0;
    securityBuffers[3].BufferType = SECBUFFER_EMPTY;
    securityBuffers[3].pvBuffer = 0;

    SecBufferDesc bufferDesc;

    bufferDesc.ulVersion = SECBUFFER_VERSION;
    bufferDesc.cBuffers = 4;
    bufferDesc.pBuffers = securityBuffers;

    SECURITY_STATUS ss = EncryptMessage(_phctxt, 0, &bufferDesc, 0);

    if (ss < SEC_E_OK) {
        ec = asio::error_code(ss, asio::error::get_ssl_category());
        return ssl_want::want_nothing;
    }

    int size =
        securityBuffers[0].cbBuffer + securityBuffers[1].cbBuffer + securityBuffers[2].cbBuffer;

    _pOutBuffer->resize(size);

    bytes_transferred = messageLength;

    return ssl_want::want_output;
}
