#define PROST_IMPLEMENTATION
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include "prost/prost.h"
#include "prost/std.h"

typedef enum {
    T_NUMBER,
    T_ID,
    T_STRING,
    T_BLOCK,
    T_SYMBOL,
    T_LABEL,
    T_EOF,
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    const char *src;
    size_t pos;
} Lexer;

typedef struct {
    char *name;
    size_t position;
} Label;

typedef struct {
    Token *tokens;
    size_t current;
    size_t count;
    ProstVM *vm;
    Label *labels;
    size_t label_count;
    size_t label_capacity;
} Parser;

static char peek(Lexer *lex) {
    return lex->src[lex->pos];
}

static char get(Lexer *lex) {
    return lex->src[lex->pos++];
}

static void skip_ws(Lexer *lex) {
    while (isspace(peek(lex))) get(lex);
    if (peek(lex) == ';') {
        while (peek(lex) && peek(lex) != '\n') get(lex);
        skip_ws(lex);
    }
}

static char *substr(const char *s, size_t start, size_t end) {
    size_t len = end - start;
    char *res = malloc(len + 1);
    memcpy(res, s + start, len);
    res[len] = '\0';
    return res;
}

Token next_token(Lexer *lex) {
    skip_ws(lex);
    char c = peek(lex);

    if (c == '\0') {
        return (Token){T_EOF, NULL};
    }

    if (isdigit(c) || (c == '-' && isdigit(lex->src[lex->pos + 1]))) {
        size_t start = lex->pos;
        if (c == '-') get(lex);
        while (isdigit(peek(lex))) get(lex);
        return (Token){T_NUMBER, substr(lex->src, start, lex->pos)};
    }

    if (isalpha(c) || c == '_') {
        size_t start = lex->pos;
        while (isalnum(peek(lex)) || peek(lex) == '_') get(lex);
        size_t end = lex->pos;
        
        if (peek(lex) == ':') {
            get(lex);
            return (Token){T_LABEL, substr(lex->src, start, end)};
        }
        
        return (Token){T_ID, substr(lex->src, start, end)};
    }

    if (c == '"') {
        get(lex);
        size_t start = lex->pos;
        while (peek(lex) && peek(lex) != '"') {
            if (peek(lex) == '\\') get(lex);
            get(lex);
        }
        size_t end = lex->pos;
        if (peek(lex) == '"') get(lex);
        return (Token){T_STRING, substr(lex->src, start, end)};
    }

    if (c == '{') {
        int depth = 0;
        size_t start = lex->pos;
        do {
            if (peek(lex) == '{') depth++;
            if (peek(lex) == '}') depth--;
            get(lex);
        } while (peek(lex) && depth > 0);
        return (Token){T_BLOCK, substr(lex->src, start, lex->pos)};
    }

    if (c == '*' || c == ':' || c == '@' || c == '.' || c == '=' || c == '}') {
        get(lex);
        return (Token){T_SYMBOL, substr(lex->src, lex->pos - 1, lex->pos)};
    }

    size_t start = lex->pos;
    get(lex);
    return (Token){T_ID, substr(lex->src, start, lex->pos)};
}

const char* token_to_str(TokenType t) {
    switch(t) {
        case T_NUMBER: return "NUMBER";
        case T_ID: return "ID";
        case T_STRING: return "STRING";
        case T_BLOCK: return "BLOCK";
        case T_SYMBOL: return "SYMBOL";
        case T_LABEL: return "LABEL";
        case T_EOF: return "EOF";
        default: return "UNKNOWN";
    }
}

static Token peek_parser(Parser *p) {
    if (p->current >= p->count) {
        return (Token){T_EOF, NULL};
    }
    return p->tokens[p->current];
}

static Token advance(Parser *p) {
    if (p->current >= p->count) {
        return (Token){T_EOF, NULL};
    }
    return p->tokens[p->current++];
}

static bool match(Parser *p, TokenType type) {
    Token tok = peek_parser(p);
    return tok.type == type;
}

static Token expect(Parser *p, TokenType type) {
    Token tok = advance(p);
    if (tok.type != type) {
        fprintf(stderr, "Parse error: expected %s, got %s\n",
                token_to_str(type), token_to_str(tok.type));
        exit(1);
    }
    return tok;
}

