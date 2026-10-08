#include <stdio.h>
#include <string.h>

#include "optifine/frontend/c_frontend.h"
#include "stb_c_lexer.h"
#include "stb_ds.h"

void frontend_c_init_state(CompilerState* s, char* input_buf, int input_buf_len) {
    arrfree(s->da_variables);
    stb_c_lexer_init(&s->lex, input_buf, input_buf+input_buf_len, (char *) malloc(0x10000), 0x10000);
}

// Peeking functions that take 'stb_lexer lex' do not modify the state as 'lex' is copied

bool frontend_c_expect_next_token(stb_lexer lex, int token_type) {
    stb_c_lexer_get_token(&lex);
    return lex.token == token_type;
}

int frontend_c_peek_next_token(stb_lexer lex) {
    stb_c_lexer_get_token(&lex);
    return lex.token;
}

void frontend_c_error_and_exit(stb_lexer lex) {
    stb_lex_location loc;
    stb_c_lexer_get_location(&lex, lex.parse_point, &loc);
    printf("\nError at %d:%d\n", loc.line_number, loc.line_offset);
}

void frontend_c_parse_args(CompilerState *s) { return; }

void frontend_c_expect_and_get_next_token(CompilerState *s, int token_type) {
    if (!frontend_c_expect_next_token(s->lex, token_type)) frontend_c_error_and_exit(s->lex);
    stb_c_lexer_get_token(&s->lex);
    printf("Got token: ");
    stb_c_lexer_print_token(&s->lex);
    printf("\n");
}

void frontend_c_parse_expr(CompilerState *s) {
    frontend_c_expect_and_get_next_token(s, CLEX_intlit);
    int expr_res = s->lex.int_number;
    printf("Parsed expression: %d\n", expr_res);
}

void frontend_c_parse_statement(CompilerState *s) {
    int tok = frontend_c_peek_next_token(s->lex);
    switch (tok) {
    case CLEX_id: {
        char *type = s->lex.string;
        if (strcmp(type, "return") == 0) {
            stb_c_lexer_get_token(&s->lex);
            frontend_c_parse_expr(s);
            frontend_c_expect_and_get_next_token(s, ';');
            printf("Found return pog\n");
        }
    } break;
    }
    return;
}

void frontend_c_parse_block(CompilerState* s) {
    while (!frontend_c_expect_next_token(s->lex, '}')) {
        frontend_c_parse_statement(s);
    }
}

void frontend_c_compile_file(CompilerState* s) {

    frontend_c_expect_and_get_next_token(s, CLEX_id);
    char* type = strdup(s->lex.string);

    frontend_c_expect_and_get_next_token(s, CLEX_id);
    char* identifier = strdup(s->lex.string);

    if (!frontend_c_expect_next_token(s->lex, '(')) {
        printf("Global var with type %s and name %s\nParsing is yet not implemented.", type, identifier);
        return;
    }
    stb_c_lexer_get_token(&s->lex);
    frontend_c_parse_args(s);
    frontend_c_expect_and_get_next_token(s, ')');

    if (!frontend_c_expect_next_token(s->lex, '{')) {
        printf("Function declaration with type %s and name %s\n", type, identifier);
        return;
    }
    stb_c_lexer_get_token(&s->lex);
    frontend_c_parse_block(s);
    frontend_c_expect_and_get_next_token(s, '}');

    printf("Function definition with type '%s' and name '%s'\n", type, identifier);

    free(type);
    free(identifier);
    return;
}
