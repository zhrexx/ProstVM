#define PROST_IMPLEMENTATION
#include "prost/prost.h"
#include "prost/std.h"
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOK_NUM,
    TOK_IDENT,
    TOK_STR,
    TOK_LBRACE,
    TOK_LPAREN,
    TOK_RBRACE,
    TOK_RPAREN,
    TOK_COLON,
    TOK_DOT,
    TOK_AT,
    TOK_STAR,
    TOK_EQ,
    TOK_EOF,
} TokenKind;

typedef struct {
    TokenKind kind;
    char *lexeme;
    int line;
    int col;
} Token;

typedef struct {
    const char *input;
    size_t pos;
    size_t len;
    int line;
    int col;
} Tokenizer;

typedef struct {
    char *name;
    size_t position;
} Label;

typedef struct {
    Label *labels;
    size_t count;
    size_t capacity;
} LabelTable;

typedef struct {
    Token *tokens;
    size_t pos;
    size_t count;
    ProstVM *vm;
    LabelTable *current_labels;
} ParserState;

static void label_table_init(LabelTable *lt) {
    lt->labels = NULL;
    lt->count = 0;
    lt->capacity = 0;
}

static void label_table_free(LabelTable *lt) {
    for (size_t i = 0; i < lt->count; i++) {
        free(lt->labels[i].name);
    }
    free(lt->labels);
}

static void label_table_add(LabelTable *lt, const char *name, size_t position) {
    if (lt->count >= lt->capacity) {
        lt->capacity = lt->capacity == 0 ? 8 : lt->capacity * 2;
        lt->labels = realloc(lt->labels, lt->capacity * sizeof(Label));
    }
    lt->labels[lt->count].name = strdup(name);
    lt->labels[lt->count].position = position;
    lt->count++;
}

static bool label_table_find(LabelTable *lt, const char *name, size_t *out_position) {
    for (size_t i = 0; i < lt->count; i++) {
        if (strcmp(lt->labels[i].name, name) == 0) {
            *out_position = lt->labels[i].position;
            return true;
        }
    }
    return false;
}

static void tok_init(Tokenizer *t, const char *src) {
    t->input = src;
    t->pos = 0;
    t->len = strlen(src);
    t->line = 1;
    t->col = 1;
}

static char tok_peek(Tokenizer *t) {
    if (t->pos >= t->len)
        return '\0';
    return t->input[t->pos];
}

static char tok_advance(Tokenizer *t) {
    if (t->pos >= t->len)
        return '\0';
    char c = t->input[t->pos++];
    if (c == '\n') {
        t->line++;
        t->col = 1;
    } else {
        t->col++;
    }
    return c;
}

static void tok_skip_whitespace(Tokenizer *t) {
    while (1) {
        char c = tok_peek(t);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            tok_advance(t);
        } else if (c == ';') {
            while (tok_peek(t) && tok_peek(t) != '\n') {
                tok_advance(t);
            }
        } else {
            break;
        }
    }
}

static Token tok_make_token(TokenKind kind, char *lexeme, int line, int col) {
    Token tok;
    tok.kind = kind;
    tok.lexeme = lexeme;
    tok.line = line;
    tok.col = col;
    return tok;
}

static char *tok_extract_range(const char *src, size_t start, size_t end) {
    size_t len = end - start;
    char *s = malloc(len + 1);
    memcpy(s, src + start, len);
    s[len] = '\0';
    return s;
}

