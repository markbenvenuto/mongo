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
 * Reusable buffer. Behaves as a sort of producer consumer queue in a sense.
 *
 * Data is added to the buffer then removed.
 *
 * Typical workflow:
 * - Write data
 * - Write more data
 * - Read some data
 * - Keeping reading until empty
 *
 * Invariants:
 * - Once reading from a buffer is started, no more writes are permitted until
 *   consumer has read all the entire buffer.
 */
class ReusableBuffer {
public:
    ReusableBuffer(std::size_t initialSize) {
        _buffer.reset(new unsigned char[initialSize]);
        _capacity = initialSize;
    }

    /**
     * Is buffer empty?
     */
    bool empty() const {
        return _size == 0;
    }

    /**
     * Get raw pointer to buffer.
     */
    unsigned char* data() {
        return _buffer.get();
    }

    /**
     * Get current number of elements in buffer.
     */
    std::size_t size() const {
        return _size;
    }

    /**
     * Reset to empty state.
     */
    void reset() {
        _bufPos = 0;
        _size = 0;
    }

    /**
     * Add data to empty buffer.
     */
    void fill(const void* data, std::size_t length) {
        ASIO_ASSERT(_size == 0);
        ASIO_ASSERT(_bufPos == 0);
        append(data, length);
    }

    /**
     * Add data to empty buffer.
     */
    void fill(const std::vector<unsigned char>& vec) {
        append(vec.data(), vec.size());
    }

    /**
     * Reset current position to specified pointer in buffer.
     */
    void resetPos(void* pos, std::size_t size) {
        ASIO_ASSERT(pos >= _buffer.get() && pos < (_buffer.get() + _size));
        _bufPos = (unsigned char*)pos - _buffer.get();
        resize(_bufPos + size);
    }

    /**
     * Append data to buffer.
     */
    void append(const void* data, std::size_t length) {
        ASIO_ASSERT(_bufPos == 0);
        auto originalSize = _size;
        resize(_size + length);
        std::copy(reinterpret_cast<const unsigned char*>(data),
                  reinterpret_cast<const unsigned char*>(data) + length,
                  _buffer.get() + originalSize);
    }

    /**
     * Read data from buffer. Can be a partial read.
     */
    void read(void* data, std::size_t length, std::size_t& outLength) {
        if (length >= (size() - _bufPos)) {
            // We have less then ASIO wants, give them everything we have
            outLength = size() - _bufPos;
            memcpy(data, _buffer.get() + _bufPos, size() - _bufPos);

            // We are empty so reset our state to need encrypted data for the next callwant_
            _bufPos = 0;
            _size = 0;
        } else {
            // ASIO wants less then we have so give them just what they want
            outLength = length;
            memcpy(data, _buffer.get() + _bufPos, length);

            _bufPos += length;
        }
    }

    /**
     * Realloc buffer preserving existing data.
     */
    void resize(std::size_t size) {
        if (size > _capacity) {
            std::unique_ptr<unsigned char[]> temp(new unsigned char[size]);

            memcpy(temp.get(), _buffer.get(), _size);
            _buffer.swap(temp);
            _capacity = _size;
        }
        _size = size;
    }

private:
    // Buffer of data
    std::unique_ptr<unsigned char[]> _buffer;

    // Current read position in buffer
    std::size_t _bufPos{0};

    // Count of elements in buffer
    std::size_t _size{0};

    // Capacity of buffer for elements, always >= _size
    std::size_t _capacity;
};


const std::size_t kDefaultBufferSize = 16 * 1024;

enum class ssl_want {
    // Returned by functions to indicate that the engine wants input. The input
    // buffer should be updated to point to the data. The engine then needs to
    // be called again to retry the operation.
    want_input_and_retry = -2,

    // Returned by functions to indicate that the engine wants to write output.
    // The output buffer points to the data to be written. The engine then
    // needs to be called again to retry the operation.
    want_output_and_retry = -1,

    // Returned by functions to indicate that the engine doesn't need input or
    // output.
    want_nothing = 0,

    // Returned by functions to indicate that the engine wants to write output.
    // The output buffer points to the data to be written. After that the
    // operation is complete, and the engine does not need to be called again.
    want_output = 1
};

// TODO
class SSLHandshakeManager {
public:
    // TODO
    enum class HandshakeMode {
        Unknown,
        Client,
        Server,
    };

    // TODO
    enum class HandshakeState { Continue, Done };


    SSLHandshakeManager(PCtxtHandle hctxt,
                        PCredHandle phcred,
                        ReusableBuffer* inBuffer,
                        ReusableBuffer* outBuffer,
                        SCHANNEL_CRED* cred)
        : _state(State::HandshakeStart),
          _phctxt(hctxt),
          _cred(cred),
          _phcred(phcred),
          _pInBuffer(inBuffer),
          _pOutBuffer(outBuffer),
          _mode(HandshakeMode::Unknown) {}

