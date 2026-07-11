#include <assert.h>
#include <stdint.h>
#include "tree_sitter/parser.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VEC_RESIZE(vec, _cap)                                                  \
    void *tmp = realloc((vec)->data, (_cap) * sizeof((vec)->data[0]));         \
    assert(tmp != NULL);                                                       \
    (vec)->data = tmp;                                                         \
    (vec)->cap = (_cap);

#define VEC_PUSH(vec, el)                                                      \
    if ((vec)->cap == (vec)->len) {                                            \
        VEC_RESIZE((vec), MAX(16, (vec)->len * 2));                            \
    }                                                                          \
    (vec)->data[(vec)->len++] = (el);

#define VEC_POP(vec) (vec)->len--;

#define VEC_BACK(vec) ((vec)->data[(vec)->len - 1])

#define VEC_FREE(vec)                                                          \
    {                                                                          \
        if ((vec)->data != NULL)                                               \
            free((vec)->data);                                                 \
    }

#define VEC_CLEAR(vec)                                                         \
    { (vec)->len = 0; }

enum TokenType {
    NEWLINE,
    INDENT,
    DEDENT,
    STRING_START,
    STRING_CONTENT,
    STRING_END,
    STRING_NAME_START,
    NODE_PATH_START,
    CLOSE_PAREN,
    CLOSE_BRACKET,
    CLOSE_BRACE,
    COMMA,
    BODY_END,
};

enum {
    SingleQuote = 1 << 0,
    DoubleQuote = 1 << 1,
    Triple = 1 << 2,
    Raw = 1 << 3,
};

typedef struct {
    char flags;
} StringDelimiter;

static inline StringDelimiter new_string_delimiter() { return (StringDelimiter){0}; }

static inline bool is_multiline_string_delimiter(StringDelimiter *delimiter) {
    return delimiter->flags & Triple;
}

static inline bool is_raw_string_delimiter(StringDelimiter *delimiter) {
    return delimiter->flags & Raw;
}

static inline int32_t get_expected_string_end_char(StringDelimiter *delimiter) {
    if (delimiter->flags & SingleQuote) {
        return '\'';
    }
    if (delimiter->flags & DoubleQuote) {
        return '"';
    }
    return 0;
}

static inline void set_triple(StringDelimiter *delimiter) {
    delimiter->flags |= Triple;
}

static inline void set_raw(StringDelimiter *delimiter) {
    delimiter->flags |= Raw;
}

static inline void set_end_character(StringDelimiter *delimiter, int32_t character) {
    switch (character) {
        case '\'':
            delimiter->flags |= SingleQuote;
            break;
        case '"':
            delimiter->flags |= DoubleQuote;
            break;
        default:
            assert(false);
    }
}

typedef struct {
    uint32_t len;
    uint32_t cap;
    uint16_t *data;
} indent_vec;