static Token tok_next(Tokenizer *t) {
    tok_skip_whitespace(t);

    if (tok_peek(t) == '\0') {
        return tok_make_token(TOK_EOF, NULL, t->line, t->col);
    }

    int start_line = t->line;
    int start_col = t->col;
    char c = tok_peek(t);

    if (c == '{') {
        tok_advance(t);
        return tok_make_token(TOK_LBRACE, NULL, start_line, start_col);
    }
    if (c == '}') {
        tok_advance(t);
        return tok_make_token(TOK_RBRACE, NULL, start_line, start_col);
    }
    if (c == '(') {
        tok_advance(t);
        return tok_make_token(TOK_LPAREN, NULL, start_line, start_col);
    }
    if (c == ')') {
        tok_advance(t);
        return tok_make_token(TOK_RPAREN, NULL, start_line, start_col);
    }
    if (c == ':') {
        tok_advance(t);
        return tok_make_token(TOK_COLON, NULL, start_line, start_col);
    }
    if (c == '.') {
        tok_advance(t);
        return tok_make_token(TOK_DOT, NULL, start_line, start_col);
    }
    if (c == '@') {
        tok_advance(t);
        return tok_make_token(TOK_AT, NULL, start_line, start_col);
    }
    if (c == '*') {
        tok_advance(t);
        return tok_make_token(TOK_STAR, NULL, start_line, start_col);
    }
    if (c == '=') {
        tok_advance(t);
        return tok_make_token(TOK_EQ, NULL, start_line, start_col);
    }

    if (c == '"') {
        tok_advance(t);
        size_t start = t->pos;
        while (tok_peek(t) && tok_peek(t) != '"') {
            if (tok_peek(t) == '\\')
                tok_advance(t);
            tok_advance(t);
        }
        size_t end = t->pos;
        if (tok_peek(t) == '"')
            tok_advance(t);
        char *lexeme = tok_extract_range(t->input, start, end);
        return tok_make_token(TOK_STR, lexeme, start_line, start_col);
    }

    if (isdigit(c) || (c == '-' && t->pos + 1 < t->len && isdigit(t->input[t->pos + 1]))) {
        size_t start = t->pos;
        if (c == '-')
            tok_advance(t);
        while (isdigit(tok_peek(t)))
            tok_advance(t);
        char *lexeme = tok_extract_range(t->input, start, t->pos);
        return tok_make_token(TOK_NUM, lexeme, start_line, start_col);
    }

    if (isalpha(c) || c == '_') {
        size_t start = t->pos;
        while (isalnum(tok_peek(t)) || tok_peek(t) == '_')
            tok_advance(t);
        char *lexeme = tok_extract_range(t->input, start, t->pos);
        return tok_make_token(TOK_IDENT, lexeme, start_line, start_col);
    }

    tok_advance(t);
    return tok_make_token(TOK_EOF, NULL, start_line, start_col);
}

static Token *tok_tokenize(const char *src, size_t *out_count) {
    Tokenizer t;
    tok_init(&t, src);

    Token *tokens = NULL;
    size_t count = 0;
    size_t capacity = 0;

    while (1) {
        Token tok = tok_next(&t);

        if (count >= capacity) {
            capacity = capacity == 0 ? 16 : capacity * 2;
            tokens = realloc(tokens, capacity * sizeof(Token));
        }

        tokens[count++] = tok;
        if (tok.kind == TOK_EOF)
            break;
    }

    *out_count = count;
    return tokens;
}

static Token parser_peek(ParserState *p) {
    if (p->pos >= p->count) {
        return (Token){TOK_EOF, NULL, 0, 0};
    }
    return p->tokens[p->pos];
}

static Token parser_advance(ParserState *p) {
    if (p->pos >= p->count) {
        return (Token){TOK_EOF, NULL, 0, 0};
    }
    return p->tokens[p->pos++];
}

static bool parser_check(ParserState *p, TokenKind kind) {
    return parser_peek(p).kind == kind;
}

static Token parser_expect(ParserState *p, TokenKind kind) {
    Token tok = parser_advance(p);
    if (tok.kind != kind) {
        fprintf(stderr, "Parse error at %d:%d: unexpected token\n", tok.line, tok.col);
        exit(1);
    }
    return tok;
}

static bool parser_match(ParserState *p, TokenKind kind) {
    if (parser_check(p, kind)) {
        parser_advance(p);
        return true;
    }
    return false;
}

static void inst_array_push(InstructionArray *arr, Instruction inst) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity == 0 ? 16 : arr->capacity * 2;
        arr->data = realloc(arr->data, arr->capacity * sizeof(Instruction));
    }
    arr->data[arr->count++] = inst;
}

