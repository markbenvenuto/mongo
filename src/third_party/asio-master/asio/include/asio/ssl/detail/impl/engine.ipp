//
// ssl/detail/impl/engine.ipp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_SSL_DETAIL_IMPL_ENGINE_IPP
#define ASIO_SSL_DETAIL_IMPL_ENGINE_IPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#include "asio/detail/throw_error.hpp"
#include "asio/error.hpp"
#include "asio/ssl/detail/engine.hpp"
#include "asio/ssl/error.hpp"
#include "asio/ssl/verify_context.hpp"

#include "asio/detail/push_options.hpp"

#include <algorithm>

#if 0

namespace asio {
namespace ssl {
namespace detail {

engine::engine(SSL_CTX* context)
  : ssl_(::SSL_new(context))
{
  if (!ssl_)
  {
    asio::error_code ec(
        static_cast<int>(::ERR_get_error()),
        asio::error::get_ssl_category());
    asio::detail::throw_error(ec, "engine");
  }

#if (OPENSSL_VERSION_NUMBER < 0x10000000L)
  accept_mutex().init();
#endif // (OPENSSL_VERSION_NUMBER < 0x10000000L)

  ::SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);
  ::SSL_set_mode(ssl_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
#if defined(SSL_MODE_RELEASE_BUFFERS)
  ::SSL_set_mode(ssl_, SSL_MODE_RELEASE_BUFFERS);
#endif // defined(SSL_MODE_RELEASE_BUFFERS)

  ::BIO* int_bio = 0;
  ::BIO_new_bio_pair(&int_bio, 0, &ext_bio_, 0);
  ::SSL_set_bio(ssl_, int_bio, int_bio);
}

engine::~engine()
{
  if (SSL_get_app_data(ssl_))
  {
    delete static_cast<verify_callback_base*>(SSL_get_app_data(ssl_));
    SSL_set_app_data(ssl_, 0);
  }

  ::BIO_free(ext_bio_);
  ::SSL_free(ssl_);
}

SSL* engine::native_handle()
{
  return ssl_;
}

asio::error_code engine::set_verify_mode(
    verify_mode v, asio::error_code& ec)
{
  ::SSL_set_verify(ssl_, v, ::SSL_get_verify_callback(ssl_));

  ec = asio::error_code();
  return ec;
}

asio::error_code engine::set_verify_depth(
    int depth, asio::error_code& ec)
{
  ::SSL_set_verify_depth(ssl_, depth);

  ec = asio::error_code();
  return ec;
}

asio::error_code engine::set_verify_callback(
    verify_callback_base* callback, asio::error_code& ec)
{
  if (SSL_get_app_data(ssl_))
    delete static_cast<verify_callback_base*>(SSL_get_app_data(ssl_));

  SSL_set_app_data(ssl_, callback);

  ::SSL_set_verify(ssl_, ::SSL_get_verify_mode(ssl_),
      &engine::verify_callback_function);

  ec = asio::error_code();
  return ec;
}

int engine::verify_callback_function(int preverified, X509_STORE_CTX* ctx)
{
  if (ctx)
  {
    if (SSL* ssl = static_cast<SSL*>(
          ::X509_STORE_CTX_get_ex_data(
            ctx, ::SSL_get_ex_data_X509_STORE_CTX_idx())))
    {
      if (SSL_get_app_data(ssl))
      {
        verify_callback_base* callback =
          static_cast<verify_callback_base*>(
              SSL_get_app_data(ssl));

        verify_context verify_ctx(ctx);
        return callback->call(preverified != 0, verify_ctx) ? 1 : 0;
      }
    }
  }

  return 0;
}

engine::want engine::handshake(
    stream_base::handshake_type type, asio::error_code& ec)
{
  return perform((type == asio::ssl::stream_base::client)
      ? &engine::do_connect : &engine::do_accept, 0, 0, ec, 0);
}

engine::want engine::shutdown(asio::error_code& ec)
{
  return perform(&engine::do_shutdown, 0, 0, ec, 0);
}

engine::want engine::write(const asio::const_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }

