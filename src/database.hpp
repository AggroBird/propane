#ifndef _HEADER_STRING_TABLE
#define _HEADER_STRING_TABLE

#include "common.hpp"
#include "propane_block.hpp"
#include "block_writer.hpp"

namespace propane
{
    template<typename key_t, typename value_t> struct find_result
    {
        find_result(key_t key = key_t(invalid_index), value_t* value = nullptr) :
            key(key),
            value(value) {}

        key_t key;

        inline operator bool() const
        {
            return value != nullptr;
        }
        inline value_t& operator*()
        {
            return *value;
        }
        inline const value_t& operator*() const
        {
            return *value;
        }
        inline value_t* operator->()
        {
            return value;
        }
        inline const value_t* operator->() const
        {
            return value;
        }

    private:
        value_t* value;
    };

    template<typename key_t, typename value_t, bool is_const> struct invalid_result
    {
        static constexpr find_result<key_t, value_t> make() { return find_result<key_t, value_t>(key_t(invalid_index), nullptr); }
    };
    template<typename key_t, typename value_t> struct invalid_result<key_t, value_t, true>
    {
        static constexpr find_result<key_t, const value_t> make() { return find_result<key_t, const value_t>(key_t(invalid_index), nullptr); }
    };
    template<typename key_t> struct invalid_result<key_t, void, true> { static constexpr key_t make() { return key_t(invalid_index); } };
    template<typename key_t> struct invalid_result<key_t, void, false> { static constexpr key_t make() { return key_t(invalid_index); } };

    template<typename key_t, typename value_t> struct database_content
    {
        using find_result_type = find_result<key_t, value_t>;
        using const_find_result_type = find_result<key_t, const value_t>;

        struct name_pair : public find_result_type
        {
            name_pair() :
                find_result_type(invalid_result<key_t, value_t, false>::make()) {}
            name_pair(string_view name, const find_result_type& f) :
                find_result_type(f),
                name(name) {}

            string_view name;
        };

        struct const_name_pair : public const_find_result_type
        {
            const_name_pair() :
                const_find_result_type(invalid_result<key_t, value_t, true>::make()) {}
            const_name_pair(string_view name, const const_find_result_type& f) :
                const_find_result_type(f),
                name(name) {}

            string_view name;
        };

        database_content() = default;
        template <typename... arg_t> database_content(key_t key, arg_t&&... arg) :
            key(key),
            value(std::forward<arg_t>(arg)...) {}

        key_t key;
        value_t value;

        find_result_type make_result() { return find_result_type(key, &value); }
        const_find_result_type make_result() const { return const_find_result_type(key, &value); }
    };

    template<typename key_t> struct database_content<key_t, void>
    {
        using find_result_type = key_t;
        using const_find_result_type = key_t;

        struct name_pair
        {
            name_pair() :
                key(key_t(invalid_index)) {}
            name_pair(string_view name, key_t key) :
                key(key),
                name(name) {}

            string_view name;
            key_t key;
        };

        struct const_name_pair
        {
            const_name_pair() :
                key(key_t(invalid_index)) {}
            const_name_pair(string_view name, key_t key) :
                key(key),
                name(name) {}

            string_view name;
            key_t key;
        };

        database_content() = default;
        database_content(key_t key) :
            key(key) {}

        key_t key;

        key_t make_result() { return key; }
        key_t make_result() const { return key; }
    };

    template<typename key_t, typename value_t> struct database_entry : public string_offset
    {
        database_entry() = default;

        template<typename... arg_t> database_entry(index_t offset, index_t length, key_t key, arg_t&&... arg) :
            string_offset(offset, length),
            value(key, std::forward<arg_t>(arg)...) {}

        database_content<key_t, value_t> value;
    };

    template<typename key_t, typename value_t> struct static_database
    {
        using entry_type = database_entry<key_t, value_t>;
        using content_type = database_content<key_t, value_t>;
        using const_name_pair_type = typename content_type::const_name_pair;

        static_block<entry_type> entries;
        static_block<char> strings;

        inline bool empty() const
        {
            return entries.empty();
        }
        inline size_t size() const
        {
            return entries.size();
        }

        inline const_name_pair_type operator[](key_t key) const
        {
            const size_t idx = size_t(key);
            if (idx < entries.size())
            {
                auto& val = entries[idx];
                string_view name_str = string_view(strings.data() + val.offset, val.length);
                return const_name_pair_type(name_str, val.value.make_result());
            }
            return const_name_pair_type();
        }
    };

