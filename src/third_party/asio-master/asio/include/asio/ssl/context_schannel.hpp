// SChannel implementation
#if !defined(MONGO_CONFIG_SSL_PROVIDER_WINDOWS)
#error Only include this file in the SChannel Implementation
#endif

namespace asio {
namespace ssl {

class context
    : public context_base,
    private noncopyable
{
public:
    /// The native handle type of the SSL context.
    typedef SCHANNEL_CRED* native_handle_type;

    /// Constructor.
    ASIO_DECL explicit context(method m);

#if defined(ASIO_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)
    /// Move-construct a context from another.
    /**
    * This constructor moves an SSL context from one object to another.
    *
    * @param other The other context object from which the move will occur.
    *
    * @note Following the move, the following operations only are valid for the
    * moved-from object:
    * @li Destruction.
    * @li As a target for move-assignment.
    */
    ASIO_DECL context(context&& other);

    /// Move-assign a context from another.
    /**
    * This assignment operator moves an SSL context from one object to another.
    *
    * @param other The other context object from which the move will occur.
    *
    * @note Following the move, the following operations only are valid for the
    * moved-from object:
    * @li Destruction.
    * @li As a target for move-assignment.
    */
    ASIO_DECL context& operator=(context&& other);
#endif // defined(ASIO_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)

    /// Destructor.
    ASIO_DECL ~context();

    /// Get the underlying implementation in the native type.
    /**
    * This function may be used to obtain the underlying implementation of the
    * context. This is intended to allow access to context functionality that is
    * not otherwise provided.
    */
    ASIO_DECL native_handle_type native_handle();

private:
    // The underlying native implementation.
    native_handle_type handle_;
};

} // namespace ssl
} // namespace asio