static InstructionArray parse_func_body(ParserState *p) {
    InstructionArray instructions;
    instructions.data = NULL;
    instructions.count = 0;
    instructions.capacity = 0;

    while (!parser_check(p, TOK_RBRACE) && !parser_check(p, TOK_EOF)) {
        Token tok = parser_peek(p);

        if (tok.kind == TOK_DOT) {
            parser_advance(p);
            Token label_name = parser_expect(p, TOK_IDENT);
            parser_expect(p, TOK_COLON);
            label_table_add(p->current_labels, label_name.lexeme, instructions.count);
            continue;
        }

        if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "push") == 0) {
            parser_advance(p);
            Token arg = parser_advance(p);
            Instruction inst = {Push, WORD(0)};
            if (parser_check(p, TOK_AT)) {
                parser_advance(p);
                Token name = parser_expect(p, TOK_NUM);
                inst.type = PushRegister;
                inst.arg = WORD(atoi(name.lexeme));
            } else {
                if (arg.kind == TOK_NUM) {
                    inst.arg = WORD((uint64_t)atoll(arg.lexeme));
                } else if (arg.kind == TOK_STR) {
                    inst.arg = word_string(arg.lexeme);
                } else if (arg.kind == TOK_IDENT) {
                    inst.arg = word_string(arg.lexeme);
                }
            }

            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "pop") == 0) {
            parser_advance(p);
            Token arg = parser_advance(p);
            if (arg.kind == TOK_IDENT) {
                Instruction inst = {Pop, word_string(arg.lexeme)};
                inst_array_push(&instructions, inst);
            }
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "drop") == 0) {
            parser_advance(p);
            Instruction inst = {Drop, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && (strcmp(tok.lexeme, "halt") == 0 || strcmp(tok.lexeme, "ret") == 0)) {
            parser_advance(p);
            Instruction inst = {Halt, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "return") == 0) {
            parser_advance(p);
            Instruction inst = {Return, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "call") == 0) {
            parser_advance(p);
            Instruction inst;
            if (parser_check(p, TOK_AT)) {
                parser_advance(p);
                Token name = parser_expect(p, TOK_IDENT);
                inst.type = CallExtern;
                inst.arg = WORD(strdup(name.lexeme));
            } else {
                Token name = parser_expect(p, TOK_IDENT);
                inst.type = Call;
                inst.arg = WORD(strdup(name.lexeme));
            }
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "jmp") == 0) {
            parser_advance(p);
            Token target = parser_advance(p);
            Instruction inst = {Jmp, WORD(0)};
            if (target.kind == TOK_DOT) {
                Token label_name = parser_expect(p, TOK_IDENT);
                inst.arg = word_string(label_name.lexeme);
            } else if (target.kind == TOK_NUM) {
                inst.arg = WORD(atoi(target.lexeme));
            }
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "jmpif") == 0) {
            parser_advance(p);
            Token target = parser_advance(p);
            Instruction inst = {JmpIf, WORD(0)};
            if (target.kind == TOK_DOT) {
                Token label_name = parser_expect(p, TOK_IDENT);
                inst.arg = word_string(label_name.lexeme);
            } else if (target.kind == TOK_NUM) {
                inst.arg = WORD(atoi(target.lexeme));
            }
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "dup") == 0) {
            parser_advance(p);
            Instruction inst = {Dup, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "swap") == 0) {
            parser_advance(p);
            Instruction inst = {Swap, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "over") == 0) {
            parser_advance(p);
            Instruction inst = {Over, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "eq") == 0) {
            parser_advance(p);
            Instruction inst = {Eq, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "neq") == 0) {
            parser_advance(p);
            Instruction inst = {Neq, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "lt") == 0) {
            parser_advance(p);
            Instruction inst = {Lt, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "lte") == 0) {
            parser_advance(p);
            Instruction inst = {Lte, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "gt") == 0) {
            parser_advance(p);
            Instruction inst = {Gt, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else if (tok.kind == TOK_IDENT && strcmp(tok.lexeme, "gte") == 0) {
            parser_advance(p);
            Instruction inst = {Gte, WORD(NULL)};
            inst_array_push(&instructions, inst);
        } else {
            parser_advance(p);
        }
    }

    for (size_t i = 0; i < instructions.count; i++) {
        Instruction *inst = &instructions.data[i];
        if ((inst->type == Jmp || inst->type == JmpIf) && inst->arg.as_pointer != NULL) {
            char *label_name = (char *) inst->arg.as_pointer;
            if (isalpha(label_name[0]) || label_name[0] == '_') {
                size_t position;
                if (label_table_find(p->current_labels, label_name, &position)) {
                    free(label_name);
                    inst->arg = WORD(position);
                } else {
                    fprintf(stderr, "Error: Undefined label '%s'\n", label_name);
                    exit(1);
                }
            }
        }
    }

    return instructions;
}