    /**
     * Set the current hanshake mode as client or server.
     *
     * Idempotent if called with same mode otherwise it asserts.
     */
    void setMode(HandshakeMode mode) {
        ASIO_ASSERT(_mode == HandshakeMode::Unknown || _mode == mode);
        ASIO_ASSERT(mode != HandshakeMode::Unknown);
        _mode = mode;
    }


    ssl_want nextHandshake(asio::error_code& ec, HandshakeState* pHandshakeState) {
        ASIO_ASSERT(_mode != HandshakeMode::Unknown);
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

                want = doServerHandshake(true, ec, pHandshakeState);
            } else {
                startClientHandshake(ec);

                want = doClientHandshake(ec);
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

            if (want == ssl_want::want_nothing || *pHandshakeState == HandshakeState::Done) {
                setState(State::Done);
            } else {
                setState(State::NeedMoreHandshakeData);
            }

            return want;
        }
    }

    ssl_want beginShutdown(asio::error_code& ec) {
        ASIO_ASSERT(_mode != HandshakeMode::Unknown);
        _state = State::HandshakeStart;

        return startShutdown(ec);
    }


    /*
     * Injest data from ASIO that has been received.
     */
    void writeEncryptedData(const void* data, std::size_t length) {
        // We have more data, it may not be enough to decode
        // but we will figure that out later
        if (_state != State::HandshakeStart) {
            setState(State::HaveEncryptedData);
        }

        // TODO: reexamine this
        // If we have extra encrypted data from the last encryption, copy it over to our buffer
        if (_extraEncryptedBuffer.size()) {
            _pInBuffer->fill(_extraEncryptedBuffer);
            _extraEncryptedBuffer.clear();
        }

        _pInBuffer->append(data, length);
    }

    /**
     * Returns true if there is data to send over the wire
     */
    bool hasOutputData() {
        return !_pOutBuffer->empty();
    }

    /**
     * Get data to sent over the network.
     */
    void readOutputBuffer(void* data, size_t inLength, size_t& outLength) {
        _pOutBuffer->read(data, inLength, outLength);
    }

private:
    void startServerHandshake(asio::error_code& ec) {
        TimeStamp lifetime;
        SECURITY_STATUS ss = AcquireCredentialsHandleW(NULL,
                                                       (LPWSTR)UNISP_NAME,
                                                       SECPKG_CRED_INBOUND,
                                                       NULL,
                                                       _cred,
                                                       NULL,
                                                       NULL,
                                                       _phcred,
                                                       &lifetime);
        if (ss != SEC_E_OK) {
            ASIO_ASSERT(false);
            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return;
        }
    }

    void startClientHandshake(asio::error_code& ec) {
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
            ASIO_ASSERT(false);

            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return;
        }
    }

    DWORD getServerFlags() {
        return ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
            ASC_REQ_EXTENDED_ERROR | ASC_REQ_STREAM;
    }

    DWORD getClientFlags() {
        return ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
            ISC_REQ_USE_SUPPLIED_CREDS | ISC_REQ_MANUAL_CRED_VALIDATION | ISC_REQ_STREAM;

    }

    class ContextBufferDeleter {
    public:
        ContextBufferDeleter(void** buf) : _buf(buf) {}
        ~ContextBufferDeleter() {
            if (*_buf != nullptr) {
                FreeContextBuffer(*_buf);
            }
        }
    private:
        void** _buf;
    };

    ssl_want startShutdown(asio::error_code& ec) {
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
            ASIO_ASSERT(false);

            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }

        // TODO - when shutdown is complete do this:
        //else if (::SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN) {
        //    ec = asio::error::eof;
        //    return want_nothing;

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
                ASIO_ASSERT(false);

                ec = asio::error_code(ss, asio::error::get_ssl_category());
                return ssl_want::want_nothing;
            }


            _pOutBuffer->reset();
            _pOutBuffer->append(outputBuffer.pvBuffer, outputBuffer.cbBuffer);

            if (SEC_E_OK == ss && outputBuffer.cbBuffer != 0) {
                ec = asio::error::eof;
                return ssl_want::want_output;
            } else {
                // Does it ever return these states?
                //if (SEC_I_CONTINUE_NEEDED == ss || SEC_I_COMPLETE_AND_CONTINUE == ss) {
                //    ASIO_ASSERT(false);
                //}
                ASIO_ASSERT(false);
                return ssl_want::want_nothing;
            }
        } else {
            ULONG ContextAttributes;
            // TODO???
            // TODO: SCH_CRED_SNI_CREDENTIAL
            // TODO: set target name to SNI name
            const char* pszTarget = "localhost";

            DWORD sspiFlags = getClientFlags() | ISC_REQ_ALLOCATE_MEMORY;

            ss = InitializeSecurityContextA(_phcred,
                _phctxt,
                (SEC_CHAR*)pszTarget,
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
                ASIO_ASSERT(false);

                ec = asio::error_code(ss, asio::error::get_ssl_category());
                return ssl_want::want_nothing;
            }

            ASIO_ASSERT(false);
        }



        return ssl_want::want_nothing;
    }

    ssl_want doServerHandshake(bool newConversation,
                               asio::error_code& ec,
                               HandshakeState* pHandshakeState) {
        TimeStamp lifetime;
        
        _pOutBuffer->resize(16 * 1024);

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

            ASIO_ASSERT(false);

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
                      reinterpret_cast<unsigned char*>(extraBuffer->pvBuffer) +
                          extraBuffer->cbBuffer,
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

    ssl_want doClientHandshake(asio::error_code& ec) {
        // TODO???
        // TODO: SCH_CRED_SNI_CREDENTIAL
        // TODO: set target name to SNI name
        const char* pszTarget = "localhost";

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
                                            (SEC_CHAR*)pszTarget,
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
                                            (SEC_CHAR*)pszTarget,
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

            ASIO_ASSERT(false);

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
                // TODO: assert _extraEncryptedBuffer.size() == 0
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

        _pOutBuffer->reset();
        _pOutBuffer->append(outputBuffers[0].pvBuffer, outputBuffers[0].cbBuffer);

        // Reset the input buffer
        _pInBuffer->reset();

        if (needOutput) {
            return ssl_want::want_output_and_retry;
        }

        return ssl_want::want_nothing;
    }

private:
    // TODO: error state?
    // TODO: assert and document
    enum class State {
        HandshakeStart,
        NeedMoreHandshakeData,
        HaveEncryptedData,
        Done,
        // HaveDecryptedData,
    };

    void setState(State s) {
        _state = s;
    }

private:
    State _state;
    HandshakeMode _mode;

    ReusableBuffer* _pInBuffer;
    std::vector<unsigned char> _extraEncryptedBuffer;

    ReusableBuffer* _pOutBuffer;

    SCHANNEL_CRED* _cred;
    PCtxtHandle _phctxt;
    PCredHandle _phcred;
};

