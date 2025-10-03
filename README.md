# Prost Virtual Machine

**Prost** — a blend of the German "Prost!" (Cheers!) and the Russian "Просто" (Simple) — is a virtual machine that prioritizes simplicity without compromising on power.

## Architecture

```
Assembler → State → Optimizer → Bytecode | Bytecode → Runner → Result
```

## Assembly Language

Prost assembly is **function-based**, providing clear scope boundaries and improved code organization.

### Structure

```asm
; Functions are the top-level construct
main {
    push 42
    call fibonacci
    ret
}

fibonacci {
    ; Labels are only allowed inside functions
    ; They provide jump targets for control flow
    
    loop_start:
        dup
        push 1
        sub
        dup
        push 0
        jle loop_end
        jmp loop_start
    
    loop_end:
        ret
}

utils {
    print_number:
        ; Another label inside a different function
        call @print
        ret
}
```

### Key Principles

- **Function scope**: All executable code must be inside a function
- **Label locality**: Labels exist only within their containing function
- **No global labels**: Labels cannot be referenced across function boundaries
- **Clear structure**: Functions provide natural boundaries for optimization and debugging

### Benefits

1. **Modularity**: Functions are self-contained units
2. **Namespace clarity**: Labels don't pollute global namespace
3. **Optimization**: Function boundaries allow for better optimization passes
4. **Readability**: Clear visual structure mimics high-level languages
5. **Error prevention**: Prevents accidental cross-function jumps

## External Functions (FFI)

Prost supports external functions through dynamically loaded libraries (.so on Unix, .dll on Windows). The FFI system is designed with simplicity and safety in mind.

### Library Interface

Each external library must export a registration function:

```c
void p_register_library(ProstVM* vm);
```

This function is called during library loading and allows the library to register its exported functions with the VM.

### Function Signature

All external functions follow a uniform calling convention:

```c
ProstValue p_function_name(ProstVM* vm);
```

**Key principles:**
- **Single parameter**: Functions receive only a pointer to the VM state
- **Stack-based arguments**: Arguments are retrieved from the VM's stack
- **Stack-based returns**: Return values are pushed onto the VM's stack
- **Uniform interface**: All external functions share the same signature, simplifying dynamic dispatch

### Example

```c
// mathlib.c - Example external library

#include "prost.h"

// External function implementation
ProstValue p_math_sqrt(ProstVM* vm) {
    ProstValue arg = p_stack_pop(vm);
    if (arg.type != PROST_NUMBER) {
        return p_error(vm, "sqrt expects a number");
    }
    
    double result = sqrt(arg.as_number);
    return p_number(result);
}

// Registration function - called when library is loaded
void p_register_library(ProstVM* vm) {
    p_register_function(vm, "sqrt", p_math_sqrt);
    // ... register more functions
}
```

### Benefits

1. **Simplicity**: Single, consistent function signature
2. **Flexibility**: VM state provides access to stack, memory, and error handling
3. **Type Safety**: Functions validate their own arguments
4. **Dynamic Loading**: Libraries can be loaded at runtime
5. **Encapsulation**: VM internals are accessed through a stable API

### Assembly 

example
```asm 
mem {
    name: 10 ; Init value
}


__entry {
    push 10
    *mem.name ; deref memory to get value on the stack 
    call @print
}

```