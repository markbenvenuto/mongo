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
        Empty,
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



    SSLReadBuffer() : _state(State::Empty) {
        _buffer.reserve(16 * 1024);
    }

    engine::want readDecryptedData(void* data, std::size_t length, std::size_t &outLength) {
        outLength = 0;

        // We are empty, i.e. brand new, tell ASIO we want data
        if (_state == State::Empty) {
            setState(State::NeedMoreEncryptedData);
            return engine::want_input_and_retry;
        }

        // Our last state was that we needed more encrypted data, so tell ASIO we still want some
        // TODO: invariant this???
        if (_state == State::NeedMoreEncryptedData) {
            return engine::want_input_and_retry;
        }


        // If we have enrypted data, try to decrypt it
        if (_state == State::HaveEncryptedData) {
            engine::want wantState = TryDecryptBuffer();
         
            if (wantState != engine::want_nothing) {
                return wantState;
            }
        }

        // We decrypted data in the past, hand it back to ASIO until we are out of decrypted data
        assert(_state == State::HaveDecryptedData);

        if (length >= ( buffer.size() - bufPos)) {
            // We have less then ASIO wants, give them everything we have
            outLength = _buffer.size();
            memcpy(data, _buffer.data() + bufPos, _buffer.size() - bufPos);

            // We are empty so reset our state to need encrypted data for the next call
            setState(State::NeedMoreEncryptedData);
            bufPos = 0;
        } else {
            // ASIO wants less then we have
            outLength = length;
            memcpy(data, _buffer.data(), length);

            bufPos = length;
        }

        return engine::want_nothing;
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
        SecBuff[0].pvBuffer = _buffer.get();

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
            &_hctxt,
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
        //outBuf = (char*)pDataBuffer->pvBuffer;
        //outLen = pDataBuffer->cbBuffer;

        if (pExtraBuffer != NULL && pExtraBuffer->cbBuffer > 0) {
            // TODO: assert _extraEncryptedBuffer.size() == 0
            _extraEncryptedBuffer.clear();
            std::copy(pExtraBuffer->pvBuffer, pExtraBuffer->pvBuffer + pExtraBuffer->cbBuffer, std::back_inserter(_extraEncryptedBuffer));
        }

        return engine::want_nothing;
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