  return perform(&engine::do_write,
      const_cast<void*>(data.data()),
      data.size(), ec, &bytes_transferred);
}

engine::want engine::read(const asio::mutable_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }

  return perform(&engine::do_read, data.data(),
      data.size(), ec, &bytes_transferred);
}

asio::mutable_buffer engine::get_output(
    const asio::mutable_buffer& data)
{
  int length = ::BIO_read(ext_bio_,
      data.data(), static_cast<int>(data.size()));

  return asio::buffer(data,
      length > 0 ? static_cast<std::size_t>(length) : 0);
}

asio::const_buffer engine::put_input(
    const asio::const_buffer& data)
{
  int length = ::BIO_write(ext_bio_,
      data.data(), static_cast<int>(data.size()));

  return asio::buffer(data +
      (length > 0 ? static_cast<std::size_t>(length) : 0));
}

const asio::error_code& engine::map_error_code(
    asio::error_code& ec) const
{
  // We only want to map the error::eof code.
  if (ec != asio::error::eof)
    return ec;

  // If there's data yet to be read, it's an error.
  if (BIO_wpending(ext_bio_))
  {
    ec = asio::ssl::error::stream_truncated;
    return ec;
  }

  // SSL v2 doesn't provide a protocol-level shutdown, so an eof on the
  // underlying transport is passed through.
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
  if (ssl_->version == SSL2_VERSION)
    return ec;
#endif // (OPENSSL_VERSION_NUMBER < 0x10100000L)

  // Otherwise, the peer should have negotiated a proper shutdown.
  if ((::SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN) == 0)
  {
    ec = asio::ssl::error::stream_truncated;
  }

  return ec;
}

#if (OPENSSL_VERSION_NUMBER < 0x10000000L)
asio::detail::static_mutex& engine::accept_mutex()
{
  static asio::detail::static_mutex mutex = ASIO_STATIC_MUTEX_INIT;
  return mutex;
}
#endif // (OPENSSL_VERSION_NUMBER < 0x10000000L)

engine::want engine::perform(int (engine::* op)(void*, std::size_t),
    void* data, std::size_t length, asio::error_code& ec,
    std::size_t* bytes_transferred)
{
  std::size_t pending_output_before = ::BIO_ctrl_pending(ext_bio_);
  ::ERR_clear_error();
  int result = (this->*op)(data, length);
  int ssl_error = ::SSL_get_error(ssl_, result);
  int sys_error = static_cast<int>(::ERR_get_error());
  std::size_t pending_output_after = ::BIO_ctrl_pending(ext_bio_);

  if (ssl_error == SSL_ERROR_SSL)
  {
    ec = asio::error_code(sys_error,
        asio::error::get_ssl_category());
    return want_nothing;
  }

  if (ssl_error == SSL_ERROR_SYSCALL)
  {
    ec = asio::error_code(sys_error,
        asio::error::get_system_category());
    return want_nothing;
  }

  if (result > 0 && bytes_transferred)
    *bytes_transferred = static_cast<std::size_t>(result);

  if (ssl_error == SSL_ERROR_WANT_WRITE)
  {
    ec = asio::error_code();
    return want_output_and_retry;
  }
  else if (pending_output_after > pending_output_before)
  {
    ec = asio::error_code();
    return result > 0 ? want_output : want_output_and_retry;
  }
  else if (ssl_error == SSL_ERROR_WANT_READ)
  {
    ec = asio::error_code();
    return want_input_and_retry;
  }
  else if (::SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN)
  {
    ec = asio::error::eof;
    return want_nothing;
  }
  else
  {
    ec = asio::error_code();
    return want_nothing;
  }
}

int engine::do_accept(void*, std::size_t)
{
#if (OPENSSL_VERSION_NUMBER < 0x10000000L)
  asio::detail::static_mutex::scoped_lock lock(accept_mutex());
#endif // (OPENSSL_VERSION_NUMBER < 0x10000000L)
  return ::SSL_accept(ssl_);
}

int engine::do_connect(void*, std::size_t)
{
  return ::SSL_connect(ssl_);
}

int engine::do_shutdown(void*, std::size_t)
{
  int result = ::SSL_shutdown(ssl_);
  if (result == 0)
    result = ::SSL_shutdown(ssl_);
  return result;
}

int engine::do_read(void* data, std::size_t length)
{
  return ::SSL_read(ssl_, data,
      length < INT_MAX ? static_cast<int>(length) : INT_MAX);
}

int engine::do_write(void* data, std::size_t length)
{
  return ::SSL_write(ssl_, data,
      length < INT_MAX ? static_cast<int>(length) : INT_MAX);
}

} // namespace detail
} // namespace ssl
} // namespace asio
#else