static void add_label(Parser *p, const char *name, size_t position) {
    if (p->label_count >= p->label_capacity) {
        p->label_capacity = p->label_capacity == 0 ? 8 : p->label_capacity * 2;
        p->labels = realloc(p->labels, p->label_capacity * sizeof(Label));
    }
    p->labels[p->label_count].name = strdup(name);
    p->labels[p->label_count].position = position;
    p->label_count++;
}

static size_t find_label(Parser *p, const char *name) {
    for (size_t i = 0; i < p->label_count; i++) {
        if (strcmp(p->labels[i].name, name) == 0) {
            return p->labels[i].position;
        }
    }
    fprintf(stderr, "Error: Label '%s' not found\n", name);
    exit(1);
}

static void parse_mem_block(Parser *p) {
    expect(p, T_ID);
    Token block = expect(p, T_BLOCK);

    Lexer inner_lex = {block.value + 1, 0};

    while (inner_lex.src[inner_lex.pos] && inner_lex.src[inner_lex.pos] != '}') {
        skip_ws(&inner_lex);

        if (inner_lex.src[inner_lex.pos] == '}') break;

        Token name = next_token(&inner_lex);
        if (name.type == T_EOF) break;

        if (name.type == T_SYMBOL && name.value && strcmp(name.value, "}") == 0) {
            if (name.value) free(name.value);
            break;
        }

        if (name.type != T_ID) {
            if (name.value) free(name.value);
            continue;
        }

        Token colon = next_token(&inner_lex);
        if (colon.type != T_SYMBOL || strcmp(colon.value, ":") != 0) {
            fprintf(stderr, "Expected ':' after memory name\n");
            if (name.value) free(name.value);
            if (colon.value) free(colon.value);
            continue;
        }

        Token value = next_token(&inner_lex);
        if (value.type == T_NUMBER) {
            Word w = WORD(atoll(value.value));
            char *name_copy = strdup(name.value);
            xmap_set(&p->vm->memory, name_copy, w);
        }

        if (name.value) free(name.value);
        if (colon.value) free(colon.value);
        if (value.value) free(value.value);
    }
}

