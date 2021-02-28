# Propane Intermediate Language Specification

The following documents defines the Propane language standard as implemented by the standard Propane toolchain.

:warning: **This document is incomplete, and will continue to undergo changes.**

## Types

Propane is a strongly typed language. Because Propane is designed to be translatable to C, the language shares many similarities to the C standard.

### Base types

The Propane language guarantees the following types exist with the following sizes:

```
byte     signed 8 bit integer
ubyte    unsigned 8 bit integer
short    signed 16 bit integer
ushort   unsigned 16 bit integer
int      signed 32 bit integer
uint     unsigned 32 bit integer
long     signed 64 bit integer
ulong    unsigned 64 bit integer
float    32 bit floating point
double   64 bit floating point
```

Additionally, the alias types `size` and `offset` will refer to the native integer types for the current architecture:
* On 32 bit architectures this will be uint and int respectively.
* On 64 bit architectures this will be ulong and long respectively.

Propane implements `void` as an incomplete type. It cannot be declared on a stack or used as a method parameter. However, pointers to void and methods returning void are permitted.

### Pointer types

```c
int*
```

Pointer types behave the same as in C. Pointer arithmetic is automatically scaled by the size of the underlying data type.

The size of a pointer type is dependant on the architecture, but is guaranteed to be the same size as the `size` and `offset` types.

### Array types

```c
int[5]
```

Array types are defined to be fixed, static sized collections of a type. Unlike C, arrays in Propane are passed by value, and arithmetic operations performed on a pointer-to-array will scale by the full array size.

### Signature types

```c
void(int,int)
```

Signature types are implemented as a pointer to a method. Signature types are implemented as an abstract handle to a method that can be dynamically invoked. Unlike C, method pointers cannot be explicitly converted to other types. Signature types can only be assigned with the address of a method which signature exactly matches the parameters and return type, other method pointers of the same type, or a null pointer. Attempting invoke method pointers converted through pointer conversions will result in undefined behaviour.

The size of a method pointer is implementation specific and cannot be guaranteed. Extra care should be taken when using method pointers as fields in structs.

### Structure and union types

```c
struct vector3
	float x
	float y
	float z
end
```

Like C, Propane includes structures and unions. Structures allow nesting of other types.

The memory layout of a structure is implementation specific and cannot be guaranteed. The memory address of the first member must be the same as the address of structure itself. Unions are guaranteed to have all values at zero offset.

## Globals and constants

```c
global
	int global_integer 15
	vector3 global_vec3 1.0f 2.0f 3.0f
end
```

Global are defined as unique data that can be accessed from any method. Globals exist at application startup and have a predetermined value. Globals that do not get initialized with a value default to zero. Uninitialized global data will default to zero.

Constants are similar to globals in scope, but must their value must be immutable. Despite being immuatble, it is valid to take the address of a constant and use constants in arithmetic expressions. Modifying the value of a constant directly or indirectly through pointer access will result in undefined behaviour. Unlike globals, constants are not guaranteed to exist in memory at a specified location, as compilers might optimize or inline constant values depending on their usage.

Global signature types can be initialized with a method address or a null pointer and changed at runtime, while constant signature types must always be initialized with a valid method address.

## Methods

```c
method add_numbers returns int
	parameters
		0: int
		1: int
	end
	
	stack
		0: int
	end
	
	set {0} (0)
	add {0} (1)
	
	retv {0}
end
```

Methods in Propane are implemented as subroutines that can be invoked from any other method. Propane methods have no support for overloading and each method name has to be unique. Arguments passed to a method are copied by value and methods cannot modify any variables of the calling method's stack, unless passed by pointer.

Methods variable stacks are fixed size and cannot grow or shrink at runtime. Like in C, stack data is not initialized and accessing uninitialized data will result in undefined behaviour.

## Instruction set

### Zero instruction

Noop does nothing.

```
noop
```

### Assignment instructions

`set` and `conv` (convert) provide implicit and explicit conversion between different or equal types (see Conversion rules).

```
set    <address>    <address/constant>    (implicit conversion)
conv   <address>    <address/constant>    (explicit conversion)
```

### Arithmetic instructions

Arithmetic instructions require arithmetic (integral and floating point) types. Right-hand operand must be compatible with destination left-hand operand (see Conversion rules).

```
not    <address>                          (bitwise complement)
neg    <address>                          (negate)
mul    <address>    <address/constant>    (multiply)
div    <address>    <address/constant>    (divide)
mod    <address>    <address/constant>    (modulus)
add    <address>    <address/constant>    (addition)
sub    <address>    <address/constant>    (subtraction)
lsh    <address>    <address/constant>    (left shift)
rsh    <address>    <address/constant>    (right shift)
and    <address>    <address/constant>    (bitwise and)
xor    <address>    <address/constant>    (bitwise exclusive or)
or     <address>    <address/constant>    (bitwise or)
```

### Pointer instructions

Pointer instructions require pointer types as left-hand operand. Left-hand operand cannot be void pointer type.
* `padd` and `psub` will add or substract the operand scaled by the underlying type size. The right-hand operand must be integral.
* `pdif` will take the offset between two pointers, divided by the underlying type size. Both pointers must be of the same type.

