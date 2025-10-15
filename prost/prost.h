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

#define P_REGISTERS_COUNT 32
#define CALL_FRAME_POOL_SIZE 256

typedef enum {
    Push, Pop, Drop, Halt, Call, CallExtern, Return, Jmp, JmpIf,
    Dup, Swap, Over, Eq, Neq, Lt, Lte, Gt, Gte, INSTRUCTION_COUNT
} InstructionType;

typedef struct {
    InstructionType type;
    Word arg;
} Instruction;

typedef struct {
    const char *name;
    Word value;
} ProstRegister;

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

typedef struct {
    const char *function_name;
    void *function_ptr;
    size_t return_ip;
} CallFrame;

typedef struct {
    Instruction *data;
    size_t count;
    size_t capacity;
} InstructionArray;

typedef struct {
    InstructionArray instructions;
} Function;

typedef struct ProstVM ProstVM;
typedef void (*p_external_function)(ProstVM *vm);
typedef ProstStatus (*InstructionHandler)(ProstVM *vm, Instruction *inst);

struct ProstVM {
    ProstRegister registers[P_REGISTERS_COUNT];
    XVec stack;
    XVec call_stack;
    XMap functions;
    XMap external_functions;
    ProstStatus status;
    bool running;
    int exit_code;
    const char *current_function;
    Function *current_function_ptr;
    size_t current_ip;
    InstructionHandler jump_table[INSTRUCTION_COUNT];
    CallFrame *frame_pool;
    size_t frame_pool_index;
};

ProstVM *p_init();
void p_free(ProstVM *vm);
ProstStatus p_load_library(ProstVM *vm, const char *path);
ProstStatus p_register_external(ProstVM *vm, const char *name, p_external_function fn);
ByteBuf p_to_bytecode(ProstVM *vm);
ProstStatus p_from_bytecode(ProstVM *vm, const char *bytecode);
ProstStatus p_call(ProstVM *vm, const char *name);
ProstStatus p_call_extern(ProstVM *vm, const char *name);
ProstStatus p_execute_instruction(ProstVM *vm, Instruction *instruction);
ProstStatus p_run(ProstVM *vm);

static inline Word p_pop(ProstVM *vm);
static inline void p_push(ProstVM *vm, Word w);
static inline Word p_peek(ProstVM *vm);
Word p_expect(ProstVM *vm, WordType t);
void p_throw_warning(ProstVM *vm, const char *msg, ...);

#ifdef PROST_IMPLEMENTATION

char *format(char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    char *str = malloc(len + 1);
    va_start(args, fmt);
    vsprintf(str, fmt, args);
    va_end(args);
    return str;
}

static inline Word p_pop(ProstVM *vm) {
    if (xvec_empty(&vm->stack)) {
        vm->status = P_ERR_STACK_UNDERFLOW;
        return WORD(NULL);
    }
    vm->status = P_OK;
    return xvec_pop(&vm->stack);
}

static inline void p_push(ProstVM *vm, Word w) {
    xvec_push(&vm->stack, w);
}

static inline Word p_peek(ProstVM *vm) {
    if (xvec_empty(&vm->stack)) {
        vm->status = P_ERR_STACK_UNDERFLOW;
        return WORD(NULL);
    }
    Word *top = xvec_get(&vm->stack, xvec_len(&vm->stack) - 1);
    return *top;
}

static ProstStatus handle_push(ProstVM *vm, Instruction *inst) {
    if (inst->arg.type == WPOINTER && word_owns_memory(&inst->arg)) {
        for (int i = 0; i < P_REGISTERS_COUNT; i++) {
            if (strcmp(vm->registers[i].name, (const char *)inst->arg.as_pointer) == 0) {
                p_push(vm, vm->registers[i].value);
                return P_OK;
            }
        }
    }
    p_push(vm, inst->arg);
    return P_OK;
}

