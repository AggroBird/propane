#ifndef WITHOUT_EXAMPLE

#include "propane_generator.hpp"
#include "propane_parser.hpp"
#include "propane_assembly.hpp"
#include "propane_translator.hpp"

#include <cinttypes>
#include <iostream>

int32_t main()
{
    try
    {
        // Example assembly generation
        propane::generator gen("example");

        // Create an entry point
        const propane::method_idx main_idx = gen.declare_method("main");
        const propane::signature_idx main_sig = gen.make_signature(propane::type_idx::i32, {});
        propane::generator::method_writer& main = gen.define_method(main_idx, main_sig);

        // Create a stack with two integers, 4 and 5, and an extra number to hold the result
        main.set_stack({
                propane::type_idx::i32,
                propane::type_idx::i32,
                propane::type_idx::i32
            });
        main.write_set(propane::stack(0), propane::constant(4));
        main.write_set(propane::stack(1), propane::constant(5));

        // Create a function that multiplies two numbers
        const propane::method_idx mult_idx = gen.declare_method("MultiplyNumbers");
        const propane::signature_idx mult_sig = gen.make_signature(propane::type_idx::i32,
            { propane::type_idx::i32, propane::type_idx::i32 });
        propane::generator::method_writer& mult = gen.define_method(mult_idx, mult_sig);
        mult.write_mul(propane::param(0), propane::param(1));
        mult.write_retv(propane::param(0));

        // Call and store the result in the extra int
        main.write_call(mult_idx, { propane::stack(0), propane::stack(1) });
        main.write_set(propane::stack(2), propane::retval());

        // Foward declare a method that adds two numbers
        // (the definition will be imported from text later)
        const propane::method_idx add_idx = gen.declare_method("AddNumbers");

        // Add an extra constant (and store the result again)
        main.write_call(add_idx, { propane::stack(2), propane::constant(15) });
        main.write_set(propane::stack(2), propane::retval());

        // Print our result (should print 35)
        main.write_dump(propane::stack(2));

        // Return
        main.write_retv(propane::constant(0));


        // Get intermediate from generator
        propane::intermediate generated = gen.finalize();

        // Import the definition of AddNumbers method from the example text file
        propane::intermediate parsed = propane::parser<propane::language_propane>::parse("examples/example_method.ptf");


        // Merge the two intermediates together
        generated += parsed;

        // Link into an assembly
        propane::assembly assembly = propane::assembly(generated);


        // Translate assembly into C and Propane text
        propane::translator<propane::language_c>::generate("generated_example.c", assembly);
        propane::translator<propane::language_propane>::generate("generated_example.ptf", assembly);


        // Execute the example
        return propane::execute_assembly(assembly);
    }
    catch (propane::propane_exception& e)
    {
        std::cout << e.what() << std::endl;
        return -1;
    }
}

#endif