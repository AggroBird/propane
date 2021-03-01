#ifndef _HEADER_PROPANE_BLOCK
#define _HEADER_PROPANE_BLOCK

#include <algorithm>
#include <initializer_list>
#include <string_view>

namespace propane
{
	template<typename value_t, bool trivial> struct block_data
	{
		// non-trivial implementation
		// (objects have to be constructed and destructed properly)

		inline static void construct(value_t* dst, size_t length)
		{
			for (size_t i = 0; i < length; i++)
				new(&dst[i]) value_t();
		}
		inline static void deconstruct(const value_t* dst, size_t length) noexcept
		{
			for (size_t i = 0; i < length; i++)
				dst[i].~value_t();
		}
		inline static void copy(value_t* dst, const value_t* src, size_t length)
		{
			for (size_t i = 0; i < length; i++)
				new(&dst[i]) value_t(src[i]);
		}
	};
	template<typename value_t> struct block_data<value_t, true>
	{
		// trivial implementation
		// (can just be copied as binary)

		inline static void construct(value_t* dst, size_t length) noexcept
		{

		}
		inline static void deconstruct(const value_t* dst, size_t length) noexcept
		{

		}
		inline static void copy(value_t* dst, const value_t* src, size_t length) noexcept
		{
			std::memcpy(dst, src, sizeof(value_t) * length);
		}
	};

	// Block
	template<typename value_t> class block
	{
	public:
		using value_type = value_t;

		block() : ptr(nullptr), len(0)
		{

		}
		explicit block(size_t length)
		{
			construct(length);
		}
		block(std::initializer_list<value_t> init)
		{
			copy(init.begin(), init.size());
		}
		block(const value_t* data, size_t length)
		{
			copy(data, length);
		}
		~block()
		{
			deconstruct();
		}

		block(const block& other)
		{
			copy(other.ptr, other.len);
		}
		block& operator=(const block& other)
		{
			if (&other == this) return *this;

			deconstruct();

			copy(other.ptr, other.len);

			return *this;
		}
		block(block&& other) noexcept : block()
		{
			std::swap(ptr, other.ptr);
			std::swap(len, other.len);
		}
		block& operator=(block&& other) noexcept
		{
			if (&other == this) return *this;

			deconstruct();

			std::swap(ptr, other.ptr);
			std::swap(len, other.len);

			return *this;
		}

		inline size_t size() const noexcept
		{
			return len;
		}
		inline bool empty() const noexcept
		{
			return len == 0;
		}
		inline void clear()
		{
			deconstruct();
		}

		inline value_t& operator[](size_t idx) noexcept
		{
			return ptr[idx];
		}
		inline const value_t& operator[](size_t idx) const noexcept
		{
			return ptr[idx];
		}

		inline value_t* data() noexcept
		{
			return ptr;
		}
		inline const value_t* data() const noexcept
		{
			return ptr;
		}

		inline value_t* begin() noexcept
		{
			return ptr;
		}
		inline const value_t* begin() const noexcept
		{
			return ptr;
		}
		inline value_t* end() noexcept
		{
			return ptr + len;
		}
		inline const value_t* end() const noexcept
		{
			return ptr + len;
		}

	private:
		inline void construct(size_t length)
		{
			if (length)
			{
				len = length;
				ptr = alloc(len);
				block_data<value_t, std::is_trivially_default_constructible<value_t>::value>::construct(ptr, len);
			}
			else
			{
				ptr = nullptr;
				len = 0;
			}
		}
		inline void deconstruct()
		{
			if (ptr)
			{
				block_data<value_t, std::is_trivially_destructible<value_t>::value>::deconstruct(ptr, len);
				dealloc(ptr);
				ptr = nullptr;
			}
			len = 0;
		}
		inline void copy(const value_t* data, size_t length)
		{
			if (length)
			{
				len = length;
				ptr = alloc(len);
				block_data<value_t, std::is_trivially_copyable<value_t>::value>::copy(ptr, data, len);
			}
			else
			{
				ptr = nullptr;
				len = 0;
			}
		}

		inline value_t* alloc(size_t length)
		{
			return reinterpret_cast<value_t*>(operator new(sizeof(value_t) * length));
		}
		inline void dealloc(value_t* ptr) noexcept
		{
			operator delete(ptr);
		}

		value_t* ptr;
		size_t len;
	};

