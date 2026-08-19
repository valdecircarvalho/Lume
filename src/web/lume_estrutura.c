/* "Veja por dentro": expoe ao navegador as duas primeiras etapas do
 * interpretador — lexer e parser.
 *
 * Os rotulos saem em portugues de proposito. O aluno esta aprendendo numa
 * linguagem em portugues; ver "declaracao de variavel" ensina, ver
 * "STMT_VARIABLE_DECLARATION" so traduz um problema em outro. Os nomes tecnicos
 * ficam disponiveis ao lado, para quem quiser cruzar com o codigo-fonte.
 */
#include "web/lume_estrutura.h"
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "memory.h"
#include "source.h"
#include "token.h"
#include "web/lume_json.h"

static void escrever_expressao(JsonBuffer *b, const Expr *expressao);
static void escrever_comando(JsonBuffer *b, const Stmt *comando);

/* ---------- tokens ---------- */

/* Uma familia por token, para o painel poder colorir sem repetir a lista de
   tipos que ja existe em token.h. */
static const char *familia_do_token(TokenType tipo) {
    switch (tipo) {
        case TOKEN_KW_VARIAVEL: case TOKEN_KW_CONSTANTE: case TOKEN_KW_SE:
        case TOKEN_KW_SENAO: case TOKEN_KW_ENQUANTO: case TOKEN_KW_PARA:
        case TOKEN_KW_DE: case TOKEN_KW_ATE: case TOKEN_KW_FUNCAO:
        case TOKEN_KW_RETORNE: case TOKEN_KW_VERDADEIRO: case TOKEN_KW_FALSO:
        case TOKEN_KW_NULO: case TOKEN_KW_E: case TOKEN_KW_OU: case TOKEN_KW_NAO:
        case TOKEN_KW_IMPORTE: case TOKEN_KW_EXPORTE:
            return "palavra-chave";
        case TOKEN_IDENTIFIER: return "nome";
        case TOKEN_INTEGER: case TOKEN_DECIMAL: return "numero";
        case TOKEN_STRING: return "texto";
        case TOKEN_NEWLINE: return "quebra";
        case TOKEN_EOF: return "fim";
        case TOKEN_LEFT_PAREN: case TOKEN_RIGHT_PAREN: case TOKEN_LEFT_BRACE:
        case TOKEN_RIGHT_BRACE: case TOKEN_LEFT_BRACKET: case TOKEN_RIGHT_BRACKET:
        case TOKEN_COMMA: case TOKEN_DOT: case TOKEN_COLON: case TOKEN_SEMICOLON:
            return "pontuacao";
        case TOKEN_PLUS: case TOKEN_MINUS: case TOKEN_STAR: case TOKEN_SLASH:
        case TOKEN_PERCENT: case TOKEN_EQUAL: case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL: case TOKEN_GREATER: case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS: case TOKEN_LESS_EQUAL:
            return "operador";
    }
    return "outro";
}

static void escrever_tokens(JsonBuffer *b, const TokenArray *tokens) {
    size_t indice;
    json_texto(b, "[");
    for (indice = 0U; indice < tokens->count; indice++) {
        const Token *token = &tokens->data[indice];
        if (indice > 0U) json_texto(b, ",");
        json_texto(b, "{\"tipo\":");
        json_string(b, token_type_name(token->type), strlen(token_type_name(token->type)));
        json_texto(b, ",\"familia\":");
        json_string(b, familia_do_token(token->type), strlen(familia_do_token(token->type)));
        json_texto(b, ",\"texto\":");
        /* A quebra de linha nao tem lexema util para mostrar numa tabela. */
        if (token->type == TOKEN_NEWLINE) json_texto(b, "\"\\u21b5\"");
        else if (token->type == TOKEN_EOF) json_texto(b, "\"\"");
        else json_string(b, token_lexeme(token), token_length(token));
        json_texto(b, ",\"l\":"); json_numero(b, token->span.start.line);
        json_texto(b, ",\"c\":"); json_numero(b, token->span.start.column);
        json_texto(b, "}");
    }
    json_texto(b, "]");
}