static Instruction** parse_instruction(Parser *p, size_t *inst_count, size_t *current_pos) {
    *inst_count = 0;
    Token tok = peek_parser(p);

    if (tok.type == T_LABEL) {
        Token label_tok = advance(p);
        add_label(p, label_tok.value, *current_pos);
        return NULL;
    }

    if (tok.type == T_ID && strcmp(tok.value, "mem") == 0) {
        advance(p);
        Token dot = expect(p, T_SYMBOL);
        Token name = expect(p, T_ID);
        Token eq = advance(p);

        if (eq.type == T_SYMBOL && strcmp(eq.value, "=") == 0) {
            Token value_tok = advance(p);

            Instruction **instructions = malloc(sizeof(Instruction*) * 2);
            *inst_count = 2;

            instructions[0] = malloc(sizeof(Instruction));

            if (value_tok.type == T_NUMBER) {
                instructions[0]->type = Push;
                instructions[0]->arg = WORD((void*)(intptr_t)atoi(value_tok.value));
            }
            else if (value_tok.type == T_SYMBOL && strcmp(value_tok.value, "*") == 0) {
                Token mem_keyword = expect(p, T_ID);
                Token mem_dot = expect(p, T_SYMBOL);
                Token mem_name = expect(p, T_ID);

                instructions[0]->type = DerefMemory;
                instructions[0]->arg = WORD(strdup(mem_name.value));
            }
            else {
                fprintf(stderr, "Unsupported value type in memory assignment\n");
                free(instructions[0]);
                free(instructions);
                return NULL;
            }

            instructions[1] = malloc(sizeof(Instruction));
            instructions[1]->type = AssignMemory;
            instructions[1]->arg = WORD(strdup(name.value));

            return instructions;
        }

        return NULL;
    }

    tok = advance(p);
    Instruction **instructions = malloc(sizeof(Instruction*));
    *inst_count = 1;
    instructions[0] = malloc(sizeof(Instruction));
    Instruction *inst = instructions[0];

    if (tok.type == T_ID) {
        if (strcmp(tok.value, "push") == 0) {
            inst->type = Push;
            Token arg = advance(p);
            if (arg.type == T_NUMBER) {
                inst->arg = word_uint64((uint64_t)atoll(arg.value));
            } else if (arg.type == T_STRING) {
                inst->arg = word_string(strdup(arg.value));
            } else if (arg.type == T_ID && strcmp(arg.value, "mem") == 0) {
                expect(p, T_SYMBOL);
                Token mem_name = expect(p, T_ID);
                inst->type = DerefMemory;
                inst->arg = WORD(strdup(mem_name.value));
            } else {
                fprintf(stderr, "Error: push expects a number or string or memory\n");
                free(inst);
                free(instructions);
                return NULL;
            }
        }
        else if (strcmp(tok.value, "drop") == 0) {
            inst->type = Drop;
            inst->arg = WORD(NULL);
        }
        else if (strcmp(tok.value, "halt") == 0 || strcmp(tok.value, "ret") == 0) {
            inst->type = Halt;
            inst->arg = WORD(NULL);
        }
        else if (strcmp(tok.value, "call") == 0) {
            Token name = advance(p);
            if (name.type == T_SYMBOL && strcmp(name.value, "@") == 0) {
                Token fn_name = expect(p, T_ID);
                inst->type = CallExtern;
                inst->arg = WORD(strdup(fn_name.value));
            } else if (name.type == T_ID) {
                inst->type = Call;
                inst->arg = WORD(strdup(name.value));
            }
        } else if (strcmp(tok.value, "jmp") == 0) {
            Token target = advance(p);
            inst->type = Jmp;
            if (target.type == T_NUMBER) {
                inst->arg = word_int((int64_t)atoll(target.value));
            } else if (target.type == T_ID) {
                inst->arg = word_string(strdup(target.value));
            }
        } else if (strcmp(tok.value, "jmpif") == 0) {
            Token target = advance(p);
            inst->type = JmpIf;
            if (target.type == T_NUMBER) {
                inst->arg = word_int((int64_t)atoll(target.value));
            } else if (target.type == T_ID) {
                inst->arg = word_string(strdup(target.value));
            }
        }
        else {
            fprintf(stderr, "Unknown instruction: %s\n", tok.value);
            free(inst);
            free(instructions);
            return NULL;
        }
    }
    else if (tok.type == T_SYMBOL && strcmp(tok.value, "*") == 0) {
        Token mem = expect(p, T_ID);
        Token dot = expect(p, T_SYMBOL);
        Token name = expect(p, T_ID);

        inst->type = DerefMemory;
        inst->arg = WORD(strdup(name.value));
    }
    else {
        fprintf(stderr, "Unexpected token: %s\n", token_to_str(tok.type));
        free(inst);
        free(instructions);
        return NULL;
    }

    return instructions;
}

static void parse_function(Parser *p) {
    Token name = expect(p, T_ID);
    Token block = expect(p, T_BLOCK);

    Function *fn = malloc(sizeof(Function));
    xvec_init(&fn->instructions, 0);
    xvec_init(&fn->labels, 0);

    Lexer inner_lex = {block.value + 1, 0};
    Parser inner_parser;
    inner_parser.vm = p->vm;
    inner_parser.current = 0;
    inner_parser.labels = NULL;
    inner_parser.label_count = 0;
    inner_parser.label_capacity = 0;

    Token *inner_tokens = NULL;
    size_t inner_count = 0;
    size_t inner_capacity = 0;

    Token inner_tok;
    while ((inner_tok = next_token(&inner_lex)).type != T_EOF) {
        if (inner_tok.value && strcmp(inner_tok.value, "}") == 0) {
            free(inner_tok.value);
            break;
        }

        if (inner_count >= inner_capacity) {
            inner_capacity = inner_capacity == 0 ? 8 : inner_capacity * 2;
            inner_tokens = realloc(inner_tokens, inner_capacity * sizeof(Token));
        }

        inner_tokens[inner_count++] = inner_tok;
    }

    inner_parser.tokens = inner_tokens;
    inner_parser.count = inner_count;

    size_t current_pos = 0;
    while (inner_parser.current < inner_parser.count) {
        size_t inst_count = 0;
        Instruction **instructions = parse_instruction(&inner_parser, &inst_count, &current_pos);

        if (instructions) {
            for (size_t i = 0; i < inst_count; i++) {
                xvec_push(&fn->instructions, WORD(instructions[i]));
                current_pos++;
            }
            free(instructions);
        }
    }

    for (size_t i = 0; i < xvec_len(&fn->instructions); i++) {
        Word *inst_word = xvec_get(&fn->instructions, i);
        Instruction *inst = (Instruction *)inst_word->as_pointer;
        
        if ((inst->type == Jmp || inst->type == JmpIf) && inst->arg.type == WPOINTER) {
            char *label_name = (char *)inst->arg.as_pointer;
            size_t target = find_label(&inner_parser, label_name);
            free(label_name);
            inst->arg = word_int((int64_t)target);
        }
    }

    for (size_t i = 0; i < inner_parser.label_count; i++) {
        free(inner_parser.labels[i].name);
    }
    free(inner_parser.labels);

    for (size_t i = 0; i < inner_count; i++) {
        if (inner_tokens[i].value) free(inner_tokens[i].value);
    }
    free(inner_tokens);

    char *name_copy = strdup(name.value);
    xmap_set(&p->vm->functions, name_copy, WORD(fn));
}

