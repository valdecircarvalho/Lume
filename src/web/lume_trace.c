/* Coletor de trace para o depurador visual do playground.
 *
 * A CLI consome os mesmos eventos em src/education.c e imprime texto, pausando
 * entre passos com fgets — o que nao existe no navegador. Aqui o programa roda
 * uma vez do inicio ao fim e a fita inteira e serializada em JSON, para o
 * JavaScript percorrer para frente e para tras. Sai mais barato e da um recurso
 * que a CLI nao tem: voltar um passo.
 *
 * O comentario em src/trace.h avisa que os campos do TraceEvent so valem
 * durante a chamada sincrona, entao tudo e copiado aqui dentro.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "education.h"
#include "environment.h"
#include "interpreter.h"
#include "lexer.h"
#include "memory.h"
#include "parser.h"
#include "trace.h"
#include "value.h"
#include "web/lume_json.h"
#include "web/lume_trace.h"

/* Tetos generosos para material didatico, mas que impedem um laco de um milhao
   de voltas de consumir toda a memoria do navegador. */
#define MAX_EVENTOS   20000U
#define MAX_VARIAVEIS 60U
/* O interpretador limita a recursao em LUME_MAX_CALL_DEPTH (200); a folga aqui
   e para nao depender desse numero por acidente. */
#define MAX_QUADROS   256U

static const char *nome_do_tipo(TraceEventType tipo) {
    switch (tipo) {
        case TRACE_PROGRAM_START:     return "inicio";
        case TRACE_PROGRAM_END:       return "fim";
        case TRACE_DECLARE_VARIABLE:  return "declara-variavel";
        case TRACE_DECLARE_CONSTANT:  return "declara-constante";
        case TRACE_DECLARE_FUNCTION:  return "declara-funcao";
        case TRACE_ASSIGN:            return "atribui";
        case TRACE_IF_CONDITION:      return "condicao-se";
        case TRACE_WHILE_CONDITION:   return "condicao-enquanto";
        case TRACE_WHILE_ITERATION:   return "volta-enquanto";
        case TRACE_WHILE_END:         return "fim-enquanto";
        case TRACE_FOR_START:         return "inicio-para";
        case TRACE_FOR_ITERATION:     return "volta-para";
        case TRACE_FOR_END:           return "fim-para";
        case TRACE_FUNCTION_CALL:     return "chama-funcao";
        case TRACE_FUNCTION_ENTER:    return "entra-funcao";
        case TRACE_FUNCTION_RETURN:   return "retorna-funcao";
        case TRACE_NATIVE_CALL:       return "chama-nativa";
        case TRACE_OUTPUT:            return "escreve";
        case TRACE_LIST_CREATE:       return "cria-lista";
        case TRACE_INDEX_READ:        return "le-indice";
        case TRACE_INDEX_WRITE:       return "escreve-indice";
        case TRACE_LIST_APPEND:       return "adiciona-lista";
        case TRACE_LIST_REMOVE:       return "remove-lista";
        case TRACE_MODULE_IMPORT:     return "importa-modulo";
        case TRACE_MODULE_LOADED:     return "modulo-carregado";
    }
    return "desconhecido";
}

typedef struct { JsonBuffer *saida; size_t escritas; bool primeira; } ColetorVariaveis;

static void visitar_binding(void *contexto, const char *nome, size_t comprimento,
                            const Value *valor, bool mutavel) {
    ColetorVariaveis *coletor = (ColetorVariaveis *)contexto;
    char *formatado = NULL; size_t tamanho = 0U;
    /* Funcoes e modulos poluiriam o inspetor: o aluno quer ver dados. */
    if (valor == NULL || valor->type == VALUE_CALLABLE || valor->type == VALUE_MODULE) return;
    if (coletor->escritas >= MAX_VARIAVEIS) return;
    if (!coletor->primeira) json_texto(coletor->saida, ",");
    coletor->primeira = false;
    coletor->escritas++;
    json_texto(coletor->saida, "{\"n\":");
    json_string(coletor->saida, nome, comprimento);
    json_texto(coletor->saida, ",\"v\":");
    if (value_format(valor, &formatado, &tamanho) && formatado != NULL) {
        json_string(coletor->saida, formatado, tamanho);
    } else {
        json_texto(coletor->saida, "\"?\"");
    }
    memory_free(formatado);
    json_texto(coletor->saida, ",\"m\":");
    json_texto(coletor->saida, mutavel ? "true" : "false");
    json_texto(coletor->saida, "}");
}

