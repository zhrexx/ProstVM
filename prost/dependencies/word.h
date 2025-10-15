#ifndef XWORD_H
#define XWORD_H

#include <stdint.h>
#include <string.h>

typedef enum {
    WINT,
    WFLOAT,
    WPOINTER,
    WCHAR_,
} WordType;

typedef enum {
    WF_NONE        = 0,
    WF_IS_STRING   = 1 << 0,
    WF_IS_UNSIGNED = 1 << 1,
    WF_OWNS_MEMORY = 1 << 2,
} WordFlags;

typedef struct {
    WordType type;
    uint8_t flags;
    union {
        void *as_pointer;
        int64_t as_int;
        double as_float;
        char as_char;
    };
} Word;

static inline Word word_int(int64_t value) {
    return (Word){ .type = WINT, .as_int = value, .flags = 0 };
}

static inline Word word_uint(uint64_t value) {
    return (Word){ .type = WINT, .as_int = (int64_t)value, .flags = WF_IS_UNSIGNED };
}

static inline Word word_float(double value) {
    return (Word){ .type = WFLOAT, .as_float = value };
}

static inline Word word_pointer(void *ptr, bool owns_memory) {
    return (Word){ .type = WPOINTER, .as_pointer = ptr, .flags = owns_memory ? WF_OWNS_MEMORY : 0 };
}

// this is used for WORD
static inline Word word_pointer_(void *ptr) {
    return (Word){ .type = WPOINTER, .as_pointer = ptr, .flags = 0 };
}

static inline Word word_char(char c) {
    return (Word){ .type = WCHAR_, .as_char = c };
}

static inline Word word_string(const char *s) {
    return (Word){
        .type = WPOINTER,
        .as_pointer = strdup(s),
        .flags = WF_IS_STRING | WF_OWNS_MEMORY
    };
}


#define WORD(val) _Generic((val), \
int: word_int, \
long: word_int, \
long long: word_int, \
unsigned int: word_uint, \
unsigned long: word_uint, \
unsigned long long: word_uint, \
float: word_float, \
double: word_float, \
char: word_char, \
char*: word_string, \
const char*: word_string, \
void*: word_pointer_, \
default: word_pointer_ \
)(val)


const char *word_type_to_str(WordType t) {
    switch (t) {
        case WINT: return "WINT";
        case WFLOAT: return "WFLOAT";
        case WPOINTER: return "WPOINTER";
        case WCHAR_: return "WCHAR_";
        default: return "UNKNOWN";
    }
}

static inline bool word_is_string(const Word *w) {
    return (w->flags & WF_IS_STRING) != 0;
}

static inline bool word_is_unsigned(const Word *w) {
    return (w->flags & WF_IS_UNSIGNED) != 0;
}

static inline bool word_owns_memory(const Word *w) {
    return (w->flags & WF_OWNS_MEMORY) != 0;
}

static inline bool word_has_flag(const Word *w, WordFlags flag) {
    return (w->flags & flag) != 0;
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

#endif //XWORD_H