static ProstStatus handle_pop(ProstVM *vm, Instruction *inst) {
    if (inst->arg.type == WPOINTER && word_owns_memory(&inst->arg)) {
        for (int i = 0; i < P_REGISTERS_COUNT; i++) {
            if (strcmp(vm->registers[i].name, (const char *)inst->arg.as_pointer) == 0) {
                Word w = p_pop(vm);
                vm->registers[i].value = w;
                return vm->status;
            }
        }
    }
    return P_ERR_INVALID_INDEX;
}

static ProstStatus handle_drop(ProstVM *vm, Instruction *inst) {
    p_pop(vm);
    return vm->status;
}

static ProstStatus handle_halt(ProstVM *vm, Instruction *inst) {
    vm->running = false;
    return P_OK;
}

static ProstStatus handle_call(ProstVM *vm, Instruction *inst) {
    const char *fn_name = (const char *)inst->arg.as_pointer;
    return p_call(vm, fn_name);
}

static ProstStatus handle_call_extern(ProstVM *vm, Instruction *inst) {
    const char *fn_name = (const char *)inst->arg.as_pointer;
    return p_call_extern(vm, fn_name);
}

static ProstStatus handle_return(ProstVM *vm, Instruction *inst) {
    if (xvec_empty(&vm->call_stack)) {
        return P_ERR_CALL_STACK_UNDERFLOW;
    }
    Word frame_word = xvec_pop(&vm->call_stack);
    CallFrame *frame = (CallFrame *)frame_word.as_pointer;
    vm->current_function = frame->function_name;
    vm->current_function_ptr = (Function *)frame->function_ptr;
    vm->current_ip = frame->return_ip;
    if (vm->frame_pool_index > 0) {
        vm->frame_pool_index--;
    }
    return P_OK;
}

static ProstStatus handle_jmp(ProstVM *vm, Instruction *inst) {
    vm->current_ip = inst->arg.as_int;
    return P_OK;
}

static ProstStatus handle_jmpif(ProstVM *vm, Instruction *inst) {
    if (p_expect(vm, WINT).as_int == 1) {
        vm->current_ip = inst->arg.as_int;
    }
    return vm->status;
}

static ProstStatus handle_eq(ProstVM *vm, Instruction *inst) {
    Word w1 = p_peek(vm);
    Word w2 = p_peek(vm);
    if (w1.type == WPOINTER && word_is_string(&w1) && w2.type == WPOINTER && word_is_string(&w2)) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer) == 0 ? 1 : 0));
    } else if (w1.type == WPOINTER && w1.type == w2.type) {
        p_push(vm, WORD(w1.as_pointer == w2.as_pointer ? 1 : 0));
    } else if (w1.type == w2.type && w1.type == WINT) {
        p_push(vm, WORD(w1.as_int == w2.as_int ? 1 : 0));
    } else if (w1.type == WFLOAT && w2.type == WFLOAT) {
        p_push(vm, WORD(w1.as_float == w2.as_float ? 1 : 0));
    } else {
        p_push(vm, WORD(0));
    }
    return P_OK;
}

static ProstStatus handle_neq(ProstVM *vm, Instruction *inst) {
    Word w = p_peek(vm);
    if (w.type == WINT) {
        p_push(vm, WORD(w.as_int != 0 ? 1 : 0));
    }
    return P_OK;
}

static ProstStatus handle_lt(ProstVM *vm, Instruction *inst) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if (w1.type == WPOINTER && word_is_string(&w1) && w2.type == WPOINTER && word_is_string(&w2)) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer) < 0 ? 1 : 0));
    } else if (w1.type == WPOINTER && w1.type == w2.type) {
        p_push(vm, WORD(w1.as_pointer < w2.as_pointer ? 1 : 0));
    } else if (w1.type == WINT && w2.type == WINT) {
        p_push(vm, WORD(w1.as_int < w2.as_int ? 1 : 0));
    } else if (w1.type == WFLOAT && w2.type == WFLOAT) {
        p_push(vm, WORD(w1.as_float < w2.as_float ? 1 : 0));
    } else {
        p_push(vm, WORD(0));
    }
    return P_OK;
}

