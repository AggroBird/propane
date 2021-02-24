# Propane Language Definition

## Instruction set

### Zero instruction
```
noop
```

### Assignment instructions
```
set    <address>    <address/constant>
conv   <address>    <address/constant>
```

### Arithmetic instructions
```
not    <address>
neg    <address>
mul    <address>    <address/constant>
div    <address>    <address/constant>
mod    <address>    <address/constant>
add    <address>    <address/constant>
sub    <address>    <address/constant>
lsh    <address>    <address/constant>
rsh    <address>    <address/constant>
and    <address>    <address/constant>
xor    <address>    <address/constant>
or     <address>    <address/constant>
```

### Pointer instructions
```
padd   <address>    <address/constant>
psub   <address>    <address/constant>
pdif   <address>    <address/constant>
```

### Comparison instructions
```
cmp    <address>    <address/constant>
ceq    <address>    <address/constant>
cne    <address>    <address/constant>
cgt    <address>    <address/constant>
cge    <address>    <address/constant>
clt    <address>    <address/constant>
cle    <address>    <address/constant>
cze    <address>
cnz    <address>
```

### Control flow instructions
```
br     <label>
beq    <label>      <address><address/constant>
bne    <label>      <address><address/constant>
bgt    <label>      <address><address/constant>
bge    <label>      <address><address/constant>
blt    <label>      <address><address/constant>
ble    <label>      <address><address/constant>
bze    <label>      <address>
bnz    <label>      <address>
sw     <address>    <labels...>
```

### Method instructions
```
call   <method>     <args...>
callv  <address>    <args...>
ret
retv   <address/constant>
```

### Debug instructions
```
dump   <address/constant>
```

## Binary format
