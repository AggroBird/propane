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
		typedef find_result<key_t, value_t> find_result_type;
		typedef find_result<key_t, const value_t> const_find_result_type;

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
		typedef key_t find_result_type;
		typedef key_t const_find_result_type;

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

		template<typename... arg_t> database_entry(index_t offset, index_t length, hash_t hash, key_t key, arg_t&&... arg) :
			string_offset(offset, length),
			hash(hash),
			value(key, std::forward<arg_t>(arg)...) {}

		aligned_hash_t hash;
		database_content<key_t, value_t> value;
	};

	template<typename key_t, typename value_t> struct static_database
	{
		typedef database_entry<key_t, value_t> entry_type;
		typedef database_content<key_t, value_t> content_type;
		typedef typename content_type::const_name_pair const_name_pair_type;

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
		typedef database_entry<key_t, value_t> entry_type;
		typedef database_content<key_t, value_t> content_type;
		typedef typename content_type::find_result_type find_result_type;
		typedef typename content_type::const_find_result_type const_find_result_type;
		typedef typename content_type::name_pair name_pair_type;
		typedef typename content_type::const_name_pair const_name_pair_type;

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
			const hash_t hash = hash64(name.data(), name.size() * sizeof(char));

			auto find = lookup.find(hash);
			if (find == lookup.end())
			{
				const size_t idx = entries.size();
				entries.push_back(entry_type(index_t(strings.size()), index_t(name.size()), hash, key_t(idx), std::forward<arg_t>(arg)...));
				strings.insert(strings.end(), name.begin(), name.end());
				lookup.emplace(hash, key_t(idx));
				auto& value = entries[idx].value;
				return value.make_result();
			}

			auto& replace = entries[size_t(find->second)];
			replace = entry_type(replace.offset, replace.length, hash, replace.value.key, std::forward<arg_t>(arg)...);
			return replace.value.make_result();
		}

		inline find_result_type find(string_view name) noexcept
		{
			const hash_t hash = hash64(name.data(), name.size() * sizeof(char));

			auto find = lookup.find(hash);
			if (find == lookup.end())
			{
				return invalid_result<key_t, value_t, false>::make();
			}

			return entries[size_t(find->second)].value.make_result();
		}
		inline const_find_result_type find(string_view name) const noexcept
		{
			const hash_t hash = hash64(name.data(), name.size() * sizeof(char));

			auto find = lookup.find(hash);
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
			write_entries.write_direct(entries.data(), (index_t)entries.size());
			write_entries.increment_length((index_t)entries.size());

			auto& write_strings = writer.write_deferred();
			write_strings.write_direct(strings.data(), (index_t)strings.size());
			write_strings.increment_length((index_t)strings.size());
		}
		inline void deserialize_database(const static_database<key_t, value_t>& t)
		{
			entries.clear();
			entries.insert(entries.end(), t.entries.begin(), t.entries.end());

			lookup.clear();
			for (size_t i = 0; i < entries.size(); i++)
			{
				lookup.emplace(entries[i].hash, key_t(i));
			}

			strings.clear();
			strings.insert(strings.end(), t.strings.begin(), t.strings.end());
		}

		inline void serialize_string_table(block_writer& writer) const
		{
			auto& write_entries = writer.write_deferred();
			for (auto& it : entries)
			{
				const string_offset& str_offset = it;
				write_entries.write(str_offset);
			}
			write_entries.increment_length((index_t)entries.size());

			auto& write_strings = writer.write_deferred();
			write_strings.write_direct(strings.data(), (index_t)strings.size());
			write_strings.increment_length((index_t)strings.size());
		}

	private:
		unordered_map<hash_t, key_t> lookup;
		vector<entry_type> entries;
		string strings;
	};
}

#endif