static ProstStatus handle_lte(ProstVM *vm, Instruction *inst) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if (w1.type == WPOINTER && word_is_string(&w1) && w2.type == WPOINTER && word_is_string(&w2)) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer) <= 0 ? 1 : 0));
    } else if (w1.type == WPOINTER && w1.type == w2.type) {
        p_push(vm, WORD(w1.as_pointer <= w2.as_pointer ? 1 : 0));
    } else if (w1.type == WINT && w2.type == WINT) {
        p_push(vm, WORD(w1.as_int <= w2.as_int ? 1 : 0));
    } else if (w1.type == WFLOAT && w2.type == WFLOAT) {
        p_push(vm, WORD(w1.as_float <= w2.as_float ? 1 : 0));
    } else {
        p_push(vm, WORD(0));
    }
    return P_OK;
}

static ProstStatus handle_gt(ProstVM *vm, Instruction *inst) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if (w1.type == WPOINTER && word_is_string(&w1) && w2.type == WPOINTER && word_is_string(&w2)) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer) > 0 ? 1 : 0));
    } else if (w1.type == WPOINTER && w1.type == w2.type) {
        p_push(vm, WORD(w1.as_pointer > w2.as_pointer ? 1 : 0));
    } else if (w1.type == WINT && w2.type == WINT) {
        p_push(vm, WORD(w1.as_int > w2.as_int ? 1 : 0));
    } else if (w1.type == WFLOAT && w2.type == WFLOAT) {
        p_push(vm, WORD(w1.as_float > w2.as_float ? 1 : 0));
    } else {
        p_push(vm, WORD(0));
    }
    return P_OK;
}

static ProstStatus handle_gte(ProstVM *vm, Instruction *inst) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if (w1.type == WPOINTER && word_is_string(&w1) && w2.type == WPOINTER && word_is_string(&w2)) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer) >= 0 ? 1 : 0));
    } else if (w1.type == WPOINTER && w1.type == w2.type) {
        p_push(vm, WORD(w1.as_pointer >= w2.as_pointer ? 1 : 0));
    } else if (w1.type == WINT && w2.type == WINT) {
        p_push(vm, WORD(w1.as_int >= w2.as_int ? 1 : 0));
    } else if (w1.type == WFLOAT && w2.type == WFLOAT) {
        p_push(vm, WORD(w1.as_float >= w2.as_float ? 1 : 0));
    } else {
        p_push(vm, WORD(0));
    }
    return P_OK;
}

static ProstStatus handle_dup(ProstVM *vm, Instruction *inst) {
    Word w = p_peek(vm);
    p_push(vm, w);
    return P_OK;
}

static ProstStatus handle_swap(ProstVM *vm, Instruction *inst) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    p_push(vm, w1);
    p_push(vm, w2);
    return P_OK;
}

static ProstStatus handle_over(ProstVM *vm, Instruction *inst) {
    Word *w = xvec_get(&vm->stack, vm->stack.size - 2);
    p_push(vm, *w);
    return P_OK;
}