/* Percorre um encadeamento de ambientes acumulando as variaveis. 'ate_global'
   diz se o global entra: no topo do programa ele e o proprio quadro principal,
   mas dentro de uma funcao ele e um quadro separado — senao as variaveis
   globais apareceriam repetidas em cada chamada. */
static size_t escrever_bindings(const Environment *ambiente, bool ate_global,
                                ColetorVariaveis *coletor) {
    const Environment *atual = ambiente;
    while (atual != NULL && coletor->escritas < MAX_VARIAVEIS) {
        if (atual->is_global && !ate_global) break;
        environment_visit_current(atual, visitar_binding, coletor);
        if (atual->is_global) break;
        atual = atual->parent;
    }
    return coletor->escritas;
}

static const Environment *achar_global(const Environment *ambiente) {
    const Environment *atual = ambiente;
    while (atual != NULL && !atual->is_global) atual = atual->parent;
    return atual;
}

static void abrir_quadro(JsonBuffer *b, const char *nome, size_t comprimento, bool primeiro) {
    if (!primeiro) json_texto(b, ",");
    json_texto(b, "{\"q\":");
    json_string(b, nome, comprimento);
    json_texto(b, ",\"vars\":[");
}

/* Pilha propria de ambientes de chamada.
 *
 * O ambiente de uma chamada tem como pai o ambiente de FECHAMENTO, nao o do
 * chamador — que e o escopo lexico correto, mas significa que subindo pelos
 * pais nunca se alcancam os quadros de fora. Sem esta pilha, em fatorial(4) o
 * inspetor mostraria um unico n quando existem quatro, dando a impressao de que
 * a variavel foi sobrescrita: exatamente o mal-entendido que a visualizacao
 * deveria desfazer.
 */
typedef struct {
    const Environment *ambiente;
    const char *nome;
    size_t nome_comprimento;
} Quadro;

typedef struct {
    JsonBuffer buffer; size_t contagem; bool truncado; bool primeiro;
    Quadro pilha[MAX_QUADROS]; size_t topo;
} Coletor;

/* Emite os quadros vivos, do mais externo para o mais interno — a mesma ordem
   em que o Python Tutor os empilha. */
static void escrever_quadros(JsonBuffer *b, const Coletor *coletor, const TraceEvent *evento) {
    ColetorVariaveis vars;
    const Environment *global;
    size_t indice;
    json_texto(b, "[");
    if (coletor->topo == 0U) {
        /* Fora de qualquer funcao: bloco e topo do programa sao o mesmo quadro. */
        abrir_quadro(b, "principal", 9U, true);
        vars.saida = b; vars.escritas = 0U; vars.primeira = true;
        escrever_bindings(evento->environment, true, &vars);
        json_texto(b, "]}");
        json_texto(b, "]");
        return;
    }
    global = achar_global(evento->environment);
    abrir_quadro(b, "principal", 9U, true);
    vars.saida = b; vars.escritas = 0U; vars.primeira = true;
    if (global != NULL) escrever_bindings(global, true, &vars);
    json_texto(b, "]}");
    for (indice = 0U; indice < coletor->topo; indice++) {
        const Quadro *quadro = &coletor->pilha[indice];
        /* O quadro mais interno usa o ambiente do evento, que ja inclui os
           blocos abertos dentro dele; os de fora usam o que foi empilhado. */
        const Environment *ambiente = (indice + 1U == coletor->topo)
            ? evento->environment : quadro->ambiente;
        abrir_quadro(b, quadro->nome, quadro->nome_comprimento, false);
        vars.saida = b; vars.escritas = 0U; vars.primeira = true;
        escrever_bindings(ambiente, false, &vars);
        json_texto(b, "]}");
    }
    json_texto(b, "]");
}

static void escrever_valor(JsonBuffer *b, const char *chave, const Value *valor) {
    char *formatado = NULL; size_t tamanho = 0U;
    if (valor == NULL) return;
    json_texto(b, ",\""); json_texto(b, chave); json_texto(b, "\":");
    if (value_format(valor, &formatado, &tamanho) && formatado != NULL) {
        json_string(b, formatado, tamanho);
    } else {
        json_texto(b, "null");
    }
    memory_free(formatado);
}

