#ifndef _HEADER_PROPANE_VERSION
#define _HEADER_PROPANE_VERSION

#include <cinttypes>

#define PROPANE_VERSION_MAJOR 1
#define PROPANE_VERSION_MINOR 0
#define PROPANE_VERSION_CHANGELIST 854

namespace propane
{
	namespace version
	{
		constexpr uint16_t major = PROPANE_VERSION_MAJOR;
		constexpr uint16_t minor = PROPANE_VERSION_MINOR;
		constexpr uint32_t changelist = PROPANE_VERSION_CHANGELIST;
	}

	enum class platform_endianness : uint8_t
	{
		unknown = 0,
		little,
		big,
		little_word,
		big_word,
	};

	enum class platform_architecture : uint8_t
	{
		unknown = 0,
		x32,
		x64,
	};

	struct toolchain_version
	{
		toolchain_version();
		toolchain_version(uint16_t major, uint16_t minor, uint32_t changelist, platform_endianness endianness, platform_architecture architecture);

		uint16_t major() const noexcept;
		uint16_t minor() const noexcept;
		uint32_t changelist() const noexcept;

		platform_endianness endianness() const noexcept;
		platform_architecture architecture() const noexcept;

		bool operator==(const toolchain_version& other) const noexcept;
		bool operator!=(const toolchain_version& other) const noexcept;

		bool is_compatible() const noexcept;

		static toolchain_version current();

	private:
		uint64_t value;
	};
}

#endif