/* ---------- arvore ---------- */

static void abrir_no(JsonBuffer *b, const char *rotulo, const char *tecnico, SourceSpan span) {
    json_texto(b, "{\"no\":");
    json_string(b, rotulo, strlen(rotulo));
    json_texto(b, ",\"tecnico\":");
    json_string(b, tecnico, strlen(tecnico));
    json_texto(b, ",\"l\":"); json_numero(b, span.start.line);
    json_texto(b, ",\"c\":"); json_numero(b, span.start.column);
}
static void detalhe(JsonBuffer *b, const char *texto, size_t comprimento) {
    json_texto(b, ",\"detalhe\":");
    json_string(b, texto, comprimento);
}
static void abrir_filhos(JsonBuffer *b) { json_texto(b, ",\"filhos\":["); }
static void fechar_no(JsonBuffer *b, bool tinha_filhos) {
    json_texto(b, tinha_filhos ? "]}" : "}");
}

static const char *nome_do_unario(UnaryOperator operador) {
    switch (operador) {
        case UNARY_POSITIVE: return "+";
        case UNARY_NEGATIVE: return "-";
        case UNARY_NOT:      return "nao";
    }
    return "?";
}
static const char *nome_do_binario(BinaryOperator operador) {
    switch (operador) {
        case BINARY_ADD: return "+";              case BINARY_SUBTRACT: return "-";
        case BINARY_MULTIPLY: return "*";         case BINARY_DIVIDE: return "/";
        case BINARY_REMAINDER: return "%";        case BINARY_EQUAL: return "==";
        case BINARY_NOT_EQUAL: return "!=";       case BINARY_LESS: return "<";
        case BINARY_LESS_EQUAL: return "<=";      case BINARY_GREATER: return ">";
        case BINARY_GREATER_EQUAL: return ">=";   case BINARY_LOGICAL_AND: return "e";
        case BINARY_LOGICAL_OR: return "ou";
    }
    return "?";
}

static void escrever_expressao(JsonBuffer *b, const Expr *e) {
    size_t indice;
    if (e == NULL) { json_texto(b, "null"); return; }
    switch (e->type) {
        case EXPR_LITERAL: {
            char *formatado = NULL; size_t tamanho = 0U;
            abrir_no(b, "valor fixo", "EXPR_LITERAL", e->span);
            if (value_format(&e->as.literal, &formatado, &tamanho) && formatado != NULL) {
                detalhe(b, formatado, tamanho);
            }
            memory_free(formatado);
            fechar_no(b, false);
            break;
        }
        case EXPR_IDENTIFIER:
            abrir_no(b, "nome", "EXPR_IDENTIFIER", e->span);
            detalhe(b, e->as.identifier.name, e->as.identifier.length);
            fechar_no(b, false);
            break;
        case EXPR_UNARY:
            abrir_no(b, "operacao de um lado", "EXPR_UNARY", e->span);
            detalhe(b, nome_do_unario(e->as.unary.operator_type),
                    strlen(nome_do_unario(e->as.unary.operator_type)));
            abrir_filhos(b);
            escrever_expressao(b, e->as.unary.operand);
            fechar_no(b, true);
            break;
        case EXPR_BINARY:
            abrir_no(b, "operacao", "EXPR_BINARY", e->span);
            detalhe(b, nome_do_binario(e->as.binary.operator_type),
                    strlen(nome_do_binario(e->as.binary.operator_type)));
            abrir_filhos(b);
            escrever_expressao(b, e->as.binary.left);
            json_texto(b, ",");
            escrever_expressao(b, e->as.binary.right);
            fechar_no(b, true);
            break;
        case EXPR_GROUPING:
            abrir_no(b, "agrupamento", "EXPR_GROUPING", e->span);
            abrir_filhos(b);
            escrever_expressao(b, e->as.grouping.expression);
            fechar_no(b, true);
            break;
        case EXPR_CALL:
            abrir_no(b, "chamada", "EXPR_CALL", e->span);
            abrir_filhos(b);
            escrever_expressao(b, e->as.call.callee);
            for (indice = 0U; indice < e->as.call.argument_count; indice++) {
                json_texto(b, ",");
                escrever_expressao(b, e->as.call.arguments[indice]);
            }
            fechar_no(b, true);
            break;
        case EXPR_LIST:
            abrir_no(b, "lista", "EXPR_LIST", e->span);
            abrir_filhos(b);
            for (indice = 0U; indice < e->as.list.count; indice++) {
                if (indice > 0U) json_texto(b, ",");
                escrever_expressao(b, e->as.list.elements[indice]);
            }
            fechar_no(b, true);
            break;
        case EXPR_INDEX:
            abrir_no(b, "posicao da lista", "EXPR_INDEX", e->span);
            abrir_filhos(b);
            escrever_expressao(b, e->as.index.target);
            json_texto(b, ",");
            escrever_expressao(b, e->as.index.index);
            fechar_no(b, true);
            break;
        case EXPR_MEMBER:
            abrir_no(b, "membro do modulo", "EXPR_MEMBER", e->span);
            detalhe(b, e->as.member.name, e->as.member.name_length);
            abrir_filhos(b);
            escrever_expressao(b, e->as.member.target);
            fechar_no(b, true);
            break;
    }
}