typedef struct {
    indent_vec indents;
    StringDelimiter delimiter;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

/*
 * Skips one whitespace character and update the current line's indentation
 * accordingly.
 */
static inline bool skip_whitespace(TSLexer *lexer, uint32_t *indent_length,
                                   bool *found_end_of_line) {
    if (lexer->lookahead == '\n') {
        *found_end_of_line = true;
        *indent_length = 0;
        skip(lexer);
        return true;
    } else if (lexer->lookahead == ' ') {
        (*indent_length)++;
        skip(lexer);
        return true;
    } else if (lexer->lookahead == '\r' || lexer->lookahead == '\f') {
        *indent_length = 0;
        skip(lexer);
        return true;
    } else if (lexer->lookahead == '\t') {
        *indent_length += 8;
        skip(lexer);
        return true;
    }

    return false;
}

static inline void handle_quote(TSLexer *lexer, StringDelimiter *delimiter, char quote) {
    set_end_character(delimiter, quote);
    advance(lexer);
    lexer->mark_end(lexer);

    if (lexer->lookahead == quote) {
        advance(lexer);
        if (lexer->lookahead == quote) {
            advance(lexer);
            lexer->mark_end(lexer);
            set_triple(delimiter);
        }
    }
}

bool tree_sitter_gdscript_external_scanner_scan(void *payload, TSLexer *lexer,
                                                const bool *valid_symbols) {
    Scanner *scanner = (Scanner *)payload;

    bool error_recovery_mode =
        valid_symbols[STRING_CONTENT] && valid_symbols[INDENT];

    if (valid_symbols[STRING_CONTENT] && scanner->delimiter.flags != 0 &&
        !error_recovery_mode) {
        StringDelimiter delimiter = scanner->delimiter;
        int32_t expected_string_end_delimiter_char = get_expected_string_end_char(&delimiter);
        bool has_content = false;
        while (lexer->lookahead) {
            if (lexer->lookahead == '\\') {
                if (is_raw_string_delimiter(&delimiter)) {
                    // Step over the backslash.
                    lexer->advance(lexer, false);
                    // Step over any escaped quotes.
                    if (lexer->lookahead == get_expected_string_end_char(&delimiter) ||
                        lexer->lookahead == '\\') {
                        lexer->advance(lexer, false);
                    }
                    continue;
                } else {
                    lexer->mark_end(lexer);
                    lexer->result_symbol = STRING_CONTENT;
                    return has_content;
                }
            } else if (lexer->lookahead == expected_string_end_delimiter_char) {
                if (is_multiline_string_delimiter(&delimiter)) {
                    // We're in a multiline string, the delimiter is ''' or """
                    // so we need to scan all 3.
                    lexer->mark_end(lexer);
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == expected_string_end_delimiter_char) {
                        lexer->advance(lexer, false);
                        if (lexer->lookahead == expected_string_end_delimiter_char) {
                            if (has_content) {
                                lexer->result_symbol = STRING_CONTENT;
                            } else {
                                lexer->advance(lexer, false);
                                lexer->mark_end(lexer);
                                scanner->delimiter = new_string_delimiter();
                                lexer->result_symbol = STRING_END;
                            }
                            return true;
                        }
                        lexer->mark_end(lexer);
                        lexer->result_symbol = STRING_CONTENT;
                        return true;
                    }
                    lexer->mark_end(lexer);
                    lexer->result_symbol = STRING_CONTENT;
                    return true;
                }
                if (has_content) {
                    lexer->result_symbol = STRING_CONTENT;
                } else {
                    lexer->advance(lexer, false);
                    scanner->delimiter = new_string_delimiter();
                    lexer->result_symbol = STRING_END;
                }
                lexer->mark_end(lexer);
                return true;
            }
            advance(lexer);
            has_content = true;
        }
    }

    lexer->mark_end(lexer);
    bool found_end_of_line = false;
    uint32_t indent_length = 0;
    int32_t first_comment_indent_length = -1;

    for (;;) {
        if (skip_whitespace(lexer, &indent_length, &found_end_of_line)) {
            continue;
        } else if (lexer->lookahead == '#' &&
                   (valid_symbols[INDENT] || valid_symbols[DEDENT] ||
                    valid_symbols[NEWLINE])) {
            // A comment following code shouldn't produce an indent or dedent.
            // We ignore comment indentation and return so the parser can
            // continue.
            //
            // In an older version, we accounted for comment indentation and it
            // could end a function or if statement body prematurely, making the
            // resulting AST semantically incorrect.
            if (!found_end_of_line) {
                return false;
            }

            if (first_comment_indent_length == -1) {
                first_comment_indent_length = (int32_t)indent_length;
            }

            while (lexer->lookahead && lexer->lookahead != '\n') {
                skip(lexer);
            }
            if (lexer->lookahead == '\n') {
                skip(lexer);
            }
            indent_length = 0;
        } else if (lexer->lookahead == '\\') {
            skip(lexer);
            if (lexer->lookahead == '\r') {
                skip(lexer);
            }
            if (lexer->lookahead == '\n' || lexer->eof(lexer)) {
                skip(lexer);
            } else {
                return false;
            }
        } else if (lexer->eof(lexer)) {
            indent_length = 0;
            found_end_of_line = true;
            break;
        } else {
            break;
        }
    }

    if (found_end_of_line) {
        if (scanner->indents.len > 0) {
            uint16_t current_indent_length = VEC_BACK(&scanner->indents);

            if (valid_symbols[INDENT] &&
                indent_length > current_indent_length) {
                VEC_PUSH(&scanner->indents, indent_length);
                lexer->result_symbol = INDENT;
                return true;
            }

            if (valid_symbols[DEDENT] &&
                indent_length < current_indent_length &&
                first_comment_indent_length < (int32_t)current_indent_length) {
                VEC_POP(&scanner->indents);
                lexer->result_symbol = DEDENT;
                return true;
            }
        }

        if (valid_symbols[NEWLINE] && !error_recovery_mode) {
            lexer->result_symbol = NEWLINE;
            return true;
        }
    }

    // This if statement can be placed before the above if statement that
    // handles newlines. However, it feels safer to give indentation and
    // newlines higher precedence.
    if (
        // Guard against BODY_END tokens overriding valid tokens.
        !valid_symbols[COMMA] &&
        !valid_symbols[CLOSE_PAREN] && !valid_symbols[CLOSE_BRACE] &&
        !valid_symbols[CLOSE_BRACKET] &&

        // Body ends occur in error recovery mode since the grammar does not
        // (cannot?) specify that a body can end with the below characters
        // without consuming them itself.
        (error_recovery_mode || valid_symbols[BODY_END])) {
        if (lexer->lookahead == ',' || // separator
            lexer->lookahead == ')' || // args, params, paren expr
            lexer->lookahead == '}' || // dictionary (may not be needed)
            lexer->lookahead == ']'    // array
        ) {
            // BODY_END tokens can take the place of a dedent. Therefore, we
            // should pop the stack when DEDENT is valid.
            if (valid_symbols[DEDENT] && scanner->indents.len > 0) {
                VEC_POP(&scanner->indents);
            }
            lexer->result_symbol = BODY_END;
            return true;
        }
    }

    if (first_comment_indent_length == -1 &&
        (valid_symbols[STRING_START] ||
         valid_symbols[STRING_NAME_START] ||
         valid_symbols[NODE_PATH_START])) {
        StringDelimiter delimiter = new_string_delimiter();
        enum TokenType start_symbol = STRING_START;
        bool has_prefix = true;

        switch (lexer->lookahead) {
            case 'r': set_raw(&delimiter); break;
            case '&': start_symbol = STRING_NAME_START; break;
            case '^': start_symbol = NODE_PATH_START; break;

            // For backward compatibility with 3.x versions
            case '@': start_symbol = NODE_PATH_START; break;
            default: has_prefix = false; break;
        }

        if (has_prefix) advance(lexer);

        if (lexer->lookahead == '\'' || lexer->lookahead == '"') {
            handle_quote(lexer, &delimiter, lexer->lookahead);
        }

        if (get_expected_string_end_char(&delimiter)) {
            scanner->delimiter = delimiter;
            lexer->result_symbol = start_symbol;
            return true;
        }

        if (has_prefix) {
            return false;
        }
    }

    return false;
}