static void parse_func_decl(ParserState *p) {
    Token name = parser_expect(p, TOK_IDENT);
    parser_expect(p, TOK_LBRACE);

    LabelTable labels;
    label_table_init(&labels);
    p->current_labels = &labels;

    InstructionArray instructions = parse_func_body(p);

    parser_expect(p, TOK_RBRACE);

    Function *fn = malloc(sizeof(Function));
    fn->instructions = instructions;

    char *name_copy = strdup(name.lexeme);
    xmap_set(&p->vm->functions, name_copy, WORD(fn));

    label_table_free(&labels);
    p->current_labels = NULL;
}

static void parse_toplevel(ParserState *p) {
    while (!parser_check(p, TOK_EOF)) {
        Token tok = parser_peek(p);
        if (tok.kind == TOK_IDENT) {
            parse_func_decl(p);
        } else {
            parser_advance(p);
        }
    }
}

static ProstStatus assemble(ProstVM *vm, const char *src) {
    size_t token_count;
    Token *tokens = tok_tokenize(src, &token_count);

    ParserState parser;
    parser.tokens = tokens;
    parser.pos = 0;
    parser.count = token_count;
    parser.vm = vm;
    parser.current_labels = NULL;

    parse_toplevel(&parser);

    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i].lexeme)
            free(tokens[i].lexeme);
    }
    free(tokens);

    return P_OK;
}

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

static void print_usage(const char *prog) {
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
}

int main(int argc, char **argv) {
    bool dont_run = false;
    bool dont_compile = false;
    bool verbose = false;
    char *output_file = "out.pco";
    char *input_file = NULL;
    XVec load_library = xvec_create(2);

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"output", required_argument, 0, 'o'},
        {"dont-run", no_argument, 0, 'r'},
        {"dont-compile", no_argument, 0, 'c'},
        {"verbose", no_argument, 0, 'v'},
        {"library", required_argument, 0, 'd'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "ho:rcvd:", long_options, &option_index)) != -1) {
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
        if (verbose)
            printf("Detected bytecode file (.pco), skipping compilation\n");
    }

    ProstVM *vm = p_init();
    if (!vm) {
        fprintf(stderr, "Error: Failed to initialize VM\n");
        return 1;
    }
    register_std(vm);

    for (int i = 0; i < xvec_len(&load_library); i++) {
        p_load_library(vm, (const char *) xvec_get(&load_library, i)->as_pointer);
    }
    xvec_free(&load_library);

    if (!dont_compile) {
        if (verbose)
            printf("Reading source file: %s\n", input_file);

        char *source = read_file(input_file);
        if (!source) {
            p_free(vm);
            return 1;
        }

        if (verbose)
            printf("Assembling...\n");

        ProstStatus status = assemble(vm, source);
        free(source);

        if (verbose)
            printf("Generating bytecode...\n");

        ByteBuf bytecode = p_to_bytecode(vm);

        if (verbose)
            printf("Writing bytecode to: %s\n", output_file);

        FILE *f = fopen(output_file, "wb");
        if (!f) {
            fprintf(stderr, "Error: Could not write to file '%s'\n", output_file);
            p_free(vm);
            return 1;
        }

        fwrite(bytecode.data, 1, bytecode.len, f);
        fclose(f);

        if (verbose)
            printf("Compilation successful (%zu bytes)\n", bytecode.len);
    }

    if (!dont_run) {
        const char *bytecode_file = dont_compile ? input_file : output_file;

        if (verbose)
            printf("Loading bytecode from: %s\n", bytecode_file);

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

        if (verbose)
            printf("Loading bytecode into VM...\n");

        ProstStatus status = p_from_bytecode(vm, bytecode);
        free(bytecode);

        if (status != P_OK) {
            fprintf(stderr, "Error: Failed to load bytecode (status %d)\n", status);
            p_free(vm);
            return 1;
        }

        if (verbose)
            printf("Running program...\n");

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
                printf("Top of stack: %ld\n", (long) top.as_pointer);
            }
            printf("Exit code: %d\n", vm->exit_code);
        }
    }

    unload_std();
    p_free(vm);
    return 0;
}