static void escrever_bloco(JsonBuffer *b, const StmtArray *comandos) {
    size_t indice;
    for (indice = 0U; indice < comandos->count; indice++) {
        if (indice > 0U) json_texto(b, ",");
        escrever_comando(b, comandos->data[indice]);
    }
}

static void escrever_comando(JsonBuffer *b, const Stmt *c) {
    size_t indice;
    if (c == NULL) { json_texto(b, "null"); return; }
    switch (c->type) {
        case STMT_EXPRESSION:
            abrir_no(b, "expressao solta", "STMT_EXPRESSION", c->span);
            abrir_filhos(b);
            escrever_expressao(b, c->as.expression.expression);
            fechar_no(b, true);
            break;
        case STMT_VARIABLE_DECLARATION:
        case STMT_CONSTANT_DECLARATION:
            abrir_no(b, c->type == STMT_VARIABLE_DECLARATION
                        ? "declaracao de variavel" : "declaracao de constante",
                     c->type == STMT_VARIABLE_DECLARATION
                        ? "STMT_VARIABLE_DECLARATION" : "STMT_CONSTANT_DECLARATION", c->span);
            detalhe(b, c->as.declaration.name, c->as.declaration.name_length);
            abrir_filhos(b);
            escrever_expressao(b, c->as.declaration.initializer);
            fechar_no(b, true);
            break;
        case STMT_ASSIGNMENT:
            abrir_no(b, "atribuicao", "STMT_ASSIGNMENT", c->span);
            detalhe(b, c->as.assignment.name, c->as.assignment.name_length);
            abrir_filhos(b);
            escrever_expressao(b, c->as.assignment.value);
            fechar_no(b, true);
            break;
        case STMT_BLOCK:
            abrir_no(b, "bloco", "STMT_BLOCK", c->span);
            abrir_filhos(b);
            escrever_bloco(b, &c->as.block.statements);
            fechar_no(b, true);
            break;
        case STMT_IF:
            abrir_no(b, "se", "STMT_IF", c->span);
            abrir_filhos(b);
            escrever_expressao(b, c->as.if_statement.condition);
            json_texto(b, ",");
            escrever_comando(b, c->as.if_statement.then_branch);
            if (c->as.if_statement.else_branch != NULL) {
                json_texto(b, ",");
                escrever_comando(b, c->as.if_statement.else_branch);
            }
            fechar_no(b, true);
            break;
        case STMT_WHILE:
            abrir_no(b, "enquanto", "STMT_WHILE", c->span);
            abrir_filhos(b);
            escrever_expressao(b, c->as.while_statement.condition);
            json_texto(b, ",");
            escrever_comando(b, c->as.while_statement.body);
            fechar_no(b, true);
            break;
        case STMT_FOR:
            abrir_no(b, "para", "STMT_FOR", c->span);
            detalhe(b, c->as.for_statement.iterator_name, c->as.for_statement.iterator_length);
            abrir_filhos(b);
            escrever_expressao(b, c->as.for_statement.start);
            json_texto(b, ",");
            escrever_expressao(b, c->as.for_statement.end);
            json_texto(b, ",");
            escrever_comando(b, c->as.for_statement.body);
            fechar_no(b, true);
            break;
        case STMT_FUNCTION: {
            abrir_no(b, "funcao", "STMT_FUNCTION", c->span);
            detalhe(b, c->as.function.name, c->as.function.name_length);
            json_texto(b, ",\"parametros\":[");
            for (indice = 0U; indice < c->as.function.parameter_count; indice++) {
                if (indice > 0U) json_texto(b, ",");
                json_string(b, c->as.function.parameters[indice],
                            c->as.function.parameter_lengths[indice]);
            }
            json_texto(b, "]");
            abrir_filhos(b);
            escrever_comando(b, c->as.function.body);
            fechar_no(b, true);
            break;
        }
        case STMT_RETURN:
            abrir_no(b, "retorne", "STMT_RETURN", c->span);
            abrir_filhos(b);
            escrever_expressao(b, c->as.return_statement.value);
            fechar_no(b, true);
            break;
        case STMT_INDEX_ASSIGNMENT:
            abrir_no(b, "atribuicao em posicao", "STMT_INDEX_ASSIGNMENT", c->span);
            abrir_filhos(b);
            escrever_expressao(b, c->as.index_assignment.target);
            json_texto(b, ",");
            escrever_expressao(b, c->as.index_assignment.index);
            json_texto(b, ",");
            escrever_expressao(b, c->as.index_assignment.value);
            fechar_no(b, true);
            break;
        case STMT_IMPORT:
            abrir_no(b, "importe", "STMT_IMPORT", c->span);
            detalhe(b, c->as.import.path, c->as.import.path_length);
            fechar_no(b, false);
            break;
    }
}