unsigned tree_sitter_gdscript_external_scanner_serialize(void *payload,
                                                         char *buffer) {
    Scanner *scanner = (Scanner *)payload;

    size_t size = 0;

    buffer[size++] = scanner->delimiter.flags;

    for (uint32_t iter = 1; iter < scanner->indents.len &&
                            size + 1 < TREE_SITTER_SERIALIZATION_BUFFER_SIZE;
         ++iter) {
        uint16_t indent = scanner->indents.data[iter];
        buffer[size++] = (char)(indent & 0xff);
        buffer[size++] = (char)((indent >> 8) & 0xff);
    }

    return size;
}

void tree_sitter_gdscript_external_scanner_deserialize(void *payload,
                                                       const char *buffer,
                                                       unsigned length) {
    Scanner *scanner = (Scanner *)payload;

    VEC_CLEAR(&scanner->indents);
    VEC_PUSH(&scanner->indents, 0);
    scanner->delimiter = new_string_delimiter();

    if (length > 0) {
        size_t size = 0;

        scanner->delimiter.flags = buffer[size++];

        // Deserialize the indents
        for (; size + 1 < length; size += 2) {
            uint16_t indent = (unsigned char)buffer[size] |
                              ((unsigned char)buffer[size + 1] << 8);
            VEC_PUSH(&scanner->indents, indent);
        }
    }
}

void *tree_sitter_gdscript_external_scanner_create() {
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    _Static_assert(sizeof(StringDelimiter) == sizeof(char), "");
#else
    assert(sizeof(Delimiter) == sizeof(char));
#endif
    Scanner *scanner = calloc(1, sizeof(Scanner));
    if (!scanner) {
        return NULL;
    }

    tree_sitter_gdscript_external_scanner_deserialize(scanner, NULL, 0);
    return scanner;
}

void tree_sitter_gdscript_external_scanner_destroy(void *payload) {
    Scanner *scanner = (Scanner *)payload;
    VEC_FREE(&scanner->indents);
    free(scanner);
}
