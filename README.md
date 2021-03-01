# Propane Intermediate Language

## About Propane

Propane is an intermediate bytecode language that can be executed in an interpreter or cross-compiled into other destination languages. Originally designed to generate C code, Propane can be used to generate any language that supports pointer arithmetics, arrays and structs.

This repository contains an experimental toolchain written in C++ that has a compiler front-end to allow implementation of custom parsers and generators, an interpreter for runtime execution and a cross-compiler to generate C code from Propane assemblies. It also includes a parser for a text representation of the intermediate language to allow writing assemblies by hand for testing purposes.

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

Propane assemblies are exported as binary blobs that contain all the necessary information for cross compilation including field names, label locations, stack offsets, method signatures etc.

## Experimental Toolchain

The experimental toolchain is intended to serve as a proof of concept for the intermediate language. The layout of the toolchain is as following:

![alt text](https://github.com/AggroBird/propane/blob/main/toolchain.png?raw=true "Toolchain Graph Image")

Propane can be parsed from text files or generated by a generator. The resulting intermediate files contain data of defined types/methods and references to declared types/methods. These intermediates can be merged together. The merge implementation is independent, so the merge process between two intermediates can be performed on a seperate thread (at least two intermediates per thread). Intermediates are implemented as binary blobs, which makes it easy for them to be stored on disk to prevent regeneration of unchanged code, or stored as a static library for later.

At the end of the merge process, only one intermediate should remain which contains all definitions. Any missing definitions of types/methods will prevent the intermediate to be linked into an assembly. Like intermediates, an assembly is implemented as a binary blob that can be stored on disk as a file.

Assemblies can be fed into a translator to generate other programming languages or executed by an interpreter.

## Resources

- [Propane intermediate language specification](LANGUAGE.md)
- [Toolchain example](example.cpp) C++ example of generating an assembly out of intermediates, and translation to C.
- [Generator header](include/propane_generator.hpp) Main header required for parsers and compilers.
- [Runtime header](include/propane_runtime.hpp) Assembly data required for cross compilers and interpreters.
- [Experimental C translator](src/translator_c.cpp) Experimental implementation of a Propane assembly to C code generator.
- [Experimental interpreter](src/interpreter.cpp) Experimental implementation of a Propane assembly interpreter.

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

Propane has been conceived as a study project. Propane is intended to be used for building small tools and games. The experimental toolchain has not been tested in professional environments or large projects.

## Dependencies

The Propane toolchain requires C++20 to build.

## License

Propane is licensed under the [MIT](LICENSE) license.