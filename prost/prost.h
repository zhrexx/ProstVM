// TODO: labels, jmp, jmpif
#ifndef PROST_H
#define PROST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#include "dependencies/xvec.h"
#include "dependencies/xmap.h"
#include "dependencies/bb.h"

typedef enum {
    Push,
    Drop,
    Halt,
    Call,
    CallExtern,
    Return,
    DerefMemory,
    AssignMemory,
    Jmp,
    JmpIf,
} InstructionType;

typedef struct {
    InstructionType type;
    Word arg;
} Instruction;

typedef enum {
    P_OK = 0,
    P_ERR_STACK_UNDERFLOW,
    P_ERR_INVALID_BYTECODE,
    P_ERR_LIBRARY_NOT_FOUND,
    P_ERR_FUNCTION_NOT_FOUND,
    P_ERR_INVALID_INDEX,
    P_ERR_CALL_STACK_UNDERFLOW,
    P_ERR_INVALID_VM_STATE,
    P_ERR_GENERAL_VM_ERROR,
} ProstStatus;

/**
 * ProstVM - Virtual machine state
 *
 * Note: status field enables a cleaner API for operations like p_pop(),
 * allowing them to return the value directly while still reporting errors.
 */
typedef struct ProstVM {
    XVec stack;                 // Word
    XVec call_stack;            // CallFrame*
    XMap memory;                // XEntry(name, Word value)

    XMap functions;             // XEntry(name, Function*)
    XMap external_functions;    // XEntry(name, p_external_function)
    ProstStatus status;         // Status of latest operation (see above)

    bool running;
    int exit_code;              // Exit code of the program

    // CEC (Current Execution Context)
    const char *current_function;
    size_t current_ip;
} ProstVM;

typedef struct {
    XVec instructions;
    XVec labels;
} Function;

typedef struct {
    const char *function_name;
    size_t return_ip;
} CallFrame;

typedef void (*p_external_function)(ProstVM *vm);

ProstVM *p_init();
void p_free(ProstVM *vm);

ProstStatus p_load_library(ProstVM *vm, const char *path);
ProstStatus p_register_external(ProstVM *vm, const char *name, p_external_function fn);

ByteBuf p_to_bytecode(ProstVM *vm);
ProstStatus p_from_bytecode(ProstVM *vm, const char *bytecode);

ProstStatus p_call(ProstVM *vm, const char *name);                          // call a function
ProstStatus p_call_extern(ProstVM *vm, const char *name);                   // call a external function
ProstStatus p_execute_instruction(ProstVM *vm, Instruction instruction);    // execute a instruction
ProstStatus p_run(ProstVM *vm);                                             // runs __entry

Word p_pop(ProstVM *vm);                                                    // Pops a value from the stack (sets vm->status)
void p_push(ProstVM *vm, Word w);                                           // Pushes a value onto the stack
Word p_peek(ProstVM *vm);                                                   // Peeks at the top value of the stack (sets vm->status)
Word p_expect(ProstVM *vm, WordType t);                                     // Pops and type-checks the top value of the stack

// Adapted from Sean Barrett's single-header library pattern
// Define PROST_IMPLEMENTATION in one source file before including prost.h
// to compile the implementation once, avoiding multiple definition errors.
#ifdef PROST_IMPLEMENTATION

// SECTION INIT-DEINIT
ProstVM *p_init() {
    ProstVM *vm = (ProstVM *)malloc(sizeof(ProstVM));
    if (!vm) return NULL;

    xvec_init(&vm->stack, 0);
    xvec_init(&vm->call_stack, 0);
    xmap_init(&vm->functions, 0);
    xmap_init(&vm->external_functions, 0);
    xmap_init(&vm->memory, 0);

    vm->status = P_OK;
    vm->running = false;
    vm->exit_code = 0;
    vm->current_function = NULL;
    vm->current_ip = 0;

    return vm;
}

void p_free(ProstVM *vm) {
    if (!vm) return;

    xvec_free(&vm->stack);

    for (size_t i = 0; i < vm->call_stack.size; i++) {
        Word *frame_word = xvec_get(&vm->call_stack, i);
        if (frame_word && frame_word->as_pointer) {
            free(frame_word->as_pointer);
        }
    }
    xvec_free(&vm->call_stack);

    for (size_t i = 0; i < vm->functions.size; i++) {
        XEntry iter = vm->functions.entries[i];
        Function *fn = (Function *)iter.value.as_pointer;
        if (fn) {
            xvec_free(&fn->instructions);
            xvec_free(&fn->labels);
            free(fn);
        }
    }

    xmap_free(&vm->functions);
    xmap_free(&vm->external_functions);
    xmap_free(&vm->memory);

    free(vm);
}
// END SECTION INIT-DEINIT

