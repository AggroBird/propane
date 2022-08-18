#include "assembly_data.hpp"
#include "database.hpp"
#include "errors.hpp"
#include "library.hpp"

#include <cmath>

#define VALIDATE(errc, expr, ...) ENSURE(errc, expr, propane::runtime_exception, __VA_ARGS__)

#define VALIDATE_ASSEMBLY(expr) VALIDATE(ERRC::RTM_INVALID_ASSEMBLY, expr, \
    "Attempted to execute an invalid assembly")
#define VALIDATE_COMPATIBILITY(expr) VALIDATE(ERRC::RTM_INCOMPATIBLE_ASSEMBLY, expr, \
    "Attempted to execute an assembly that was build using an incompatible toolchain")
#define VALIDATE_ENTRYPOINT(expr) VALIDATE(ERRC::RTM_ENTRYPOINT_NOT_FOUND, expr, \
    "Failed to find main entrypoint in assembly")
#define VALIDATE_STACK_ALLOCATION(expr) VALIDATE(ERRC::RTM_STACK_ALLOCATION_FAILURE, expr, \
    "Failed to allocate sufficient memory for runtime stack")
#define VALIDATE_STACK_OVERFLOW(expr, stack_size, stack_capacity) VALIDATE(ERRC::RTM_STACK_OVERFLOW, expr, \
    "Runtime stack overflow (%/%)", stack_size, stack_capacity)
#define VALIDATE_CALLSTACK_LIMIT(expr, max_depth) VALIDATE(ERRC::RTM_CALLSTACK_LIMIT_REACHED, expr, \
    "Maximum callstack depth of % exceeded", max_depth)
#define VALIDATE_RUNTIME_HASH(expr) VALIDATE(ERRC::RTM_RUNTIME_HASH_MISMATCH, expr, \
    "Runtime hash value mismatch")

namespace propane
{
    class host_memory final
    {
    public:
        NOCOPY_CLASS_DEFAULT(host_memory, size_t len)
        {
            handle = host::allocate(len);
        }
        ~host_memory()
        {
            host::free(handle);
        }

        inline bool protect()
        {
            return host::protect(handle);
        }

        inline void* data() const noexcept
        {
            return handle.address;
        }
        inline size_t size() const noexcept
        {
            return handle.size;
        }
        inline operator bool() const noexcept
        {
            return handle;
        }

    private:
        hostmem handle;
    };

    template<typename value_t> value_t get_value(const uint8_t* addr) { return *reinterpret_cast<const value_t*>(addr); }
    template<typename value_t> void dump_value(const uint8_t* addr) { std::cout << get_value<value_t>(addr); }
    template<> inline void dump_value<int8_t>(const uint8_t* addr) { std::cout << static_cast<int32_t>(get_value<int8_t>(addr)); }
    template<> inline void dump_value<uint8_t>(const uint8_t* addr) { std::cout << static_cast<uint32_t>(get_value<uint8_t>(addr)); }
    template<> inline void dump_value<bool>(const uint8_t* addr)
    {
        const uint8_t b = get_value<uint8_t>(addr);
        if (b == 0) std::cout << "false";
        else if (b == 1) std::cout << "true";
        else std::cout << static_cast<uint32_t>(b);
    }
    template<typename value_t> void dump_var(const uint8_t* addr)
    {
        std::cout << '(';
        dump_value<value_t>(addr);
        std::cout << ')';
    }

    struct stack_data_t
    {
        stack_data_t(uint8_t* data, size_t capacity) :
            data(data), capacity(capacity) {}

        ~stack_data_t()
        {
            if (data != nullptr)
            {
                free(data);
            }
        }

        uint8_t* const data;
        const size_t capacity;
        size_t size = 0;
    };

    class runtime_library
    {
    public:
        struct call
        {
            call() = default;
            call(const external_call_info& cinf) :
                name(cinf.name),
                forward(cinf.forward),
                handle(cinf.handle) {}

            string_view name;
            external_call::forward_method forward = nullptr;
            void(*handle)() = nullptr;
        };

        runtime_library() = default;
        runtime_library(string_view path) :
            handle(path) {}

        host_library handle;
        indexed_block<uint32_t, call> calls;
    };

    struct data_table_view
    {
        data_table_view() : info(nullptr), data(nullptr) {}
        data_table_view(const field* info, uint8_t* data) :
            info(info),
            data(data) {}

        inline const field& operator[](global_idx index) const noexcept
        {
            return info[static_cast<size_t>(index)];
        }

        const field* info;
        uint8_t* data;
    };

    // Stack frame
    struct stack_frame_t
    {
        stack_frame_t() = default;
        stack_frame_t(size_t iptr, size_t return_offset, size_t frame_offset, size_t param_offset, size_t stack_offset, size_t stack_end, method_idx method) :
            iptr(iptr),
            return_offset(return_offset),
            frame_offset(frame_offset),
            param_offset(param_offset),
            stack_offset(stack_offset),
            stack_end(stack_end),
            method(method) {}

        // Current instruction offset at the time of calling
        size_t iptr = 0;
        // Offset on the previous stack frame where the return value should go
        size_t return_offset = 0;
        // Offset of the pushed stackframe
        size_t frame_offset = 0;
        // Offset of the method parameters
        size_t param_offset = 0;
        // Offset of the method stackvars
        size_t stack_offset = 0;
        // End of the method stack (excluding return values)
        size_t stack_end = 0;
        // Current executing method
        method_idx method = method_idx::invalid;
    };

    class interpreter final
    {
    public:
        NOCOPY_CLASS_DEFAULT(interpreter, const assembly_data& asm_data, const method& main, const runtime_data& runtime, runtime_parameters parameters) :
            stack(allocate_stack(parameters)),
            parameters(parameters),
            data(asm_data),
            types(asm_data.types.data()),
            methods(asm_data.methods.data()),
            signatures(asm_data.signatures.data()),
            offsets(asm_data.offsets.data()),
            global_data(asm_data.globals.data.data(), asm_data.globals.data.size()),
            global_tables(),
            database(asm_data.database)
        {
            // Initialize externals
            for (size_t i = 0; i < runtime.libraries.size(); i++)
            {
                const auto& init_lib = runtime.libraries[name_idx(i)];

                runtime_library lib(init_lib.name);

                lib.calls = indexed_block<uint32_t, runtime_library::call>(init_lib.calls.size());
                auto src = init_lib.calls.data();
                for (auto& it : lib.calls)
                {
                    it = runtime_library::call(*src++);
                }

                // Preload symbols
                if (init_lib.preload_symbols)
                {
                    for (auto& call : lib.calls)
                    {
                        if (call.handle) continue;

                        if (!lib.handle.is_open())
                        {
                            const bool opened = lib.handle.open();
                            ASSERT(opened, "Failed to load library");
                        }
                        call.handle = lib.handle.get_proc(call.name.data());
                        ASSERT(call.handle, "Failed to find function");
                    }
                }

                libraries.push_back(std::move(lib));
            }

            global_tables[0] = data_table_view(asm_data.globals.info.data(), global_data.data());
            global_tables[1] = data_table_view(asm_data.constants.info.data(), const_cast<uint8_t*>(asm_data.constants.data.data()));

            // Push space for the return value
            constexpr size_t int_size = get_base_type_size(type_idx::i32);
            stack.size = int_size;

            // Push return type and stack frame
            push_stack_frame(main, get_signature(main.signature));

            // Execute
            execute();

            // Fetch return code
            ASSERT(stack.size >= int_size, "Invalid stack size: %", stack.size);
            ASSERT(callstack_depth == 0, "Invalid callstack depth: %", callstack_depth);
            return_code = *reinterpret_cast<const int32_t*>(stack.data);
        }

        inline operator int32_t() const noexcept
        {
            return return_code;
        }

    private:
        void execute()
        {
            while (iptr)
            {
                ASSERT(iptr >= ibeg && iptr <= iend, "Instruction pointer out of range");

                const opcode op = read_bytecode<opcode>(iptr);
                switch (op)
                {
                    case opcode::noop: break;

                    case opcode::set: set(); break;
                    case opcode::conv: conv(); break;

                    case opcode::ari_not: ari_not(); break;
                    case opcode::ari_neg: ari_neg(); break;
                    case opcode::ari_mul: ari_mul(); break;
                    case opcode::ari_div: ari_div(); break;
                    case opcode::ari_mod: ari_mod(); break;
                    case opcode::ari_add: ari_add(); break;
                    case opcode::ari_sub: ari_sub(); break;
                    case opcode::ari_lsh: ari_lsh(); break;
                    case opcode::ari_rsh: ari_rsh(); break;
                    case opcode::ari_and: ari_and(); break;
                    case opcode::ari_xor: ari_xor(); break;
                    case opcode::ari_or: ari_or(); break;

                    case opcode::padd: padd(); break;
                    case opcode::psub: psub(); break;
                    case opcode::pdif: pdif(); break;

                    case opcode::cmp: write<int32_t>(push_return_value(type_idx::i32)) = cmp(); break;
                    case opcode::ceq: write<int32_t>(push_return_value(type_idx::i32)) = ceq(); break;
                    case opcode::cne: write<int32_t>(push_return_value(type_idx::i32)) = cne(); break;
                    case opcode::cgt: write<int32_t>(push_return_value(type_idx::i32)) = cgt(); break;
                    case opcode::cge: write<int32_t>(push_return_value(type_idx::i32)) = cge(); break;
                    case opcode::clt: write<int32_t>(push_return_value(type_idx::i32)) = clt(); break;
                    case opcode::cle: write<int32_t>(push_return_value(type_idx::i32)) = cle(); break;
                    case opcode::cze: write<int32_t>(push_return_value(type_idx::i32)) = cze(); break;
                    case opcode::cnz: write<int32_t>(push_return_value(type_idx::i32)) = cnz(); break;

                    case opcode::br: br(); break;

                    case opcode::beq: beq(); break;
                    case opcode::bne: bne(); break;
                    case opcode::bgt: bgt(); break;
                    case opcode::bge: bge(); break;
                    case opcode::blt: blt(); break;
                    case opcode::ble: ble(); break;
                    case opcode::bze: bze(); break;
                    case opcode::bnz: bnz(); break;

                    case opcode::sw: sw(); break;

                    case opcode::call: call(); break;
                    case opcode::callv: callv(); break;
                    case opcode::ret: ret(); break;
                    case opcode::retv: retv(); break;

                    case opcode::dump: dump(); break;

                    default: ASSERT(false, "Malformed opcode: %", static_cast<uint32_t>(op));
                }
            }
        }

