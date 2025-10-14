# Prost Virtual Machine

**Prost** — from German "Prost!" (Cheers!) and Russian "Просто" (Simple). A toast to simplicity.

## Architecture

```
Assembler → Bytecode → Runner
```

## The Language

Minimal core. Everything else is stdlib.

### Core Instructions (10 total)

- `push` - Push value to stack
- `drop` - Drop top of stack
- `halt` - Stop execution
- `call` - Call function
- `call @name` - Call external function
- `ret` - Return from function
- `*mem.name` - Dereference memory (load to stack)
- `mem.name =` - Assign to memory (store from stack) 
- `jmp` - Jump to label
- `jmpif` - Jump if true

**That's it.** No `add`, `sub`, `mul`, `div` in the VM. Use stdlib:

```asm
__entry {
    push 5
    push 3
    call @add    ; stdlib function
    call @print  ; stdlib function
    ret
}
```

## Assembly Structure

```asm
mem {
    counter: 0
}

__entry {
    push 42
    call fibonacci
    ret
}

fibonacci {
    loop_start:
        push 1
        call @sub
        jmpif loop_start
    ret
}
```

## External Functions (FFI)

Extend Prost with `.so`/`.dll` libraries. One signature. Stack in, stack out.

```c
void math_sqrt(ProstVM* vm) {
    Word arg = p_pop(vm);
    p_push(WORD(sqrt(arg.as_number))); // will detect the type and use the right function
}

void p_register_library(ProstVM* vm) {
    p_register_external(vm, "math_sqrt", math_sqrt); // please prefix your functions with a specific prefix to avoid conflicts
}
```

```asm
main {
    push 16
    call @sqrt
    call @print
    ret
}
```

## Philosophy - i think its to bad so some features i wanna builtin

**Minimal core. Powerful ecosystem.**

The VM does one thing: execute bytecode. Everything else — math, I/O, string ops — lives in libraries where it belongs.

Prost! 🍻