#ifndef _HEADER_SERIALIZABLE
#define _HEADER_SERIALIZABLE

#include "propane_block.hpp"
#include "block_writer.hpp"
#include "common.hpp"

#include <vector>
#include <map>
#include <string_view>
#include <type_traits>

namespace propane
{
    namespace serialization
    {
        // Get elements
        template<typename... elem_t> struct serializable_elements { static constexpr size_t count = sizeof...(elem_t); };
        template<typename value_t> struct get_serializable_elements { using type = serializable_elements<>; };

        // Is packed
        template<typename value_t> struct is_packed
        {
            static constexpr bool value = std::is_arithmetic_v<value_t> || std::is_enum_v<value_t>;
        };
        template<typename value_t> constexpr bool is_packed_v = is_packed<value_t>::value;

        // Is serializable
        template<typename value_t> struct is_serializable
        {
            static constexpr bool value = is_packed_v<value_t>;
        };
        template<> struct is_serializable<std::string> { static constexpr bool value = true; };
        template<> struct is_serializable<std::string_view> { static constexpr bool value = true; };
        template<typename value_t> struct is_serializable<block<value_t>> { static constexpr bool value = is_serializable<value_t>::value; };
        template<typename value_t> struct is_serializable<std::vector<value_t>> { static constexpr bool value = is_serializable<value_t>::value; };
        template<typename key_t, typename value_t> struct is_serializable<std::map<key_t, value_t>> { static constexpr bool value = is_serializable<key_t>::value && is_serializable<value_t>::value; };
        template<typename key_t, typename value_t> struct is_serializable<indexed_vector<key_t, value_t>> { static constexpr bool value = is_serializable<value_t>::value; };
        template<typename value_t> constexpr bool is_serializable_v = is_serializable<value_t>::value;

        // Compatibility
        template<typename lhs_t, typename rhs_t> struct is_serialization_compatible
        {
            static constexpr bool value = std::is_same_v<lhs_t, rhs_t> || (is_serializable_v<lhs_t> && is_serializable_v<rhs_t>);
        };
        template<> struct is_serialization_compatible<std::string, static_string> { static constexpr bool value = true; };
        template<> struct is_serialization_compatible<std::string_view, static_string> { static constexpr bool value = true; };
        template<typename lhs_t, typename rhs_t> struct is_serialization_compatible<block<lhs_t>, static_block<rhs_t>>
        {
            static constexpr bool value = is_serialization_compatible<lhs_t, rhs_t>::value;
        };
        template<typename lhs_t, typename rhs_t> struct is_serialization_compatible<std::vector<lhs_t>, static_block<rhs_t>>
        {
            static constexpr bool value = is_serialization_compatible<lhs_t, rhs_t>::value;
        };
        template<typename lhs_key_t, typename lhs_value_t, typename rhs_key_t, typename rhs_value_t> struct is_serialization_compatible<std::map<lhs_key_t, lhs_value_t>, static_lookup_block<rhs_key_t, rhs_value_t>>
        {
            static constexpr bool value = is_serialization_compatible<lhs_key_t, rhs_key_t>::value || is_serialization_compatible<lhs_value_t, rhs_value_t>::value;
        };
        template<typename lhs_key_t, typename lhs_value_t, typename rhs_key_t, typename rhs_value_t> struct is_serialization_compatible<indexed_vector<lhs_key_t, lhs_value_t>, indexed_static_block<rhs_key_t, rhs_value_t>>
        {
            static constexpr bool value = is_serialization_compatible<lhs_value_t, rhs_value_t>::value;
        };
        template<typename lhs_t, typename rhs_t> constexpr bool is_serialization_compatible_v = is_serialization_compatible<lhs_t, rhs_t>::value;

