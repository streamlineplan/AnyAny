#pragma once

#include <type_traits>
#include <cstddef>

// Yes, msvc do not support EBO which is already GUARANTEED by C++ standard for ~13 years
#if defined(_MSC_VER)
#define AA_MSVC_EBO __declspec(empty_bases)
#else
#define AA_MSVC_EBO
#endif

namespace aa {

template <typename...>
struct type_list {};

constexpr inline size_t npos = size_t(-1);

}  // namespace aa

namespace aa::noexport {

// this tuple exist only because i cant constexpr cast function pointer to void* for storing in vtable
template <typename T, size_t>
struct value_in_tuple {
  T value{};
};

template <typename...>
struct tuple_base {};

template <size_t... Is, typename... Ts>
struct AA_MSVC_EBO tuple_base<std::index_sequence<Is...>, Ts...> : value_in_tuple<Ts, Is>... {
  constexpr tuple_base(Ts... args) noexcept : value_in_tuple<Ts, Is>{static_cast<Ts&&>(args)}... {
  }
};
// stores values always in right order(in memory), used only for function pointers in vtable
template <typename... Ts>
struct tuple : tuple_base<std::index_sequence_for<Ts...>, Ts...> {
  using tuple::tuple_base::tuple_base;
};

// in this library tuple used ONLY for function pointers in vtable, so get_value returns value
template <size_t I, typename U>
constexpr U get_value(value_in_tuple<U, I> v) noexcept {
  return v.value;
}

template <size_t I, typename... Args>
struct number_of_impl {
  static constexpr size_t value = aa::npos;  // no such element in pack
};
template <size_t I, typename T, typename... Args>
struct number_of_impl<I, T, T, Args...> {
  static constexpr size_t value = I;
};
template <size_t I, typename T, typename First, typename... Args>
struct number_of_impl<I, T, First, Args...> {
  static constexpr size_t value = number_of_impl<I + 1, T, Args...>::value;
};

// npos if no such type in pack
template <typename T, typename... Args>
inline constexpr size_t number_of_first = number_of_impl<0, T, Args...>::value;

template <typename T, typename... Args>
struct is_one_of : std::bool_constant<(std::is_same_v<T, Args> || ...)> {};

template <typename T>
constexpr inline bool always_false = false;

template <typename Self>
consteval bool is_const_method() noexcept {
  if (std::is_reference_v<Self>)
    return std::is_const_v<std::remove_reference_t<Self>>;
  return true;  // passing by value is a const method!
}

template <typename>
struct any_method_traits {};

// returns false if it is ill-formed to pass non-const reference into function which accepts T
template <typename T>
consteval bool is_const_arg() {
  return !std::is_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>;
}
// Note: for signature && added to arguments for forwarding
template <typename R, typename Self, typename... Args>
struct any_method_traits<R (*)(Self, Args...)> {
  using self_sample_type = Self;
  using result_type = R;
  static constexpr bool is_const = is_const_method<Self>();
  using type_erased_self_type = std::conditional_t<is_const, const void*, void*>;
  using type_erased_signature_type = R (*)(type_erased_self_type, Args&&...);
  using args = aa::type_list<Args...>;
  // for invoke_match
  static constexpr bool is_noexcept = false;
  using all_args = aa::type_list<Self, Args...>;
  using decay_args = aa::type_list<std::decay_t<Self>, std::decay_t<Args>...>;
  static constexpr std::size_t args_count = sizeof...(Args) + 1;
  static constexpr std::array args_const = {is_const_arg<Self>(), is_const_arg<Args>()...};
};
// noexcept version
template <typename R, typename Self, typename... Args>
struct any_method_traits<R (*)(Self, Args...) noexcept> {
  using self_sample_type = Self;
  using result_type = R;
  static constexpr bool is_const = is_const_method<Self>();
  using type_erased_self_type = std::conditional_t<is_const, const void*, void*>;
  using type_erased_signature_type = R (*)(type_erased_self_type, Args&&...);
  using args = aa::type_list<Args...>;
  // for invoke_match
  static constexpr bool is_noexcept = true;
  using all_args = aa::type_list<Self, Args...>;
  using decay_args = aa::type_list<std::decay_t<Self>, std::decay_t<Args>...>;
  static constexpr std::size_t args_count = sizeof...(Args) + 1;
  static constexpr std::array args_const = {is_const_arg<Self>(), is_const_arg<Args>()...};
};
// for invoke_match (only lambdas without capture)
template <typename Class, typename R, typename... Args>
struct any_method_traits<R (Class::*)(Args...) const> {
  using result_type = R;
  static constexpr bool is_noexcept = false;
  using all_args = aa::type_list<Args...>;
  using decay_args = aa::type_list<std::decay_t<Args>...>;
  static constexpr std::size_t args_count = sizeof...(Args);
  static constexpr std::array args_const = {is_const_arg<Args>()...};
};
template <typename Class, typename R, typename... Args>
struct any_method_traits<R (Class::*)(Args...) const noexcept> {
  using result_type = R;
  static constexpr bool is_noexcept = true;
  using all_args = aa::type_list<Args...>;
  using decay_args = aa::type_list<std::decay_t<Args>...>;
  static constexpr std::size_t args_count = sizeof...(Args);
  static constexpr std::array args_const = {is_const_arg<Args>()...};
};

template <aa::lambda_without_capture T>
struct any_method_traits<T> : any_method_traits<decltype(&T::operator())> {};

template <typename Method, typename Alloc, size_t SooS>
static consteval bool check_copy() {
  if constexpr (requires { typename Method::allocator_type; })
    return std::is_same_v<typename Method::allocator_type, Alloc> && SooS == Method::SooS_value;
  else
    return true;
}

consteval bool starts_with(aa::type_list<>, auto&&) {
  return true;
}
// first type not equal
consteval bool starts_with(auto&&, auto&&) {
  return false;
}
template <typename Head, typename... Ts1, typename... Ts2>
consteval bool starts_with(aa::type_list<Head, Ts1...>, aa::type_list<Head, Ts2...>) {
  return starts_with(aa::type_list<Ts1...>{}, aa::type_list<Ts2...>{});
}

// returns index in list where first typelist starts as subset in second typelist or npos if no such index
template <typename... Ts1, typename Head, typename... Ts2>
consteval size_t find_subset(aa::type_list<Ts1...> needle, aa::type_list<Head, Ts2...> all,
                             size_t n = 0) noexcept {
  if constexpr (sizeof...(Ts1) >= sizeof...(Ts2) + 1)
    return std::is_same_v<aa::type_list<Ts1...>, aa::type_list<Head, Ts2...>> ? n : ::aa::npos;
  else if constexpr (starts_with(needle, all))
    return n;
  else
    return find_subset(needle, aa::type_list<Ts2...>{}, n + 1);
}

}  // namespace aa::noexport