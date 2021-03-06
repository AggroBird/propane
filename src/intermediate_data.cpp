#include "intermediate_data.hpp"
#include "constants.hpp"

namespace propane
{
	namespace constants
	{
		constexpr size_t data_offset = intermediate_header.size() + sizeof(toolchain_version);
	}

	bool intermediate::is_valid() const noexcept
	{
		return constants::validate_intermediate_header(content);
	}
	intermediate::operator bool() const noexcept
	{
		return is_valid();
	}

	toolchain_version intermediate::version() const noexcept
	{
		if (content.size() >= constants::data_offset)
		{
			return *reinterpret_cast<const toolchain_version*>(content.data() + constants::intermediate_header.size());
		}
		return toolchain_version();
	}
	bool intermediate::is_compatible() const noexcept
	{
		return version().is_compatible();
	}

	span<const uint8_t> intermediate::data() const noexcept
	{
		return content;
	}
	bool intermediate::load(span<const uint8_t> from_bytes)
	{
		if (!constants::validate_intermediate_header(from_bytes)) return false;

		content = block<uint8_t>(from_bytes.data(), from_bytes.size());
		return true;
	}

	intermediate intermediate::operator+(const intermediate& other) const
	{
		if (&other != this && !other.content.empty())
		{
			if (content.empty())
			{
				return other;
			}
			else
			{
				;
				intermediate result;
				gen_intermediate_data::serialize(result, gen_intermediate_data::merge(*this, other));
				return result;
			}
		}
		return *this;
	}
	intermediate& intermediate::operator+=(const intermediate& other)
	{
		if (&other != this && !other.content.empty())
		{
			if (content.empty())
			{
				content = other.content;
			}
			else
			{
				gen_intermediate_data::serialize(*this, gen_intermediate_data::merge(*this, other));
			}
		}
		return *this;
	}


	void gen_intermediate_data::serialize(intermediate& dst, const gen_intermediate_data& data)
	{
		block_writer writer;
		writer.write_direct(constants::intermediate_header);
		writer.write_direct(toolchain_version::current());
		writer.write(data);
		vector<uint8_t> serialized = writer.finalize();
		append_bytecode(serialized, constants::footer);

		dst.content = block<uint8_t>(serialized.data(), serialized.size());
	}
	gen_intermediate_data gen_intermediate_data::deserialize(const intermediate& im)
	{
		gen_intermediate_data result;
		const im_assembly_data& im_data = *reinterpret_cast<const im_assembly_data*>(im.content.data() + constants::data_offset);
		serialization::serializer<gen_intermediate_data>::read(im_data, result);
		return result;
	}


	void gen_intermediate_data::initialize_base_types()
	{
		// Initialize base types
		for (size_t i = 0; i < base_type_count(); i++)
		{
			const auto& btype_info = base_types[i];
			const name_idx name = database.emplace(btype_info.name, btype_info.type).key;
			gen_type type(name, btype_info);
			types.push_back(type);
		}

		// Aliases
		for (size_t i = 0; i < alias_type_count(); i++)
		{
			const auto& btype_info = alias_types[i];
			database.emplace(btype_info.name, btype_info.type);
		}
	}

	void gen_intermediate_data::restore_lookup_tables()
	{
		vector<uint8_t> keybuf;
		keybuf.reserve(32);

		for (size_t i = 0; i < signatures.size(); i++)
		{
			const signature_idx index = signature_idx(i);
			signatures[index].make_key(keybuf);
			signature_lookup.emplace(keybuf, index);
		}
		for (size_t i = 0; i < offsets.size(); i++)
		{
			const offset_idx index = offset_idx(i);
			offsets[index].name.make_key(keybuf);
			offset_lookup.emplace(keybuf, index);
		}
	}
	void gen_intermediate_data::restore_generated_types()
	{
		for (auto& t : types)
		{
			if (t.is_generated())
			{
				if (t.is_pointer())
				{
					types[t.generated.pointer.underlying_type].pointer_type = t.index;
				}
				else if (t.is_array())
				{
					types[t.generated.array.underlying_type].array_types.emplace(t.generated.array.array_size, t.index);
				}
				else if (t.is_signature())
				{
					signatures[t.generated.signature.index].signature_type = t.index;
				}
			}
		}
	}
}
