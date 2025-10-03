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

void register_std(ProstVM *vm) {
    p_register_external(vm, "print", print);
}


#endif //STD_H