# Propane Language Definition

The following documents defines the Propane language standard. For custom implementations, these rules can serve as a guideline. The standard Propane toolchain follows these rules strictly.

The Propane language is largely based on the C programming language standard, and shares many similarities.

## Types

Propane is a strongly typed language.

### Base types

Standard build-in arithmetic types. They behave the same as their C standard equivalents in terms of operations and size.

```
byte     signed 8 bit integer
ubyte    unsigned 8 bit integer
short    signed 16 bit integer
ushort   unsigned 16 bit integer
int      signed 32 bit integer
uint     unsigned 32 bit integer
long     signed 64 bit integer
ulong    unsigned 64 bit integer
float    32 bit floating point number
double   64 bit floating point number
```

Additionally, the alias types `size` and `offset` will refer to the native integer types for the current architecture:
* On 32 bit architectures this will be uint and int respectively.
* On 64 bit architectures this will be ulong and long respectively.

Propane implements `void` as a special type that cannot be declared on the stack or as a method parameter. The only use of `void` is to indicate methods with no return value.

### Pointer types

Pointer types behave the same as in C. They point to a location in memory, and pointer addition/substraction will result in the underlying type size being added to the memory address. In Propane, any type can have a pointer type, including pointer to pointers and pointer to method signatures.

Void pointers behave the same as in C. They cannot be implicitly incremented or decremented, nor can they be dereferenced.

The size of a pointer is dependant on the architecture, but is guaranteed to be the same as the `size` type.

### Array types

Array types are implemented as a collection of elements. Array type lengths are constant will not change at runtime. Unlike C, Propane arrays are passed by value.

The size of an array type is guaranteed to be the size of the underlying type multiplied by the array length. Incrementing an array type pointer will result in the entire size of the array being added to the memory address.

### Signature types

Signature types are implemented as a pointer to a method. Signature types are implemented as an abstract handle to a method that can be dynamically invoked. Unlike C, method pointers cannot be explicitly converted between different types. Signature types can only be assigned with the address of a method that exactly matches the parameters and return type, or other method pointers of the same type. Attempting to convert method pointer types through pointers will result in undefined behaviour.

The size of a method pointer is implementation specific and cannot be guaranteed. Extra care should be taken when using method pointers as fields in structs.

### Structure and union types

Like C, Propane includes structures and unions. Structures allow nesting of other types. Unions are guaranteed to have all values at zero offset. Propane currently requires no alignment or padding for fields, so extra care should be taken when converting Propane to other languages that do.

## Instruction set

### Zero instruction
Noop does nothing.
```
noop
```

### Assignment instructions
`set` and `conv` (convert) follow the following behaviour:
* If left-hand operand type and right-hand operand type are the same:
  * `set` will copy the entire value of right to left, regardless of size.
* If left-hand operand type and right-hand operand type are not the same:
  * If left-hand operand type is arithmetic:
    * If right-hand operand type is arithmetic:
      * `set` will implicitly convert the value of right to the type of left if possible (see Conversion Rules), and copy the result of the conversion to left.
      * `conv` will explicitly convert the value of right to the type of left regardless of type, and copy the result of the conversion to left. Any conversion of arithmetic types that are larger than the destination arithmetic type will result in loss of data.
	* If right-hand operand type is a pointer:
	  * `conv` will convert the pointer to arithmetic value. Any conversion of pointers that are larger than the destination arithmetic type will result in loss of data.
  * If left-hand operand type is a pointer:
    * If right-hand operand type is arithmetic:
	  * `conv` will convert the arithmetic value to a pointer. Any conversion of arithmetic types that are larger than the native pointer size will result in loss of data.
	* If right-hand operand type is a pointer:
	  * `conv` will copy the value of right to left. The resulting value will point to the same address in memory, but the type of the pointer has changed.
```
set    <address>    <address/constant>
conv   <address>    <address/constant>
```

### Arithmetic instructions
Arithmetic instructions require arithmetic (integral and floating point) types. Right-hand operand must be compatible with destination left-hand operand (see Conversion Rules). Result of these operations will be the same as their C equivalents.
```
not    <address>                          (bitwise complement)
neg    <address>                          (negate)
mul    <address>    <address/constant>    (multiply)
div    <address>    <address/constant>    (divide)
mod    <address>    <address/constant>    (modulus)
add    <address>    <address/constant>    (addition)
sub    <address>    <address/constant>    (substraction)
lsh    <address>    <address/constant>    (left shift)
rsh    <address>    <address/constant>    (right shift)
and    <address>    <address/constant>    (bitwise and)
xor    <address>    <address/constant>    (bitwise exclusive or)
or     <address>    <address/constant>    (bitwise or)
```

### Pointer instructions
Pointer instructions require pointer types.
* `padd` and `psub` will add and substract the underlying type size multiplied by the operand.
* `pdif` will take the offset between two pointers, divided by the underlying type size, granted both pointers are the same type.
```
padd   <address>    <address/constant>
psub   <address>    <address/constant>
pdif   <address>    <address/constant>
```

### Comparison instructions
Arithmetic instructions require arithmetic (integral and floating point) or pointer types. All comparison instructions do not have a destination operand, but instead push their result as a return value on the stack as an integer.
* `cmp` will push -1 if left is lesser than right, 1 if left is greater than right and 0 if left is equal to right.
* Any other instruction will push 1 if the expression is true and 0 if false.
```
cmp    <address>    <address/constant>    (compare)
ceq    <address>    <address/constant>    (equal)
cne    <address>    <address/constant>    (not equal)
cgt    <address>    <address/constant>    (greater than)
cge    <address>    <address/constant>    (greater or equal)
clt    <address>    <address/constant>    (less than)
cle    <address>    <address/constant>    (less or equal)
cze    <address>                          (compare to zero)
cnz    <address>                          (compare not zero)
```

### Control flow instructions
Control flow instructions jump to another instruction within the same method.
* `br` will unconditionally jump to label location.
* All other instructions will jump to label if condition equals to true, where the condition is tested using the comparision instructions defined above.
* The `sw` instruction allows implenting lookup tables, where the first operand is an integral value, and the following operands a list of label locations. The index must be within the range of the label list.
```
br     <label>
beq    <label>      <address>     <address/constant>
bne    <label>      <address>     <address/constant>
bgt    <label>      <address>     <address/constant>
bge    <label>      <address>     <address/constant>
blt    <label>      <address>     <address/constant>
ble    <label>      <address>     <address/constant>
bze    <label>      <address>
bnz    <label>      <address>
sw     <address>    <labels...>
```

### Method instructions
Method instructions invoke other methods and push the return value on the stack if one is provided.
* `call` will invoke a method using a constant address.
* `callv` (call virtual) will invoke a method using a dynamic method handle.
All parameters are pushed as parameters on the stack using the same conversion as the `set` instruction. The number of arguments must match the number of parameters in the methods signature.
Methods that return no value should return using the `ret` instruction, or `retv` if a return value is expected. Return values are copied onto the caller stack using the same conversion as the `set` instruction.
```
call   <method>     <args...>
callv  <address>    <args...>
ret
retv   <address/constant>
```

### Debug instructions
Dump prints the value of the variable specified at the address to the standard text output.
```
dump   <address/constant>
```

### Conversion rules

TODO

### Address formats

TODO