        void dump_assembly() const
        {
            // Types
            std::cout << "TYPES: " << std::endl;
            for (size_t tidx = 0; tidx < data.types.size(); tidx++)
            {
                auto& t = types[tidx];

                std::cout << tidx << ": " << get_name(t);
                if (t.meta.index != meta_idx::invalid)
                {
                    std::cout << " (" << data.metatable[t.meta.index] << ":" << t.meta.line_number << ")";
                }
                if (!t.fields.empty())
                {
                    std::cout << " { ";
                    bool first = true;
                    for (auto& field : t.fields)
                    {
                        if (!first) std::cout << ", ";
                        first = false;
                        std::cout << get_name(get_type(field.type)) << ' ' << database[field.name];
                    }
                    std::cout << " }";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;

            // Signatures
            std::cout << "SIGNATURES: " << std::endl;
            for (size_t sidx = 0; sidx < data.signatures.size(); sidx++)
            {
                auto& s = signatures[sidx];

                std::cout << sidx << ": " << get_name(get_type(s.return_type));
                std::cout << '(';
                if (!s.parameters.empty())
                {
                    for (size_t i = 0; i < s.parameters.size(); i++)
                    {
                        const auto& param = s.parameters[i];

                        if (i > 0) std::cout << ", ";
                        std::cout << get_name(get_type(param.type));
                    }
                }
                std::cout << ')' << std::endl;
            }
            std::cout << std::endl;

            // Methods
            std::cout << "METHODS: " << std::endl;
            for (size_t midx = 0; midx < data.methods.size(); midx++)
            {
                auto& m = methods[midx];
                auto& s = get_signature(m.signature);

                std::cout << midx << ": " << get_name(get_type(s.return_type)) << " ";
                std::cout << database[m.name];

                std::cout << '(';
                if (!s.parameters.empty())
                {
                    for (size_t i = 0; i < s.parameters.size(); i++)
                    {
                        const auto& param = s.parameters[i];

                        if (i > 0) std::cout << ", ";
                        std::cout << get_name(get_type(param.type));
                    }
                }
                std::cout << ')';
                if (m.meta.index != meta_idx::invalid)
                {
                    std::cout << " (" << data.metatable[m.meta.index] << ":" << m.meta.line_number << ")";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }


        // Implementations of ops
        inline void set() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            set(sub, lhs_addr, rhs_addr);
        }
        inline void set(subcode sub, uint8_t* lhs_addr, const uint8_t* rhs_addr) noexcept
        {
            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) = read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) = read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) = (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) = (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) = read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) = (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) = read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) = (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) = (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) = (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) = (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) = read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) = (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) = (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) = read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) = (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) = (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) = (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) = (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) = (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) = (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) = read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) = (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) = (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) = (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) = read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) = (float)read<int8_t>(rhs_addr); return;
                case subcode(27): write<float>(lhs_addr) = (float)read<uint8_t>(rhs_addr); return;
                case subcode(28): write<float>(lhs_addr) = (float)read<int16_t>(rhs_addr); return;
                case subcode(29): write<float>(lhs_addr) = (float)read<uint16_t>(rhs_addr); return;
                case subcode(30): write<float>(lhs_addr) = (float)read<int32_t>(rhs_addr); return;
                case subcode(31): write<float>(lhs_addr) = (float)read<uint32_t>(rhs_addr); return;
                case subcode(32): write<float>(lhs_addr) = (float)read<int64_t>(rhs_addr); return;
                case subcode(33): write<float>(lhs_addr) = (float)read<uint64_t>(rhs_addr); return;
                case subcode(34): write<float>(lhs_addr) = read<float>(rhs_addr); return;
                case subcode(35): write<double>(lhs_addr) = (double)read<int8_t>(rhs_addr); return;
                case subcode(36): write<double>(lhs_addr) = (double)read<uint8_t>(rhs_addr); return;
                case subcode(37): write<double>(lhs_addr) = (double)read<int16_t>(rhs_addr); return;
                case subcode(38): write<double>(lhs_addr) = (double)read<uint16_t>(rhs_addr); return;
                case subcode(39): write<double>(lhs_addr) = (double)read<int32_t>(rhs_addr); return;
                case subcode(40): write<double>(lhs_addr) = (double)read<uint32_t>(rhs_addr); return;
                case subcode(41): write<double>(lhs_addr) = (double)read<int64_t>(rhs_addr); return;
                case subcode(42): write<double>(lhs_addr) = (double)read<uint64_t>(rhs_addr); return;
                case subcode(43): write<double>(lhs_addr) = (double)read<float>(rhs_addr); return;
                case subcode(44): write<double>(lhs_addr) = read<double>(rhs_addr); return;
                case subcode(45): memcpy(lhs_addr, rhs_addr, get_addr_type(true).total_size); break;
            }
        }
        inline void conv() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) = read<int8_t>(rhs_addr); return;
                case subcode(1): write<int8_t>(lhs_addr) = (int8_t)read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int8_t>(lhs_addr) = (int8_t)read<int16_t>(rhs_addr); return;
                case subcode(3): write<int8_t>(lhs_addr) = (int8_t)read<uint16_t>(rhs_addr); return;
                case subcode(4): write<int8_t>(lhs_addr) = (int8_t)read<int32_t>(rhs_addr); return;
                case subcode(5): write<int8_t>(lhs_addr) = (int8_t)read<uint32_t>(rhs_addr); return;
                case subcode(6): write<int8_t>(lhs_addr) = (int8_t)read<int64_t>(rhs_addr); return;
                case subcode(7): write<int8_t>(lhs_addr) = (int8_t)read<uint64_t>(rhs_addr); return;
                case subcode(8): write<int8_t>(lhs_addr) = (int8_t)read<float>(rhs_addr); return;
                case subcode(9): write<int8_t>(lhs_addr) = (int8_t)read<double>(rhs_addr); return;
                case subcode(10): write<uint8_t>(lhs_addr) = (uint8_t)read<int8_t>(rhs_addr); return;
                case subcode(11): write<uint8_t>(lhs_addr) = read<uint8_t>(rhs_addr); return;
                case subcode(12): write<uint8_t>(lhs_addr) = (uint8_t)read<int16_t>(rhs_addr); return;
                case subcode(13): write<uint8_t>(lhs_addr) = (uint8_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint8_t>(lhs_addr) = (uint8_t)read<int32_t>(rhs_addr); return;
                case subcode(15): write<uint8_t>(lhs_addr) = (uint8_t)read<uint32_t>(rhs_addr); return;
                case subcode(16): write<uint8_t>(lhs_addr) = (uint8_t)read<int64_t>(rhs_addr); return;
                case subcode(17): write<uint8_t>(lhs_addr) = (uint8_t)read<uint64_t>(rhs_addr); return;
                case subcode(18): write<uint8_t>(lhs_addr) = (uint8_t)read<float>(rhs_addr); return;
                case subcode(19): write<uint8_t>(lhs_addr) = (uint8_t)read<double>(rhs_addr); return;
                case subcode(20): write<int16_t>(lhs_addr) = (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(21): write<int16_t>(lhs_addr) = (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(22): write<int16_t>(lhs_addr) = read<int16_t>(rhs_addr); return;
                case subcode(23): write<int16_t>(lhs_addr) = (int16_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<int16_t>(lhs_addr) = (int16_t)read<int32_t>(rhs_addr); return;
                case subcode(25): write<int16_t>(lhs_addr) = (int16_t)read<uint32_t>(rhs_addr); return;
                case subcode(26): write<int16_t>(lhs_addr) = (int16_t)read<int64_t>(rhs_addr); return;
                case subcode(27): write<int16_t>(lhs_addr) = (int16_t)read<uint64_t>(rhs_addr); return;
                case subcode(28): write<int16_t>(lhs_addr) = (int16_t)read<float>(rhs_addr); return;
                case subcode(29): write<int16_t>(lhs_addr) = (int16_t)read<double>(rhs_addr); return;
                case subcode(30): write<uint16_t>(lhs_addr) = (uint16_t)read<int8_t>(rhs_addr); return;
                case subcode(31): write<uint16_t>(lhs_addr) = (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(32): write<uint16_t>(lhs_addr) = (uint16_t)read<int16_t>(rhs_addr); return;
                case subcode(33): write<uint16_t>(lhs_addr) = read<uint16_t>(rhs_addr); return;
                case subcode(34): write<uint16_t>(lhs_addr) = (uint16_t)read<int32_t>(rhs_addr); return;
                case subcode(35): write<uint16_t>(lhs_addr) = (uint16_t)read<uint32_t>(rhs_addr); return;
                case subcode(36): write<uint16_t>(lhs_addr) = (uint16_t)read<int64_t>(rhs_addr); return;
                case subcode(37): write<uint16_t>(lhs_addr) = (uint16_t)read<uint64_t>(rhs_addr); return;
                case subcode(38): write<uint16_t>(lhs_addr) = (uint16_t)read<float>(rhs_addr); return;
                case subcode(39): write<uint16_t>(lhs_addr) = (uint16_t)read<double>(rhs_addr); return;
                case subcode(40): write<int32_t>(lhs_addr) = (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(41): write<int32_t>(lhs_addr) = (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(42): write<int32_t>(lhs_addr) = (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(43): write<int32_t>(lhs_addr) = (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(44): write<int32_t>(lhs_addr) = read<int32_t>(rhs_addr); return;
                case subcode(45): write<int32_t>(lhs_addr) = (int32_t)read<uint32_t>(rhs_addr); return;
                case subcode(46): write<int32_t>(lhs_addr) = (int32_t)read<int64_t>(rhs_addr); return;
                case subcode(47): write<int32_t>(lhs_addr) = (int32_t)read<uint64_t>(rhs_addr); return;
                case subcode(48): write<int32_t>(lhs_addr) = (int32_t)read<float>(rhs_addr); return;
                case subcode(49): write<int32_t>(lhs_addr) = (int32_t)read<double>(rhs_addr); return;
                case subcode(50): write<uint32_t>(lhs_addr) = (uint32_t)read<int8_t>(rhs_addr); return;
                case subcode(51): write<uint32_t>(lhs_addr) = (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(52): write<uint32_t>(lhs_addr) = (uint32_t)read<int16_t>(rhs_addr); return;
                case subcode(53): write<uint32_t>(lhs_addr) = (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(54): write<uint32_t>(lhs_addr) = (uint32_t)read<int32_t>(rhs_addr); return;
                case subcode(55): write<uint32_t>(lhs_addr) = read<uint32_t>(rhs_addr); return;
                case subcode(56): write<uint32_t>(lhs_addr) = (uint32_t)read<int64_t>(rhs_addr); return;
                case subcode(57): write<uint32_t>(lhs_addr) = (uint32_t)read<uint64_t>(rhs_addr); return;
                case subcode(58): write<uint32_t>(lhs_addr) = (uint32_t)read<float>(rhs_addr); return;
                case subcode(59): write<uint32_t>(lhs_addr) = (uint32_t)read<double>(rhs_addr); return;
                case subcode(60): write<int64_t>(lhs_addr) = (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(61): write<int64_t>(lhs_addr) = (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(62): write<int64_t>(lhs_addr) = (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(63): write<int64_t>(lhs_addr) = (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(64): write<int64_t>(lhs_addr) = (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(65): write<int64_t>(lhs_addr) = (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(66): write<int64_t>(lhs_addr) = read<int64_t>(rhs_addr); return;
                case subcode(67): write<int64_t>(lhs_addr) = (int64_t)read<uint64_t>(rhs_addr); return;
                case subcode(68): write<int64_t>(lhs_addr) = (int64_t)read<float>(rhs_addr); return;
                case subcode(69): write<int64_t>(lhs_addr) = (int64_t)read<double>(rhs_addr); return;
                case subcode(70): write<uint64_t>(lhs_addr) = (uint64_t)read<int8_t>(rhs_addr); return;
                case subcode(71): write<uint64_t>(lhs_addr) = (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(72): write<uint64_t>(lhs_addr) = (uint64_t)read<int16_t>(rhs_addr); return;
                case subcode(73): write<uint64_t>(lhs_addr) = (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(74): write<uint64_t>(lhs_addr) = (uint64_t)read<int32_t>(rhs_addr); return;
                case subcode(75): write<uint64_t>(lhs_addr) = (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(76): write<uint64_t>(lhs_addr) = (uint64_t)read<int64_t>(rhs_addr); return;
                case subcode(77): write<uint64_t>(lhs_addr) = read<uint64_t>(rhs_addr); return;
                case subcode(78): write<uint64_t>(lhs_addr) = (uint64_t)read<float>(rhs_addr); return;
                case subcode(79): write<uint64_t>(lhs_addr) = (uint64_t)read<double>(rhs_addr); return;
                case subcode(80): write<float>(lhs_addr) = (float)read<int8_t>(rhs_addr); return;
                case subcode(81): write<float>(lhs_addr) = (float)read<uint8_t>(rhs_addr); return;
                case subcode(82): write<float>(lhs_addr) = (float)read<int16_t>(rhs_addr); return;
                case subcode(83): write<float>(lhs_addr) = (float)read<uint16_t>(rhs_addr); return;
                case subcode(84): write<float>(lhs_addr) = (float)read<int32_t>(rhs_addr); return;
                case subcode(85): write<float>(lhs_addr) = (float)read<uint32_t>(rhs_addr); return;
                case subcode(86): write<float>(lhs_addr) = (float)read<int64_t>(rhs_addr); return;
                case subcode(87): write<float>(lhs_addr) = (float)read<uint64_t>(rhs_addr); return;
                case subcode(88): write<float>(lhs_addr) = read<float>(rhs_addr); return;
                case subcode(89): write<float>(lhs_addr) = (float)read<double>(rhs_addr); return;
                case subcode(90): write<double>(lhs_addr) = (double)read<int8_t>(rhs_addr); return;
                case subcode(91): write<double>(lhs_addr) = (double)read<uint8_t>(rhs_addr); return;
                case subcode(92): write<double>(lhs_addr) = (double)read<int16_t>(rhs_addr); return;
                case subcode(93): write<double>(lhs_addr) = (double)read<uint16_t>(rhs_addr); return;
                case subcode(94): write<double>(lhs_addr) = (double)read<int32_t>(rhs_addr); return;
                case subcode(95): write<double>(lhs_addr) = (double)read<uint32_t>(rhs_addr); return;
                case subcode(96): write<double>(lhs_addr) = (double)read<int64_t>(rhs_addr); return;
                case subcode(97): write<double>(lhs_addr) = (double)read<uint64_t>(rhs_addr); return;
                case subcode(98): write<double>(lhs_addr) = (double)read<float>(rhs_addr); return;
                case subcode(99): write<double>(lhs_addr) = read<double>(rhs_addr); return;
            }
        }

        inline void ari_not() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) = ~read<int8_t>(lhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) = ~read<uint8_t>(lhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) = ~read<int16_t>(lhs_addr); return;
                case subcode(3): write<uint16_t>(lhs_addr) = ~read<uint16_t>(lhs_addr); return;
                case subcode(4): write<int32_t>(lhs_addr) = ~read<int32_t>(lhs_addr); return;
                case subcode(5): write<uint32_t>(lhs_addr) = ~read<uint32_t>(lhs_addr); return;
                case subcode(6): write<int64_t>(lhs_addr) = ~read<int64_t>(lhs_addr); return;
                case subcode(7): write<uint64_t>(lhs_addr) = ~read<uint64_t>(lhs_addr); return;
            }
        }
        inline void ari_neg() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) = -read<int8_t>(lhs_addr); return;
                case subcode(1): write<int16_t>(lhs_addr) = -read<int16_t>(lhs_addr); return;
                case subcode(2): write<int32_t>(lhs_addr) = -read<int32_t>(lhs_addr); return;
                case subcode(3): write<int64_t>(lhs_addr) = -read<int64_t>(lhs_addr); return;
                case subcode(4): write<float>(lhs_addr) = -read<float>(lhs_addr); return;
                case subcode(5): write<double>(lhs_addr) = -read<double>(lhs_addr); return;
            }
        }
        inline void ari_mul() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) *= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) *= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) *= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) *= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) *= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) *= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) *= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) *= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) *= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) *= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) *= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) *= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) *= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) *= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) *= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) *= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) *= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) *= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) *= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) *= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) *= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) *= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) *= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) *= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) *= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) *= read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) *= (float)read<int8_t>(rhs_addr); return;
                case subcode(27): write<float>(lhs_addr) *= (float)read<uint8_t>(rhs_addr); return;
                case subcode(28): write<float>(lhs_addr) *= (float)read<int16_t>(rhs_addr); return;
                case subcode(29): write<float>(lhs_addr) *= (float)read<uint16_t>(rhs_addr); return;
                case subcode(30): write<float>(lhs_addr) *= (float)read<int32_t>(rhs_addr); return;
                case subcode(31): write<float>(lhs_addr) *= (float)read<uint32_t>(rhs_addr); return;
                case subcode(32): write<float>(lhs_addr) *= (float)read<int64_t>(rhs_addr); return;
                case subcode(33): write<float>(lhs_addr) *= (float)read<uint64_t>(rhs_addr); return;
                case subcode(34): write<float>(lhs_addr) *= read<float>(rhs_addr); return;
                case subcode(35): write<double>(lhs_addr) *= (double)read<int8_t>(rhs_addr); return;
                case subcode(36): write<double>(lhs_addr) *= (double)read<uint8_t>(rhs_addr); return;
                case subcode(37): write<double>(lhs_addr) *= (double)read<int16_t>(rhs_addr); return;
                case subcode(38): write<double>(lhs_addr) *= (double)read<uint16_t>(rhs_addr); return;
                case subcode(39): write<double>(lhs_addr) *= (double)read<int32_t>(rhs_addr); return;
                case subcode(40): write<double>(lhs_addr) *= (double)read<uint32_t>(rhs_addr); return;
                case subcode(41): write<double>(lhs_addr) *= (double)read<int64_t>(rhs_addr); return;
                case subcode(42): write<double>(lhs_addr) *= (double)read<uint64_t>(rhs_addr); return;
                case subcode(43): write<double>(lhs_addr) *= (double)read<float>(rhs_addr); return;
                case subcode(44): write<double>(lhs_addr) *= read<double>(rhs_addr); return;
            }
        }
        inline void ari_div() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) /= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) /= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) /= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) /= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) /= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) /= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) /= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) /= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) /= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) /= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) /= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) /= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) /= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) /= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) /= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) /= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) /= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) /= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) /= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) /= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) /= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) /= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) /= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) /= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) /= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) /= read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) /= (float)read<int8_t>(rhs_addr); return;
                case subcode(27): write<float>(lhs_addr) /= (float)read<uint8_t>(rhs_addr); return;
                case subcode(28): write<float>(lhs_addr) /= (float)read<int16_t>(rhs_addr); return;
                case subcode(29): write<float>(lhs_addr) /= (float)read<uint16_t>(rhs_addr); return;
                case subcode(30): write<float>(lhs_addr) /= (float)read<int32_t>(rhs_addr); return;
                case subcode(31): write<float>(lhs_addr) /= (float)read<uint32_t>(rhs_addr); return;
                case subcode(32): write<float>(lhs_addr) /= (float)read<int64_t>(rhs_addr); return;
                case subcode(33): write<float>(lhs_addr) /= (float)read<uint64_t>(rhs_addr); return;
                case subcode(34): write<float>(lhs_addr) /= read<float>(rhs_addr); return;
                case subcode(35): write<double>(lhs_addr) /= (double)read<int8_t>(rhs_addr); return;
                case subcode(36): write<double>(lhs_addr) /= (double)read<uint8_t>(rhs_addr); return;
                case subcode(37): write<double>(lhs_addr) /= (double)read<int16_t>(rhs_addr); return;
                case subcode(38): write<double>(lhs_addr) /= (double)read<uint16_t>(rhs_addr); return;
                case subcode(39): write<double>(lhs_addr) /= (double)read<int32_t>(rhs_addr); return;
                case subcode(40): write<double>(lhs_addr) /= (double)read<uint32_t>(rhs_addr); return;
                case subcode(41): write<double>(lhs_addr) /= (double)read<int64_t>(rhs_addr); return;
                case subcode(42): write<double>(lhs_addr) /= (double)read<uint64_t>(rhs_addr); return;
                case subcode(43): write<double>(lhs_addr) /= (double)read<float>(rhs_addr); return;
                case subcode(44): write<double>(lhs_addr) /= read<double>(rhs_addr); return;
            }
        }
        inline void ari_mod() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) %= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) %= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) %= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) %= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) %= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) %= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) %= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) %= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) %= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) %= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) %= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) %= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) %= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) %= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) %= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) %= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) %= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) %= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) %= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) %= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) %= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) %= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) %= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) %= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) %= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) %= read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<int8_t>(rhs_addr)); return;
                case subcode(27): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<uint8_t>(rhs_addr)); return;
                case subcode(28): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<int16_t>(rhs_addr)); return;
                case subcode(29): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<uint16_t>(rhs_addr)); return;
                case subcode(30): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<int32_t>(rhs_addr)); return;
                case subcode(31): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<uint32_t>(rhs_addr)); return;
                case subcode(32): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<int64_t>(rhs_addr)); return;
                case subcode(33): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), (float)read<uint64_t>(rhs_addr)); return;
                case subcode(34): write<float>(lhs_addr) = fmodf(read<float>(lhs_addr), read<float>(rhs_addr)); return;
                case subcode(35): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<int8_t>(rhs_addr)); return;
                case subcode(36): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<uint8_t>(rhs_addr)); return;
                case subcode(37): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<int16_t>(rhs_addr)); return;
                case subcode(38): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<uint16_t>(rhs_addr)); return;
                case subcode(39): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<int32_t>(rhs_addr)); return;
                case subcode(40): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<uint32_t>(rhs_addr)); return;
                case subcode(41): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<int64_t>(rhs_addr)); return;
                case subcode(42): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<uint64_t>(rhs_addr)); return;
                case subcode(43): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), (double)read<float>(rhs_addr)); return;
                case subcode(44): write<double>(lhs_addr) = fmod(read<double>(lhs_addr), read<double>(rhs_addr)); return;
            }
        }
        inline void ari_add() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) += read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) += read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) += (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) += (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) += read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) += (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) += read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) += (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) += (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) += (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) += (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) += read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) += (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) += (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) += read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) += (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) += (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) += (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) += (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) += (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) += (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) += read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) += (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) += (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) += (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) += read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) += (float)read<int8_t>(rhs_addr); return;
                case subcode(27): write<float>(lhs_addr) += (float)read<uint8_t>(rhs_addr); return;
                case subcode(28): write<float>(lhs_addr) += (float)read<int16_t>(rhs_addr); return;
                case subcode(29): write<float>(lhs_addr) += (float)read<uint16_t>(rhs_addr); return;
                case subcode(30): write<float>(lhs_addr) += (float)read<int32_t>(rhs_addr); return;
                case subcode(31): write<float>(lhs_addr) += (float)read<uint32_t>(rhs_addr); return;
                case subcode(32): write<float>(lhs_addr) += (float)read<int64_t>(rhs_addr); return;
                case subcode(33): write<float>(lhs_addr) += (float)read<uint64_t>(rhs_addr); return;
                case subcode(34): write<float>(lhs_addr) += read<float>(rhs_addr); return;
                case subcode(35): write<double>(lhs_addr) += (double)read<int8_t>(rhs_addr); return;
                case subcode(36): write<double>(lhs_addr) += (double)read<uint8_t>(rhs_addr); return;
                case subcode(37): write<double>(lhs_addr) += (double)read<int16_t>(rhs_addr); return;
                case subcode(38): write<double>(lhs_addr) += (double)read<uint16_t>(rhs_addr); return;
                case subcode(39): write<double>(lhs_addr) += (double)read<int32_t>(rhs_addr); return;
                case subcode(40): write<double>(lhs_addr) += (double)read<uint32_t>(rhs_addr); return;
                case subcode(41): write<double>(lhs_addr) += (double)read<int64_t>(rhs_addr); return;
                case subcode(42): write<double>(lhs_addr) += (double)read<uint64_t>(rhs_addr); return;
                case subcode(43): write<double>(lhs_addr) += (double)read<float>(rhs_addr); return;
                case subcode(44): write<double>(lhs_addr) += read<double>(rhs_addr); return;
            }
        }
        inline void ari_sub() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) -= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) -= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) -= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) -= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) -= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) -= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) -= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) -= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) -= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) -= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) -= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) -= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) -= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) -= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) -= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) -= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) -= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) -= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) -= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) -= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) -= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) -= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) -= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) -= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) -= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) -= read<uint64_t>(rhs_addr); return;
                case subcode(26): write<float>(lhs_addr) -= (float)read<int8_t>(rhs_addr); return;
                case subcode(27): write<float>(lhs_addr) -= (float)read<uint8_t>(rhs_addr); return;
                case subcode(28): write<float>(lhs_addr) -= (float)read<int16_t>(rhs_addr); return;
                case subcode(29): write<float>(lhs_addr) -= (float)read<uint16_t>(rhs_addr); return;
                case subcode(30): write<float>(lhs_addr) -= (float)read<int32_t>(rhs_addr); return;
                case subcode(31): write<float>(lhs_addr) -= (float)read<uint32_t>(rhs_addr); return;
                case subcode(32): write<float>(lhs_addr) -= (float)read<int64_t>(rhs_addr); return;
                case subcode(33): write<float>(lhs_addr) -= (float)read<uint64_t>(rhs_addr); return;
                case subcode(34): write<float>(lhs_addr) -= read<float>(rhs_addr); return;
                case subcode(35): write<double>(lhs_addr) -= (double)read<int8_t>(rhs_addr); return;
                case subcode(36): write<double>(lhs_addr) -= (double)read<uint8_t>(rhs_addr); return;
                case subcode(37): write<double>(lhs_addr) -= (double)read<int16_t>(rhs_addr); return;
                case subcode(38): write<double>(lhs_addr) -= (double)read<uint16_t>(rhs_addr); return;
                case subcode(39): write<double>(lhs_addr) -= (double)read<int32_t>(rhs_addr); return;
                case subcode(40): write<double>(lhs_addr) -= (double)read<uint32_t>(rhs_addr); return;
                case subcode(41): write<double>(lhs_addr) -= (double)read<int64_t>(rhs_addr); return;
                case subcode(42): write<double>(lhs_addr) -= (double)read<uint64_t>(rhs_addr); return;
                case subcode(43): write<double>(lhs_addr) -= (double)read<float>(rhs_addr); return;
                case subcode(44): write<double>(lhs_addr) -= read<double>(rhs_addr); return;
            }
        }
        inline void ari_lsh() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) <<= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) <<= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) <<= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) <<= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) <<= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) <<= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) <<= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) <<= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) <<= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) <<= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) <<= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) <<= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) <<= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) <<= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) <<= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) <<= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) <<= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) <<= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) <<= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) <<= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) <<= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) <<= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) <<= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) <<= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) <<= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) <<= read<uint64_t>(rhs_addr); return;
            }
        }
        inline void ari_rsh() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) >>= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) >>= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) >>= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) >>= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) >>= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) >>= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) >>= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) >>= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) >>= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) >>= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) >>= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) >>= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) >>= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) >>= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) >>= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) >>= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) >>= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) >>= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) >>= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) >>= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) >>= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) >>= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) >>= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) >>= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) >>= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) >>= read<uint64_t>(rhs_addr); return;
            }
        }
        inline void ari_and() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) &= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) &= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) &= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) &= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) &= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) &= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) &= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) &= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) &= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) &= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) &= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) &= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) &= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) &= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) &= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) &= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) &= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) &= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) &= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) &= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) &= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) &= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) &= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) &= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) &= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) &= read<uint64_t>(rhs_addr); return;
            }
        }
        inline void ari_xor() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) ^= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) ^= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) ^= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) ^= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) ^= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) ^= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) ^= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) ^= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) ^= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) ^= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) ^= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) ^= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) ^= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) ^= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) ^= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) ^= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) ^= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) ^= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) ^= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) ^= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) ^= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) ^= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) ^= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) ^= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) ^= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) ^= read<uint64_t>(rhs_addr); return;
            }
        }
        inline void ari_or() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): write<int8_t>(lhs_addr) |= read<int8_t>(rhs_addr); return;
                case subcode(1): write<uint8_t>(lhs_addr) |= read<uint8_t>(rhs_addr); return;
                case subcode(2): write<int16_t>(lhs_addr) |= (int16_t)read<int8_t>(rhs_addr); return;
                case subcode(3): write<int16_t>(lhs_addr) |= (int16_t)read<uint8_t>(rhs_addr); return;
                case subcode(4): write<int16_t>(lhs_addr) |= read<int16_t>(rhs_addr); return;
                case subcode(5): write<uint16_t>(lhs_addr) |= (uint16_t)read<uint8_t>(rhs_addr); return;
                case subcode(6): write<uint16_t>(lhs_addr) |= read<uint16_t>(rhs_addr); return;
                case subcode(7): write<int32_t>(lhs_addr) |= (int32_t)read<int8_t>(rhs_addr); return;
                case subcode(8): write<int32_t>(lhs_addr) |= (int32_t)read<uint8_t>(rhs_addr); return;
                case subcode(9): write<int32_t>(lhs_addr) |= (int32_t)read<int16_t>(rhs_addr); return;
                case subcode(10): write<int32_t>(lhs_addr) |= (int32_t)read<uint16_t>(rhs_addr); return;
                case subcode(11): write<int32_t>(lhs_addr) |= read<int32_t>(rhs_addr); return;
                case subcode(12): write<uint32_t>(lhs_addr) |= (uint32_t)read<uint8_t>(rhs_addr); return;
                case subcode(13): write<uint32_t>(lhs_addr) |= (uint32_t)read<uint16_t>(rhs_addr); return;
                case subcode(14): write<uint32_t>(lhs_addr) |= read<uint32_t>(rhs_addr); return;
                case subcode(15): write<int64_t>(lhs_addr) |= (int64_t)read<int8_t>(rhs_addr); return;
                case subcode(16): write<int64_t>(lhs_addr) |= (int64_t)read<uint8_t>(rhs_addr); return;
                case subcode(17): write<int64_t>(lhs_addr) |= (int64_t)read<int16_t>(rhs_addr); return;
                case subcode(18): write<int64_t>(lhs_addr) |= (int64_t)read<uint16_t>(rhs_addr); return;
                case subcode(19): write<int64_t>(lhs_addr) |= (int64_t)read<int32_t>(rhs_addr); return;
                case subcode(20): write<int64_t>(lhs_addr) |= (int64_t)read<uint32_t>(rhs_addr); return;
                case subcode(21): write<int64_t>(lhs_addr) |= read<int64_t>(rhs_addr); return;
                case subcode(22): write<uint64_t>(lhs_addr) |= (uint64_t)read<uint8_t>(rhs_addr); return;
                case subcode(23): write<uint64_t>(lhs_addr) |= (uint64_t)read<uint16_t>(rhs_addr); return;
                case subcode(24): write<uint64_t>(lhs_addr) |= (uint64_t)read<uint32_t>(rhs_addr); return;
                case subcode(25): write<uint64_t>(lhs_addr) |= read<uint64_t>(rhs_addr); return;
            }
        }

        inline void padd() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            const size_t underlying_size = get_addr_type(false).generated.pointer.underlying_size;
            switch (sub)
            {
                case subcode(0): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<int8_t>(rhs_addr)); return;
                case subcode(1): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<uint8_t>(rhs_addr)); return;
                case subcode(2): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<int16_t>(rhs_addr)); return;
                case subcode(3): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<uint16_t>(rhs_addr)); return;
                case subcode(4): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<int32_t>(rhs_addr)); return;
                case subcode(5): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<uint32_t>(rhs_addr)); return;
                case subcode(6): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * read<size_t>(rhs_addr)); return;
                case subcode(7): write<uint8_t*>(lhs_addr) += ((size_t)underlying_size * (size_t)read<uint64_t>(rhs_addr)); return;
            }
        }
        inline void psub() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            const size_t underlying_size = get_addr_type(false).generated.pointer.underlying_size;
            switch (sub)
            {
                case subcode(0): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<int8_t>(rhs_addr)); return;
                case subcode(1): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<uint8_t>(rhs_addr)); return;
                case subcode(2): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<int16_t>(rhs_addr)); return;
                case subcode(3): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<uint16_t>(rhs_addr)); return;
                case subcode(4): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<int32_t>(rhs_addr)); return;
                case subcode(5): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<uint32_t>(rhs_addr)); return;
                case subcode(6): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * read<size_t>(rhs_addr)); return;
                case subcode(7): write<uint8_t*>(lhs_addr) -= ((size_t)underlying_size * (size_t)read<uint64_t>(rhs_addr)); return;
            }
        }
        inline void pdif() noexcept
        {
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            const offset_t underlying_size = offset_t(get_addr_type(false).generated.pointer.underlying_size);
            const offset_t lhs = reinterpret_cast<offset_t>(dereference(lhs_addr));
            const offset_t rhs = reinterpret_cast<offset_t>(dereference(rhs_addr));
            write<offset_t>(push_return_value(derive_type_index_v<offset_t>)) = (lhs - rhs) / underlying_size;
        }

        inline int32_t cmp() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return compare((int32_t)read<int8_t>(lhs_addr), (int32_t)read<int8_t>(rhs_addr));
                case subcode(1): return compare((int32_t)read<int8_t>(lhs_addr), (int32_t)read<uint8_t>(rhs_addr));
                case subcode(2): return compare((int32_t)read<int8_t>(lhs_addr), (int32_t)read<int16_t>(rhs_addr));
                case subcode(3): return compare((int32_t)read<int8_t>(lhs_addr), (int32_t)read<uint16_t>(rhs_addr));
                case subcode(4): return compare((int32_t)read<int8_t>(lhs_addr), read<int32_t>(rhs_addr));
                case subcode(5): return compare((int64_t)read<int8_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(6): return compare((int64_t)read<int8_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(7): return compare((float)read<int8_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(8): return compare((double)read<int8_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(9): return compare((int32_t)read<uint8_t>(lhs_addr), (int32_t)read<int8_t>(rhs_addr));
                case subcode(10): return compare((int32_t)read<uint8_t>(lhs_addr), (int32_t)read<uint8_t>(rhs_addr));
                case subcode(11): return compare((int32_t)read<uint8_t>(lhs_addr), (int32_t)read<int16_t>(rhs_addr));
                case subcode(12): return compare((int32_t)read<uint8_t>(lhs_addr), (int32_t)read<uint16_t>(rhs_addr));
                case subcode(13): return compare((int32_t)read<uint8_t>(lhs_addr), read<int32_t>(rhs_addr));
                case subcode(14): return compare((int64_t)read<uint8_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(15): return compare((int64_t)read<uint8_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(16): return compare((uint64_t)read<uint8_t>(lhs_addr), read<uint64_t>(rhs_addr));
                case subcode(17): return compare((float)read<uint8_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(18): return compare((double)read<uint8_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(19): return compare((int32_t)read<int16_t>(lhs_addr), (int32_t)read<int8_t>(rhs_addr));
                case subcode(20): return compare((int32_t)read<int16_t>(lhs_addr), (int32_t)read<uint8_t>(rhs_addr));
                case subcode(21): return compare((int32_t)read<int16_t>(lhs_addr), (int32_t)read<int16_t>(rhs_addr));
                case subcode(22): return compare((int32_t)read<int16_t>(lhs_addr), (int32_t)read<uint16_t>(rhs_addr));
                case subcode(23): return compare((int32_t)read<int16_t>(lhs_addr), read<int32_t>(rhs_addr));
                case subcode(24): return compare((int64_t)read<int16_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(25): return compare((int64_t)read<int16_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(26): return compare((float)read<int16_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(27): return compare((double)read<int16_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(28): return compare((int32_t)read<uint16_t>(lhs_addr), (int32_t)read<int8_t>(rhs_addr));
                case subcode(29): return compare((int32_t)read<uint16_t>(lhs_addr), (int32_t)read<uint8_t>(rhs_addr));
                case subcode(30): return compare((int32_t)read<uint16_t>(lhs_addr), (int32_t)read<int16_t>(rhs_addr));
                case subcode(31): return compare((int32_t)read<uint16_t>(lhs_addr), (int32_t)read<uint16_t>(rhs_addr));
                case subcode(32): return compare((int32_t)read<uint16_t>(lhs_addr), read<int32_t>(rhs_addr));
                case subcode(33): return compare((int64_t)read<uint16_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(34): return compare((int64_t)read<uint16_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(35): return compare((uint64_t)read<uint16_t>(lhs_addr), read<uint64_t>(rhs_addr));
                case subcode(36): return compare((float)read<uint16_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(37): return compare((double)read<uint16_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(38): return compare(read<int32_t>(lhs_addr), (int32_t)read<int8_t>(rhs_addr));
                case subcode(39): return compare(read<int32_t>(lhs_addr), (int32_t)read<uint8_t>(rhs_addr));
                case subcode(40): return compare(read<int32_t>(lhs_addr), (int32_t)read<int16_t>(rhs_addr));
                case subcode(41): return compare(read<int32_t>(lhs_addr), (int32_t)read<uint16_t>(rhs_addr));
                case subcode(42): return compare(read<int32_t>(lhs_addr), read<int32_t>(rhs_addr));
                case subcode(43): return compare((int64_t)read<int32_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(44): return compare((int64_t)read<int32_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(45): return compare((float)read<int32_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(46): return compare((double)read<int32_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(47): return compare((int64_t)read<uint32_t>(lhs_addr), (int64_t)read<int8_t>(rhs_addr));
                case subcode(48): return compare((int64_t)read<uint32_t>(lhs_addr), (int64_t)read<uint8_t>(rhs_addr));
                case subcode(49): return compare((int64_t)read<uint32_t>(lhs_addr), (int64_t)read<int16_t>(rhs_addr));
                case subcode(50): return compare((int64_t)read<uint32_t>(lhs_addr), (int64_t)read<uint16_t>(rhs_addr));
                case subcode(51): return compare((int64_t)read<uint32_t>(lhs_addr), (int64_t)read<int32_t>(rhs_addr));
                case subcode(52): return compare(read<uint32_t>(lhs_addr), read<uint32_t>(rhs_addr));
                case subcode(53): return compare((int64_t)read<uint32_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(54): return compare((uint64_t)read<uint32_t>(lhs_addr), read<uint64_t>(rhs_addr));
                case subcode(55): return compare((float)read<uint32_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(56): return compare((double)read<uint32_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(57): return compare(read<int64_t>(lhs_addr), (int64_t)read<int8_t>(rhs_addr));
                case subcode(58): return compare(read<int64_t>(lhs_addr), (int64_t)read<uint8_t>(rhs_addr));
                case subcode(59): return compare(read<int64_t>(lhs_addr), (int64_t)read<int16_t>(rhs_addr));
                case subcode(60): return compare(read<int64_t>(lhs_addr), (int64_t)read<uint16_t>(rhs_addr));
                case subcode(61): return compare(read<int64_t>(lhs_addr), (int64_t)read<int32_t>(rhs_addr));
                case subcode(62): return compare(read<int64_t>(lhs_addr), (int64_t)read<uint32_t>(rhs_addr));
                case subcode(63): return compare(read<int64_t>(lhs_addr), read<int64_t>(rhs_addr));
                case subcode(64): return compare((float)read<int64_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(65): return compare((double)read<int64_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(66): return compare(read<uint64_t>(lhs_addr), (uint64_t)read<uint8_t>(rhs_addr));
                case subcode(67): return compare(read<uint64_t>(lhs_addr), (uint64_t)read<uint16_t>(rhs_addr));
                case subcode(68): return compare(read<uint64_t>(lhs_addr), (uint64_t)read<uint32_t>(rhs_addr));
                case subcode(69): return compare(read<uint64_t>(lhs_addr), read<uint64_t>(rhs_addr));
                case subcode(70): return compare((float)read<uint64_t>(lhs_addr), read<float>(rhs_addr));
                case subcode(71): return compare((double)read<uint64_t>(lhs_addr), read<double>(rhs_addr));
                case subcode(72): return compare(read<float>(lhs_addr), (float)read<int8_t>(rhs_addr));
                case subcode(73): return compare(read<float>(lhs_addr), (float)read<uint8_t>(rhs_addr));
                case subcode(74): return compare(read<float>(lhs_addr), (float)read<int16_t>(rhs_addr));
                case subcode(75): return compare(read<float>(lhs_addr), (float)read<uint16_t>(rhs_addr));
                case subcode(76): return compare(read<float>(lhs_addr), (float)read<int32_t>(rhs_addr));
                case subcode(77): return compare(read<float>(lhs_addr), (float)read<uint32_t>(rhs_addr));
                case subcode(78): return compare(read<float>(lhs_addr), (float)read<int64_t>(rhs_addr));
                case subcode(79): return compare(read<float>(lhs_addr), (float)read<uint64_t>(rhs_addr));
                case subcode(80): return compare(read<float>(lhs_addr), read<float>(rhs_addr));
                case subcode(81): return compare((double)read<float>(lhs_addr), read<double>(rhs_addr));
                case subcode(82): return compare(read<double>(lhs_addr), (double)read<int8_t>(rhs_addr));
                case subcode(83): return compare(read<double>(lhs_addr), (double)read<uint8_t>(rhs_addr));
                case subcode(84): return compare(read<double>(lhs_addr), (double)read<int16_t>(rhs_addr));
                case subcode(85): return compare(read<double>(lhs_addr), (double)read<uint16_t>(rhs_addr));
                case subcode(86): return compare(read<double>(lhs_addr), (double)read<int32_t>(rhs_addr));
                case subcode(87): return compare(read<double>(lhs_addr), (double)read<uint32_t>(rhs_addr));
                case subcode(88): return compare(read<double>(lhs_addr), (double)read<int64_t>(rhs_addr));
                case subcode(89): return compare(read<double>(lhs_addr), (double)read<uint64_t>(rhs_addr));
                case subcode(90): return compare(read<double>(lhs_addr), (double)read<float>(rhs_addr));
                case subcode(91): return compare(read<double>(lhs_addr), read<double>(rhs_addr));
            }
            return 0;
        }
        inline int32_t ceq() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) == (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) == (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) == (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) == (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) == read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) == (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) == (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) == (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) == (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) == read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) == read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) == (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) == (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) == (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) == (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) == read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) == (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) == (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) == (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) == (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) == read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) == read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) == (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) == (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) == (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) == (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) == read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) == (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) == (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) == (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) == (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) == (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) == read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) == read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) == (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) == (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) == (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) == (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) == (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) == (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) == read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) == (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) == (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) == (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) == read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) == read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) == read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) == (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) == (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) == (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) == (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) == (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) == (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) == (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) == (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) == read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) == read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) == (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) == (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) == (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) == (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) == (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) == (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) == (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) == (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) == (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) == read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t cne() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) != (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) != (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) != (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) != (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) != read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) != (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) != (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) != (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) != (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) != read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) != read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) != (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) != (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) != (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) != (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) != read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) != (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) != (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) != (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) != (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) != read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) != read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) != (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) != (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) != (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) != (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) != read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) != (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) != (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) != (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) != (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) != (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) != read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) != read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) != (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) != (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) != (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) != (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) != (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) != (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) != read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) != (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) != (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) != (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) != read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) != read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) != read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) != (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) != (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) != (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) != (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) != (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) != (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) != (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) != (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) != read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) != read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) != (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) != (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) != (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) != (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) != (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) != (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) != (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) != (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) != (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) != read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t cgt() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) > (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) > (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) > (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) > (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) > read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) > (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) > (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) > (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) > (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) > read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) > read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) > (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) > (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) > (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) > (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) > read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) > (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) > (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) > (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) > (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) > read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) > read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) > (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) > (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) > (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) > (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) > read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) > (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) > (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) > (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) > (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) > (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) > read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) > read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) > (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) > (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) > (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) > (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) > (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) > (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) > read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) > (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) > (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) > (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) > read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) > read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) > read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) > (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) > (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) > (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) > (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) > (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) > (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) > (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) > (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) > read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) > read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) > (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) > (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) > (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) > (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) > (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) > (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) > (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) > (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) > (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) > read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t cge() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) >= (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) >= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) >= (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) >= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) >= read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) >= (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) >= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) >= (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) >= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) >= read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) >= read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) >= (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) >= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) >= (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) >= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) >= read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) >= (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) >= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) >= (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) >= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) >= read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) >= read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) >= (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) >= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) >= (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) >= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) >= read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) >= (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) >= (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) >= (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) >= (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) >= (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) >= read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) >= read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) >= (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) >= (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) >= (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) >= (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) >= (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) >= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) >= read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) >= (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) >= (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) >= (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) >= read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) >= (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) >= (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) >= (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) >= (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) >= (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) >= (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) >= (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) >= (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) >= read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) >= read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) >= (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) >= (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) >= (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) >= (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) >= (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) >= (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) >= (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) >= (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) >= (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) >= read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t clt() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) < (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) < (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) < (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) < (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) < read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) < (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) < (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) < (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) < (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) < read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) < read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) < (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) < (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) < (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) < (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) < read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) < (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) < (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) < (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) < (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) < read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) < read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) < (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) < (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) < (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) < (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) < read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) < (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) < (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) < (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) < (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) < (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) < read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) < read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) < (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) < (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) < (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) < (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) < (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) < (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) < read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) < (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) < (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) < (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) < read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) < read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) < read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) < (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) < (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) < (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) < (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) < (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) < (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) < (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) < (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) < read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) < read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) < (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) < (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) < (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) < (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) < (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) < (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) < (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) < (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) < (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) < read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t cle() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);
            auto rhs_addr = read_address(true);

            switch (sub)
            {
                case subcode(0): return (int32_t)read<int8_t>(lhs_addr) <= (int32_t)read<int8_t>(rhs_addr);
                case subcode(1): return (int32_t)read<int8_t>(lhs_addr) <= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(2): return (int32_t)read<int8_t>(lhs_addr) <= (int32_t)read<int16_t>(rhs_addr);
                case subcode(3): return (int32_t)read<int8_t>(lhs_addr) <= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(4): return (int32_t)read<int8_t>(lhs_addr) <= read<int32_t>(rhs_addr);
                case subcode(5): return (int64_t)read<int8_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(6): return (int64_t)read<int8_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(7): return (float)read<int8_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(8): return (double)read<int8_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(9): return (int32_t)read<uint8_t>(lhs_addr) <= (int32_t)read<int8_t>(rhs_addr);
                case subcode(10): return (int32_t)read<uint8_t>(lhs_addr) <= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(11): return (int32_t)read<uint8_t>(lhs_addr) <= (int32_t)read<int16_t>(rhs_addr);
                case subcode(12): return (int32_t)read<uint8_t>(lhs_addr) <= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(13): return (int32_t)read<uint8_t>(lhs_addr) <= read<int32_t>(rhs_addr);
                case subcode(14): return (int64_t)read<uint8_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(15): return (int64_t)read<uint8_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(16): return (uint64_t)read<uint8_t>(lhs_addr) <= read<uint64_t>(rhs_addr);
                case subcode(17): return (float)read<uint8_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(18): return (double)read<uint8_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(19): return (int32_t)read<int16_t>(lhs_addr) <= (int32_t)read<int8_t>(rhs_addr);
                case subcode(20): return (int32_t)read<int16_t>(lhs_addr) <= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(21): return (int32_t)read<int16_t>(lhs_addr) <= (int32_t)read<int16_t>(rhs_addr);
                case subcode(22): return (int32_t)read<int16_t>(lhs_addr) <= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(23): return (int32_t)read<int16_t>(lhs_addr) <= read<int32_t>(rhs_addr);
                case subcode(24): return (int64_t)read<int16_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(25): return (int64_t)read<int16_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(26): return (float)read<int16_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(27): return (double)read<int16_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(28): return (int32_t)read<uint16_t>(lhs_addr) <= (int32_t)read<int8_t>(rhs_addr);
                case subcode(29): return (int32_t)read<uint16_t>(lhs_addr) <= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(30): return (int32_t)read<uint16_t>(lhs_addr) <= (int32_t)read<int16_t>(rhs_addr);
                case subcode(31): return (int32_t)read<uint16_t>(lhs_addr) <= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(32): return (int32_t)read<uint16_t>(lhs_addr) <= read<int32_t>(rhs_addr);
                case subcode(33): return (int64_t)read<uint16_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(34): return (int64_t)read<uint16_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(35): return (uint64_t)read<uint16_t>(lhs_addr) <= read<uint64_t>(rhs_addr);
                case subcode(36): return (float)read<uint16_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(37): return (double)read<uint16_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(38): return read<int32_t>(lhs_addr) <= (int32_t)read<int8_t>(rhs_addr);
                case subcode(39): return read<int32_t>(lhs_addr) <= (int32_t)read<uint8_t>(rhs_addr);
                case subcode(40): return read<int32_t>(lhs_addr) <= (int32_t)read<int16_t>(rhs_addr);
                case subcode(41): return read<int32_t>(lhs_addr) <= (int32_t)read<uint16_t>(rhs_addr);
                case subcode(42): return read<int32_t>(lhs_addr) <= read<int32_t>(rhs_addr);
                case subcode(43): return (int64_t)read<int32_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(44): return (int64_t)read<int32_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(45): return (float)read<int32_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(46): return (double)read<int32_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(47): return (int64_t)read<uint32_t>(lhs_addr) <= (int64_t)read<int8_t>(rhs_addr);
                case subcode(48): return (int64_t)read<uint32_t>(lhs_addr) <= (int64_t)read<uint8_t>(rhs_addr);
                case subcode(49): return (int64_t)read<uint32_t>(lhs_addr) <= (int64_t)read<int16_t>(rhs_addr);
                case subcode(50): return (int64_t)read<uint32_t>(lhs_addr) <= (int64_t)read<uint16_t>(rhs_addr);
                case subcode(51): return (int64_t)read<uint32_t>(lhs_addr) <= (int64_t)read<int32_t>(rhs_addr);
                case subcode(52): return read<uint32_t>(lhs_addr) <= read<uint32_t>(rhs_addr);
                case subcode(53): return (int64_t)read<uint32_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(54): return (uint64_t)read<uint32_t>(lhs_addr) <= read<uint64_t>(rhs_addr);
                case subcode(55): return (float)read<uint32_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(56): return (double)read<uint32_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(57): return read<int64_t>(lhs_addr) <= (int64_t)read<int8_t>(rhs_addr);
                case subcode(58): return read<int64_t>(lhs_addr) <= (int64_t)read<uint8_t>(rhs_addr);
                case subcode(59): return read<int64_t>(lhs_addr) <= (int64_t)read<int16_t>(rhs_addr);
                case subcode(60): return read<int64_t>(lhs_addr) <= (int64_t)read<uint16_t>(rhs_addr);
                case subcode(61): return read<int64_t>(lhs_addr) <= (int64_t)read<int32_t>(rhs_addr);
                case subcode(62): return read<int64_t>(lhs_addr) <= (int64_t)read<uint32_t>(rhs_addr);
                case subcode(63): return read<int64_t>(lhs_addr) <= read<int64_t>(rhs_addr);
                case subcode(64): return (float)read<int64_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(65): return (double)read<int64_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(66): return read<uint64_t>(lhs_addr) <= (uint64_t)read<uint8_t>(rhs_addr);
                case subcode(67): return read<uint64_t>(lhs_addr) <= (uint64_t)read<uint16_t>(rhs_addr);
                case subcode(68): return read<uint64_t>(lhs_addr) <= (uint64_t)read<uint32_t>(rhs_addr);
                case subcode(69): return read<uint64_t>(lhs_addr) <= read<uint64_t>(rhs_addr);
                case subcode(70): return (float)read<uint64_t>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(71): return (double)read<uint64_t>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(72): return read<float>(lhs_addr) <= (float)read<int8_t>(rhs_addr);
                case subcode(73): return read<float>(lhs_addr) <= (float)read<uint8_t>(rhs_addr);
                case subcode(74): return read<float>(lhs_addr) <= (float)read<int16_t>(rhs_addr);
                case subcode(75): return read<float>(lhs_addr) <= (float)read<uint16_t>(rhs_addr);
                case subcode(76): return read<float>(lhs_addr) <= (float)read<int32_t>(rhs_addr);
                case subcode(77): return read<float>(lhs_addr) <= (float)read<uint32_t>(rhs_addr);
                case subcode(78): return read<float>(lhs_addr) <= (float)read<int64_t>(rhs_addr);
                case subcode(79): return read<float>(lhs_addr) <= (float)read<uint64_t>(rhs_addr);
                case subcode(80): return read<float>(lhs_addr) <= read<float>(rhs_addr);
                case subcode(81): return (double)read<float>(lhs_addr) <= read<double>(rhs_addr);
                case subcode(82): return read<double>(lhs_addr) <= (double)read<int8_t>(rhs_addr);
                case subcode(83): return read<double>(lhs_addr) <= (double)read<uint8_t>(rhs_addr);
                case subcode(84): return read<double>(lhs_addr) <= (double)read<int16_t>(rhs_addr);
                case subcode(85): return read<double>(lhs_addr) <= (double)read<uint16_t>(rhs_addr);
                case subcode(86): return read<double>(lhs_addr) <= (double)read<int32_t>(rhs_addr);
                case subcode(87): return read<double>(lhs_addr) <= (double)read<uint32_t>(rhs_addr);
                case subcode(88): return read<double>(lhs_addr) <= (double)read<int64_t>(rhs_addr);
                case subcode(89): return read<double>(lhs_addr) <= (double)read<uint64_t>(rhs_addr);
                case subcode(90): return read<double>(lhs_addr) <= (double)read<float>(rhs_addr);
                case subcode(91): return read<double>(lhs_addr) <= read<double>(rhs_addr);
            }
            return 0;
        }
        inline int32_t cze() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);

            switch (sub)
            {
                case subcode(0): return read<int8_t>(lhs_addr) == 0;
                case subcode(1): return read<uint8_t>(lhs_addr) == 0;
                case subcode(2): return read<int16_t>(lhs_addr) == 0;
                case subcode(3): return read<uint16_t>(lhs_addr) == 0;
                case subcode(4): return read<int32_t>(lhs_addr) == 0;
                case subcode(5): return read<uint32_t>(lhs_addr) == 0;
                case subcode(6): return read<int64_t>(lhs_addr) == 0;
                case subcode(7): return read<uint64_t>(lhs_addr) == 0;
                case subcode(8): return read<float>(lhs_addr) == 0;
                case subcode(9): return read<double>(lhs_addr) == 0;
            }
            return 0;
        }
        inline int32_t cnz() noexcept
        {
            const subcode sub = read_subcode();
            auto lhs_addr = read_address(false);

            switch (sub)
            {
                case subcode(0): return read<int8_t>(lhs_addr) != 0;
                case subcode(1): return read<uint8_t>(lhs_addr) != 0;
                case subcode(2): return read<int16_t>(lhs_addr) != 0;
                case subcode(3): return read<uint16_t>(lhs_addr) != 0;
                case subcode(4): return read<int32_t>(lhs_addr) != 0;
                case subcode(5): return read<uint32_t>(lhs_addr) != 0;
                case subcode(6): return read<int64_t>(lhs_addr) != 0;
                case subcode(7): return read<uint64_t>(lhs_addr) != 0;
                case subcode(8): return read<float>(lhs_addr) != 0;
                case subcode(9): return read<double>(lhs_addr) != 0;
            }
            return 0;
        }

        inline void br() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            jump(branch_location);
        }
        inline void beq() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (ceq()) jump(branch_location);
        }
        inline void bne() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cne()) jump(branch_location);
        }
        inline void bgt() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cgt()) jump(branch_location);
        }
        inline void bge() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cge()) jump(branch_location);
        }
        inline void blt() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (clt()) jump(branch_location);
        }
        inline void ble() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cle()) jump(branch_location);
        }
        inline void bze() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cze()) jump(branch_location);
        }
        inline void bnz() noexcept
        {
            const uint32_t branch_location = read_bytecode<uint32_t>(iptr);

            if (cnz()) jump(branch_location);
        }

        inline void sw() noexcept
        {
            const uint8_t* idx_addr = read_address(false);

            uint32_t idx = 0;
            switch (addr_type[false])
            {
                case type_idx::i8: idx = (uint32_t)read<i8>(idx_addr); break;
                case type_idx::u8: idx = (uint32_t)read<u8>(idx_addr); break;
                case type_idx::i16: idx = (uint32_t)read<i16>(idx_addr); break;
                case type_idx::u16: idx = (uint32_t)read<u16>(idx_addr); break;
                case type_idx::i32: idx = (uint32_t)read<i32>(idx_addr); break;
                case type_idx::u32: idx = (uint32_t)read<u32>(idx_addr); break;
                case type_idx::i64: idx = (uint32_t)read<i64>(idx_addr); break;
                case type_idx::u64: idx = (uint32_t)read<u64>(idx_addr); break;
            }

            const uint32_t label_count = read_bytecode<uint32_t>(iptr);

            const uint32_t* labels = reinterpret_cast<const uint32_t*>(iptr);
            iptr += sizeof(uint32_t) * label_count;

            if (idx < label_count)
            {
                jump(labels[idx]);
            }
        }

        inline void jump(uint32_t target) noexcept
        {
            iptr = ibeg + target;

            clear_return_value();
        }

        inline void call()
        {
            const method_idx call_idx = read_bytecode<method_idx>(iptr);
            ASSERT(is_valid_method(call_idx), "Attempted to invoke an invalid method");
            const auto& call_method = get_method(call_idx);
            push_stack_frame(call_method, get_signature(call_method.signature));
        }
        inline void callv()
        {
            const uint8_t* method_ptr = read_address(false);
            size_t method_handle = *reinterpret_cast<const size_t*>(method_ptr);
            ASSERT(method_handle != 0, "Attempted to invoke a null method pointer");
            method_handle ^= data.runtime_hash;
            ASSERT(is_valid_method(method_handle), "Attempted to invoke an invalid method pointer");
            const method& call_method = get_method(method_idx(method_handle));
            push_stack_frame(call_method, get_signature(get_addr_type(false).generated.signature.index));
        }
        inline void ret()
        {
            clear_return_value();

            pop_stack_frame();
        }
        inline void retv()
        {
            const subcode sub = read_subcode();
            const uint8_t* ret_value = read_address(true);

            // Set return value (of the current signature)
            return_value_addr = stack.data + sf.return_offset;
            return_value_type = current_signature->return_type;

            set(sub, return_value_addr, ret_value);

            pop_stack_frame();
        }

        inline void dump()
        {
            const uint8_t* src_addr = read_address(true);

            dump_recursive(src_addr, get_addr_type(true));

            std::cout << std::endl;
        }


        void dump_recursive(const uint8_t* addr, const type& type)
        {
            std::cout << get_name(type);
            switch (type.index)
            {
                case type_idx::i8: dump_var<i8>(addr); break;
                case type_idx::u8: dump_var<u8>(addr); break;
                case type_idx::i16: dump_var<i16>(addr); break;
                case type_idx::u16: dump_var<u16>(addr); break;
                case type_idx::i32: dump_var<i32>(addr); break;
                case type_idx::u32: dump_var<u32>(addr); break;
                case type_idx::i64: dump_var<i64>(addr); break;
                case type_idx::u64: dump_var<u64>(addr); break;
                case type_idx::f32: dump_var<f32>(addr); break;
                case type_idx::f64: dump_var<f64>(addr); break;
                default:
                {
                    if (type.is_pointer() || type.is_signature())
                    {
                        std::cout << '(' << (void*)*reinterpret_cast<const uint8_t* const*>(addr) << ')';
                    }
                    else if (type.is_array())
                    {
                        std::cout << '{';
                        const uint8_t* ptr = addr;
                        const auto& underlying_type = get_type(type.generated.array.underlying_type);
                        for (size_t i = 0; i < type.generated.array.array_size; i++)
                        {
                            std::cout << (i == 0 ? " " : ", ");
                            dump_recursive(ptr + underlying_type.total_size * i, get_type(underlying_type.index));
                        }
                        std::cout << " }";
                    }
                    else if (!type.fields.empty())
                    {
                        std::cout << '{';
                        for (size_t i = 0; i < type.fields.size(); i++)
                        {
                            auto& field = type.fields[i];
                            std::cout << (i == 0 ? " " : ", ");
                            std::cout << database[field.name] << " = ";
                            dump_recursive(addr + field.offset, get_type(field.type));
                        }
                        std::cout << " }";
                    }
                    else
                    {
                        std::cout << "(?)";
                    }
                }
                break;
            }
        }
        string_view get_name(const type& t) const
        {
            if (t.name != name_idx::invalid)
            {
                return database[t.name];
            }
            else
            {
                size_t& index = const_cast<size_t&>(generated_name_index);
                string& buffer = const_cast<string*>(generated_name_buffers)[index];
                data.generate_name(t.index, buffer);
                index = (index + 1) & 1;
                return buffer;
            }
        }


        inline const type& get_type(type_idx type) const noexcept
        {
            return types[static_cast<size_t>(type)];
        }
        inline const method& get_method(method_idx method) const noexcept
        {
            return methods[static_cast<size_t>(method)];
        }
        inline const signature& get_signature(signature_idx signature) const noexcept
        {
            return signatures[static_cast<size_t>(signature)];
        }
        inline bool is_valid_method(method_idx method) const noexcept
        {
            return static_cast<size_t>(method) < data.methods.size();
        }
        inline bool is_valid_method(size_t method_handle) const noexcept
        {
            return method_handle < data.methods.size();
        }

        inline subcode read_subcode() noexcept
        {
            return read_bytecode<subcode>(iptr);
        }
        uint8_t* read_address(bool is_rhs) noexcept
        {
            uint8_t* result = nullptr;

            const address_data_t& addr = *reinterpret_cast<const address_data_t*>(iptr);

            const auto& minf = *current_method;
            const auto& csig = *current_signature;

            const uint32_t index = addr.header.index();
            switch (addr.header.type())
            {
                case address_type::stackvar:
                {
                    if (index == address_header_constants::index_max)
                    {
                        result = return_value_addr;
                        addr_type[is_rhs] = return_value_type;
                    }
                    else
                    {
                        const auto& stack_var = minf.stackvars[index];
                        const size_t offset = sf.stack_offset + stack_var.offset;

                        result = stack.data + offset;
                        addr_type[is_rhs] = stack_var.type;
                    }
                }
                break;

                case address_type::parameter:
                {
                    const auto& param = csig.parameters[index];
                    const size_t offset = sf.param_offset + param.offset;

                    result = stack.data + offset;
                    addr_type[is_rhs] = param.type;
                }
                break;

                case address_type::global:
                {
                    global_idx global = (global_idx)index;

                    const data_table_view& table = global_tables[is_constant_flag_set(global)];
                    global &= global_flags::constant_mask;

                    const auto& global_info = table[global];
                    result = table.data + global_info.offset;
                    addr_type[is_rhs] = global_info.type;
                }
                break;

                case address_type::constant:
                {
                    const type_idx btype_idx = type_idx(index);
                    iptr += sizeof(address_header);
                    uint8_t* ptr = (uint8_t*)iptr;
                    const auto& type = get_type(btype_idx);
                    iptr += type.total_size;
                    addr_type[is_rhs] = type.index;
                    return ptr;
                }
                break;
            }

            switch (addr.header.modifier())
            {
                case address_modifier::none: break;

                case address_modifier::direct_field:
                {
                    const auto& field = offsets[static_cast<size_t>(addr.field)];
                    result += field.offset;
                    addr_type[is_rhs] = field.type;
                }
                break;

                case address_modifier::indirect_field:
                {
                    const auto& field = offsets[static_cast<size_t>(addr.field)];
                    result = dereference(result) + field.offset;
                    addr_type[is_rhs] = field.type;
                }
                break;

                case address_modifier::offset:
                {
                    const auto& current_type = get_addr_type(is_rhs);
                    if (current_type.is_pointer())
                    {
                        const type& underlying_type = get_type(current_type.generated.pointer.underlying_type);
                        result = dereference(result) + underlying_type.total_size * addr.offset;
                        addr_type[is_rhs] = underlying_type.index;
                    }
                    else if (current_type.is_array())
                    {
                        const type& underlying_type = get_type(current_type.generated.array.underlying_type);
                        result = result + underlying_type.total_size * addr.offset;
                        addr_type[is_rhs] = underlying_type.index;
                    }
                }
                break;
            }

            switch (addr.header.prefix())
            {
                case address_prefix::none: break;

                case address_prefix::indirection:
                {
                    const auto& current_type = get_addr_type(is_rhs);
                    result = dereference(result);
                    addr_type[is_rhs] = current_type.generated.pointer.underlying_type;
                }
                break;

                case address_prefix::address_of:
                {
                    tmp_var[is_rhs] = reinterpret_cast<size_t>(result);
                    result = reinterpret_cast<uint8_t*>(&tmp_var[is_rhs]);

                    const auto& current_type = get_addr_type(is_rhs);
                    const type_idx dst_type = current_type.pointer_type;
                    addr_type[is_rhs] = dst_type == type_idx::invalid ? type_idx::vptr : dst_type;
                }
                break;

                case address_prefix::size_of:
                {
                    tmp_var[is_rhs] = get_addr_type(is_rhs).total_size;
                    result = reinterpret_cast<uint8_t*>(&tmp_var[is_rhs]);

                    addr_type[is_rhs] = derive_type_index_v<size_t>;
                }
                break;
            }

            iptr += sizeof(address_data_t);

            return result;
        }

        void push_stack_frame(const method& method, const signature& calling_signature)
        {
            const signature& signature = get_signature(method.signature);
            ASSERT(signature.index == calling_signature.index, "Call signature mismatch");

            const auto& bytecode = method.bytecode;

            const size_t frame_offset = stack.size;
            const size_t return_offset = sf.stack_end;

            if (!method.is_external())
            {
                callstack_depth++;
                VALIDATE_CALLSTACK_LIMIT(callstack_depth <= parameters.max_callstack_depth, parameters.max_callstack_depth);

                const size_t param_offset = frame_offset + sizeof(stack_frame_t);
                uint8_t* param_ptr = stack.data + param_offset;
                const size_t stack_offset = param_offset + calling_signature.parameters_size;

                // Push method stack size
                const size_t stack_end = stack.size + method.method_stack_size + sizeof(stack_frame_t);
                const size_t new_stack_size = stack.size + method.total_stack_size + sizeof(stack_frame_t);
                VALIDATE_STACK_OVERFLOW(new_stack_size <= stack.capacity, new_stack_size, stack.capacity);
                stack.size = new_stack_size;

                // Write parameters
                const size_t parameter_count = calling_signature.parameters.size();
                const size_t arg_count = iptr ? static_cast<size_t>(read_bytecode<uint8_t>(iptr)) : 0;
                ASSERT(arg_count == parameter_count, "Invalid argument count");
                if (parameter_count > 0)
                {
                    uint8_t* param_ptr = stack.data + param_offset;
                    for (size_t i = 0; i < parameter_count; i++)
                    {
                        const stackvar& parameter = calling_signature.parameters[i];
                        const subcode sub = read_subcode();
                        const uint8_t* arg_addr = read_address(true);
                        uint8_t* param_addr = param_ptr + parameter.offset;
                        set(sub, param_addr, arg_addr);
                    }
                }

                // Write stack frame
                sf.iptr = iptr - ibeg;
                *reinterpret_cast<stack_frame_t*>(stack.data + frame_offset) = sf;

                // Call
                sf = stack_frame_t(0, return_offset, frame_offset, param_offset, stack_offset, stack_end, method.index);
                ibeg = iptr = bytecode.data();
                iend = ibeg + bytecode.size();
                current_method = &method;
                current_signature = &signature;

                // Clear return value after a call
                clear_return_value();
            }
            else
            {
                ASSERT(bytecode.size() == sizeof(runtime_call_index), "Invalid external index");
                const runtime_call_index cidx = *reinterpret_cast<const runtime_call_index*>(bytecode.data());
                
                // Ensure method handle
                ASSERT(libraries.is_valid_index(cidx.library), "Invalid library index");
                auto& lib = libraries[cidx.library];
                ASSERT(lib.calls.is_valid_index(cidx.index), "Invalid call index");
                auto& call = lib.calls[cidx.index];
                if (!call.handle)
                {
                    if (!lib.handle.is_open())
                    {
                        const bool opened = lib.handle.open();
                        ASSERT(opened, "Failed to load library");
                    }
                    call.handle = lib.handle.get_proc(call.name.data());
                    ASSERT(call.handle, "Failed to find function");
                }

                // Push method stack size (parameters only for external methods)
                const size_t param_offset = stack.size;
                uint8_t* param_ptr = stack.data + param_offset;
                if (method.total_stack_size > 0)
                {
                    const size_t new_stack_size = stack.size + (method.total_stack_size);
                    VALIDATE_STACK_OVERFLOW(new_stack_size <= stack.capacity, new_stack_size, stack.capacity);
                    stack.size = new_stack_size;
                }

                // Write parameters
                const size_t parameter_count = calling_signature.parameters.size();
                const size_t arg_count = iptr ? static_cast<size_t>(read_bytecode<uint8_t>(iptr)) : 0;
                ASSERT(arg_count == parameter_count, "Invalid argument count");
                if (parameter_count > 0)
                {
                    for (size_t i = 0; i < parameter_count; i++)
                    {
                        const stackvar& parameter = calling_signature.parameters[i];
                        const subcode sub = read_subcode();
                        const uint8_t* arg_addr = read_address(true);
                        uint8_t* param_addr = param_ptr + parameter.offset;
                        set(sub, param_addr, arg_addr);
                    }
                }

                // Invoke external
                call.forward(call.handle, stack.data + return_offset, stack.data + param_offset);

                // Set return value here since we return immediately
                const type_idx return_type_idx = calling_signature.return_type;
                return_value_addr = stack.data + return_offset;
                return_value_type = return_type_idx;
                
                // Pop stackframe
                stack.size = frame_offset;
            }
        }
        void pop_stack_frame()
        {
            // Restore stackframe
            ASSERT(callstack_depth > 0, "Stack frame pop overflow");
            sf = *reinterpret_cast<stack_frame_t*>(stack.data + sf.frame_offset);
            if (sf.method != method_idx::invalid)
            {
                const method& calling_method = get_method(sf.method);
                current_method = &calling_method;
                current_signature = &get_signature(calling_method.signature);
                const auto& bytecode = calling_method.bytecode;
                ibeg = bytecode.data();
                iend = ibeg + bytecode.size();
                iptr = ibeg + sf.iptr;
            }
            else
            {
                current_method = nullptr;
                current_signature = nullptr;
                iptr = ibeg = iend = nullptr;
            }
            callstack_depth--;
        }

        inline uint8_t* push_return_value(type_idx type) noexcept
        {
            return_value_addr = stack.data + sf.stack_end;
            return_value_type = type;
            return return_value_addr;
        }
        inline void clear_return_value() noexcept
        {
            return_value_addr = nullptr;
            return_value_type = type_idx::voidtype;
        }


        inline stack_data_t allocate_stack(const runtime_parameters& parameters)
        {
            uint8_t* data = nullptr;
            size_t capacity = 0;

            // Try and find a stack that fits
            for (size_t i = sizeof(size_t) * 8; i > 0; i--)
            {
                capacity = (static_cast<size_t>(1) << static_cast<size_t>(i - 1));
                if (capacity >= parameters.min_stack_size && capacity <= parameters.max_stack_size)
                {
                    data = (uint8_t*)malloc(capacity);
                    if (data) break;
                }
            }

            VALIDATE_STACK_ALLOCATION(data != nullptr);

            return stack_data_t(data, capacity);
        }


        // Stack data
        stack_data_t stack;

        // Temporary variables
        size_t tmp_var[2];
        type_idx addr_type[2];
        inline const type& get_addr_type(bool is_rhs) noexcept
        {
            return get_type(addr_type[is_rhs]);
        }

        // Return value
        uint8_t* return_value_addr;
        type_idx return_value_type;

        // Instruction pointer
        const uint8_t* iptr = nullptr;
        const uint8_t* ibeg = nullptr;
        const uint8_t* iend = nullptr;

        // Current method
        const method* current_method = nullptr;
        const signature* current_signature = nullptr;

        // Stack frame
        stack_frame_t sf;
        uint32_t callstack_depth = 0;

        // Lookup
        const type* const types;
        const method* const methods;
        const signature* const signatures;
        const field_offset* const offsets;

        // Globals/constants
        block<uint8_t> global_data;
        data_table_view global_tables[2];

        // Externals
        indexed_vector<name_idx, runtime_library> libraries;

        // Strings
        const string_table<name_idx>& database;
        string generated_name_buffers[2];
        size_t generated_name_index = 0;

        // Input data
        const assembly_data& data;
        const runtime_parameters parameters;

        int32_t return_code = 0;
    };


    int32_t runtime::execute(const assembly& linked_assembly, runtime_parameters parameters) const
    {
        VALIDATE_ASSEMBLY(linked_assembly.is_valid());
        VALIDATE_COMPATIBILITY(linked_assembly.is_compatible());

        // Find main
        const assembly_data& asm_data = linked_assembly.assembly_ref();
        VALIDATE_ENTRYPOINT(asm_data.methods.is_valid_index(asm_data.main));

        // Setup runtime
        const auto& rt_data = self();
        VALIDATE_RUNTIME_HASH(asm_data.runtime_hash == rt_data.hash);

        // Copy assembly binary into a protected memory area
        const auto asm_binary = linked_assembly.assembly_binary();
        host_memory host_mem(asm_binary.size());
        ASSERT(host_mem, "Failed to allocate memory pages from host");
        memcpy(host_mem.data(), asm_binary.data(), asm_binary.size());
        const bool protect = host_mem.protect();
        ASSERT(protect, "Failed to switch host memory pages to protected");

        // Execute
        const assembly_data& protected_data = *reinterpret_cast<assembly_data*>(host_mem.data());
        return interpreter(protected_data, protected_data.methods[protected_data.main], rt_data, parameters);
    }
}