    template<typename key_t, typename value_t> class database
    {
    public:
        using entry_type = database_entry<key_t, value_t>;
        using content_type = database_content<key_t, value_t>;
        using find_result_type = typename content_type::find_result_type;
        using const_find_result_type = typename content_type::const_find_result_type;
        using name_pair_type = typename content_type::name_pair;
        using const_name_pair_type = typename content_type::const_name_pair;

        database() = default;

        database(const database& other) = delete;
        database& operator=(const database& other) = delete;

        database(database&& other) noexcept
        {
            take_from(std::move(other));
        }
        database& operator=(database&& other) noexcept
        {
            if (&other != this)
            {
                take_from(std::move(other));
            }
            return *this;
        }


        inline find_result_type operator[](string_view name)
        {
            return emplace(name);
        }

        inline name_pair_type operator[](key_t key) noexcept
        {
            if (is_valid_index(key))
            {
                auto& val = entries[size_t(key)];
                string_view name_str = string_view(strings.data() + val.offset, val.length);
                return name_pair_type(name_str, val.value.make_result());
            }
            return name_pair_type();
        }
        inline const_name_pair_type operator[](key_t key) const noexcept
        {
            if (is_valid_index(key))
            {
                auto& val = entries[size_t(key)];
                string_view name_str = string_view(strings.data() + val.offset, val.length);
                return const_name_pair_type(name_str, val.value.make_result());
            }
            return const_name_pair_type();
        }

        template <typename... arg_t> inline find_result_type emplace(string_view name, arg_t&&... arg)
        {
            const char* ptr = name.data();
            const database_string_view view(&ptr, static_cast<index_t>(name.size()));

            auto find = lookup.find(view);
            if (find == lookup.end())
            {
                const size_t idx = entries.size();
                const index_t offset = static_cast<index_t>(strings.size());
                const index_t length = static_cast<index_t>(name.size());

                // Insert entry
                entries.push_back(entry_type(offset, length, key_t(idx), std::forward<arg_t>(arg)...));
                // Insert string and update pointer (in case it changed)
                strings.insert(strings.end(), name.begin(), name.end());
                string_data = strings.data();
                // Insert lookup
                lookup.emplace(database_string_view(&string_data, offset, length), key_t(idx));

                return entries.back().value.make_result();
            }

            auto& replace = entries[size_t(find->second)];
            replace = entry_type(replace.offset, replace.length, replace.value.key, std::forward<arg_t>(arg)...);
            return replace.value.make_result();
        }

        inline find_result_type find(string_view name) noexcept
        {
            const char* ptr = name.data();
            const database_string_view view(&ptr, static_cast<index_t>(name.size()));

            auto find = lookup.find(view);
            if (find == lookup.end())
            {
                return invalid_result<key_t, value_t, false>::make();
            }

            return entries[size_t(find->second)].value.make_result();
        }
        inline const_find_result_type find(string_view name) const noexcept
        {
            const char* ptr = name.data();
            const database_string_view view(&ptr, static_cast<index_t>(name.size()));

            auto find = lookup.find(view);
            if (find == lookup.end())
            {
                return invalid_result<key_t, value_t, true>::make();
            }

            return entries[size_t(find->second)].value.make_result();
        }

        inline void clear() noexcept
        {
            lookup.clear();
            entries.clear();
            strings.clear();
        }

        inline bool empty() const noexcept
        {
            return entries.empty();
        }
        inline size_t size() const noexcept
        {
            return entries.size();
        }
        inline bool is_valid_index(key_t key) const noexcept
        {
            return size_t(key) < entries.size();
        }

        inline void serialize_database(block_writer& writer) const
        {
            auto& write_entries = writer.write_deferred();
            write_entries.write_direct(entries.data(), static_cast<index_t>(entries.size()));
            write_entries.increment_length(static_cast<index_t>(entries.size()));

            auto& write_strings = writer.write_deferred();
            write_strings.write_direct(strings.data(), static_cast<index_t>(strings.size()));
            write_strings.increment_length(static_cast<index_t>(strings.size()));
        }
        inline void deserialize_database(const static_database<key_t, value_t>& t)
        {
            strings.clear();
            strings.insert(strings.end(), t.strings.begin(), t.strings.end());
            string_data = strings.data();

            entries.clear();
            entries.insert(entries.end(), t.entries.begin(), t.entries.end());

            // Recreate the lookup table
            lookup.clear();
            for (size_t idx = 0; idx < entries.size(); idx++)
            {
                const auto& entry = entries[idx];
                lookup.emplace(database_string_view(&string_data, entry.offset, entry.length), key_t(idx));
            }
        }

        inline void serialize_string_table(block_writer& writer) const
        {
            auto& write_entries = writer.write_deferred();
            for (auto& it : entries)
            {
                const string_offset& str_offset = it;
                write_entries.write(str_offset);
            }
            write_entries.increment_length(static_cast<index_t>(entries.size()));

            auto& write_strings = writer.write_deferred();
            write_strings.write_direct(strings.data(), static_cast<index_t>(strings.size()));
            write_strings.increment_length(static_cast<index_t>(strings.size()));
        }

    private:
        void take_from(database&& other) noexcept
        {
            strings = std::move(other.strings);
            string_data = strings.data();
            entries = std::move(other.entries);

            // Move the lookups and update the pointer to the string data
            // (use extract to prevent reallocation)
            lookup.clear();
            while (!other.lookup.empty())
            {
                auto pair = other.lookup.extract(other.lookup.begin());
                pair.key().ptr = &string_data;
                lookup.insert(std::move(pair));
            }
        }

        class database_string_view
        {
        public:
            database_string_view() = default;
            database_string_view(const char** ptr, index_t length) :
                offset(0, length),
                ptr(ptr) {}
            database_string_view(const char** ptr, index_t offset, index_t length) :
                offset(offset, length),
                ptr(ptr) {}

            string_offset offset = string_offset(0, 0);
            const char** ptr = nullptr;
        };

        struct database_hash
        {
            inline size_t operator()(const database_string_view& str) const
            {
                return fnv::hash(*str.ptr + str.offset.offset, str.offset.length);
            }
        };

        struct database_compare
        {
            inline bool operator()(const database_string_view& lhs, const database_string_view& rhs) const
            {
                if (lhs.offset.length == rhs.offset.length)
                {
                    return memcmp(*lhs.ptr + lhs.offset.offset, *rhs.ptr + rhs.offset.offset, lhs.offset.length) == 0;
                }
                return false;
            }
        };

        string strings;
        const char* string_data = nullptr;
        vector<entry_type> entries;
        unordered_map<database_string_view, key_t, database_hash, database_compare> lookup;
    };
}

#endif