	// Static block
	template<typename value_t> struct static_block
	{
	public:
		using value_type = value_t;

		static_block() = default;

		static_block(const static_block&) = delete;
		static_block& operator=(const static_block&) = delete;
		static_block(static_block&&) = delete;
		static_block& operator=(static_block&&) = delete;

		static_block(uint32_t off, uint32_t len) : off(off), len(len) {}

		inline size_t size() const noexcept
		{
			return len;
		}
		inline bool empty() const noexcept
		{
			return len == 0;
		}

		inline value_t& operator[](size_t idx) noexcept
		{
			return data()[idx];
		}
		inline const value_t& operator[](size_t idx) const noexcept
		{
			return data()[idx];
		}

		inline value_t* data() noexcept
		{
			return reinterpret_cast<value_t*>(reinterpret_cast<uint8_t*>(this) + off);
		}
		inline const value_t* data() const noexcept
		{
			return reinterpret_cast<const value_t*>(reinterpret_cast<const uint8_t*>(this) + off);
		}

		inline value_t* begin() noexcept
		{
			return data();
		}
		inline const value_t* begin() const noexcept
		{
			return data();
		}
		inline value_t* end() noexcept
		{
			return data() + len;
		}
		inline const value_t* end() const noexcept
		{
			return data() + len;
		}

	private:
		uint32_t off;
		uint32_t len;
	};


	// Lookup tables
	template<typename key_t, typename value_t> struct table_pair
	{
		key_t key;
		value_t value;

		inline bool operator<(const table_pair& other) const noexcept
		{
			return key < other.key;
		}
		inline bool operator>(const table_pair& other) const noexcept
		{
			return key > other.key;
		}
	};

	template<typename key_t> struct lookup_compare
	{
		inline int32_t operator()(const key_t& lhs, const key_t& rhs) const noexcept
		{
			if (lhs == rhs)
				return 0;
			if (lhs < rhs)
				return -1;
			return 1;
		}
	};

	template<typename key_t, typename compare_t> struct lookup_find
	{
		template<typename value_t> inline static key_t get_key(const table_pair<key_t, value_t>& pair) noexcept
		{
			return pair.key;
		}
		inline static key_t get_key(const key_t& key) noexcept
		{
			return key;
		}

		template<typename arg_t> inline static arg_t* find(const key_t& key, arg_t* data, size_t length) noexcept
		{
			if (length != 0)
			{
				compare_t c;
				uint32_t first = 0, last = uint32_t(length), half;

			iter:
				half = first + ((last - first) >> 1);
				arg_t& rhs = *(data + half);

				const int32_t result = c(key, get_key(rhs));
				if (result == 0) return &rhs;
				if (last - first > 1)
				{
					*(result < 0 ? &last : &first) = half;
					goto iter;
				}
			}
			return nullptr;
		}
	};