        // Serializable size check
        template<typename value_t, typename... rest_t> struct serializable_size_check_recursive;
        template<typename value_t> struct serializable_size_check_recursive<value_t>
        {
            static constexpr size_t size = sizeof(value_t);
        };
        template<typename value_t, typename... rest_t> struct serializable_size_check_recursive
        {
            static constexpr size_t size = sizeof(value_t) + serializable_size_check_recursive<rest_t...>::size;
        };
        template<typename value_t, typename... rest_t> struct serializable_size_check
        {
            static constexpr size_t member_size = serializable_size_check_recursive<rest_t...>::size;
            static constexpr size_t object_size = sizeof(value_t);
            static constexpr bool value = member_size == object_size;
        };
        template<typename value_t, typename... rest_t> constexpr bool serializable_size_check_v = serializable_size_check<value_t, rest_t...>::value;

        // Serializable alignment check
        template<typename value_t> constexpr bool serializable_alignment_check_v = alignof(value_t) == alignof(uint32_t);

        // Get type at idx
        template<typename> constexpr bool false_condition = false;
        template<size_t idx, typename elements> struct serializable_element;
        template<size_t idx> struct serializable_element<idx, serializable_elements<>>
        {
            static_assert(false_condition<std::integral_constant<size_t, idx>>, "index out of bounds");
        };
        template<typename value_t, typename... rest_t> struct serializable_element<0, serializable_elements<value_t, rest_t...>>
        {
            using type = value_t;
        };
        template<size_t idx, typename value_t, typename... rest_t> struct serializable_element<idx, serializable_elements<value_t, rest_t...>> : public serializable_element<idx - 1, serializable_elements<rest_t...>>
        {

        };

        // Compatibility check
        template<typename elem_t, size_t idx, typename value_t, typename... rest_t> struct check_serialization_compatible_recursive;
        template<typename elem_t, size_t idx, typename value_t> struct check_serialization_compatible_recursive<elem_t, idx, value_t>
        {
            using elements = typename get_serializable_elements<elem_t>::type;
            using element = typename serializable_element<idx, elements>::type;

            static constexpr bool value = is_serialization_compatible_v<element, value_t>;
        };
        template<typename elem_t, size_t idx, typename value_t, typename... rest_t> struct check_serialization_compatible_recursive
        {
            using elements = typename get_serializable_elements<elem_t>::type;
            using element = typename serializable_element<idx, elements>::type;

            static constexpr bool value = is_serialization_compatible_v<element, value_t> && check_serialization_compatible_recursive<elem_t, idx + 1, rest_t...>::value;
        };

        template<typename elem_t, typename... rest_t> struct check_serialization_compatible
        {
            static constexpr bool value = check_serialization_compatible_recursive<elem_t, 0, rest_t...>::value;
        };
        template<typename value_t, typename... rest_t> constexpr bool check_serialization_compatible_v = check_serialization_compatible<value_t, rest_t...>::value;

        // Serialization check
        template<typename value_t, typename... rest_t> struct serializable_check_recursive;
        template<typename value_t> struct serializable_check_recursive<value_t>
        {
            static constexpr bool value = is_serializable_v<value_t>;
        };
        template<typename value_t, typename... rest_t> struct serializable_check_recursive
        {
            static constexpr bool value = is_serializable_v<value_t> && serializable_check_recursive<rest_t...>::value;
        };
        template<typename value_t, typename... rest_t> struct serializable_check
        {
            static constexpr bool value = serializable_check_recursive<rest_t...>::value;
        };
        template<typename value_t, typename... rest_t> constexpr bool serializable_check_v = serializable_check<value_t, rest_t...>::value;

        template<typename value_t> struct serializer;

        template<typename dst_t> inline const dst_t& read_data(const void*& data) noexcept
        {
            return *reinterpret_cast<const dst_t*&>(data)++;
        }

