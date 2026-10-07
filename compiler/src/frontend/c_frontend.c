#include <stdio.h>

#include "optifine/frontend/c_frontend.h"
#include "stb_c_lexer.h"
#include "stb_ds.h"

void frontend_c_init_state(CompilerState* s, char* input_buf, int input_buf_len) {
    arrfree(s->da_variables);
    stb_c_lexer_init(&s->lex, input_buf, input_buf+input_buf_len, (char *) malloc(0x10000), 0x10000);
}

bool frontend_c_expect_next_token(stb_lexer lex, int token_type) {
    stb_c_lexer_get_token(&lex);
    return lex.token == token_type;
}

void frontend_c_error_and_exit(stb_lexer lex) {
    stb_lex_location loc;
    stb_c_lexer_get_location(&lex, lex.parse_point, &loc);
    printf("\nError at %d:%d\n", loc.line_number, loc.line_offset);
}

void frontend_c_parse_args(CompilerState* s) {
    return;
}

void frontend_c_parse_block(CompilerState* s) {
    while (!frontend_c_expect_next_token(s->lex, '}')) {
        stb_c_lexer_get_token(&s->lex);
        stb_c_lexer_print_token(&s->lex);
    }
}

void frontend_c_compile_file(CompilerState* s) {

    if (!frontend_c_expect_next_token(s->lex, CLEX_id)) frontend_c_error_and_exit(s->lex);
    stb_c_lexer_get_token(&s->lex);
    char* type = strdup(s->lex.string);

    if (!frontend_c_expect_next_token(s->lex, CLEX_id)) frontend_c_error_and_exit(s->lex);
    stb_c_lexer_get_token(&s->lex);
    char* identifier = strdup(s->lex.string);

    if (!frontend_c_expect_next_token(s->lex, '(')) {
        printf("Global var with type %s and name %s\nParsing is yet not implemented.", type, identifier);
        return;
    }
    stb_c_lexer_get_token(&s->lex);

    frontend_c_parse_args(s);

    if (!frontend_c_expect_next_token(s->lex, ')')) frontend_c_error_and_exit(s->lex);
    stb_c_lexer_get_token(&s->lex);

    if (!frontend_c_expect_next_token(s->lex, '{')) {
        printf("Function declaration with type %s and name %s\n", type, identifier);
    }
    stb_c_lexer_get_token(&s->lex);

    frontend_c_parse_block(s);

    if (!frontend_c_expect_next_token(s->lex, '}')) frontend_c_error_and_exit(s->lex);
    stb_c_lexer_get_token(&s->lex);

    printf("Function definition with type %s and name %s\n", type, identifier);

    free(type);
    free(identifier);
    return;
}
