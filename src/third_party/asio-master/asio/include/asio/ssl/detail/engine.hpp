//
// ssl/detail/engine.hpp
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_SSL_DETAIL_ENGINE_HPP
#define ASIO_SSL_DETAIL_ENGINE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#include "asio/buffer.hpp"
#include "asio/detail/static_mutex.hpp"
#include "asio/ssl/detail/openssl_types.hpp"
#include "asio/ssl/detail/verify_callback.hpp"
#include "asio/ssl/stream_base.hpp"
#include "asio/ssl/verify_mode.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace ssl {
namespace detail {


#define SEC_SUCCESS(Status) ((Status) >= 0)
//
//class ReusableBuffer {
//    std::vector<unsigned char> _buffer;
//    size_t _bufPos{0};
//public:
//    // TODO: reset output to iniatial size
//    ReusableBuffer(std::size_t initialSize) {
//        _buffer.reserve(initialSize);
//    }
//    bool empty() const { return _buffer.empty(); }
//
//    unsigned char* data() {
//        return _buffer.data();
//    }
//
//    std::size_t size() const {
//        return _buffer.size();
//    }
//
//    void resize(std::size_t size) {
//        _buffer.resize(size);
//    }
//
//    void reset() {
//        _bufPos = 0;
//        _buffer.clear();
//    }
//
//    void resetPos(void* pos, std::size_t size) {
//        ASIO_ASSERT(pos >= _buffer.data() && pos < (_buffer.data() + _buffer.size()));
//        _bufPos = (unsigned char*)pos - _buffer.data();
//        resize(_bufPos + size);
//    }
//
//    void fill(const void* data, std::size_t length) {
//        ASIO_ASSERT(_buffer.empty());
//        ASIO_ASSERT(_bufPos == 0);
//        append(data, length);
//    }
//
//    void fill(const std::vector<unsigned char> &vec) {
//        append(vec.data(), vec.size());
//    }
//
//
//    void append(const void* data, std::size_t length) {
//        ASIO_ASSERT(_bufPos == 0);
//        std::copy(reinterpret_cast<const unsigned char*>(data),
//            reinterpret_cast<const unsigned char*>(data) + length, std::back_inserter(_buffer));
//    }
//
//    void read(void* data, std::size_t length, std::size_t *outLength) {
//        if (length >= (_buffer.size() - _bufPos)) {
//            // We have less then ASIO wants, give them everything we have
//            *outLength = _buffer.size();
//            memcpy(data, _buffer.data() + _bufPos, _buffer.size() - _bufPos);
//
//            // We are empty so reset our state to need encrypted data for the next call
//            _bufPos = 0;
//            _buffer.clear();
//            ASIO_ASSERT(_buffer.size() == 0);
//        } else {
//            // ASIO wants less then we have so give them just what they want
//            *outLength = length;
//            memcpy(data, _buffer.data(), length);
//
//            _bufPos+= length;
//        }
//    }
//};

class ReusableBuffer {
    std::unique_ptr<unsigned char[]> _buffer;
    size_t _bufPos{0};
    size_t _size{0};
    size_t _capacity;
public:
    // TODO: reset output to iniatial size
    ReusableBuffer(std::size_t initialSize) {
        _buffer.reset(new unsigned char[initialSize]);
        _capacity = initialSize;
    }
    bool empty() const { return _size == 0; }

    unsigned char* data() {
        return _buffer.get();
    }

    std::size_t size() const {
        return _size;
    }

        void reset() {
            _bufPos = 0;
            _size =0;
        }

    void resize(std::size_t size) {
        if (size > _capacity) {
            std::unique_ptr<unsigned char[]> temp(new unsigned char[size]);

            memcpy(temp.get(), _buffer.get(), _size);
            _buffer.swap(temp);
            _capacity = _size;
        }
        _size = size;
    }


    void fill(const void* data, std::size_t length) {
        ASIO_ASSERT(_size == 0);
        ASIO_ASSERT(_bufPos == 0);
        append(data, length);
    }

    void fill(const std::vector<unsigned char> &vec) {
        append(vec.data(), vec.size());
    }


    void resetPos(void* pos, std::size_t size) {
        ASIO_ASSERT(pos >= _buffer.get() && pos < (_buffer.get() + _size));
        _bufPos = (unsigned char*)pos - _buffer.get();
        resize(_bufPos + size);
    }

    void append(const void* data, std::size_t length) {
        ASIO_ASSERT(_bufPos == 0);
        auto originalSize = _size;
        resize(_size + length);
        printf("-- Pushing data to %d - %d\n", (int)originalSize, (int)length);
        std::copy(reinterpret_cast<const unsigned char*>(data),
            reinterpret_cast<const unsigned char*>(data) + length, _buffer.get() + originalSize);
    }

