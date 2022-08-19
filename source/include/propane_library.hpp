#ifndef _HEADER_PROPANE_LIBRARY
#define _HEADER_PROPANE_LIBRARY

#include "propane_runtime.hpp"

#include <tuple>

#define BIND_NATIVE_FIELD(type, name) propane::make_field<uint8_t>(#name, offsetof(type, name))
#define BIND_NATIVE_TYPE(type, name, fields) template<> constexpr propane::native_type_info propane::native_type_info_v<type> = propane::make_type<type>(name, fields)

namespace propane
{
    namespace native
    {
        // Get type size
        template<typename value_t> constexpr size_t type_size_v = sizeof(value_t);
        template<> constexpr size_t type_size_v<void> = 0;

        struct typedecl : public native_type_info
        {
            constexpr typedecl() = default;
            constexpr typedecl(const native_type_info& info, size_t pointer_depth) :
                native_type_info(info),
                pointer_depth(pointer_depth) {}
            
            size_t pointer_depth = 0;
        };

        // Parameter info
        struct parameter : public typedecl
        {
            parameter() = default;
            parameter(const native_type_info& info, size_t offset, size_t pointer_depth) :
                typedecl(info, pointer_depth),
                offset(offset) {}

            size_t offset = 0;
        };

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
            static inline void generate_signature(parameter* result, size_t& offset) noexcept
            {

            }
            template<size_t idx, typename... tuple_args> static inline void read_argument(std::tuple<tuple_args...>& tup, const void*& data) noexcept
            {

            }
        };
        template<typename value_t, typename... param_t> class method_signature_param<value_t, param_t...> : public method_signature_param<param_t...>
        {
        public:
            static inline void generate_signature(parameter* result, size_t& offset) noexcept
            {
                typedef decay_base_t<value_t> param_type;
                typedef derive_pointer_info<param_type>::base_type param_base_type;
                constexpr native_type_info type_info = native_type_info_v<param_base_type>;
                static_assert(!type_info.name.empty(), "Undefined type");
                constexpr size_t pointer_depth = derive_pointer_depth_v<param_type>;
                *result++ = parameter(type_info, offset, pointer_depth);
                offset += pointer_depth == 0 ? type_info.size : sizeof(void*);
                method_signature_param<param_t...>::generate_signature(result, offset);
            }
            template<size_t idx, typename... tuple_args> static inline void read_argument(std::tuple<tuple_args...>& tup, const void*& data) noexcept
            {
                std::get<idx>(tup) = *reinterpret_cast<const value_t*&>(data)++;
                method_signature_param<param_t...>::template read_argument<idx + 1>(tup, data);
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
            static inline void invoke(void* ret_val, const void* param, retval_t(*call)(param_t...))
            {
                std::tuple<std::decay_t<param_t>...> tup;

                method_signature_param<param_t...>::template read_argument<0>(tup, param);

                *reinterpret_cast<retval_t*>(ret_val) = expand(call, tup);
            }
        };
        template<typename... param_t> class method_invoke<void, param_t...>
        {
        public:
            static inline void invoke(void* ret_val, const void* param, void(*call)(param_t...))
            {
                std::tuple<std::decay_t<param_t>...> tup;

                method_signature_param<param_t...>::template read_argument<0>(tup, param);

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
        typedef void(*forward_method)(void(*)(), void*, const void*);

    private:
        external_call(std::string_view name);

        template<typename retval_t, typename... param_t> static void bind_method(external_call& call, retval_t(*method)(param_t...))
        {
            struct bind
            {
                bind()
                {
                    parameters_size = 0;
                    native::method_signature_param<param_t...>::generate_signature(parameters, parameters_size);
                }

                static void forward_call(void(*handle)(), void* ret_val, const void* param)
                {
                    const auto method_ptr = (retval_t(*)(param_t...))(handle);
                    native::method_invoke<retval_t, param_t...>::invoke(ret_val, param, method_ptr);
                }

                // Array size cannot be 0
                native::parameter parameters[sizeof...(param_t) == 0 ? 1 : sizeof...(param_t)];
                size_t parameters_size;
            };

            static const bind instance;

            typedef native::decay_base_t<retval_t> return_type;
            typedef derive_pointer_info<return_type>::base_type retval_base_type;
            constexpr native_type_info type_info = native_type_info_v<retval_base_type>;
            static_assert(!type_info.name.empty(), "Undefined type");
            constexpr size_t pointer_depth = derive_pointer_depth_v<return_type>;

            call.forward = bind::forward_call;
            call.return_type = native::typedecl(type_info, pointer_depth);
            call.parameters = std::span<const native::parameter>(instance.parameters, sizeof...(param_t));
            call.parameters_size = instance.parameters_size;
            call.handle = (void(*)())(method);
        }

        friend class library;

        std::string_view name;
        forward_method forward = nullptr;
        native::typedecl return_type;
        std::span<const native::parameter> parameters;
        size_t parameters_size = 0;
        void(*handle)() = nullptr;

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
    // Symbols are loaded lazily at runtime unless specified otherwise.
    class library : public handle<class library_data, sizeof(size_t) * 16>
    {
    public:
        library(std::string_view path, bool preload_symbols, std::span<const external_call> calls);
        library(std::string_view path, bool preload_symbols, std::initializer_list<external_call> calls) :
            library(path, preload_symbols, init_span(calls)) {}
        ~library();

    private:
        friend class environment;
    };
}

#endif