        // Write binary
        template<typename value_t, bool compact> struct serialize_binary
        {
            inline static void write(block_writer& writer, const value_t& value)
            {
                static_assert(is_serializable_v<value_t>, "Type is not serializable");

                serializer<value_t>::write(writer, value);
            }
            inline static void write(block_writer& writer, const value_t* ptr, size_t count)
            {
                static_assert(is_serializable_v<value_t>, "Type is not serializable");

                for (size_t i = 0; i < count; i++)
                {
                    serializer<value_t>::write(writer, ptr[i]);
                }
            }
            inline static void read(const void*& data, value_t& value)
            {
                static_assert(is_serializable_v<value_t>, "Type is not serializable");

                serializer<value_t>::read(data, value);
            }
            inline static void read(const void*& data, value_t* ptr, size_t count)
            {
                static_assert(is_serializable_v<value_t>, "Type is not serializable");

                for (size_t i = 0; i < count; i++)
                {
                    serializer<value_t>::read(data, ptr[i]);
                }
            }
        };
        template<typename value_t> struct serialize_binary<value_t, true>
        {
            inline static void write(block_writer& writer, const value_t& value)
            {
                static_assert(std::is_trivially_copyable_v<value_t>, "type is not trivially copyable");

                writer.write_direct(value);
            }
            inline static void write(block_writer& writer, const value_t* ptr, size_t count)
            {
                static_assert(std::is_trivially_copyable_v<value_t>, "type is not trivially copyable");

                writer.write_direct(ptr, static_cast<uint32_t>(count));
            }
            inline static void read(const void*& data, value_t& value)
            {
                static_assert(std::is_trivially_copyable_v<value_t>, "type is not trivially copyable");

                value = *reinterpret_cast<const value_t*&>(data)++;
            }
            inline static void read(const void*& data, value_t* ptr, size_t count)
            {
                static_assert(std::is_trivially_copyable_v<value_t>, "type is not trivially copyable");

                memcpy(ptr, data, count * sizeof(value_t));
                reinterpret_cast<const value_t*&>(data) += count;
            }
        };

        // Impl (override this for specific implementation)
        template<typename value_t> struct serializer
        {
            inline static void write(block_writer& writer, const value_t& value)
            {
                static_assert(serialization::is_serializable_v<value_t>, "type is not serializable");

                serialize_binary<value_t, is_packed_v<value_t>>::write(writer, value);
            }
            inline static void read(const void*& data, value_t& value)
            {
                serialize_binary<value_t, is_packed_v<value_t>>::read(data, value);
            }
        };

        // Vector
        template<typename value_t> struct serializer<std::vector<value_t>>
        {
            inline static void write(block_writer& writer, const std::vector<value_t>& value)
            {
                auto& w = writer.write_deferred();
                serialize_binary<value_t, is_packed_v<value_t>>::write(w, value.data(), value.size());
                w.increment_length(static_cast<uint32_t>(value.size()));
            }
            inline static void read(const void*& data, std::vector<value_t>& value)
            {
                const auto& r = read_data<static_block<value_t>>(data);
                value.resize(r.size());
                const void* ptr = r.data();
                serialize_binary<value_t, is_packed_v<value_t>>::read(ptr, value.data(), value.size());
            }
        };

        // Block
        template<typename value_t> struct serializer<block<value_t>>
        {
            inline static void write(block_writer& writer, const block<value_t>& value)
            {
                auto& w = writer.write_deferred();
                serialize_binary<value_t, is_packed_v<value_t>>::write(w, value.data(), value.size());
                w.increment_length(static_cast<uint32_t>(value.size()));
            }
            inline static void read(const void*& data, block<value_t>& value)
            {
                const auto& r = read_data<static_block<value_t>>(data);
                value = block<value_t>(r.size());
                const void* ptr = r.data();
                serialize_binary<value_t, is_packed_v<value_t>>::read(ptr, value.data(), value.size());
            }
        };

        // Map
        template<typename key_t, typename value_t> struct serializer<std::map<key_t, value_t>>
        {
            inline static void write(block_writer& writer, const std::map<key_t, value_t>& value)
            {
                auto& w = writer.write_deferred();
                for (auto& it : value)
                {
                    serializer<key_t>::write(w, it.first);
                    serializer<value_t>::write(w, it.second);
                }
                w.increment_length(static_cast<uint32_t>(value.size()));
            }
            inline static void read(const void*& data, std::map<key_t, value_t>& value)
            {
                std::terminate();
            }
        };