// TODO
class SSLReadManager {

private:
    // TODO: assert states and document transitions
    // TODO: error state?
    enum class State {
        NeedMoreEncryptedData,
        HaveEncryptedData,
        HaveDecryptedData,
    };



public:
    enum class DecryptState {
        Continue,
        Shutdown,
        Renegotiate,
    };

    SSLReadManager(PCtxtHandle hctxt, PCredHandle hcred, ReusableBuffer* pInBuffer)
        : _state(State::NeedMoreEncryptedData),
          _phctxt(hctxt),
          _phcred(hcred),
          _pInBuffer(pInBuffer) {}

    ssl_want readDecryptedData(void* data,
                               std::size_t length,
                               asio::error_code& ec,
                               std::size_t& bytes_transferred,
        DecryptState *pDecryptState) {
        bytes_transferred = 0;
        ec = asio::error_code();
        *pDecryptState = DecryptState::Continue;

        // Our last state was that we needed more encrypted data, so tell ASIO we still want some
        if (_state == State::NeedMoreEncryptedData) {
            return ssl_want::want_input_and_retry;
        }


        // If we have enrypted data, try to decrypt it
        if (_state == State::HaveEncryptedData) {
            ssl_want wantState = decryptBuffer(ec, pDecryptState);
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
        // TODO: handle empty decrypted buffer
        ASIO_ASSERT(_state == State::HaveDecryptedData);

        _pInBuffer->read(data, length, bytes_transferred);

        // Have we read all the decrypted data?
        // TODO- examine this again
        if (_pInBuffer->empty()) {
            // If we have some extra encrypted data, it needs to be checked if it is at least a 
            // valid SSL packet, so set the state machine to reflect that
            // TODO: examine all extra buffer handling
            // if(!_extraEncryptedBuffer.empty()) {
            //     setState(State::HaveEncryptedData);
            // } else
             {
                // We are empty so reset our state to need encrypted data for the next call
                setState(State::NeedMoreEncryptedData);
            }
        }

        return ssl_want::want_nothing;
    }

    void writeData(const void* data, std::size_t length) {
        // We have more data, it may not be enough to decode
        // but we will figure that out later
        setState(State::HaveEncryptedData);

        // If we have extra encrypted data from the last encryption, copy it over to our buffer
        // TODO- examine this again - we should process this during decryption instead
        if (_extraEncryptedBuffer.size()) {
            _pInBuffer->fill(_extraEncryptedBuffer);
            _extraEncryptedBuffer.clear();
        }

        _pInBuffer->append(data, length);
    }

private:
    ssl_want decryptBuffer(asio::error_code& ec, DecryptState *pDecryptState) {
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
                ASIO_ASSERT(false);

                ec = asio::error_code(ss, asio::error::get_ssl_category());
                return ssl_want::want_nothing;
            }
        }

