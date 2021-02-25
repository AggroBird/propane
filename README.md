# Propane

## About Propane

Propane is an intermediate bytecode language that can be executed in an interpreter or cross-compiled into other destination languages. Originally designed to generate C code, Propane can be used to generate any language that supports pointer arithmetics, arrays and structs.

This repository contains an experimental toolchain written in C++ with a compiler front-end to allow implementation of custom parsers and generators, an interpreter for runtime execution and a cross-compiler to generate C code from Propane assemblies. It also includes a parser for a prototype programming language to allow writing assemblies by hand for testing purposes.

Typical Propane code can look like this:

```c
method main returns int
	stack
    	0: int
    	1: int
	end
    
	// Add two numbers and return the result
	set {0} 3
	set {1} 5
    
	add {0} {1}
    
	retv {0}
end
```

Running this through the provided C generator would yield the following result:

```c
int32_t $main()
{
	int32_t $0s = 3;
	int32_t $1s = 5;
	$0s += $1s;
	return $0s;
}
```

Propane assemblies are exported as binary blobs that contain all the necessary information for cross compilation including field names, label locations, method signatures etc.

## Resources

- <INSTRUCTION BINARY FORMAT AND EXAMPLES>
- <ASSEMBLY CONTENT INFO>
- <LINK TO GENERATOR HEADER>
- <LINK TO C GENERATOR>
- <LINK TO INTERPRETER>

## Current features

- Turing-complete stack based reduced instruction set
- Support for methods, structs, pointers, virtual calls, branches, etc.
- Interpreter to execute bytecode directly in a runtime
- Generator that converts assemblies into C code, ready to be compiled to any platform
- Tools to generate Propane from and to text files via a prototype programming language
- Call methods from a subset of the C standard library

## Potential future additions

- Strings
- Alignment and padding
- Compile time optimization
- Multithreading
- Dynamic libraries

## Motivations

Propane has been conceived as a study project. Propane is intended to be used for building small tools and games. The toolchain has not been tested in professional environments or large projects.

## Dependencies

The Propane toolchain uses C++17 std::string_view and C++20 std::span. All other code should be C++14 compatible.

## License

Propane is licensed under the [MIT](LICENSE) license.