// SECTION EXTERN
ProstStatus p_load_library(ProstVM *vm, const char *path) {
    if (!vm || !path) {
        vm->status = P_ERR_INVALID_INDEX;
        return vm->status;
    }

#ifdef _WIN32
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        vm->status = P_ERR_LIBRARY_NOT_FOUND;
        return vm->status;
    }
#else
    void *handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        vm->status = P_ERR_LIBRARY_NOT_FOUND;
        return vm->status;
    }
#endif

    typedef ProstStatus (*p_lib_init_fn)(ProstVM*);
    p_lib_init_fn init_fn;

#ifdef _WIN32
    init_fn = (p_lib_init_fn)GetProcAddress(handle, "p_register_library");
#else
    init_fn = (p_lib_init_fn)dlsym(handle, "p_register_library");
#endif

    if (!init_fn) {
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        vm->status = P_ERR_LIBRARY_NOT_FOUND;
        return vm->status;
    }

    ProstStatus status = init_fn(vm);

#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif

    vm->status = status;
    return vm->status;
}

ProstStatus p_register_external(ProstVM *vm, const char *name, p_external_function fn) {
    if (!vm || !name || !fn) {
        vm->status = P_ERR_INVALID_INDEX;
        return vm->status;
    }

    xmap_set(&vm->external_functions, name, WORD(fn));
    vm->status = P_OK;
    return vm->status;
}
// END SECTION EXTERN

// SECTION BYTECODE
ByteBuf p_to_bytecode(ProstVM *vm) {
    ByteBuf bb;
    bb_init(&bb, 1024);

    bb_append(&bb, "PROST", 6);
    bb_push(&bb, 1);

    uint32_t memory_count = (uint32_t)vm->memory.size;
    bb_append(&bb, &memory_count, sizeof(uint32_t));

    for (size_t i = 0; i < vm->memory.size; i++) {
        XEntry entry = vm->memory.entries[i];
        const char *var_name = entry.key;

        uint32_t name_len = (uint32_t)strlen(var_name);
        bb_append(&bb, &name_len, sizeof(uint32_t));
        bb_append(&bb, var_name, name_len);

        bb_append(&bb, &entry.value, sizeof(Word));
    }

    uint32_t fn_count = (uint32_t)vm->functions.size;
    bb_append(&bb, &fn_count, sizeof(uint32_t));

    for (size_t i = 0; i < vm->functions.size; i++) {
        XEntry entry = vm->functions.entries[i];
        const char *fn_name = entry.key;
        Function *fn = (Function *)entry.value.as_pointer;

        if (!fn) continue;

        uint32_t name_len = (uint32_t)strlen(fn_name);
        bb_append(&bb, &name_len, sizeof(uint32_t));
        bb_append(&bb, fn_name, name_len);

        uint32_t inst_count = (uint32_t)xvec_len(&fn->instructions);
        bb_append(&bb, &inst_count, sizeof(uint32_t));

        uint32_t label_count = (uint32_t)xvec_len(&fn->labels);
        bb_append(&bb, &label_count, sizeof(uint32_t));

        for (size_t j = 0; j < inst_count; j++) {
            Word *inst_word = xvec_get(&fn->instructions, j);
            if (!inst_word) continue;

            Instruction *inst = (Instruction *)inst_word->as_pointer;

            uint8_t inst_type = (uint8_t)inst->type;
            bb_append(&bb, &inst_type, sizeof(uint8_t));

            switch (inst->type) {
                case Call:
                case CallExtern:
                case DerefMemory:
                case AssignMemory: {
                    const char *str = (const char *)inst->arg.as_pointer;
                    uint32_t str_len = str ? (uint32_t)strlen(str) : 0;
                    bb_append(&bb, &str_len, sizeof(uint32_t));
                    if (str_len > 0) {
                        bb_append(&bb, str, str_len);
                    }
                    break;
                }
                default:
                    bb_append(&bb, &inst->arg, sizeof(Word));
                    break;
            }
        }

        for (size_t j = 0; j < label_count; j++) {
            Word *label = xvec_get(&fn->labels, j);
            if (label) {
                bb_append(&bb, label, sizeof(Word));
            }
        }
    }

    bb_push(&bb, 0);

    return bb;
}