    void read(void* data, std::size_t length, std::size_t *outLength) {
        printf("-- Reading data from %d - %d\n", (int)_size, (int)length);
        if (length >= (size() - _bufPos)) {
            // We have less then ASIO wants, give them everything we have
            *outLength = size() - _bufPos;
            memcpy(data, _buffer.get() + _bufPos, size() - _bufPos);

            // We are empty so reset our state to need encrypted data for the next callwant_
            _bufPos = 0;
            _size = 0;
        } else {
            // ASIO wants less then we have so give them just what they want
            *outLength = length;
            memcpy(data, _buffer.get() + _bufPos, length);

            _bufPos += length;
        }
    }
};




class engine
{
public:
  enum want
  {
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

  // Construct a new engine for the specified context.
  ASIO_DECL explicit engine(SCHANNEL_CRED* context);

  // Destructor.
  ASIO_DECL ~engine();

  // Get the underlying implementation in the native type.
  ASIO_DECL PCtxtHandle native_handle();

  // Set the peer verification mode.
  ASIO_DECL asio::error_code set_verify_mode(
      verify_mode v, asio::error_code& ec);

  // Set the peer verification depth.
  ASIO_DECL asio::error_code set_verify_depth(
      int depth, asio::error_code& ec);

  // Set a peer certificate verification callback.
  ASIO_DECL asio::error_code set_verify_callback(
      verify_callback_base* callback, asio::error_code& ec);

  // Perform an SSL handshake using either SSL_connect (client-side) or
  // SSL_accept (server-side).
  ASIO_DECL want handshake(
      stream_base::handshake_type type, asio::error_code& ec);

  // Perform a graceful shutdown of the SSL session.
  ASIO_DECL want shutdown(asio::error_code& ec);

  // Write bytes to the SSL session.
  ASIO_DECL want write(const asio::const_buffer& data,
      asio::error_code& ec, std::size_t& bytes_transferred);

  // Read bytes from the SSL session.
  ASIO_DECL want read(const asio::mutable_buffer& data,
      asio::error_code& ec, std::size_t& bytes_transferred);

  // Get output data to be written to the transport.
  ASIO_DECL asio::mutable_buffer get_output(
      const asio::mutable_buffer& data);

  // Put input data that was read from the transport.
  ASIO_DECL asio::const_buffer put_input(
      const asio::const_buffer& data);

  // Map an error::eof code returned by the underlying transport according to
  // the type and state of the SSL session. Returns a const reference to the
  // error code object, suitable for passing to a completion handler.
  ASIO_DECL const asio::error_code& map_error_code(
      asio::error_code& ec) const;

private:
  // Disallow copying and assignment.
  engine(const engine&);
  engine& operator=(const engine&);

  // Callback used when the SSL implementation wants to verify a certificate.
//   ASIO_DECL static int verify_callback_function(
//       int preverified, X509_STORE_CTX* ctx);

  CtxtHandle _hcxt;
  CredHandle _hcred;
  SCHANNEL_CRED* _pCred;

  enum class EngineState {
    NeedsHandshake,
    InProgress,
  };
  EngineState _state{EngineState::NeedsHandshake};

  class SSLReadBuffer {

      // TODO: error state?
      enum class State {
          NeedMoreEncryptedData,
          HaveEncryptedData,
          HaveDecryptedData,
      };

      State _state;
      std::vector<unsigned char> _extraEncryptedBuffer;
      ReusableBuffer _buffer;

      void setState(State s) {
          _state = s;
      }

      PCtxtHandle _phctxt;
      PCredHandle _phcred;
  public:

      SSLReadBuffer(PCtxtHandle hctxt, PCredHandle hcred) : _state(State::NeedMoreEncryptedData), _phctxt(hctxt), _phcred(hcred), _buffer(16 * 1024) {
      }

      engine::want readDecryptedData(void* data, std::size_t length, asio::error_code& ec, std::size_t &outLength) {
          outLength = 0;

          // Our last state was that we needed more encrypted data, so tell ASIO we still want some
          // TODO: invariant this???
          if (_state == State::NeedMoreEncryptedData) {
              return engine::want_input_and_retry;
          }


          // If we have enrypted data, try to decrypt it
          if (_state == State::HaveEncryptedData) {
              engine::want wantState = TryDecryptBuffer();
              if (wantState == engine::want_input_and_retry) {
                  setState(State::NeedMoreEncryptedData);
              }

              if (wantState != engine::want_nothing) {
                  return wantState;
              }
          }

          // We decrypted data in the past, hand it back to ASIO until we are out of decrypted data
          // TODO: handle empty decrypted buffer
          ASIO_ASSERT(_state == State::HaveDecryptedData);

          _buffer.read(data, length, &outLength);

          if (_buffer.empty()) {
              // We are empty so reset our state to need encrypted data for the next call
              setState(State::NeedMoreEncryptedData);
          }

          return engine::want_nothing;
      }