ProstVM *p_init() {
    ProstVM *vm = (ProstVM *)malloc(sizeof(ProstVM));
    if (!vm) return NULL;

    xvec_init(&vm->stack, 0);
    xvec_init(&vm->call_stack, 0);
    xmap_init(&vm->functions, 0);
    xmap_init(&vm->external_functions, 0);

    vm->status = P_OK;
    vm->running = false;
    vm->exit_code = 0;
    vm->current_function = NULL;
    vm->current_function_ptr = NULL;
    vm->current_ip = 0;

    vm->frame_pool = (CallFrame *)malloc(sizeof(CallFrame) * CALL_FRAME_POOL_SIZE);
    vm->frame_pool_index = 0;

    vm->jump_table[Push] = handle_push;
    vm->jump_table[Pop] = handle_pop;
    vm->jump_table[Drop] = handle_drop;
    vm->jump_table[Halt] = handle_halt;
    vm->jump_table[Call] = handle_call;
    vm->jump_table[CallExtern] = handle_call_extern;
    vm->jump_table[Return] = handle_return;
    vm->jump_table[Jmp] = handle_jmp;
    vm->jump_table[JmpIf] = handle_jmpif;
    vm->jump_table[Eq] = handle_eq;
    vm->jump_table[Neq] = handle_neq;
    vm->jump_table[Lt] = handle_lt;
    vm->jump_table[Lte] = handle_lte;
    vm->jump_table[Gt] = handle_gt;
    vm->jump_table[Gte] = handle_gte;
    vm->jump_table[Dup] = handle_dup;
    vm->jump_table[Swap] = handle_swap;
    vm->jump_table[Over] = handle_over;

    for (int i = 0; i < P_REGISTERS_COUNT; i++) {
        ProstRegister *r = &vm->registers[i];
        r->name = format("r%d", i);
        r->value = WORD(0);
    }

    return vm;
}

void p_free(ProstVM *vm) {
    if (!vm) return;

    xvec_free(&vm->stack);
    xvec_free(&vm->call_stack);

    for (size_t i = 0; i < vm->functions.size; i++) {
        XEntry iter = vm->functions.entries[i];
        Function *fn = (Function *)iter.value.as_pointer;
        if (fn) {
            if (fn->instructions.data) {
                free(fn->instructions.data);
            }
            free(fn);
        }
    }

    xmap_free(&vm->functions);
    xmap_free(&vm->external_functions);

    for (int i = 0; i < P_REGISTERS_COUNT; i++) {
        ProstRegister *r = &vm->registers[i];
        free((void*)r->name);
        r->value = WORD(0);
    }

    free(vm->frame_pool);
    free(vm);
}

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

ByteBuf p_to_bytecode(ProstVM *vm) {
    ByteBuf bb;
    bb_init(&bb, 1024);

    uint16_t fn_count = (uint16_t)vm->functions.size;
    bb_append(&bb, &fn_count, sizeof(uint16_t));

    for (size_t i = 0; i < vm->functions.size; i++) {
        XEntry entry = vm->functions.entries[i];
        const char *fn_name = entry.key;
        Function *fn = (Function *)entry.value.as_pointer;

        if (!fn) continue;

        uint16_t name_len = (uint16_t)strlen(fn_name);
        bb_append(&bb, &name_len, sizeof(uint16_t));
        bb_append(&bb, fn_name, name_len);

        uint16_t inst_count = (uint16_t)fn->instructions.count;
        bb_append(&bb, &inst_count, sizeof(uint16_t));

        for (size_t j = 0; j < inst_count; j++) {
            Instruction *inst = &fn->instructions.data[j];

            uint8_t inst_type = (uint8_t)inst->type;
            bb_append(&bb, &inst_type, sizeof(uint8_t));

            if (inst->type == Call || inst->type == CallExtern) {
                const char *str = (const char *)inst->arg.as_pointer;
                uint16_t str_len = str ? (uint16_t)strlen(str) : 0;
                bb_append(&bb, &str_len, sizeof(uint16_t));
                if (str_len > 0) {
                    bb_append(&bb, str, str_len);
                }
            } else {
                bb_append(&bb, &inst->arg, sizeof(Word));
            }
        }
    }

    return bb;
}