```
padd   <address>    <address/constant>    (pointer addition)
psub   <address>    <address/constant>    (pointer substract)
pdif   <address>    <address/constant>    (pointer difference)
```

### Comparison instructions

Comparison instructions require arithmetic or pointer types. All comparison instructions push their result as a return value on the stack as an integer type. Both left-hand and right-hand operands need to be compatible for comparison (see Conversion rules). Either operand cannot be void pointer type.
* `cmp` will push -1 if left is lesser than right, 1 if left is greater than right and 0 if left is equal to right.
* Any other instruction will push 1 if the expression is true and 0 if false.

```
cmp    <address>    <address/constant>    (compare)
ceq    <address>    <address/constant>    (compare equal)
cne    <address>    <address/constant>    (compare not equal)
cgt    <address>    <address/constant>    (compare greater than)
cge    <address>    <address/constant>    (compare greater or equal)
clt    <address>    <address/constant>    (compare less than)
cle    <address>    <address/constant>    (compare less or equal)
cze    <address>                          (compare to zero)
cnz    <address>                          (compare not zero)
```

### Control flow instructions

Control flow instructions jump to another instruction within the same method.
* `br` will unconditionally jump to label location.
* All other instructions will jump to label if condition equals to true, where the condition is tested using the comparison instructions defined above.
* The `sw` instruction allows implementation of branch tables, where the first operand is an integral value, and the following operands a list of label locations. The index must be within range of the label list.

```
br     <label>
beq    <label>      <address>     <address/constant>    (branch if equal)
bne    <label>      <address>     <address/constant>    (branch if not equal)
bgt    <label>      <address>     <address/constant>    (branch if greater than)
bge    <label>      <address>     <address/constant>    (branch if greater or equal)
blt    <label>      <address>     <address/constant>    (branch if less than)
ble    <label>      <address>     <address/constant>    (branch if less or equal)
bze    <label>      <address>                           (branch if zero)
bnz    <label>      <address>                           (branch if not zero)
sw     <address>    <label...>                          (switch)
```

### Method instructions

Method instructions invoke other methods and push the return value on the stack if one is provided.
* `call` will invoke a method using a constant address.
* `callv` (call virtual) will invoke a method using a dynamic signature type variable.
All arguments are copied as parameters on the stack using the same conversion rules as the `set` instruction. The number of arguments must match the number of parameters in the methods signature. Methods that return no value should return using the `ret` instruction, methods that do should return a value with `retv`. Return values are copied onto the caller stack using the same conversion as the `set` instruction.

```
call   <method>     <address/constant...>
callv  <address>    <address/constant...>
ret
retv   <address/constant>
```

### Debug instructions

Dump prints the value of the variable specified at the address to the standard text output.

```
dump   <address/constant>
```

## Conversion rules

### Assignment conversion rules

Assignment conversion rules apply to the `set` and `conv` instructions, as well as any other instruction that takes in one or multiple operands, including arguments for a method call and the returning of method return values.

For assignment instructions using structs, pointers and arithmetic types, the following conversion rules apply:
* If left-hand operand type and right-hand operand type are the same:
  * `set` will copy the entire value of right to left, regardless of size.
* If left-hand operand type and right-hand operand type are not the same:
  * If left-hand operand type is arithmetic:
    * If right-hand operand type is arithmetic:
      * `set` will convert the value of right to the type of left if implicit conversion is possible (see Arithmetic conversion rules).
      * `conv` will convert the value of right to the type of left regardless of data loss.
	* If right-hand operand type is a pointer:
	  * `conv` will convert the pointer to arithmetic value regardless of data loss.
  * If left-hand operand type is a pointer:
    * If right-hand operand type is arithmetic:
	  * `conv` will convert the arithmetic value to a pointer regardless of data loss.
	* If right-hand operand type is a pointer:
	  * `set` will copy the value of right to left if left is a void pointer type.
	  * `conv` will copy the value of right to left. This conversion only changes the underlying type of the pointer, the address remains the same.

All unlisted cases will result in an invalid conversion.

### Arithmetic conversion rules

Arithmetic conversion rules apply to all arithmetic instructions.

The arithmetic conversion rules assume the following order of significance in arithmetic types:

`byte < ubyte < short < ushort < int < uint < long < ulong < float < double`

Implicit conversion of different arithmetic types dictates that the left-hand operand should always be able to represent the value of the right-hand operand. If left-hand operand and right-hand operand are not of the same type, implicit conversion is only possible when:
* The left-hand type is a double and the right-hand type is a float or integral.
* The left-hand type is a float and the right-hand type is integral.
* The left-hand type has the same sign as the righ-hand type and the right-hand type is less significant than the left-hand type.
For all of the above cases, the right-hand type is converted to the left-hand type.

### Comparison conversion rules

Comparison between pointer types can only be performed if both operands are of the same type, and neither are void pointer type. If both operand types are arithmetic, the following rules apply:
* If either operand type is a double, the comparison is performed as double.
* If either operand type is a float, the comparison is performed as float.
* If both operand types are signed int or any type of lesser significance, the comparison is performed as signed int.
* If both operand types are signed long or any type of lesser significance, the comparison is performed as signed long.
* If both operands have the same sign, the comparison is performed using the most significant type.

All unlisted cases will result in an invalid comparison.