static void ao_receber_evento(void *contexto, const TraceEvent *evento) {
    Coletor *coletor = (Coletor *)contexto;
    JsonBuffer *b = &coletor->buffer;
    if (evento == NULL) return;
    coletor->contagem++;
    if (coletor->contagem > MAX_EVENTOS) { coletor->truncado = true; return; }

    if (evento->type == TRACE_FUNCTION_ENTER && coletor->topo < MAX_QUADROS) {
        Quadro *quadro = &coletor->pilha[coletor->topo++];
        quadro->ambiente = evento->environment;
        quadro->nome = evento->name != NULL ? evento->name : "funcao";
        quadro->nome_comprimento = evento->name != NULL ? evento->name_length : 6U;
    }

    if (!coletor->primeiro) json_texto(b, ",");
    coletor->primeiro = false;

    json_texto(b, "{\"t\":\""); json_texto(b, nome_do_tipo(evento->type));
    json_texto(b, "\",\"l\":");  json_numero(b, evento->span.start.line);
    json_texto(b, ",\"c\":");    json_numero(b, evento->span.start.column);
    json_texto(b, ",\"l2\":");   json_numero(b, evento->span.end.line);
    json_texto(b, ",\"c2\":");   json_numero(b, evento->span.end.column);
    json_texto(b, ",\"p\":");    json_numero(b, evento->call_depth);

    if (evento->name != NULL) {
        json_texto(b, ",\"n\":");
        json_string(b, evento->name, evento->name_length);
    }
    escrever_valor(b, "a", evento->before);
    escrever_valor(b, "d", evento->after);

    switch (evento->type) {
        case TRACE_IF_CONDITION: case TRACE_WHILE_CONDITION:
            json_texto(b, ",\"b\":");
            json_texto(b, evento->decision ? "true" : "false");
            break;
        case TRACE_WHILE_ITERATION: case TRACE_FOR_ITERATION:
            json_texto(b, ",\"i\":"); json_numero(b, evento->iteration);
            break;
        case TRACE_INDEX_READ: case TRACE_INDEX_WRITE:
            if (evento->index >= 0) { json_texto(b, ",\"x\":"); json_numero(b, (size_t)evento->index); }
            break;
        default: break;
    }

    if (evento->environment != NULL) {
        json_texto(b, ",\"v\":");
        escrever_quadros(b, coletor, evento);
    }
    json_texto(b, "}");

    /* Desempilha depois de emitir: no evento de retorno o quadro ainda existe,
       e e justamente o valor que ele devolveu que interessa ver. */
    if (evento->type == TRACE_FUNCTION_RETURN && coletor->topo > 0U) coletor->topo--;
}

bool lume_trace_executar(const char *nome, const char *codigo, size_t comprimento,
                         RuntimeIO *io, char **eventos_json, size_t *total, bool *truncado,
                         Source *fonte, ErrorList *erros) {
    TokenArray tokens; Program *programa = NULL; Environment ambiente;
    RuntimeTrace trace; Coletor coletor; bool ok;

    token_array_init(&tokens);
    environment_init(&ambiente, NULL);
    json_buffer_init(&coletor.buffer);
    coletor.contagem = 0U; coletor.truncado = false; coletor.primeiro = true;
    coletor.topo = 0U;
    json_texto(&coletor.buffer, "[");

    trace.callback = ao_receber_evento; trace.context = &coletor; trace.stop_requested = false;

    ok = source_from_bytes(fonte, nome, codigo, comprimento);
    if (ok) ok = lexer_scan(fonte, &tokens, erros);
    if (ok) ok = parser_parse_program(&tokens, &programa, erros);
    if (ok) ok = interpreter_execute_program_with_trace(programa, &ambiente, io, &trace, erros);

    json_texto(&coletor.buffer, "]");

    /* Mesma ordem de education.c: o diagnostico e exibido pelo chamador, entao
       a AST e liberada aqui, depois que ninguem mais aponta para ela. */
    environment_free(&ambiente);
    program_free(programa);
    token_array_free(&tokens);

    *eventos_json = coletor.buffer.ok ? coletor.buffer.dados : NULL;
    if (!coletor.buffer.ok) json_buffer_free(&coletor.buffer);
    *total = coletor.contagem;
    *truncado = coletor.truncado;
    return ok;
}