namespace asio {
namespace ssl {
namespace detail {

engine::engine(_SecHandle context)
  : ssl_({0,0}})
{
}

engine::~engine()
{
}

_SecHandle* engine::native_handle()
{
  return ssl_;
}

asio::error_code engine::set_verify_mode(
    verify_mode v, asio::error_code& ec)
{
  ec = asio::error_code();
  return ec;
}

asio::error_code engine::set_verify_depth(
    int depth, asio::error_code& ec)
{
  ec = asio::error_code();
  return ec;
}

asio::error_code engine::set_verify_callback(
    verify_callback_base* callback, asio::error_code& ec)
{
  ec = asio::error_code();
  return ec;
}

//int engine::verify_callback_function(int preverified, X509_STORE_CTX* ctx)
//{
//  return 0;
//}

engine::want engine::handshake(
    stream_base::handshake_type type, asio::error_code& ec)
{
  return perform((type == asio::ssl::stream_base::client)
      ? &engine::do_connect : &engine::do_accept, 0, 0, ec, 0);
}

engine::want engine::shutdown(asio::error_code& ec)
{
  return perform(&engine::do_shutdown, 0, 0, ec, 0);
}


class SSLReadBuffer {
    enum class State {
        NeedMoreEncryptedData,
        HaveEncryptedData,
        HaveDecryptedData,
    };

    State _state;
    std::vector<unsigned char> _buffer;
    std::vector<unsigned char> _extraEncryptedBuffer;
    size_t bufPos;

    void setState(State s) {
        _state = s;
    }


    _SecHandle* _hctxt;

    SSLReadBuffer(_SecHandle* hctxt) : _state(State::NeedMoreEncryptedData), _hctxt(hctxt) {
        _buffer.reserve(16 * 1024);
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
        assert(_state == State::HaveDecryptedData);

        if (length >= ( _buffer.size() - bufPos)) {
            // We have less then ASIO wants, give them everything we have
            outLength = _buffer.size();
            memcpy(data, _buffer.data() + bufPos, _buffer.size() - bufPos);

            // We are empty so reset our state to need encrypted data for the next call
            setState(State::NeedMoreEncryptedData);
            bufPos = 0;
        } else {
            // ASIO wants less then we have so give them just what they want
            outLength = length;
            memcpy(data, _buffer.data(), length);

            bufPos = length;
        }

        return engine::want_nothing;
    }

    void writeData(void* data, std::size_t length) {
        // We have more data, it may not be enough to decode
        // but we will figure that out later
        setState(State::HaveEncryptedData);

        // If we have extra encrypted data from the last encryption, copy it over to our buffer
        if (_extraEncryptedBuffer.size()) {
            std::copy(_extraEncryptedBuffer.begin(), _extraEncryptedBuffer.end(), std::back_inserter(_buffer));
            _extraEncryptedBuffer.clear();
        }

        std::copy(reinterpret_cast<unsigned char*>(data), 
            reinterpret_cast<unsigned char*>(data) + length, std::back_inserter(_buffer));
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
            _hctxt,
            &BuffDesc,
            0,
            &ulQop);

