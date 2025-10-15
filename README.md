# Prost Virtual Machine

**Prost** — from German "Prost!" (Cheers!) and Russian "Просто" (Simple). A toast to simplicity.

## Architecture

```
Assembler (.pa) → Bytecode (.pco) → Runner
```

Prost compiles assembly to bytecode format for fast execution. The VM is stack-based with 32 general-purpose registers.

## The Language

Core instruction set with built-in stack operations and comparison operators. Extended functionality through stdlib and custom libraries.

### Core Instructions

#### Stack Operations
- `push <value>` - Push value/register to stack
- `pop <register>` - Pop from stack to register
- `drop` - Discard top of stack
- `dup` - Duplicate top of stack
- `swap` - Swap top two stack values
- `over` - Copy second stack value to top

#### Control Flow
- `halt`/`ret` - Stop execution
- `call <name>` - Call internal function
- `call @name` - Call external function
- `return` - Return from function
- `jmp <addr>` - Unconditional jump
- `jmpif <addr>` - Jump if top of stack is 1

#### Comparison Operations
- `eq` - Equality check (supports int, float, string, pointer)
- `neq` - Not equal (integers)
- `lt` - Less than
- `lte` - Less than or equal
- `gt` - Greater than
- `gte` - Greater than or equal

All comparison ops push 1 (true) or 0 (false) onto the stack.

## Assembly Structure

```asm
__entry {
    push 42
    call fibonacci
    call @print
    halt
}

fibonacci {
    ; Your implementation here
    dup
    push 1
    lt
    jmpif base_case
    ; ... recursive logic
    return
    
    base_case:
        return
}
```

**Entry Point:** Every program needs an `__entry` function - this is where execution begins.

## Register System

Prost has 32 registers (`r0` through `r31`) for storing values:

```asm
main {
    push 100
    pop r0        ; Store in register r0
    push r0       ; Load from register r0
    call @print
    halt
}
```

Registers are referenced by name and can store any `Word` type (int, float, pointer).

## Type System

Prost uses a tagged union `Word` type that can hold:
- **Integers** - 64-bit signed integers
- **Floats** - Double-precision floating point
- **Pointers** - Memory addresses, strings, function pointers
- **Strings** - Owned or non-owned string data

String literals in push commands are automatically managed:
```asm
push "Hello, World!"  ; String stored with ownership tracking
```

## External Functions (FFI)

Extend Prost with `.so` (Linux) or `.dll` (Windows) libraries. Simple signature: stack in, stack out.

### Creating a Library

```c
#include "prost/prost.h"

void my_sqrt(ProstVM* vm) {
    Word arg = p_pop(vm);
    if (vm->status != P_OK) return;
    
    double result = sqrt(arg.as_float);
    p_push(vm, WORD(result));
}

// Required export
ProstStatus p_register_library(ProstVM* vm) {
    p_register_external(vm, "sqrt", my_sqrt);
    return P_OK;
}
```

### Using Libraries

```bash
# Compile with library
prost -d ./libmath.so program.pa

# Or load at runtime
prost program.pa
```

```asm
__entry {
    push 16.0
    call @sqrt
    call @print
    halt
}
```

## Command Line Usage

```bash
prost [OPTIONS] <input_file>

Options:
  -h, --help              Show help message
  -o, --output FILE       Output bytecode file (default: out.pco)
  -d, --library FILE      Load external library
  -r, --dont-run          Compile only, don't execute
  -c, --dont-compile      Run bytecode only (for .pco files)
  -v, --verbose           Enable verbose output

File Extensions:
  .pa   - Prost Assembly (source code)
  .pco  - Prost Compiled Object (bytecode)
```

### Examples

```bash
# Compile and run
prost program.pa

# Compile only
prost -r program.pa -o program.pco

# Run bytecode
prost program.pco

# Compile with library and verbose output
prost -v -d ./libmath.so program.pa
```

## Error Handling

The VM tracks execution state and provides detailed error information:

```
Runtime error: Stack underflow (status 1)
  Function: fibonacci
  Instruction pointer: 12
```

Error codes:
- `P_OK` - Success
- `P_ERR_STACK_UNDERFLOW` - Stack underflow
- `P_ERR_INVALID_BYTECODE` - Corrupted bytecode
- `P_ERR_LIBRARY_NOT_FOUND` - Library load failed
- `P_ERR_FUNCTION_NOT_FOUND` - Function not found
- `P_ERR_INVALID_INDEX` - Invalid memory access
- `P_ERR_CALL_STACK_UNDERFLOW` - Return without call
- `P_ERR_INVALID_VM_STATE` - Internal VM error

## Bytecode Format

Prost bytecode is a compact binary format:

```
Header: "PROST" (6 bytes)
Version: 1 (1 byte)
Function Count: uint32
For each function:
  - Name length: uint32
  - Name: string
  - Instruction count: uint32
  - Instructions: serialized opcodes + args
```

The bytecode is platform-independent and can be shared between systems.

## Standard Library

Prost comes with a standard library (`std.h`) providing:
- I/O operations (`print`, `input`)
- Basic arithmetic
- String manipulation
- System utilities

Include with:
```c
#include "prost/std.h"
register_std(vm);  // In your C code
```

## Example Program

```asm
; Simple counter program
__entry {
    push 0
    pop r0           ; counter = 0
    
    loop_start:
        push r0
        call @print
        
        push r0
        push 1
        call @add
        pop r0       ; counter++
        
        push r0
        push 10
        lt
        jmpif loop_start
    
    halt
}
```

## Philosophy

**Built-in where it matters. Extensions where it doesn't.**

Prost provides essential VM primitives (stack ops, control flow, comparisons) while delegating complex operations to libraries. This keeps the core simple and the ecosystem powerful.

The comparison operators (`eq`, `lt`, etc.) are built-in because they're fundamental to control flow - everything else is negotiable.

Prost! 🍻