	// Lookup block
	template<typename key_t, typename value_t, typename compare_t = lookup_compare<key_t>> struct lookup_block : public block<table_pair<key_t, value_t>>
	{
	public:
		using pair_type = table_pair<key_t, value_t>;
		using block_type = block<table_pair<key_t, value_t>>;

		lookup_block() = default;
		lookup_block(size_t length) : block_type(length)
		{

		}
		lookup_block(std::initializer_list<table_pair<key_t, value_t>> init) : block_type(init)
		{

		}
		lookup_block(const table_pair<key_t, value_t>* data, size_t length) : block_type(data, length)
		{

		}

		lookup_block(const lookup_block& other) : block_type((const block_type&)other)
		{

		}
		lookup_block& operator=(const lookup_block& other)
		{
			return (lookup_block&)block_type::operator=((const block_type&)other);
		}
		lookup_block(lookup_block&& other) noexcept : block_type((block_type&&)other)
		{

		}
		lookup_block& operator=(lookup_block&& other) noexcept
		{
			return (lookup_block&)block_type::operator=((block_type&&)other);
		}

		inline pair_type* find(const key_t& key) noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
		inline const pair_type* find(const key_t& key) const noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}

		inline void sort() noexcept
		{
			if (!this->empty()) std::sort(this->data(), this->data() + this->size());
		}
	};

	// Lookup set
	template<typename key_t, typename compare_t = lookup_compare<key_t>> struct lookup_set : public block<key_t>
	{
	public:
		using block_type = block<key_t>;

		lookup_set() = default;
		lookup_set(size_t length) : block_type(length)
		{

		}
		lookup_set(std::initializer_list<key_t> init) : block_type(init)
		{

		}
		lookup_set(const key_t* data, size_t length) : block_type(data, length)
		{

		}

		lookup_set(const lookup_set& other) : block_type((const block_type&)other)
		{

		}
		lookup_set& operator=(const lookup_set& other)
		{
			return (lookup_set&)block_type::operator=((const block_type&)other);
		}
		lookup_set(lookup_set&& other) noexcept : block_type((block_type&&)other)
		{

		}
		lookup_set& operator=(lookup_set&& other) noexcept
		{
			return (lookup_set&)block_type::operator=((block_type&&)other);
		}

		inline key_t* find(const key_t& key) noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
		inline const key_t* find(const key_t& key) const noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}

		inline void sort() noexcept
		{
			if (!this->empty()) std::sort(this->data(), this->data() + this->size());
		}
	};

	// Static lookup block
	template<typename key_t, typename value_t, typename compare_t = lookup_compare<key_t>> struct static_lookup_block : public static_block<table_pair<key_t, value_t>>
	{
	public:
		using pair_type = table_pair<key_t, value_t>;

		inline pair_type* find(const key_t& key) noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
		inline const pair_type* find(const key_t& key) const noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
	};

	// Static lookup set
	template<typename key_t, typename compare_t = lookup_compare<key_t>> struct static_lookup_set : public static_block<key_t>
	{
	public:
		using block_type = block<key_t>;

		inline key_t* find(const key_t& key) noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
		inline const key_t* find(const key_t& key) const noexcept
		{
			return lookup_find<key_t, compare_t>::find(key, this->data(), this->size());
		}
	};

	// Static string
	struct static_string : public static_block<char>
	{
	public:
		inline operator std::string_view() const noexcept
		{
			return std::string_view(data(), size());
		}
	};

	// Indexed block
	template<typename key_t, typename value_t> class indexed_block : public block<value_t>
	{
	public:
		using block<value_t>::block;

		inline value_t& operator[](key_t idx) noexcept { return block<value_t>::operator[](size_t(idx)); }
		inline const value_t& operator[](key_t idx) const noexcept { return block<value_t>::operator[](size_t(idx)); }

		inline bool is_valid_index(key_t idx) const noexcept
		{
			return size_t(idx) < block<value_t>::size();
		}
	};

	// Indexed static block
	template<typename key_t, typename value_t> class indexed_static_block : public static_block<value_t>
	{
	public:
		inline value_t& operator[](key_t idx) noexcept { return static_block<value_t>::operator[](size_t(idx)); }
		inline const value_t& operator[](key_t idx) const noexcept { return static_block<value_t>::operator[](size_t(idx)); }

		inline bool is_valid_index(key_t idx) const noexcept
		{
			return size_t(idx) < static_block<value_t>::size();
		}
	};
}

#endif