        if (!SEC_SUCCESS(ss)) {
            if (ss == SEC_E_INCOMPLETE_MESSAGE) {
                printf("Need more data for DecryptMessage");

                return engine::want_input_and_retry;
            } else {
                fprintf(stderr, "DecryptMessage failed");
                // TODO: verify(false);

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
                // TODO: assert pExtraBuffer == NULL
                pExtraBuffer = &SecBuff[i];
            }
        }

        // TODO: assert  pDataBuffer->pvBuffer == _buffer.get()
        _buffer.resize(pDataBuffer->cbBuffer);
        //outBuf = (char*)pDataBuffer->pvBuffer;
        //outLen = pDataBuffer->cbBuffer;

        if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
            // TODO: assert _extraEncryptedBuffer.size() == 0
            _extraEncryptedBuffer.clear();
            std::copy(reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer), 
                reinterpret_cast<unsigned char*>(pExtraBuffer->pvBuffer) + pExtraBuffer->cbBuffer, 
                std::back_inserter(_extraEncryptedBuffer));
        }

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
    std::vector<unsigned char> _buffer;
    std::vector<unsigned char> _extraEncryptedBuffer;

    std::vector<unsigned char> _outBuffer;
    size_t bufPos;
    CredHandle hcred;

    void setState(State s) {
        _state = s;
    }


    _SecHandle* _hctxt;

    SSLHandshakeBuffer(_SecHandle* hctxt) : _state(State::HandshakeStart), _hctxt(hctxt) {
        _buffer.reserve(16 * 1024);
        _outBuffer.reserve(16 * 1024);
    }

    engine::want next(asio::error_code& ec) {

        if (_state == State::HandshakeStart) {
            
            startServerHandshake(ec);

            engine::want want = TryAcceptClientToken(true, ec);

            setState(State::NeedMoreHandshakeData);

            return want;
        } else if (_state == State::NeedMoreHandshakeData) {
            return  engine::want_input_and_retry;
        } else {

            engine::want want = TryAcceptClientToken(false, ec);

            if (want == engine::want_nothing) {
                setState(State::Done);
            }

            return want;
        }
    }

    void startServerHandshake(asio::error_code& ec) {
        SCHANNEL_CRED credData;
        TimeStamp         Lifetime;

        PCCERT_CONTEXT serverCert; // server-side certificate
                                   //-------------------------------------------------------
                                   // Get the server certificate. 

        if (!(serverCert = getServerCertificate())) {
            // TODO verify(false);
        }

        // getServerCertificate is a placeholder function.
        credData.cCreds = 1;
        credData.paCred = &serverCert;


        SECURITY_STATUS ss = AcquireCredentialsHandleA(
            NULL,
            "SChannel",
            SECPKG_CRED_INBOUND,
            NULL,
            &credData,
            NULL,
            NULL,
            &hcred,
            &Lifetime);

        if (!SEC_SUCCESS(ss)) {
            fprintf(stderr, "AcquireCreds failed: 0x%08x\n", ss);
            // TODO verify(false);
        }

    }
    
    void writeEncryptedData(void* data, std::size_t length) {
        // We have more data, it may not be enough to decode
        // but we will figure that out later
        setState(State::HaveEncryptedData);

        // If we have extra encrypted data from the last encryption, copy it over to our buffer
        if (_extraEncryptedBuffer.size()) {
            std::copy(_extraEncryptedBuffer.begin(), _extraEncryptedBuffer.end(), std::back_inserter(_buffer));
            _extraEncryptedBuffer.clear();
        }

        std::copy(reinterpret_cast<unsigned char*>(data),
            reinterpret_cast<unsigned char*>(data) + length, std::back_inserter(_buffer));
    }
private:

