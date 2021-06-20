#ifndef _HEADER_PROPANE_LIBRARY
#define _HEADER_PROPANE_LIBRARY

#include "propane_runtime.hpp"

#include <tuple>

namespace propane
{
    namespace invoke_utility
    {
        template<typename value_t, bool pointer> struct decay_base
        {
            typedef std::decay_t<value_t> type;
        };
        template<typename value_t> struct decay_base<value_t, true>
        {
            typedef std::decay_t<std::remove_pointer_t<value_t>>* type;
        };

        template <typename value_t> using decay_base_t = typename decay_base<value_t, std::is_pointer_v<value_t>>::type;

        // Recursive method signature
        template<typename... param_t> class method_signature_param;
        template<> class method_signature_param<>
        {
        public:
            static inline void generate(stackvar* result, size_t& offset) noexcept
            {

            }
            template<size_t idx, typename... tuple_args> static inline void read_value(std::tuple<tuple_args...>& tup, const void*& data) noexcept
            {

            }
        };
        template<typename value_t, typename... param_t> class method_signature_param<value_t, param_t...> : public method_signature_param<param_t...>
        {
        public:
            static inline void generate(stackvar* result, size_t& offset) noexcept
            {
                constexpr type_idx val = derive_type_index_v<decay_base_t<value_t>>;
                static_assert(val != type_idx::invalid, "Unsupported base type provided");
                *result++ = stackvar(val, offset);
                offset += derive_base_size_v<decay_base_t<value_t>>;
                method_signature_param<param_t...>::generate(result, offset);
            }
            template<size_t idx, typename... tuple_args> static inline void read_value(std::tuple<tuple_args...>& tup, const void*& data) noexcept
            {
                std::get<idx>(tup) = *reinterpret_cast<const value_t*&>(data)++;
                method_signature_param<param_t...>::template read_value<idx + 1>(tup, data);
            }
        };

        // Tuple expansion into static fuction
        template <typename function, typename tuple_type, size_t... indices> static inline auto expand_sequence(function func, const tuple_type& tup, std::index_sequence<indices...>)
        {
            return func(std::get<indices>(tup)...);
        }
        template <typename function, typename tuple_type> static inline auto expand(function func, const tuple_type& tup)
        {
            return expand_sequence(func, tup, std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
        }

        template<typename retval_t, typename... param_t> class method_invoke
        {
        public:
            static inline void invoke(void* ret_val, const void* param, retval_t(*call)(param_t...)) noexcept
            {
                std::tuple<std::decay_t<param_t>...> tup;

                method_signature_param<param_t...>::template read_value<0>(tup, param);

                *reinterpret_cast<retval_t*>(ret_val) = expand(call, tup);
            }
        };
        template<typename... param_t> class method_invoke<void, param_t...>
        {
        public:
            static inline void invoke(void* ret_val, const void* param, void(*call)(param_t...)) noexcept
            {
                std::tuple<std::decay_t<param_t>...> tup;

                method_signature_param<param_t...>::template read_value<0>(tup, param);

                expand(call, tup);
            }
        };
    }

    // Temporary method handle. Contains information for runtime methods imported from external sources
    // (dynamic libraries or source). All resources in this object need to outlive the library's lifespan
    // (including the name string and the method handle).
    // The signature provided in the binding needs to match the signature of the method. Signatures are
    // not validated for dynamic library calls so take care to make sure the signatures match.
    class external_call
    {
    public:
        typedef void(*forward_method)(void*, void*, const void*);

    private:
        external_call(std::string_view name);

        template<typename retval_t, typename... param_t> static void bind_method(external_call& call, retval_t(*method)(param_t...))
        {
            struct bind
            {
                bind()
                {
                    parameters_size = 0;
                    invoke_utility::method_signature_param<param_t...>::generate(parameters, parameters_size);
                }

                static void forward_call(void* handle, void* ret_val, const void* param) noexcept
                {
                    const auto method_ptr = reinterpret_cast<retval_t(*)(param_t...)>(handle);
                    invoke_utility::method_invoke<retval_t, param_t...>::invoke(ret_val, param, method_ptr);
                }

                // Array size cannot be 0
                stackvar parameters[sizeof...(param_t) == 0 ? 1 : sizeof...(param_t)];
                size_t parameters_size;
            };

            constexpr type_idx ret_type = derive_type_index_v<invoke_utility::decay_base_t<retval_t>>;
            static_assert(ret_type != type_idx::invalid, "Unsupported return type provided");

            static bind instance;

            call.forward = bind::forward_call;
            call.return_type = ret_type;
            call.parameters = std::span<const stackvar>(instance.parameters, sizeof...(param_t));
            call.parameters_size = instance.parameters_size;
            call.handle = method;
        }

        friend class library;

        std::string_view name;
        forward_method forward = nullptr;
        type_idx return_type = type_idx::invalid;
        std::span<const stackvar> parameters;
        size_t parameters_size = 0;
        void* handle = nullptr;

    public:
        template<typename method_t> static external_call bind(std::string_view name, method_t method = nullptr)
        {
            external_call result(name);
            bind_method(result, method);
            return result;
        }
    };

    // Library object that contains external method definitions. If the list of external calls
    // contains any null handles during execution, the library will attempt to load a
    // dynamic library file at specified path.
    class library : public handle<class library_data, sizeof(size_t) * 16>
    {
    public:
        library(std::string_view path, std::span<const external_call> calls);
        library(std::string_view path, std::initializer_list<external_call> calls) :
            library(path, init_span(calls)) {}
        ~library();

    private:
        friend class runtime;
    };
}

#endif