        // TODO: SEC_I_CONTEXT_EXPIRED
        // TODO: SEC_I_RENEGOTIATE
        // Shutdown has been initiated at the client side
        if (ss == SEC_I_CONTEXT_EXPIRED) {
            *pDecryptState = DecryptState::Shutdown;
        } else if (ss == SEC_I_RENEGOTIATE) {
            *pDecryptState = DecryptState::Renegotiate;
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

        if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
            ASIO_ASSERT(_extraEncryptedBuffer.empty());
            _extraEncryptedBuffer.clear();
            std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer),
                      reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) +
                          pExtraBuffer->cbBuffer,
                      std::back_inserter(_extraEncryptedBuffer));
        }

        setState(State::HaveDecryptedData);

        return ssl_want::want_nothing;
    }

private:
    void setState(State s) {
        _state = s;
    }

private:
    State _state;
    std::vector<unsigned char> _extraEncryptedBuffer;
    ReusableBuffer* _pInBuffer;

    PCtxtHandle _phctxt;
    PCredHandle _phcred;
};

// TODO
class SSLWriteManager {
public:
    SSLWriteManager(PCtxtHandle hctxt, ReusableBuffer* pOutBuffer)
        : _phctxt(hctxt), _pOutBuffer(pOutBuffer) {}

    ssl_want writeUnecryptedData(const void* pMessage,
                                 std::size_t messageLength,
                                 std::size_t& bytes_transferred,
                                 asio::error_code& ec) {
        if (securityTrailerLength == ULONG_MAX) {
            SecPkgContext_StreamSizes SecPkgContextStreamSizes;

            SECURITY_STATUS ss = QueryContextAttributes(
                _phctxt, SECPKG_ATTR_STREAM_SIZES, &SecPkgContextStreamSizes);

            if (ss < SEC_E_OK) {
                ASIO_ASSERT(false);

                ec = asio::error_code(ss, asio::error::get_ssl_category());
                return ssl_want::want_nothing;
            }

            securityTrailerLength = SecPkgContextStreamSizes.cbTrailer;
            securityHeaderLength = SecPkgContextStreamSizes.cbHeader;
        }


        _pOutBuffer->resize(securityTrailerLength + securityHeaderLength + messageLength);

        SecBuffer securityBuffers[4];

        securityBuffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        securityBuffers[0].cbBuffer = securityHeaderLength;
        securityBuffers[0].pvBuffer = _pOutBuffer->data();

        // TODO: memcpy_s
        memcpy(_pOutBuffer->data() + securityHeaderLength, pMessage, messageLength);

        securityBuffers[1].BufferType = SECBUFFER_DATA;
        securityBuffers[1].cbBuffer = messageLength;
        securityBuffers[1].pvBuffer = _pOutBuffer->data() + securityHeaderLength;

        securityBuffers[2].cbBuffer = securityTrailerLength;
        securityBuffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        securityBuffers[2].pvBuffer = _pOutBuffer->data() + securityHeaderLength + messageLength;

        securityBuffers[3].cbBuffer = 0;
        securityBuffers[3].BufferType = SECBUFFER_EMPTY;
        securityBuffers[3].pvBuffer = 0;

        SecBufferDesc bufferDesc;

        bufferDesc.ulVersion = SECBUFFER_VERSION;
        bufferDesc.cBuffers = 4;
        bufferDesc.pBuffers = securityBuffers;

        SECURITY_STATUS ss = EncryptMessage(_phctxt, 0, &bufferDesc, 0);

        if (ss < SEC_E_OK) {
            ASIO_ASSERT(false);

            ec = asio::error_code(ss, asio::error::get_ssl_category());
            return ssl_want::want_nothing;
        }

        int size =
            securityBuffers[0].cbBuffer + securityBuffers[1].cbBuffer + securityBuffers[2].cbBuffer;

        _pOutBuffer->resize(size);

        bytes_transferred = messageLength;

        return ssl_want::want_output;
    }

    void readOutputBuffer(void* data, size_t inLength, size_t& outLength) {
        _pOutBuffer->read(data, inLength, outLength);
    }

private:
    ReusableBuffer* _pOutBuffer;

    PCtxtHandle _phctxt;

    ULONG securityTrailerLength{ULONG_MAX};
    ULONG securityHeaderLength{ULONG_MAX};
};