    engine::want TryAcceptClientToken(
        bool fNewConversation, asio::error_code& ec) {
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
            &hcred,
            fNewConversation ? NULL : &_hctxt,
            &InBuffDesc,
            Attribs,
            SECURITY_NATIVE_DREP,
            &_hctxt,
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
            //TODO verify(false);
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

        if ((SEC_I_COMPLETE_NEEDED == ss)
            || (SEC_I_COMPLETE_AND_CONTINUE == ss)) {
            if (SEC_I_COMPLETE_AND_CONTINUE == ss) {
                needOutput = true;
            }

            ss = CompleteAuthToken(&hctxt, &OutBuffDesc);
            if (!SEC_SUCCESS(ss)) {
                fprintf(stderr, "complete failed: 0x%08x\n", ss);
                // TODO: verify(false);
            }
        }
        printf("Token buffer generated (%lu bytes):\n",
            OutSecBuff.cbBuffer);

        _outBuffer.resize(OutSecBuff.cbBuffer);

        if (needOutput) {
            return engine::want_output_and_retry;
        }


        // assert ss == SEC_I_COMPLETE_NEEDED
        return engine::want_nothing;
    }  // end GenServerContext


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
            "MongoWinSSL", // use appropriate subject name
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
    std::vector<unsigned char> _buffer;
    size_t bufPos;

    void setState(State s) {
        _state = s;
    }

    SSLWriteBuffer(_SecHandle* hctxt, ULONG cbSecurityHeader, ULONG cbSecurityTrailer) : _state(State::HaveEmptyBuffer), _hctxt(hctxt),
        cbSecurityHeader(cbSecurityHeader), cbSecurityTrailer(cbSecurityTrailer)
    {
        _buffer.reserve(16 * 1024);
    }

    _SecHandle* _hctxt;

    ULONG cbSecurityTrailer;
    ULONG cbSecurityHeader;

    engine::want writeUnecryptedData
    (void* pMessage, std::size_t cbMessage, asio::error_code& ec) {
        SECURITY_STATUS   ss;
        SecBufferDesc     BuffDesc;
        SecBuffer       SecBuff[4];

        ULONG             ulQop = 0;

        //-----------------------------------------------------------------
        //  The size of the trailer (signature + padding) block is 
        //  determined from the global cbSecurityTrailer.

        ULONG SigBufferSize = cbSecurityTrailer + cbSecurityHeader;

        printf("Data before encryption: %s\n", pMessage);
        printf("Length of data before encryption: %d \n", cbMessage);

        //-----------------------------------------------------------------
        //  Allocate a buffer to hold the signature,
        //  encrypted data, and a DWORD  
        //  that specifies the size of the trailer block.

        _buffer.resize(SigBufferSize + cbMessage);

        //------------------------------------------------------------------
        //  Prepare buffers.

        BuffDesc.ulVersion = 0;
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
            &_hctxt,
            ulQop,
            &BuffDesc,
            0);

        if (!SEC_SUCCESS(ss)) {
            fprintf(stderr, "EncryptMessage failed: 0x%08x\n", ss);
            // TODO return(FALSE);
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
        printf("data after encryption including trailer (%lu bytes):\n",
            size);
        //PrintHexDump(*pcbOutput, *ppOutput);

        return engine::want_output;
    }  // end EncryptThis

    void readOutputBuffer(void* data, size_t inLength, size_t outLength) {
        if(length > )
#error TODO
    }
};

engine::want engine::write(const asio::const_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }

  return perform(&engine::do_write,
      const_cast<void*>(data.data()),
      data.size(), ec, &bytes_transferred);
}

engine::want engine::read(const asio::mutable_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }

  return perform(&engine::do_read, data.data(),
      data.size(), ec, &bytes_transferred);
}

asio::mutable_buffer engine::get_output(
    const asio::mutable_buffer& data)
{
  int length = ::BIO_read(ext_bio_,
      data.data(), static_cast<int>(data.size()));

  return asio::buffer(data,
      length > 0 ? static_cast<std::size_t>(length) : 0);
}

