#if !defined(MONGO_CONFIG_SSL_PROVIDER_WINDOWS)
#error Only include this file in the SChannel Implementation
#endif

namespace asio {
namespace ssl {


context::context(context::method m)
    : handle_(0)
{
}

#if defined(ASIO_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)
context::context(context&& other)
{
    handle_ = other.handle_;
    other.handle_ = 0;
}

context& context::operator=(context&& other)
{
    context tmp(ASIO_MOVE_CAST(context)(*this));
    handle_ = other.handle_;
    other.handle_ = 0;
    return *this;
}
#endif // defined(ASIO_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)

context::~context()
{
}

context::native_handle_type context::native_handle()
{
    return handle_;
}

} // namespace ssl
} // namespace asio