ProstStatus p_from_bytecode(ProstVM *vm, const char *bytecode) {
    if (!vm || !bytecode) {
        vm->status = P_ERR_INVALID_BYTECODE;
        return vm->status;
    }

    const uint8_t *ptr = (const uint8_t *)bytecode;

    if (memcmp(ptr, "PROST", 6) != 0) {
        vm->status = P_ERR_INVALID_BYTECODE;
        return vm->status;
    }
    ptr += 6;

    uint8_t version = *ptr++;
    if (version != 1) {
        vm->status = P_ERR_INVALID_BYTECODE;
        return vm->status;
    }

    uint32_t memory_count;
    memcpy(&memory_count, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    for (uint32_t i = 0; i < memory_count; i++) {
        uint32_t key_len;
        memcpy(&key_len, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        char *key = malloc(key_len + 1);
        memcpy(key, ptr, key_len);
        key[key_len] = 0;
        ptr += key_len;

        Word value;
        memcpy(&value, ptr, sizeof(Word));
        ptr += sizeof(Word);

        xmap_set(&vm->memory, key, value);
        free(key); // xmap_set strdups so original key can be freed
    }

    uint32_t fn_count;
    memcpy(&fn_count, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    for (uint32_t i = 0; i < fn_count; i++) {
        uint32_t name_len;
        memcpy(&name_len, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        char *fn_name = (char *)malloc(name_len + 1);
        if (!fn_name) {
            vm->status = P_ERR_INVALID_VM_STATE;
            return vm->status;
        }
        memcpy(fn_name, ptr, name_len);
        fn_name[name_len] = '\0';
        ptr += name_len;

        Function *fn = (Function *)malloc(sizeof(Function));
        if (!fn) {
            free(fn_name);
            vm->status = P_ERR_INVALID_VM_STATE;
            return vm->status;
        }
        xvec_init(&fn->instructions, 0);
        xvec_init(&fn->labels, 0);

        uint32_t inst_count;
        memcpy(&inst_count, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        uint32_t label_count;
        memcpy(&label_count, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);

        for (uint32_t j = 0; j < inst_count; j++) {
            Instruction *inst = (Instruction *)malloc(sizeof(Instruction));
            if (!inst) {
                vm->status = P_ERR_INVALID_VM_STATE;
                return vm->status;
            }

            uint8_t inst_type;
            memcpy(&inst_type, ptr, sizeof(uint8_t));
            ptr += sizeof(uint8_t);
            inst->type = (InstructionType)inst_type;

            switch (inst->type) {
                case Call:
                case CallExtern:
                case DerefMemory:
                case AssignMemory: {
                    uint32_t str_len;
                    memcpy(&str_len, ptr, sizeof(uint32_t));
                    ptr += sizeof(uint32_t);

                    if (str_len > 0) {
                        char *str = (char *)malloc(str_len + 1);
                        if (!str) {
                            free(inst);
                            vm->status = P_ERR_INVALID_VM_STATE;
                            return vm->status;
                        }
                        memcpy(str, ptr, str_len);
                        str[str_len] = '\0';
                        ptr += str_len;
                        inst->arg = WORD(str);
                    } else {
                        inst->arg = WORD(NULL);
                    }
                    break;
                }
                default:
                    memcpy(&inst->arg, ptr, sizeof(Word));
                    ptr += sizeof(Word);
                    break;
            }

            xvec_push(&fn->instructions, WORD(inst));
        }

        for (uint32_t j = 0; j < label_count; j++) {
            Word label;
            memcpy(&label, ptr, sizeof(Word));
            ptr += sizeof(Word);
            xvec_push(&fn->labels, label);
        }

        xmap_set(&vm->functions, fn_name, WORD(fn));
    }

    vm->status = P_OK;
    return vm->status;
}
// END SECTION BYTECODE

// SECTION CALL
ProstStatus p_call(ProstVM *vm, const char *name) {
    if (!vm || !name) {
        vm->status = P_ERR_INVALID_INDEX;
        return vm->status;
    }

    Word *fn_word = xmap_get(&vm->functions, name);
    if (!fn_word || fn_word->as_pointer == NULL) {
        vm->status = P_ERR_FUNCTION_NOT_FOUND;
        return vm->status;
    }

    CallFrame *frame = (CallFrame *)malloc(sizeof(CallFrame));
    if (!frame) {
        vm->status = P_ERR_INVALID_VM_STATE;
        return vm->status;
    }

    frame->function_name = vm->current_function;
    frame->return_ip = vm->current_ip;
    xvec_push(&vm->call_stack, WORD(frame));

    vm->current_function = name;
    vm->current_ip = 0;

    vm->status = P_OK;
    return vm->status;
}

ProstStatus p_call_extern(ProstVM *vm, const char *name) {
    if (!vm || !name) {
        vm->status = P_ERR_INVALID_INDEX;
        return vm->status;
    }

    Word *fn_word = xmap_get(&vm->external_functions, name);
    if (!fn_word || fn_word->as_pointer == NULL) {
        vm->status = P_ERR_FUNCTION_NOT_FOUND;
        return vm->status;
    }

    p_external_function fn = (p_external_function)fn_word->as_pointer;
    fn(vm);

    vm->status = P_OK;
    return vm->status;
}
// END SECTION CALL

// SECTION RUN
ProstStatus p_execute_instruction(ProstVM *vm, Instruction instruction) {
    if (!vm) return P_ERR_INVALID_INDEX;

    switch (instruction.type) {
        case Push:
            p_push(vm, instruction.arg);
            break;

        case Drop:
            p_pop(vm);
            if (vm->status != P_OK) return vm->status;
            break;

        case Halt:
            vm->running = false;
            break;

        case Call: {
            const char *fn_name = (const char *)instruction.arg.as_pointer;
            ProstStatus status = p_call(vm, fn_name);
            if (status != P_OK) return status;
            break;
        }

        case CallExtern: {
            const char *fn_name = (const char *)instruction.arg.as_pointer;
            ProstStatus status = p_call_extern(vm, fn_name);
            if (status != P_OK) return status;
            break;
        }

        case Return:
            if (xvec_empty(&vm->call_stack)) {
                vm->status = P_ERR_CALL_STACK_UNDERFLOW;
                return vm->status;
            }

            Word frame_word = xvec_pop(&vm->call_stack);
            CallFrame *frame = (CallFrame *)frame_word.as_pointer;

            vm->current_function = frame->function_name;
            vm->current_ip = frame->return_ip;

            free(frame);
            break;

        case DerefMemory:
            Word *w = xmap_get(&vm->memory, (const char *)instruction.arg.as_pointer);
            if (!w) {
                vm->status = P_ERR_INVALID_INDEX;
                return vm->status;
            }
            p_push(vm, *w);
            break;

        case AssignMemory:
            xmap_set(&vm->memory, (const char *)instruction.arg.as_pointer, p_pop(vm));
            break;

        case Jmp: 
            vm->current_ip = instruction.arg.as_int;
            break;

        case JmpIf:
            if (p_expect(vm, WINT).as_int == 1) {
                vm->current_ip = instruction.arg.as_int;
            }
            break;
        default:
            vm->status = P_ERR_INVALID_BYTECODE;
            return vm->status;
    }

    vm->status = P_OK;
    return vm->status;
}

ProstStatus p_run(ProstVM *vm) {
    if (!vm) return P_ERR_INVALID_VM_STATE;

    vm->running = true;
    vm->current_function = "__entry";
    vm->current_ip = 0;

    while (vm->running) {
        Word *fn_word = xmap_get(&vm->functions, vm->current_function);
        if (!fn_word || fn_word->as_pointer == NULL) {
            vm->status = P_ERR_FUNCTION_NOT_FOUND;
            return vm->status;
        }

        Function *fn = (Function *)fn_word->as_pointer;

        if (vm->current_ip >= xvec_len(&fn->instructions)) {
            if (xvec_empty(&vm->call_stack)) {
                vm->running = false;
                break;
            }

            Word frame_word = xvec_pop(&vm->call_stack);
            CallFrame *frame = (CallFrame *)frame_word.as_pointer;

            vm->current_function = frame->function_name;
            vm->current_ip = frame->return_ip;

            free(frame);
            continue;
        }

        Word *inst_word = xvec_get(&fn->instructions, vm->current_ip);
        if (!inst_word) {
            vm->status = P_ERR_INVALID_INDEX;
            return vm->status;
        }

        Instruction inst = *(Instruction *)inst_word->as_pointer;
        vm->current_ip++;

        ProstStatus status = p_execute_instruction(vm, inst);
        if (status != P_OK) {
            return status;
        }
    }

    return P_OK;
}
// END SECTION RUN

// SECTION STACK MANIPULATION
Word p_pop(ProstVM *vm) {
    if (xvec_empty(&vm->stack)) {
        vm->status = P_ERR_STACK_UNDERFLOW;
        return WORD(NULL);
    }
    vm->status = P_OK;
    return xvec_pop(&vm->stack);
}

void p_push(ProstVM *vm, Word w) {
    xvec_push(&vm->stack, w);
}

Word p_peek(ProstVM *vm) {
    if (xvec_empty(&vm->stack)) {
        vm->status = P_ERR_STACK_UNDERFLOW;
        return WORD(NULL);
    }
    Word *top = xvec_get(&vm->stack, xvec_len(&vm->stack) - 1);
    return *top;
}

Word p_expect(ProstVM *vm, WordType t) {
    Word w = p_pop(vm);
    if (w.type != t) {
        fprintf(stderr, "ERROR: Unexpected word type. Expected %s but got %s\n", word_type_to_str(t), word_type_to_str(w.type));
        vm->status = P_ERR_GENERAL_VM_ERROR;
        vm->running = false;
        return WORD(NULL);
    }
    return w;
}
// END SECTION STACK MANIPULATION

#endif

#endif //PROST_H
