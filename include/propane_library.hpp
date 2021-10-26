#ifndef _HEADER_PROPANE_LIBRARY
#define _HEADER_PROPANE_LIBRARY

#include "propane_runtime.hpp"

#include <tuple>

namespace propane
{
    struct native_type_info
    {
        native_type_info() = default;
        native_type_info(std::string_view type, size_t size, size_t pointer) :
            type(type),
            size(size),
            pointer(pointer) {}

        std::string_view type;
        size_t size = 0;
        size_t pointer = 0;
    };

    // Specializing this template allows for binding native structs to the runtime,
    // so they can be used as parameters in native library calls.
    // Since the runtime has no notion of padding, extra caution needs to be taken to ensure
    // the structs are properly packed have the same layout in both runtime and native environments.
    template<typename value_t> concept standard_layout_t = std::is_standard_layout_v<value_t> || std::is_same_v<value_t, void>;
    template<standard_layout_t value_t> constexpr std::string_view native_type_name_v = std::string_view();
    template<> constexpr std::string_view native_type_name_v<int8_t> = "byte";
    template<> constexpr std::string_view native_type_name_v<uint8_t> = "ubyte";
    template<> constexpr std::string_view native_type_name_v<int16_t> = "short";
    template<> constexpr std::string_view native_type_name_v<uint16_t> = "ushort";
    template<> constexpr std::string_view native_type_name_v<int32_t> = "int";
    template<> constexpr std::string_view native_type_name_v<uint32_t> = "uint";
    template<> constexpr std::string_view native_type_name_v<int64_t> = "long";
    template<> constexpr std::string_view native_type_name_v<uint64_t> = "ulong";
    template<> constexpr std::string_view native_type_name_v<float> = "float";
    template<> constexpr std::string_view native_type_name_v<double> = "double";
    template<> constexpr std::string_view native_type_name_v<void> = "void";

    namespace native
    {
        // Get type size
        template<typename value_t> constexpr size_t type_size_v = sizeof(value_t);
        template<> constexpr size_t type_size_v<void> = 0;

        // Get pointer info
        template<typename value_t> struct pointer_info
        {
            typedef value_t base_type;
            static constexpr size_t value = 0;
        };
        template<typename value_t> struct pointer_info<value_t*> : public pointer_info<value_t>
        {
            static constexpr size_t value = pointer_info<value_t>::value + 1;
        };

        // Parameter info
        struct parameter : native_type_info
        {
            parameter() = default;
            parameter(std::string_view type, size_t size, size_t pointer, size_t offset) :
                native_type_info(type, size, pointer),
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
            static inline void generate(parameter* result, size_t& offset) noexcept
            {

            }
            template<size_t idx, typename... tuple_args> static inline void read_value(std::tuple<tuple_args...>& tup, const void*& data) noexcept
            {

            }
        };
        template<typename value_t, typename... param_t> class method_signature_param<value_t, param_t...> : public method_signature_param<param_t...>
        {
        public:
            static inline void generate(parameter* result, size_t& offset) noexcept
            {
                typedef decay_base_t<value_t> param_type;
                typedef pointer_info<param_type>::base_type base_type;
                constexpr std::string_view type = native_type_name_v<base_type>;
                static_assert(type.size() > 0, "Undefined type");
                constexpr size_t size = type_size_v<base_type>;
                constexpr size_t pointer = pointer_info<param_type>::value;
                *result++ = parameter(type, size, pointer, offset);
                offset += (pointer == 0) ? size : sizeof(void*);
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
                    native::method_signature_param<param_t...>::generate(parameters, parameters_size);
                }

                static void forward_call(void* handle, void* ret_val, const void* param) noexcept
                {
                    const auto method_ptr = reinterpret_cast<retval_t(*)(param_t...)>(handle);
                    native::method_invoke<retval_t, param_t...>::invoke(ret_val, param, method_ptr);
                }

                // Array size cannot be 0
                native::parameter parameters[sizeof...(param_t) == 0 ? 1 : sizeof...(param_t)];
                size_t parameters_size;
            };

            static const bind instance;

            typedef native::decay_base_t<retval_t> return_type;
            typedef native::pointer_info<return_type>::base_type base_type;
            constexpr std::string_view type = native_type_name_v<base_type>;
            static_assert(type.size() > 0, "Undefined type");
            constexpr size_t size = native::type_size_v<base_type>;
            constexpr size_t pointer = native::pointer_info<return_type>::value;

            call.forward = bind::forward_call;
            call.return_type = native_type_info(type, size, pointer);
            call.parameters = std::span<const native::parameter>(instance.parameters, sizeof...(param_t));
            call.parameters_size = instance.parameters_size;
            call.handle = method;
        }

        friend class library;

        std::string_view name;
        forward_method forward = nullptr;
        native_type_info return_type;
        std::span<const native::parameter> parameters;
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