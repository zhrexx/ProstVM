#ifndef STD_H
#define STD_H
#include "prost.h"

void print(ProstVM *vm) {
    Word w = p_pop(vm);

    switch (w.type) {
        case WINT:
            printf("%d\n", w.as_int);
            break;
        case WUINT64:
            printf("%lu\n", w.as_uint64);
            break;
        case WPOINTER: // treat as string
            printf("%s\n", (char *)w.as_pointer);
            break;
        default: {
            return;
        }
    }

}

void add(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    uint64_t result = 0;

    if (w1.type == WINT) result += w1.as_int;
    if (w2.type == WINT) result += w2.as_int;
    if (w1.type == WUINT64) result += w1.as_uint64;
    if (w2.type == WUINT64) result += w2.as_uint64;

    p_push(vm, word_uint64(result));
}

void sub(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    int64_t result = 0;

    if (w1.type == WINT) result = w1.as_int;
    else if (w1.type == WUINT64) result = w1.as_uint64;

    if (w2.type == WINT) result -= w2.as_int;
    else if (w2.type == WUINT64) result -= w2.as_uint64;

    p_push(vm, word_uint64(result));
}

void mul(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    uint64_t result = 1;

    if (w1.type == WINT) result = w1.as_int;
    else if (w1.type == WUINT64) result = w1.as_uint64;

    if (w2.type == WINT) result *= w2.as_int;
    else if (w2.type == WUINT64) result *= w2.as_uint64;

    p_push(vm, word_uint64(result));
}

void divi(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if ((w2.type == WINT && w2.as_int == 0) || (w2.type == WUINT64 && w2.as_uint64 == 0)) {
        fprintf(stderr, "ERROR: Division by zero\n");
        vm->status = P_ERR_GENERAL_VM_ERROR;
        vm->running = false;
        p_push(vm, word_uint64(0));
        return;
    }

    uint64_t numerator = (w1.type == WINT) ? w1.as_int : w1.as_uint64;
    uint64_t denominator = (w2.type == WINT) ? w2.as_int : w2.as_uint64;

    p_push(vm, word_uint64(numerator / denominator));
}

void cmp(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);

    if (w1.type == WPOINTER && w2.type == WPOINTER) {
        p_push(vm, WORD(strcmp(w1.as_pointer, w2.as_pointer))); // pointer == string (most cases we dont use pointers in
        return;
    }
    if (w1.type == w2.type) {
        p_push(vm, WORD(w1.as_int == w2.as_int));
        return;
    }

    p_push(vm, WORD(0));
}


void register_std(ProstVM *vm) {
    p_register_external(vm, "print", print);
    p_register_external(vm, "add", add);
    p_register_external(vm, "sub", sub);
    p_register_external(vm, "mul", mul);
    p_register_external(vm, "divi", divi);
}


#endif //STD_H