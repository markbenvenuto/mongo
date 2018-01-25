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

#if !defined(MONGO_CONFIG_SSL_PROVIDER_WINDOWS)
#error Only include this file in the SChannel Implementation
#endif

namespace asio {
namespace ssl {
namespace detail {


engine::engine(SCHANNEL_CRED* context):
  _pCred(context),
  _inBuffer(kDefaultBufferSize),
  _outBuffer(kDefaultBufferSize),
  _handshakeManager(&_hcxt, &_hcred, &_inBuffer, &_outBuffer, _pCred),
  _readManager(&_hcxt, &_hcred, &_inBuffer),
  _writeManager(&_hcxt, &_outBuffer)
{
}

engine::~engine()
{
  DeleteSecurityContext(&_hcxt);
  FreeCredentialsHandle(&_hcred);
}

PCtxtHandle engine::native_handle()
{
  return nullptr;
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
engine::want ssl_want_to_engine(ssl_want want) {
  static_assert(static_cast<int>(ssl_want::want_input_and_retry) == static_cast<int>(engine::want_input_and_retry), "bad");
  static_assert(static_cast<int>(ssl_want::want_output_and_retry) == static_cast<int>(engine::want_output_and_retry), "bad");
  static_assert(static_cast<int>(ssl_want::want_nothing) == static_cast<int>(engine::want_nothing), "bad");
  static_assert(static_cast<int>(ssl_want::want_output) == static_cast<int>(engine::want_output), "bad");

  return static_cast<engine::want>(want);
}

engine::want engine::handshake(
    stream_base::handshake_type type, asio::error_code& ec)
{
  if (_state != EngineState::NeedsHandshake) {
      return want::want_nothing;
  }

  _handshakeManager.setMode((type == asio::ssl::stream_base::client) ? SSLHandshakeManager::HandshakeMode::Client : SSLHandshakeManager::HandshakeMode::Server);
  SSLHandshakeManager::HandshakeState state;
  auto w = _handshakeManager.next(ec, &state);
  if (w == ssl_want::want_nothing || state == SSLHandshakeManager::HandshakeState::Done) {
      _state = EngineState::InProgress;
  }

  return ssl_want_to_engine(w);
}

engine::want engine::shutdown(asio::error_code& ec)
{
  // TODO:
  return want::want_nothing;
}

engine::want engine::write(const asio::const_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  // TODO:
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }

  if (_state == EngineState::NeedsHandshake) {
      // Why are we trying to write before the handshake is done?
      ASIO_ASSERT(false);
      return want::want_nothing;
  } else {
      return ssl_want_to_engine(_writeManager.writeUnecryptedData(data.data(), data.size(), bytes_transferred, ec));
  }
}

engine::want engine::read(const asio::mutable_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
  if (data.size() == 0)
  {
    ec = asio::error_code();
    return engine::want_nothing;
  }


  if (_state == EngineState::NeedsHandshake) {
      // Why are we trying to read before the handshake is done?
      ASIO_ASSERT(false);
      return want::want_nothing;
  } else {
      return ssl_want_to_engine(_readManager.readDecryptedData(data.data(), data.size(), ec, bytes_transferred));
  }
}

asio::mutable_buffer engine::get_output(
    const asio::mutable_buffer& data)
{
  std::size_t length;
  _outBuffer.read(data.data(), data.size(), length);

  return asio::buffer(data, length);
}

asio::const_buffer engine::put_input(
    const asio::const_buffer& data)
{
  if (_state == EngineState::NeedsHandshake) {
      _handshakeManager.writeEncryptedData(data.data(), data.size());
  } else {

      _readManager.writeData(data.data(), data.size());
  }

  return asio::buffer(data + data.size());
}

const asio::error_code& engine::map_error_code(
    asio::error_code& ec) const
{
  return ec;
}

} // namespace detail
} // namespace ssl
} // namespace asio