        // Indexed vector
        template<typename key_t, typename value_t> struct serializer<indexed_vector<key_t, value_t>>
        {
            inline static void write(block_writer& writer, const indexed_vector<key_t, value_t>& value)
            {
                auto& w = writer.write_deferred();
                serialize_binary<value_t, is_packed_v<value_t>>::write(w, value.data(), value.size());
                w.increment_length(static_cast<uint32_t>(value.size()));
            }
            inline static void read(const void*& data, indexed_vector<key_t, value_t>& value)
            {
                const auto& r = read_data<indexed_static_block<key_t, value_t>>(data);
                value.resize(r.size());
                const void* ptr = r.data();
                serialize_binary<value_t, is_packed_v<value_t>>::read(ptr, value.data(), value.size());
            }
        };

        // Recursive (for the macro)
        template<typename value_t, typename... elem_t> struct serialize_recursive;
        template<typename value_t> struct serialize_recursive<value_t>
        {
            inline static void write(block_writer& writer, const value_t& value)
            {
                serializer<value_t>::write(writer, value);
            }
            inline static void read(const void*& data, value_t& value)
            {
                serializer<value_t>::read(data, value);
            }
        };
        template<typename value_t, typename... elem_t> struct serialize_recursive
        {
            inline static void write(block_writer& writer, const value_t& value, const elem_t&... elem)
            {
                serializer<value_t>::write(writer, value);

                serialize_recursive<elem_t...>::write(writer, elem...);
            }
            inline static void read(const void*& data, value_t& value, elem_t&... elem)
            {
                serializer<value_t>::read(data, value);

                serialize_recursive<elem_t...>::read(data, elem...);
            }
        };
    }
}


#define _SER_EVAL(...) __VA_ARGS__
#define _SER_VARCOUNT_IMPL(_,_15,_14,_13,_12,_11,_10,_9,_8,_7,_6,_5,_4,_3,_2,X_,...) X_
#define _SER_VARCOUNT(...) _SER_EVAL(_SER_VARCOUNT_IMPL(__VA_ARGS__,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,))
#define _SER_COMBINE_IMPL(x, y) x##y
#define _SER_COMBINE(x, y) _SER_COMBINE_IMPL(x,y)
#define _SER_FIRST_IMPL(x, ...) x
#define _SER_FIRST(...) _SER_EVAL(_SER_FIRST_IMPL(__VA_ARGS__,))
#define _SER_TUPLE_TAIL_IMPL(x, ...) (__VA_ARGS__)
#define _SER_TUPLE_TAIL(...) _SER_EVAL(_SER_TUPLE_TAIL_IMPL(__VA_ARGS__))

#define _SER_TRANSFORM(t, v, args) (_SER_COMBINE(_SER_TRANSFORM_, _SER_VARCOUNT args)(t, v, args))
#define _SER_TRANSFORM_1(t, v, args) v(t, _SER_FIRST args)
#define _SER_TRANSFORM_2(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_1(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_3(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_2(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_4(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_3(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_5(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_4(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_6(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_5(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_7(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_6(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_8(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_7(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_9(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_8(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_10(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_9(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_11(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_10(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_12(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_11(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_13(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_12(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_14(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_13(t, v, _SER_TUPLE_TAIL args)
#define _SER_TRANSFORM_15(t, v, args) v(t, _SER_FIRST args), _SER_TRANSFORM_14(t, v, _SER_TUPLE_TAIL args)

#define _SER_MAKE_INIT(...) __VA_ARGS__

#define _SER_MAKE_PARAM(t, x) decltype(t::x)
#define _SER_MAKE_PARAMS(t, ...) _SER_EVAL(_SER_MAKE_INIT _SER_TRANSFORM(t, _SER_MAKE_PARAM, (__VA_ARGS__)))

