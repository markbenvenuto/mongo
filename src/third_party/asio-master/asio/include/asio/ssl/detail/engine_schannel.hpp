// SChannel implementation
#if !defined(MONGO_CONFIG_SSL_PROVIDER_WINDOWS)
#error Only include this file in the SChannel Implementation
#endif

#include "asio/ssl/detail/schannel.hpp"

namespace asio {
namespace ssl {
namespace detail {


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

private:

    CtxtHandle _hcxt;
    CredHandle _hcred;
    SCHANNEL_CRED* _pCred;

    enum class EngineState {
        // Initial State
        NeedsHandshake,
        InProgress,
        //TODO: InShutdown,
    };

    EngineState _state{EngineState::NeedsHandshake};

    void setState(EngineState newState);

    ReusableBuffer _inBuffer;
    ReusableBuffer _outBuffer;

    SSLHandshakeManager _handshakeManager;
    SSLReadManager _readManager;
    SSLWriteManager _writeManager;  
};

} // namespace detail
} // namespace ssl
} // namespace asio
