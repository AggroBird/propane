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
        block_writer() : offset(0), element_count(0) {}
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
            append(reinterpret_cast<const uint8_t*>(&value), static_cast<uint32_t>(sizeof(value_t)));
        }
        template<typename value_t> inline void write_direct(const value_t* value_ptr, uint32_t count)
        {
            static_assert(std::is_trivially_copyable_v<value_t>, "Trivial type required");
            append(reinterpret_cast<const uint8_t*>(value_ptr), static_cast<uint32_t>(sizeof(value_t)) * count);
        }
        inline void write_direct(string_view str)
        {
            write_direct(str.data(), static_cast<uint32_t>(str.size()));
        }

        // Write deferred
        block_writer& write_deferred()
        {
            const uint32_t current_offset = static_cast<uint32_t>(binary.size());
            reserve(static_cast<uint32_t>(sizeof(uint32_t) * 2));
            block_writer* ptr = new block_writer(current_offset);
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
                uint32_t write_offset = static_cast<uint32_t>(binary.size());

                // Pad to ensure 32 bit alignment
                constexpr size_t alignment = sizeof(uint32_t);
                const uint32_t remaining = write_offset & (alignment - 1);
                if (remaining != 0)
                {
                    write_offset += (alignment - remaining);
                    binary.resize(write_offset);
                }

                const vector<uint8_t> cb = it->finalize();
                append(cb.data(), static_cast<uint32_t>(cb.size()));

                // Write header
                uint32_t* write = reinterpret_cast<uint32_t*>(binary.data() + it->offset);
                *write++ = (write_offset - it->offset);
                *write++ = it->element_count;
            }
            free_children();

            vector<uint8_t> result;
            swap(result, binary);
            return result;
        }

        const uint32_t offset;

        inline void increment_length(uint32_t count = 1)
        {
            element_count += count;
        }

    private:
        block_writer(uint32_t offset) : offset(offset), element_count(0) {}

        uint32_t element_count;

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