#define _SER_MAKE_ARG(t, x) const decltype(t::x)& x
#define _SER_MAKE_ARGS(t, ...) _SER_EVAL(_SER_MAKE_INIT _SER_TRANSFORM(t, _SER_MAKE_ARG, (__VA_ARGS__)))

#define _SER_MAKE_FWD(i, x) i.x
#define _SER_MAKE_FORWARD(i, ...) _SER_EVAL(_SER_MAKE_INIT _SER_TRANSFORM(i, _SER_MAKE_FWD, (__VA_ARGS__)))

#define SERIALIZABLE_CHECK(src_t, ...) \
static_assert(std::is_trivially_default_constructible_v<src_t>, #src_t " is not serializable: type is not trivially default constructible"); \
static_assert(propane::serialization::serializable_size_check_v<src_t, _SER_MAKE_PARAMS(src_t, __VA_ARGS__)>, #src_t " is not serializable: object contains padding"); \
static_assert(propane::serialization::serializable_alignment_check_v<src_t>, #src_t " is not serializable: object is not 32 bit aligned"); \
template<> struct propane::serialization::is_packed<src_t> { static constexpr bool value = true; }; \
template<> struct propane::serialization::is_serializable<src_t> { static constexpr bool value = true; }

#define SERIALIZABLE(src_t, ...) \
SERIALIZABLE_CHECK(src_t, __VA_ARGS__); \
template<> struct propane::serialization::serializer<src_t> \
{ \
    inline static void write(propane::block_writer& writer, const src_t& value) \
    { \
        writer.write_direct(value); \
    } \
    inline static void read(const void*& data, src_t& value) \
    { \
        value = *reinterpret_cast<const src_t*&>(data)++; \
    } \
}

#define SERIALIZABLE_PAIR(src_t, dst_t, ...) \
SERIALIZABLE_CHECK(dst_t, __VA_ARGS__); \
static_assert(propane::serialization::serializable_check_v<src_t, _SER_MAKE_PARAMS(src_t, __VA_ARGS__)>, #src_t " is not serializable: object contains members that are not serializable"); \
template<> struct propane::serialization::get_serializable_elements<src_t> { using type = propane::serialization::serializable_elements<_SER_MAKE_PARAMS(src_t, __VA_ARGS__)>; }; \
static_assert(propane::serialization::check_serialization_compatible_v<src_t, _SER_MAKE_PARAMS(dst_t, __VA_ARGS__)>, #dst_t " is not serializable: destination type is not compatible"); \
template<> struct propane::serialization::is_serializable<src_t> { static constexpr bool value = true; }; \
template<> struct propane::serialization::is_serialization_compatible<src_t, dst_t> { static constexpr bool value = true; }; \
template<> struct propane::serialization::serializer<src_t> \
{ \
    inline static void write(propane::block_writer& writer, const src_t& value) \
    { \
        propane::serialization::serialize_recursive<_SER_MAKE_PARAMS(src_t, __VA_ARGS__)>::write(writer, _SER_MAKE_FORWARD(value, __VA_ARGS__)); \
    } \
    inline static void read(const void*& data, src_t& value) \
    { \
        propane::serialization::serialize_recursive<_SER_MAKE_PARAMS(src_t, __VA_ARGS__)>::read(data, _SER_MAKE_FORWARD(value, __VA_ARGS__)); \
    } \
    inline static void read(const dst_t& data, src_t& value) \
    { \
            const void* ptr = &data; \
            propane::serialization::serialize_recursive<_SER_MAKE_PARAMS(src_t, __VA_ARGS__)>::read(ptr, _SER_MAKE_FORWARD(value, __VA_ARGS__)); \
    } \
}

#define CUSTOM_SERIALIZER(src_t, dst_t) \
template<> struct propane::serialization::is_serializable<src_t> { static constexpr bool value = true; }; \
template<> struct propane::serialization::is_serialization_compatible<src_t, dst_t> { static constexpr bool value = true; }; \
template<> struct propane::serialization::serializer<src_t>

#endif