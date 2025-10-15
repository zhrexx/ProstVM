#ifndef STD_H
#define STD_H
#include "prost.h"

void print(ProstVM *vm) {
    Word w = p_peek(vm);

    switch (w.type) {
        case WINT:
            printf("%llu\n", w.as_int);
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
    p_push(vm, WORD(result));
}

void sub(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    int64_t result = 0;

    if (w1.type == WINT) result = w1.as_int;
    if (w2.type == WINT) result -= w2.as_int;

    p_push(vm, WORD(result));
}

void mul(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    uint64_t result = 1;

    if (w1.type == WINT) result = w1.as_int;
    if (w2.type == WINT) result *= w2.as_int;

    p_push(vm, WORD(result));
}

void divi(ProstVM *vm) {
    Word w1 = p_pop(vm);
    Word w2 = p_pop(vm);
    if ((w2.type == WINT && w2.as_int == 0)) {
        fprintf(stderr, "ERROR: Division by zero\n");
        vm->status = P_ERR_GENERAL_VM_ERROR;
        vm->running = false;
        p_push(vm, WORD(0));
        return;
    }

    p_push(vm, WORD(w1.as_int / w2.as_int));
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

void neg(ProstVM *vm) {
    Word w = p_pop(vm);

    switch (w.type) {
        case WINT:
            p_push(vm, WORD(-w.as_int));
            break;
        default:
            p_throw_warning(vm, "Trying to negate non-numeric value");
            p_push(vm, WORD(0));
            break;
    }
}

void typeof_(ProstVM *vm) {
    Word w = p_peek(vm);
    p_push(vm, WORD(word_type_to_str(w.type)));
}


static XVec allocation_state = {0};

// Allocates memory and saves pointer
void alloc(ProstVM* vm) {
    const int size = p_expect(vm, WINT).as_int;
    void *m = malloc(size);
    xvec_push(&allocation_state, WORD(m));

    p_push(vm, WORD(m));
}



void register_std(ProstVM *vm) {
    allocation_state = xvec_create(1);

    p_register_external(vm, "print", print);
    p_register_external(vm, "add", add);
    p_register_external(vm, "sub", sub);
    p_register_external(vm, "mul", mul);
    p_register_external(vm, "divi", divi);
    p_register_external(vm, "cmp", cmp);
    p_register_external(vm, "neg", neg);
    p_register_external(vm, "alloc", alloc);
    p_register_external(vm, "typeof", typeof_);
}

void unload_std() {
    for (int i = 0; i < xvec_len(&allocation_state); i++) {
        void *ptr = xvec_get(&allocation_state, i)->as_pointer;
        if (ptr != NULL) free(ptr);
    }
    xvec_free(&allocation_state);
}

#endif //STD_H