ProstStatus p_from_bytecode(ProstVM *vm, const char *bytecode) {
    if (!vm || !bytecode) {
        vm->status = P_ERR_INVALID_BYTECODE;
        return vm->status;
    }

    const uint8_t *ptr = (const uint8_t *)bytecode;

    uint16_t fn_count;
    memcpy(&fn_count, ptr, sizeof(uint16_t));
    ptr += sizeof(uint16_t);

    for (uint16_t i = 0; i < fn_count; i++) {
        uint16_t name_len;
        memcpy(&name_len, ptr, sizeof(uint16_t));
        ptr += sizeof(uint16_t);

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

        uint16_t inst_count;
        memcpy(&inst_count, ptr, sizeof(uint16_t));
        ptr += sizeof(uint16_t);

        fn->instructions.data = (Instruction *)malloc(sizeof(Instruction) * inst_count);
        fn->instructions.count = inst_count;
        fn->instructions.capacity = inst_count;

        for (uint16_t j = 0; j < inst_count; j++) {
            Instruction *inst = &fn->instructions.data[j];

            uint8_t inst_type;
            memcpy(&inst_type, ptr, sizeof(uint8_t));
            ptr += sizeof(uint8_t);
            inst->type = (InstructionType)inst_type;

            if (inst->type == Call || inst->type == CallExtern) {
                uint16_t str_len;
                memcpy(&str_len, ptr, sizeof(uint16_t));
                ptr += sizeof(uint16_t);

                if (str_len > 0) {
                    char *str = (char *)malloc(str_len + 1);
                    if (!str) {
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
            } else {
                memcpy(&inst->arg, ptr, sizeof(Word));
                ptr += sizeof(Word);
            }
        }

        xmap_set(&vm->functions, fn_name, WORD(fn));
    }

    vm->status = P_OK;
    return vm->status;
}

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

    CallFrame *frame;
    if (vm->frame_pool_index < CALL_FRAME_POOL_SIZE) {
        frame = &vm->frame_pool[vm->frame_pool_index++];
    } else {
        frame = (CallFrame *)malloc(sizeof(CallFrame));
        if (!frame) {
            vm->status = P_ERR_INVALID_VM_STATE;
            return vm->status;
        }
    }

    frame->function_name = vm->current_function;
    frame->function_ptr = vm->current_function_ptr;
    frame->return_ip = vm->current_ip;
    xvec_push(&vm->call_stack, WORD(frame));

    vm->current_function = name;
    vm->current_function_ptr = (Function *)fn_word->as_pointer;
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

ProstStatus p_execute_instruction(ProstVM *vm, Instruction *instruction) {
    if (!vm) return P_ERR_INVALID_INDEX;

    if (instruction->type >= INSTRUCTION_COUNT) {
        vm->status = P_ERR_INVALID_BYTECODE;
        return vm->status;
    }

    return vm->jump_table[instruction->type](vm, instruction);
}

ProstStatus p_run(ProstVM *vm) {
    if (!vm) return P_ERR_INVALID_VM_STATE;

    vm->running = true;
    vm->current_function = "__entry";
    vm->current_ip = 0;

    Word *fn_word = xmap_get(&vm->functions, "__entry");
    if (!fn_word || fn_word->as_pointer == NULL) {
        vm->status = P_ERR_FUNCTION_NOT_FOUND;
        return vm->status;
    }
    vm->current_function_ptr = (Function *)fn_word->as_pointer;

    while (vm->running) {
        Function *fn = vm->current_function_ptr;

        if (vm->current_ip >= fn->instructions.count) {
            if (xvec_empty(&vm->call_stack)) {
                vm->running = false;
                break;
            }

            Word frame_word = xvec_pop(&vm->call_stack);
            CallFrame *frame = (CallFrame *)frame_word.as_pointer;

            vm->current_function = frame->function_name;
            vm->current_function_ptr = (Function *)frame->function_ptr;
            vm->current_ip = frame->return_ip;

            if (vm->frame_pool_index > 0) {
                vm->frame_pool_index--;
            }
            continue;
        }

        Instruction *inst = &fn->instructions.data[vm->current_ip];
        vm->current_ip++;

        ProstStatus status = vm->jump_table[inst->type](vm, inst);
        if (status != P_OK) {
            return status;
        }
    }

    return P_OK;
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

void p_throw_warning(ProstVM *vm, const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    printf("[PROST %s:%zu]", vm->current_function, vm->current_ip);
    vprintf(msg, args);
    printf("\n");
    va_end(args);
}

#endif
#endif