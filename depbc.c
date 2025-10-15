// DeProstByteCode
#define PROST_IMPLEMENTATION
#include "prost/prost.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Could not open file '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }

    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    return content;
}

const char *p_instr_to_str(InstructionType t) {
    switch (t) {
        case Push: {
            return "push";
        } break;
        case Pop: {
            return "pop";
        } break;
        case Drop: {
            return "drop";
        } break;
        case Halt: {
            return "halt";
        } break;
        case Call: {
            return "call";
        } break;
        case CallExtern: {
            return "call_extern";
        } break;
        case Return: {
            return "return";
        } break;
        case Jmp: {
            return "jmp";
        } break;
        case JmpIf: {
            return "jmp_if";
        } break;
        case Dup: {
            return "dup";
        } break;
        case Swap: {
            return "swap";
        } break;
        case Over: {
            return "over";
        } break;
        case Eq: {
            return "eq";
        } break;
        case Neq: {
            return "neq";
        } break;
        case Lt: {
            return "lt";
        } break;
        case Lte: {
            return "lte";
        } break;
        case Gt: {
            return "gt";
        } break;
        case Gte: {
            return "gte";
        } break;
    }
}

const char *word_to_str(const Word *w) {
    static char buffer[64];

    switch (w->type) {
        case WINT: {
            if (word_is_unsigned(w)) {
                snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)w->as_int);
            } else {
                snprintf(buffer, sizeof(buffer), "%lld", w->as_int);
            }
            return buffer;
        } break;

        case WFLOAT: {
            snprintf(buffer, sizeof(buffer), "%g", w->as_float);
            return buffer;
        } break;

        case WCHAR_: {
            snprintf(buffer, sizeof(buffer), "%c", w->as_char);
            return buffer;
        } break;

        case WPOINTER: {
            if (word_is_string(w) && w->as_pointer != NULL) {
                return (const char*)w->as_pointer;
            } else if (w->as_pointer != NULL) {
                snprintf(buffer, sizeof(buffer), "%p", w->as_pointer);
                return buffer;
            } else {
                return "";
            }
        } break;

        default: {
            return "UNKNOWN";
        }
    }
}

int main(int argc, char **argv) {
    ProstVM *vm = p_init();
    if (argc < 2) {
        fprintf(stderr, "Usage: depbc <file.pco>\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "rb");
    char *bytecode = read_file(argv[1]);
    p_from_bytecode(vm, bytecode);

    printf("Prost Bytecode Decompiler v0.1\n");

    for (size_t i = 0; i < vm->functions.size; i++) {
        printf("%s {\n", vm->functions.entries[i].key);
        Function *fn = (Function*)vm->functions.entries[i].value.as_pointer;
        for (size_t j = 0; j < xvec_len(&fn->instructions); j++) {
            Instruction *instr = ((Instruction *)xvec_get(&fn->instructions, j)->as_pointer);
            printf("%s %s\n", p_instr_to_str(instr->type), word_to_str(&instr->arg));
        }
        printf("}\n");
    }

    free(bytecode);
    return 0;
}