char *lume_estrutura_json(const char *codigo) {
    Source fonte; TokenArray tokens; ErrorList erros; Program *programa = NULL;
    JsonBuffer b; bool ok;
    if (codigo == NULL) codigo = "";
    source_init(&fonte); token_array_init(&tokens); error_list_init(&erros);
    json_buffer_init(&b);

    ok = source_from_bytes(&fonte, "principal.lume", codigo, strlen(codigo));
    if (ok) ok = lexer_scan(&fonte, &tokens, &erros);

    json_texto(&b, "{\"tokens\":");
    escrever_tokens(&b, &tokens);

    json_texto(&b, ",\"arvore\":");
    if (ok) ok = parser_parse_program(&tokens, &programa, &erros);
    if (ok && programa != NULL) {
        json_texto(&b, "{\"no\":\"programa\",\"tecnico\":\"Program\",\"l\":1,\"c\":1,\"filhos\":[");
        escrever_bloco(&b, &programa->statements);
        json_texto(&b, "]}");
    } else {
        json_texto(&b, "null");
    }

    json_texto(&b, ",\"erro\":");
    if (!ok && erros.count > 0U) {
        const LumeError *erro = &erros.data[0];
        json_texto(&b, "{\"linha\":"); json_numero(&b, erro->span.start.line);
        json_texto(&b, ",\"coluna\":"); json_numero(&b, erro->span.start.column);
        json_texto(&b, ",\"mensagem\":");
        json_string(&b, erro->message == NULL ? "" : erro->message,
                    erro->message == NULL ? 0U : strlen(erro->message));
        json_texto(&b, "}");
    } else {
        json_texto(&b, "null");
    }
    json_texto(&b, "}");

    program_free(programa);
    token_array_free(&tokens);
    error_list_free(&erros);
    source_free(&fonte);

    if (!b.ok) { json_buffer_free(&b); return NULL; }
    return b.dados;   /* alocado por memory_*, liberado com lume_web_free */
}