static void parse(Parser *p) {
    while (!match(p, T_EOF)) {
        Token tok = peek_parser(p);

        if (tok.type == T_ID) {
            if (strcmp(tok.value, "mem") == 0) {
                parse_mem_block(p);
            } else {
                parse_function(p);
            }
        } else {
            advance(p);
        }
    }
}

ProstStatus assemble(ProstVM *vm, const char *src) {
    Lexer lex = {src, 0};

    Token *tokens = NULL;
    size_t count = 0;
    size_t capacity = 0;

    Token tok;
    while ((tok = next_token(&lex)).type != T_EOF) {
        if (count >= capacity) {
            capacity = capacity == 0 ? 8 : capacity * 2;
            tokens = realloc(tokens, capacity * sizeof(Token));
        }
        tokens[count++] = tok;
    }

    Parser parser;
    parser.tokens = tokens;
    parser.count = count;
    parser.current = 0;
    parser.vm = vm;
    parser.labels = NULL;
    parser.label_count = 0;
    parser.label_capacity = 0;

    parse(&parser);

    for (size_t i = 0; i < count; i++) {
        if (tokens[i].value) free(tokens[i].value);
    }
    free(tokens);

    return P_OK;
}

char* read_file(const char *path) {
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

void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS] <input_file>\n\n", prog);
    printf("Options:\n");
    printf("  -h, --help           Show this help message\n");
    printf("  -o, --output FILE    Output bytecode file (default: out.pco)\n");
    printf("  -d, --library FILE   Load a library\n");
    printf("  -r, --dont-run       Don't run the bytecode after compilation\n");
    printf("  -c, --dont-compile   Don't compile, only run (for .pa files)\n");
    printf("  -v, --verbose        Enable verbose output\n");
    printf("\nFile Extensions:\n");
    printf("  .pa  - Prost Assembly (source code)\n");
    printf("  .pco - Prost Compiled Object (bytecode)\n");
    printf("\nNotes:\n");
    printf("  - .pco files are automatically treated as bytecode (no compilation)\n");
    printf("  - .pa files are compiled by default unless -c is specified\n");
    printf("\nExamples:\n");
    printf("  %s program.pa                       # Compile and run\n", prog);
    printf("  %s -r program.pa                    # Compile only\n", prog);
    printf("  %s program.pco                      # Run bytecode (auto-detected)\n", prog);
    printf("  %s -c program.pa                    # Skip compilation, run directly\n", prog);
    printf("  %s -o custom.pco program.pa         # Custom output file\n", prog);
}

