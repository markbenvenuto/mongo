// SChannel implementation
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

// int engine::verify_callback_function(int preverified, X509_STORE_CTX* ctx)
// {
//   return 0;
// }

engine::want ssl_want_to_engine(ssl_want want) {
  // TODO: add static asserts that these enums match
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
      return ssl_want_to_engine(_readManager.readDecryptedData(data.data(), data.size(), ec, &bytes_transferred));
  }
}

asio::mutable_buffer engine::get_output(
    const asio::mutable_buffer& data)
{
  std::size_t length;
  _outBuffer.read(data.data(), data.size(), &length);

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