asio::const_buffer engine::put_input(
    const asio::const_buffer& data)
{
  int length = ::BIO_write(ext_bio_,
      data.data(), static_cast<int>(data.size()));

  return asio::buffer(data +
      (length > 0 ? static_cast<std::size_t>(length) : 0));
}

const asio::error_code& engine::map_error_code(
    asio::error_code& ec) const
{
  // We only want to map the error::eof code.
  if (ec != asio::error::eof)
    return ec;

  // If there's data yet to be read, it's an error.
  if (BIO_wpending(ext_bio_))
  {
    ec = asio::ssl::error::stream_truncated;
    return ec;
  }

  // SSL v2 doesn't provide a protocol-level shutdown, so an eof on the
  // underlying transport is passed through.
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
  if (ssl_->version == SSL2_VERSION)
    return ec;
#endif // (OPENSSL_VERSION_NUMBER < 0x10100000L)

  // Otherwise, the peer should have negotiated a proper shutdown.
  if ((::SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN) == 0)
  {
    ec = asio::ssl::error::stream_truncated;
  }

  return ec;
}

engine::want engine::perform(int (engine::* op)(void*, std::size_t),
    void* data, std::size_t length, asio::error_code& ec,
    std::size_t* bytes_transferred)
{
  std::size_t pending_output_before = ::BIO_ctrl_pending(ext_bio_);
  ::ERR_clear_error();
  int result = (this->*op)(data, length);
  int ssl_error = ::SSL_get_error(ssl_, result);
  int sys_error = static_cast<int>(::ERR_get_error());
  std::size_t pending_output_after = ::BIO_ctrl_pending(ext_bio_);

  if (ssl_error == SSL_ERROR_SSL)
  {
    ec = asio::error_code(sys_error,
        asio::error::get_ssl_category());
    return want_nothing;
  }

  if (ssl_error == SSL_ERROR_SYSCALL)
  {
    ec = asio::error_code(sys_error,
        asio::error::get_system_category());
    return want_nothing;
  }

  if (result > 0 && bytes_transferred)
    *bytes_transferred = static_cast<std::size_t>(result);

  if (ssl_error == SSL_ERROR_WANT_WRITE)
  {
    ec = asio::error_code();
    return want_output_and_retry;
  }
  else if (pending_output_after > pending_output_before)
  {
    ec = asio::error_code();
    return result > 0 ? want_output : want_output_and_retry;
  }
  else if (ssl_error == SSL_ERROR_WANT_READ)
  {
    ec = asio::error_code();
    return want_input_and_retry;
  }
  else if (::SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN)
  {
    ec = asio::error::eof;
    return want_nothing;
  }
  else
  {
    ec = asio::error_code();
    return want_nothing;
  }
}

int engine::do_accept(void*, std::size_t)
{
#if (OPENSSL_VERSION_NUMBER < 0x10000000L)
  asio::detail::static_mutex::scoped_lock lock(accept_mutex());
#endif // (OPENSSL_VERSION_NUMBER < 0x10000000L)
  return ::SSL_accept(ssl_);
}

int engine::do_connect(void*, std::size_t)
{
  return ::SSL_connect(ssl_);
}

int engine::do_shutdown(void*, std::size_t)
{
  int result = ::SSL_shutdown(ssl_);
  if (result == 0)
    result = ::SSL_shutdown(ssl_);
  return result;
}

int engine::do_read(void* data, std::size_t length)
{
  return ::SSL_read(ssl_, data,
      length < INT_MAX ? static_cast<int>(length) : INT_MAX);
}

int engine::do_write(void* data, std::size_t length)
{
  return ::SSL_write(ssl_, data,
      length < INT_MAX ? static_cast<int>(length) : INT_MAX);
}

} // namespace detail
} // namespace ssl
} // namespace asio

#endif

#include "asio/detail/pop_options.hpp"

#endif // ASIO_SSL_DETAIL_IMPL_ENGINE_IPP