int main(int argc, char **argv) {
    bool dont_run = false;
    bool dont_compile = false;
    bool verbose = false;
    char *output_file = "out.pco";
    char *input_file = NULL;
    XVec load_library = xvec_create(2);

    static struct option long_options[] = {
        {"help",         no_argument,       0, 'h'},
        {"output",       required_argument, 0, 'o'},
        {"dont-run",     no_argument,       0, 'r'},
        {"dont-compile", no_argument,       0, 'c'},
        {"verbose",      no_argument,       0, 'v'},
        {"library",      required_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "ho:rcv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'o':
                output_file = optarg;
                break;
            case 'r':
                dont_run = true;
                break;
            case 'c':
                dont_compile = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'd':
                xvec_push(&load_library, WORD(strdup(optarg)));
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    input_file = argv[optind];

    if (dont_run && dont_compile) {
        fprintf(stderr, "Error: Cannot use --dont-run and --dont-compile together\n");
        return 1;
    }

    size_t len = strlen(input_file);
    bool is_bytecode = (len > 4 && strcmp(input_file + len - 4, ".pco") == 0);

    if (is_bytecode) {
        dont_compile = true;
        if (verbose) printf("Detected bytecode file (.pco), skipping compilation\n");
    }

    ProstVM *vm = p_init();
    if (!vm) {
        fprintf(stderr, "Error: Failed to initialize VM\n");
        return 1;
    }
    register_std(vm);

    for (int i = 0; i < xvec_len(&load_library); i++) {
        p_load_library(vm, (const char*)xvec_get(&load_library, i)->as_pointer);
    }
    xvec_free(&load_library);

    if (!dont_compile) {
        if (verbose) printf("Reading source file: %s\n", input_file);

        char *source = read_file(input_file);
        if (!source) {
            p_free(vm);
            return 1;
        }

        if (verbose) printf("Assembling...\n");

        ProstStatus status = assemble(vm, source);
        free(source);

        if (verbose) printf("Generating bytecode...\n");

        ByteBuf bytecode = p_to_bytecode(vm);

        if (verbose) printf("Writing bytecode to: %s\n", output_file);

        FILE *f = fopen(output_file, "wb");
        if (!f) {
            fprintf(stderr, "Error: Could not write to file '%s'\n", output_file);
            p_free(vm);
            return 1;
        }

        fwrite(bytecode.data, 1, bytecode.len, f);
        fclose(f);

        if (verbose) printf("Compilation successful (%zu bytes)\n", bytecode.len);
    }

    if (!dont_run) {
        const char *bytecode_file = dont_compile ? input_file : output_file;

        if (verbose) printf("Loading bytecode from: %s\n", bytecode_file);

        char *bytecode = read_file(bytecode_file);
        if (!bytecode) {
            p_free(vm);
            return 1;
        }

        if (dont_compile) {
            p_free(vm);
            vm = p_init();
            register_std(vm);
            if (!vm) {
                fprintf(stderr, "Error: Failed to initialize VM\n");
                free(bytecode);
                return 1;
            }
        }

        if (verbose) printf("Loading bytecode into VM...\n");

        ProstStatus status = p_from_bytecode(vm, bytecode);
        free(bytecode);

        if (status != P_OK) {
            fprintf(stderr, "Error: Failed to load bytecode (status %d)\n", status);
            p_free(vm);
            return 1;
        }

        if (verbose) printf("Running program...\n");

        status = p_run(vm);

        if (status != P_OK) {
            const char *error_msg = "Unknown error";
            switch (status) {
                case P_ERR_STACK_UNDERFLOW:
                    error_msg = "Stack underflow";
                    break;
                case P_ERR_INVALID_BYTECODE:
                    error_msg = "Invalid bytecode";
                    break;
                case P_ERR_LIBRARY_NOT_FOUND:
                    error_msg = "Library not found";
                    break;
                case P_ERR_FUNCTION_NOT_FOUND:
                    error_msg = "Function not found";
                    break;
                case P_ERR_INVALID_INDEX:
                    error_msg = "Invalid index";
                    break;
                case P_ERR_CALL_STACK_UNDERFLOW:
                    error_msg = "Call stack underflow";
                    break;
                case P_ERR_INVALID_VM_STATE:
                    error_msg = "Invalid VM state";
                    break;
                default:
                    break;
            }

            fprintf(stderr, "Runtime error: %s (status %d)\n", error_msg, status);
            fprintf(stderr, "  Function: %s\n", vm->current_function ? vm->current_function : "unknown");
            fprintf(stderr, "  Instruction pointer: %zu\n", vm->current_ip);
            p_free(vm);
            return 1;
        }

        if (verbose) {
            printf("\n=== Execution Complete ===\n");
            printf("Stack size: %zu\n", vm->stack.size);
            if (vm->stack.size > 0) {
                Word top = p_peek(vm);
                printf("Top of stack: %ld\n", (long)top.as_pointer);
            }
            printf("Exit code: %d\n", vm->exit_code);
        }
    }

    p_free(vm);
    return 0;
}
