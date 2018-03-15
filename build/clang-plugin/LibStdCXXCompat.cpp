#include <system_error>

namespace std {

inline namespace _V2 {

error_category::~error_category() = default;

__sso_string
error_category::_M_message(int i) const
{
  string msg = this->message(i);
  return {msg.c_str(), msg.length()};
}

error_condition
error_category::default_error_condition(int __i) const noexcept
{ return error_condition(__i, *this); }

bool
error_category::equivalent(int __i,
                           const error_condition& __cond) const noexcept
{ return default_error_condition(__i) == __cond; }

bool
error_category::equivalent(const error_code& __code, int __i) const noexcept
{ return *this == __code.category() && __code.value() == __i; }

}

}