      void writeData(const void* data, std::size_t length) {
          // We have more data, it may not be enough to decode
          // but we will figure that out later
          setState(State::HaveEncryptedData);

          // If we have extra encrypted data from the last encryption, copy it over to our buffer
          if (_extraEncryptedBuffer.size()) {
              _buffer.fill(_extraEncryptedBuffer);
              _extraEncryptedBuffer.clear();
          }

          _buffer.append(data, length);
      }
  private:

      engine::want TryDecryptBuffer() {
          SECURITY_STATUS   ss;
          SecBufferDesc     BuffDesc;
          SecBuffer         SecBuff[4];
          ULONG             ulQop = 0;

          //  Prepare the buffers to be passed to the DecryptMessage function.

          BuffDesc.ulVersion = SECBUFFER_VERSION;
          BuffDesc.cBuffers = 4;
          BuffDesc.pBuffers = SecBuff;

          SecBuff[0].cbBuffer = _buffer.size();
          SecBuff[0].BufferType = SECBUFFER_DATA;
          SecBuff[0].pvBuffer = _buffer.data();

          SecBuff[1].cbBuffer = 0;
          SecBuff[1].BufferType = SECBUFFER_EMPTY;
          SecBuff[1].pvBuffer = 0;

          SecBuff[2].cbBuffer = 0;
          SecBuff[2].BufferType = SECBUFFER_EMPTY;
          SecBuff[2].pvBuffer = 0;

          SecBuff[3].cbBuffer = 0;
          SecBuff[3].BufferType = SECBUFFER_EMPTY;
          SecBuff[3].pvBuffer = 0;

          ss = DecryptMessage(
              _phctxt,
              &BuffDesc,
              0,
              &ulQop);

          if (!SEC_SUCCESS(ss)) {
              if (ss == SEC_E_INCOMPLETE_MESSAGE) {
                  printf("Need more data for DecryptMessage");

                  return engine::want_input_and_retry;
              } else {
                  fprintf(stderr, "DecryptMessage failed");
                  ASIO_ASSERT(false);
                  // TODO: SEC_I_CONTEXT_EXPIRED
                  // TODO: SEC_I_RENEGOTIATE
              }
          }

          // Locate data and (optional) extra buffers.
          SecBuffer* pDataBuffer = NULL;
          SecBuffer* pExtraBuffer = NULL;

          for (int i = 1; i < 4; i++) {
              if (pDataBuffer == NULL && SecBuff[i].BufferType == SECBUFFER_DATA) {
                  pDataBuffer = &SecBuff[i];
                  printf("Buffers[%d].BufferType = SECBUFFER_DATA\n", i);
              }

              if (pExtraBuffer == NULL && SecBuff[i].BufferType == SECBUFFER_EXTRA) {
                  ASIO_ASSERT(pExtraBuffer == NULL);
                  pExtraBuffer = &SecBuff[i];
              }
          }

          _buffer.resetPos(pDataBuffer->pvBuffer, pDataBuffer->cbBuffer);

          if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
              ASIO_ASSERT(_extraEncryptedBuffer.empty());
              _extraEncryptedBuffer.clear();
              std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer),
                  reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) + pExtraBuffer->cbBuffer,
                  std::back_inserter(_extraEncryptedBuffer));
          }

          setState(State::HaveDecryptedData);


