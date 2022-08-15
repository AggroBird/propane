#ifndef _HEADER_BLOCK_WRITER
#define _HEADER_BLOCK_WRITER

#include "common.hpp"

namespace propane
{
    namespace serialization
    {
        template<typename value_t> struct serializer;
    }

    class block_writer final
    {
    public:
        block_writer() : off(0), len(0) {}
        ~block_writer()
        {
            free_children();
        }

        block_writer(const block_writer&) = delete;
        block_writer& operator=(const block_writer&) = delete;

        // Write (by impl)
        template<typename value_t> inline void write(const value_t& value)
        {
            serialization::serializer<value_t>::write(*this, value);
        }

        // Write direct
        template<typename value_t> inline void write_direct(const value_t& value)
        {
            static_assert(std::is_trivially_copyable_v<value_t>, "Trivial type required");
            append(reinterpret_cast<const uint8_t*>(&value), uint32_t(sizeof(value_t)));
        }
        template<typename value_t> inline void write_direct(const value_t* value_ptr, uint32_t count)
        {
            static_assert(std::is_trivially_copyable_v<value_t>, "Trivial type required");
            append(reinterpret_cast<const uint8_t*>(value_ptr), uint32_t(sizeof(value_t)) * count);
        }
        inline void write_direct(string_view str)
        {
            write_direct(str.data(), uint32_t(str.size()));
        }

        // Write deferred
        block_writer& write_deferred()
        {
            const uint32_t offset = uint32_t(binary.size());
            reserve(uint32_t(sizeof(uint32_t) * 2));
            block_writer* ptr = new block_writer(offset);
            children.push_back(ptr);
            return *ptr;
        }

        // Combine data
        // All references to block_writers created by write_deferred will become invalid
        // after calling this.
        vector<uint8_t> finalize()
        {
            for (auto& it : children)
            {
                const uint32_t write_offset = uint32_t(binary.size());
                const vector<uint8_t> cb = it->finalize();
                append(cb.data(), uint32_t(cb.size()));

                // Write header
                uint32_t* write = reinterpret_cast<uint32_t*>(binary.data() + it->off);
                *write++ = (write_offset - it->off);
                *write++ = it->len;
            }
            free_children();

            vector<uint8_t> result;
            swap(result, binary);
            return result;
        }

        const uint32_t off;

        inline void increment_length(uint32_t count = 1)
        {
            len += count;
        }

    private:
        block_writer(uint32_t off) : off(off), len(0) {}

        uint32_t len;

        inline void append(const uint8_t* ptr, uint32_t length)
        {
            binary.insert(binary.end(), ptr, ptr + length);
        }

        inline void reserve(uint32_t length)
        {
            binary.resize(binary.size() + length);
        }

        void free_children()
        {
            for (auto it : children)
                delete it;
            children.clear();
        }

        vector<uint8_t> binary;
        vector<block_writer*> children;
    };
}

#endif