          return engine::want_nothing;
      }
  };

  class SSLHandshakeBuffer {
      // TODO: error state?
      enum class State {
          HandshakeStart,
          NeedMoreHandshakeData,
          HaveEncryptedData,
          Done,
          //HaveDecryptedData,
      };


      State _state;
      ReusableBuffer  _buffer;
      std::vector<unsigned char> _extraEncryptedBuffer;

      ReusableBuffer _outBuffer;
      size_t bufPos;


      void setState(State s) {
          _state = s;
      }


      SCHANNEL_CRED* _cred;
      PCtxtHandle _phctxt;
      PCredHandle _phcred;
  public:
      enum class HandshakeMode {
          Unknown,
          Client,
          Server,
      };
      
      HandshakeMode _mode;


      void setMode(HandshakeMode mode) {
          ASIO_ASSERT(_mode == HandshakeMode::Unknown || _mode == mode);
          _mode = mode;
      }

      SSLHandshakeBuffer(PCtxtHandle hctxt, PCredHandle phcred, SCHANNEL_CRED* cred) : _state(State::HandshakeStart), _phctxt(hctxt), _cred(cred),
          _phcred(phcred),          _buffer(16 * 1024), _outBuffer(16 * 1024), _mode(HandshakeMode::Unknown) {
      }

      engine::want next(asio::error_code& ec, bool *fDone) {
          ASIO_ASSERT(_mode != HandshakeMode::Unknown);
          *fDone = false;

          if (_state == State::HandshakeStart) {
              engine::want want;
              if (_mode == HandshakeMode::Server) {
                  // ASIO will ask for the handshake to start when the input buffer is empty
                  if (_buffer.empty()) {
                      return engine::want_input_and_retry;
                  }

                  startServerHandshake(ec);

                  want = TryAcceptClientToken(true, ec, fDone);
              } else {
                  startClientHandshake(ec);

                  want = TryGenClientContext(ec);
              }

              setState(State::NeedMoreHandshakeData);

              return want;
          } else if (_state == State::NeedMoreHandshakeData) {
              return  engine::want_input_and_retry;
          } else {

              engine::want want;
              if (_mode == HandshakeMode::Server) {
                  want = TryAcceptClientToken(false, ec, fDone);
              } else {
                  want = TryGenClientContext(ec);
              }

              if (want == engine::want_nothing || *fDone == true) {
                  setState(State::Done);
              } else {
                  setState(State::NeedMoreHandshakeData);
              }

              return want;
          }
      }


      void writeEncryptedData(const void* data, std::size_t length) {
          // We have more data, it may not be enough to decode
          // but we will figure that out later
          if (_state != State::HandshakeStart) {
              setState(State::HaveEncryptedData);
          }

          // If we have extra encrypted data from the last encryption, copy it over to our buffer
          if (_extraEncryptedBuffer.size()) {
              _buffer.fill(_extraEncryptedBuffer);
              _extraEncryptedBuffer.clear();
          }

          _buffer.append(data, length);
      }

      bool hasOutputData() {

          return !_outBuffer.empty();
      }

      void readOutputBuffer(void* data, size_t inLength, size_t *outLength) {

          _outBuffer.read(data, inLength, outLength);
      }

  private:
      void startServerHandshake(asio::error_code& ec) {
          TimeStamp         Lifetime;

#if 1

          //{
          //    DWORD keyBlobLen;

          //    BOOL ret = CertGetCertificateContextProperty(_cred->paCred[0],
          //        CERT_KEY_PROV_HANDLE_PROP_ID,
          //        NULL,
          //        &keyBlobLen);

          //    if (!ret) {
          //        DWORD gle = GetLastError();
          //        if (gle != ERROR_MORE_DATA) {
          //            ASIO_ASSERT(false);
          //        }
          //    }

          //    std::unique_ptr<BYTE> keyBlob(new BYTE[keyBlobLen]);
          //    ret = CertGetCertificateContextProperty(_cred->paCred[0],
          //        CERT_KEY_PROV_HANDLE_PROP_ID,
          //        keyBlob.get(),
          //        &keyBlobLen);

          //    if (!ret) {
          //        DWORD gle = GetLastError();
          //        ASIO_ASSERT(false);
          //    }
          //}


          SECURITY_STATUS ss = AcquireCredentialsHandleA(
              NULL,
              (LPSTR)"SChannel",
              SECPKG_CRED_INBOUND,
              NULL,
              _cred,
              NULL,
              NULL,
              _phcred,
              &Lifetime);

          if (!SEC_SUCCESS(ss)) {
              fprintf(stderr, "AcquireCreds failed: 0x%08x\n", ss);
              ASIO_ASSERT(false);
          }
#else
          PCCERT_CONTEXT serverCert; // server-side certificate
                                     //-------------------------------------------------------
                                     // Get the server certificate. 

          if (!(serverCert = getServerCertificate())) {
              ASIO_ASSERT(false);
          }

          SCHANNEL_CRED credData;

          ZeroMemory(&credData, sizeof(credData));
          credData.dwVersion = SCHANNEL_CRED_VERSION;

          // getServerCertificate is a placeholder function.
          credData.cCreds = 1;
          credData.paCred = &serverCert;


          SECURITY_STATUS ss = AcquireCredentialsHandleA(
              NULL,
              (LPSTR)"SChannel",
              SECPKG_CRED_INBOUND,
              NULL,
              &credData,
              NULL,
              NULL,
              _phcred,
              &Lifetime);

          if (!SEC_SUCCESS(ss)) {
              fprintf(stderr, "AcquireCreds failed: 0x%08x\n", ss);
              ASIO_ASSERT(false);
          }

#endif
          //CertFreeCertificateContext(serverCert);

      }

      void startClientHandshake(asio::error_code& ec) {
          static CHAR      lpPackageName[1024];

          TimeStamp         Lifetime;
          //SCHANNEL_CRED credData;

          //ZeroMemory(&credData, sizeof(credData));
          //credData.dwVersion = SCHANNEL_CRED_VERSION;
          ////-------------------------------------------------------
          //// Specify the TLS V1.0 (client-side) security protocol.
          //credData.grbitEnabledProtocols = SP_PROT_TLS1_0_CLIENT;
          //credData.dwFlags |= SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;

          strcpy_s(lpPackageName, 1024 * sizeof(CHAR), "SChannel");
          SECURITY_STATUS ss = AcquireCredentialsHandleA(
              NULL,
              lpPackageName,
              SECPKG_CRED_OUTBOUND,
              NULL,
              _cred,
              NULL,
              NULL,
              _phcred,
              &Lifetime);

          if (!(SEC_SUCCESS(ss))) {
              ASIO_ASSERT(false);
          }
      }

      engine::want TryAcceptClientToken(
          bool fNewConversation, asio::error_code& ec, bool *fDone) {
          SECURITY_STATUS   ss;
          TimeStamp         Lifetime;
          SecBufferDesc     OutBuffDesc;
          SecBuffer         OutSecBuff;
          SecBufferDesc     InBuffDesc;
          SecBuffer         InSecBuff[2];
          ULONG             Attribs = 0;

          //----------------------------------------------------------------
          //  Prepare output buffers.

          OutBuffDesc.ulVersion = SECBUFFER_VERSION;
          OutBuffDesc.cBuffers = 1;
          OutBuffDesc.pBuffers = &OutSecBuff;

          _outBuffer.resize(16 * 1024);
          OutSecBuff.cbBuffer = _outBuffer.size();
          OutSecBuff.BufferType = SECBUFFER_TOKEN;
          OutSecBuff.pvBuffer = _outBuffer.data();

          //----------------------------------------------------------------
          //  Prepare input buffers.

          InBuffDesc.ulVersion = SECBUFFER_VERSION;
          InBuffDesc.cBuffers = 2;
          InBuffDesc.pBuffers = InSecBuff;

          InSecBuff[0].cbBuffer = _buffer.size();
          InSecBuff[0].BufferType = SECBUFFER_TOKEN;
          InSecBuff[0].pvBuffer = _buffer.data();

          InSecBuff[1].cbBuffer = 0;
          InSecBuff[1].BufferType = SECBUFFER_EMPTY;
          InSecBuff[1].pvBuffer = NULL;


          printf("Token buffer received (%lu bytes):\n", InSecBuff[0].cbBuffer);
          //PrintHexDump(InSecBuff[0].cbBuffer, (PBYTE)InSecBuff[0].pvBuffer);

          Attribs = ASC_REQ_SEQUENCE_DETECT |
              ASC_REQ_REPLAY_DETECT |
              ASC_REQ_CONFIDENTIALITY |
              ASC_REQ_EXTENDED_ERROR |
              //ASC_REQ_ALLOCATE_MEMORY |
              ASC_REQ_STREAM;

          ss = AcceptSecurityContext(
              _phcred,
              fNewConversation ? NULL : _phctxt,
              &InBuffDesc,
              Attribs,
              SECURITY_NATIVE_DREP,
              _phctxt,
              &OutBuffDesc,
              &Attribs,
              &Lifetime);

          if (!SEC_SUCCESS(ss)) {
              if (ss == SEC_E_INCOMPLETE_MESSAGE) {
                  printf("Need more data for AcceptClientToken");
                  // TODO: consider using SECBUFFER_MISSING and approriate optimizations
                  return engine::want_input_and_retry;
              }

              fprintf(stderr, "AcceptSecurityContext failed: 0x%08x\n", ss);
              ASIO_ASSERT(false);
          }
          printf("AcceptSecurityContext result = 0x%08x\n", ss);

          // Locate (optional) extra buffers.
          SecBuffer* pExtraBuffer = NULL;

          for (int i = 0; i < 2; i++) {
              if (pExtraBuffer == NULL && InSecBuff[i].BufferType == SECBUFFER_EXTRA) {
                  pExtraBuffer = &InSecBuff[i];
              }
          }

          if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
              // TODO: assert _extraEncryptedBuffer.size() == 0
              _extraEncryptedBuffer.clear();
              std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer),
                  reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) + pExtraBuffer->cbBuffer,
                  std::back_inserter(_extraEncryptedBuffer));
          }

          //----------------------------------------------------------------
          //  Complete token if applicable.

          bool needOutput{false};

          if (SEC_I_CONTINUE_NEEDED == ss || SEC_I_COMPLETE_AND_CONTINUE == ss || (SEC_E_OK == ss && OutSecBuff.cbBuffer != 0)) {
              needOutput = true;
          }

          if (SEC_E_OK == ss && OutSecBuff.cbBuffer != 0) {
              *fDone = true;
          }

          if ((SEC_I_COMPLETE_NEEDED == ss)
              || (SEC_I_COMPLETE_AND_CONTINUE == ss)) {

              ss = CompleteAuthToken(_phctxt, &OutBuffDesc);
              if (!SEC_SUCCESS(ss)) {
                  fprintf(stderr, "complete failed: 0x%08x\n", ss);
                  ASIO_ASSERT(false);
              }
          }
          printf("Token buffer generated (%lu bytes):\n",
              OutSecBuff.cbBuffer);

          _outBuffer.resize(OutSecBuff.cbBuffer);

          if (needOutput) {
              _buffer.reset();
    
              if (*fDone == true) {
                  return engine::want_output;
              }
              return engine::want_output_and_retry;
          }


          // assert ss == SEC_I_COMPLETE_NEEDED
          return engine::want_nothing;
      }  // end GenServerContext

      engine::want TryGenClientContext(asio::error_code& ec)
      {
          SECURITY_STATUS   ss;
          TimeStamp         Lifetime;
          SecBufferDesc     OutBuffDesc;
          SecBuffer         OutSecBuff[2];
          SecBufferDesc     InBuffDesc;
          SecBuffer         InSecBuff[2];
          ULONG             ContextAttributes;

          // TODO???
          // TODO: SCH_CRED_SNI_CREDENTIAL
          // TODO: set target name to SNI name
          const char* pszTarget = "mark";


          DWORD dwSSPIFlags = 
              ISC_REQ_SEQUENCE_DETECT |
              ISC_REQ_REPLAY_DETECT |
              ISC_REQ_CONFIDENTIALITY |
              ISC_RET_EXTENDED_ERROR |
              //ISC_REQ_ALLOCATE_MEMORY |
              ISC_REQ_USE_SUPPLIED_CREDS | 
              ISC_REQ_MANUAL_CRED_VALIDATION |
              ISC_REQ_STREAM;


          //--------------------------------------------------------------------
          //  Prepare the buffers.

          OutBuffDesc.ulVersion = SECBUFFER_VERSION;
          OutBuffDesc.cBuffers = 1;
          OutBuffDesc.pBuffers = &OutSecBuff[0];

          _outBuffer.resize(16 * 1024);
          OutSecBuff[0].cbBuffer = _outBuffer.size();
          OutSecBuff[0].BufferType = SECBUFFER_TOKEN;
          OutSecBuff[0].pvBuffer = _outBuffer.data();

          //OutSecBuff[1].cbBuffer = _outBuffer.size();
          //OutSecBuff[1].BufferType = SECBUFFER_TOKEN;
          //OutSecBuff[1].pvBuffer = _outBuffer.data();

          //-------------------------------------------------------------------
          //  The input buffer is created only if a message has been received 
          //  from the server.

          if (_buffer.size()) {
              InBuffDesc.ulVersion = SECBUFFER_VERSION;
              InBuffDesc.cBuffers = 2;
              InBuffDesc.pBuffers = &InSecBuff[0];

              InSecBuff[0].cbBuffer = _buffer.size();
              InSecBuff[0].BufferType = SECBUFFER_TOKEN;
              InSecBuff[0].pvBuffer = _buffer.data();

              InSecBuff[1].cbBuffer = 0;
              InSecBuff[1].BufferType = SECBUFFER_EMPTY;
              InSecBuff[1].pvBuffer = NULL;

              ss = InitializeSecurityContextA(
                  _phcred,
                  _phctxt,
                  (SEC_CHAR*)pszTarget,
                  dwSSPIFlags,
                  0,
                  0,
                  &InBuffDesc,
                  0,
                  _phctxt,
                  &OutBuffDesc,
                  &ContextAttributes,
                  &Lifetime);
          } else {
              ss = InitializeSecurityContextA(
                  _phcred,
                  NULL,
                  (SEC_CHAR*)pszTarget,
                  dwSSPIFlags,
                  0,
                  0,
                  NULL,
                  0,
                  _phctxt,
                  &OutBuffDesc,
                  &ContextAttributes,
                  &Lifetime);
          }

          if (!SEC_SUCCESS(ss)) {
              if (ss == SEC_E_INCOMPLETE_MESSAGE) {
                  printf("Need more data for GenClientContext");
                  return engine::want_input_and_retry;
              }

              ASIO_ASSERT(false);
          }

          if (_buffer.size()) {
              // Locate (optional) extra buffers.
              SecBuffer* pExtraBuffer = NULL;

              for (int i = 0; i < 2; i++) {
                  if (pExtraBuffer == NULL && InSecBuff[i].BufferType == SECBUFFER_EXTRA) {
                      pExtraBuffer = &InSecBuff[i];
                  }
              }

              if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
                  // TODO: assert _extraEncryptedBuffer.size() == 0
                  _extraEncryptedBuffer.clear();
                  std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer),
                      reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) + pExtraBuffer->cbBuffer,
                      std::back_inserter(_extraEncryptedBuffer));
              }
          }
          //-------------------------------------------------------------------
          //  If necessary, complete the token.
          bool needOutput{false};

          if (SEC_I_CONTINUE_NEEDED == ss || SEC_I_COMPLETE_AND_CONTINUE == ss || (SEC_E_OK == ss && OutSecBuff[0].cbBuffer != 0)) {
              needOutput = true;
          }

  /*        if (SEC_E_OK == ss && OutSecBuff[0].cbBuffer == 0) {
              *fDone = true;
          }
*/
          if ((SEC_I_COMPLETE_NEEDED == ss)
              || (SEC_I_COMPLETE_AND_CONTINUE == ss)) {
              ss = CompleteAuthToken(_phctxt, &OutBuffDesc);
              if (!SEC_SUCCESS(ss)) {
                  fprintf(stderr, "complete failed: 0x%08x\n", ss);
                  ASIO_ASSERT(false);
              }
          }

          _outBuffer.resize(OutSecBuff[0].cbBuffer);

          printf("Token buffer generated (%lu bytes):\n", OutSecBuff[0].cbBuffer);
          //PrintHexDump(OutSecBuff.cbBuffer, (PBYTE)OutSecBuff.pvBuffer);
          if (needOutput) {
              _buffer.reset();
              return engine::want_output_and_retry;
          }


          // assert ss == SEC_I_COMPLETE_NEEDED
          return engine::want_nothing;
      }



      PCCERT_CONTEXT getServerCertificate()
      {
          HCERTSTORE hMyCertStore = NULL;
          PCCERT_CONTEXT aCertContext = NULL;

          //-------------------------------------------------------
          // Open the My store, also called the personal store.
          // This call to CertOpenStore opens the Local_Machine My 
          // store as opposed to the Current_User's My store.

          hMyCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM,
              X509_ASN_ENCODING,
              0,
              CERT_SYSTEM_STORE_CURRENT_USER,
              L"MY");

          if (hMyCertStore == NULL) {
              printf("Error opening MY store for server.\n");
              goto cleanup;
          }
          //-------------------------------------------------------
          // Search for a certificate with some specified
          // string in it. This example attempts to find
          // a certificate with the string "example server" in
          // its subject string. Substitute an appropriate string
          // to find a certificate for a specific user.

          aCertContext = CertFindCertificateInStore(hMyCertStore,
              X509_ASN_ENCODING,
              0,
              CERT_FIND_SUBJECT_STR_A,
              "server", // use appropriate subject name
              NULL
          );

          if (aCertContext == NULL) {
              printf("Error retrieving server certificate.");
              goto cleanup;
          }
      cleanup:
          if (hMyCertStore) {
              CertCloseStore(hMyCertStore, 0);
          }
          return aCertContext;
      }
  };


  class SSLWriteBuffer {
      enum class State {
          HaveEmptyBuffer,
          HaveEncryptedData,
      };

      State _state;
      ReusableBuffer _buffer;

      void setState(State s) {
          _state = s;
      }
      PCtxtHandle    _phctxt;

      ULONG cbSecurityTrailer{ULONG_MAX};
      ULONG cbSecurityHeader{ULONG_MAX};

  public:
      SSLWriteBuffer(PCtxtHandle hctxt) : _state(State::HaveEmptyBuffer), _phctxt(hctxt),
          _buffer(16 * 1024)
      {

      }

      engine::want writeUnecryptedData
      (const void* pMessage, std::size_t cbMessage, std::size_t& bytes_transferred, asio::error_code& ec) {
          SECURITY_STATUS   ss;
          SecBufferDesc     BuffDesc;
          SecBuffer       SecBuff[4];

          ULONG             ulQop = 0;

          if (cbSecurityTrailer == ULONG_MAX) {
              SecPkgContext_StreamSizes SecPkgContextStreamSizes;

              ss = QueryContextAttributes(
                  _phctxt,
                  SECPKG_ATTR_STREAM_SIZES,
                  &SecPkgContextStreamSizes);

              if (!SEC_SUCCESS(ss)) {
                  fprintf(stderr, "QueryContextAttributes failed: 0x%08x\n", ss);
                  ASIO_ASSERT(false);
              }

              //----------------------------------------------------------------
              //  The following values are used for encryption and signing.

              //cbMaxSignature = SecPkgContextStreamSizes.cbMaxSignature;

              cbSecurityTrailer = SecPkgContextStreamSizes.cbTrailer;
              cbSecurityHeader = SecPkgContextStreamSizes.cbHeader;
          }

          //-----------------------------------------------------------------
          //  The size of the trailer (signature + padding) block is 
          //  determined from the global cbSecurityTrailer.
          ULONG SigBufferSize = cbSecurityTrailer + cbSecurityHeader;

          //printf("Data before encryption: %s\n", pMessage);
          printf("Length of data before encryption: %d \n", (int)cbMessage);

          //-----------------------------------------------------------------
          //  Allocate a buffer to hold the signature,
          //  encrypted data, and a DWORD  
          //  that specifies the size of the trailer block.

          _buffer.resize(SigBufferSize + cbMessage);

          //------------------------------------------------------------------
          //  Prepare buffers.

          BuffDesc.ulVersion = SECBUFFER_VERSION;
          BuffDesc.cBuffers = 4;
          BuffDesc.pBuffers = SecBuff;

          SecBuff[0].BufferType = SECBUFFER_STREAM_HEADER;
          SecBuff[0].cbBuffer = cbSecurityHeader;
          SecBuff[0].pvBuffer = _buffer.data();

          memcpy(_buffer.data() + cbSecurityHeader, pMessage, cbMessage);

          SecBuff[1].BufferType = SECBUFFER_DATA;
          SecBuff[1].cbBuffer = cbMessage;
          SecBuff[1].pvBuffer = _buffer.data() + cbSecurityHeader;

          SecBuff[2].cbBuffer = cbSecurityTrailer;
          SecBuff[2].BufferType = SECBUFFER_STREAM_TRAILER;
          SecBuff[2].pvBuffer = _buffer.data() + cbSecurityHeader + cbMessage;

          SecBuff[3].cbBuffer = 0;
          SecBuff[3].BufferType = SECBUFFER_EMPTY;
          SecBuff[3].pvBuffer = 0;

          ss = EncryptMessage(
              _phctxt,
              ulQop,
              &BuffDesc,
              0);

          if (!SEC_SUCCESS(ss)) {
              fprintf(stderr, "EncryptMessage failed: 0x%08x\n", ss);
              ASIO_ASSERT(false);
          } else {
              printf("The message has been encrypted. \n");

          }

          //------------------------------------------------------------------
          //  Indicate the size of the buffer in the first DWORD. 

          int size = SecBuff[0].cbBuffer + SecBuff[1].cbBuffer + SecBuff[2].cbBuffer;

          //*((DWORD *)*ppOutput) = size;

          //-----------------------------------------------------------------
          //  Append the encrypted data to our trailer block
          //  to form a single block. 
          //  Putting trailer at the beginning of the buffer works out 
          //  better. 

          //memcpy(*ppOutput + SecBuff[0].cbBuffer + sizeof(DWORD), pMessage,
          //    cbMessage);

          _buffer.resize(size);

          // Tell them we transfered all the bytes in the original payload, ignore the SSL overhead
          bytes_transferred = cbMessage;
          printf("data after encryption including trailer (%lu bytes):\n",
              size);
          //PrintHexDump(*pcbOutput, *ppOutput);

          return engine::want_output;
      }  // end EncryptThis

      void readOutputBuffer(void* data, size_t inLength, size_t* outLength) {

          _buffer.read(data, inLength, outLength);
      }
  };



  SSLHandshakeBuffer _handshakeBuffer;
  SSLReadBuffer _readBuffer;
  SSLWriteBuffer _writeBuffer;
/*
*/


  //BIO* ext_bio_;
};

} // namespace detail
} // namespace ssl
} // namespace asio

#include "asio/detail/pop_options.hpp"

#if defined(ASIO_HEADER_ONLY)
# include "asio/ssl/detail/impl/engine.ipp"
#endif // defined(ASIO_HEADER_ONLY)

#endif // ASIO_SSL_DETAIL_ENGINE_HPP
