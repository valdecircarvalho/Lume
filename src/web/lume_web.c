/* Camada de embedding para WebAssembly. Nao e usada pelo build nativo.
   Traduz a API de src/session.h para algo chamavel do JavaScript. */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
/* Compilar nativamente permite exercitar este arquivo sob ASan/UBSan, que e
   como o double-free do ciclo de vida da fonte foi encontrado. */
#define EMSCRIPTEN_KEEPALIVE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "diagnostic.h"
#include "memory.h"
#include "session.h"
#include "web/lume_trace.h"


/* Escapa uma sequencia de bytes como string JSON. Usado tanto para a saida do
   programa quanto para os textos do diagnostico. */
static void json_texto(FILE *destino, const char *bytes, size_t comprimento) {
    size_t indice;
    fputc('"', destino);
    for (indice = 0U; bytes != NULL && indice < comprimento; indice++) {
        unsigned char c = (unsigned char)bytes[indice];
        switch (c) {
            case '"':  fputs("\\\"", destino); break;
            case '\\': fputs("\\\\", destino); break;
            case '\n': fputs("\\n", destino); break;
            case '\r': fputs("\\r", destino); break;
            case '\t': fputs("\\t", destino); break;
            default:
                if (c < 0x20U) fprintf(destino, "\\u%04x", c);
                else fputc((int)c, destino);
        }
    }
    fputc('"', destino);
}

/* O diagnostico ja carrega a localizacao exata — e o que diagnostic_render usa
   para desenhar o caret. Levar isso ao JavaScript permite sublinhar o trecho no
   editor, em vez de o aluno contar linhas com o dedo a partir do texto. */
static void json_erro(FILE *destino, const LumeError *erro) {
    if (erro == NULL) { fputs("null", destino); return; }
    fprintf(destino, "{\"linha\":%zu,\"coluna\":%zu,\"linhaFim\":%zu,\"colunaFim\":%zu,\"tipo\":",
            erro->span.start.line, erro->span.start.column,
            erro->span.end.line, erro->span.end.column);
    json_texto(destino, error_kind_name(erro->kind), strlen(error_kind_name(erro->kind)));
    fputs(",\"mensagem\":", destino);
    json_texto(destino, erro->message == NULL ? "" : erro->message,
               erro->message == NULL ? 0U : strlen(erro->message));
    fputs(",\"dica\":", destino);
    if (erro->suggestion == NULL) fputs("null", destino);
    else json_texto(destino, erro->suggestion, strlen(erro->suggestion));
    fputs(",\"nome\":", destino);
    if (erro->subject == NULL) fputs("null", destino);
    else json_texto(destino, erro->subject, erro->subject_length);
    fputc('}', destino);
}

/* Resposta comum aos dois modos: { saida, erro, ... }. 'extra' e escrito logo
   antes de fechar o objeto, ou NULL. */
static char *montar_resposta(const char *saida, size_t tamanho, const LumeError *erro,
                             const char *eventos, size_t total, bool truncado, bool com_fita) {
    char *montado = NULL; size_t n = 0U;
    FILE *json = open_memstream(&montado, &n);
    if (json == NULL) return NULL;
    fputs("{\"saida\":", json);
    json_texto(json, saida, tamanho);
    fputs(",\"erro\":", json);
    json_erro(json, erro);
    if (com_fita) {
        fprintf(json, ",\"eventos\":%s,\"total\":%zu,\"truncado\":%s",
                eventos != NULL ? eventos : "[]", total, truncado ? "true" : "false");
    }
    fputc('}', json);
    fclose(json);
    return montado;
}

/* Executa 'codigo' com 'entrada' fazendo o papel do stdin e devolve tudo o que
   o programa escreveu, como { "saida": ..., "erro": ... }. O chamador libera
   com lume_web_free. */
EMSCRIPTEN_KEEPALIVE
char *lume_web_eval(const char *codigo, const char *entrada) {
    LumeSession sessao; ErrorList erros; Source *fonte = NULL; RuntimeIO io;
    char *saida = NULL, *resposta = NULL; size_t tamanho = 0U; FILE *in, *out; bool ok;
    if (codigo == NULL) codigo = "";
    if (entrada == NULL) entrada = "";
    /* fmemopen com tamanho 0 devolve NULL em algumas libc; sem entrada, um
       arquivo vazio de verdade e mais seguro do que depender disso. */
    in = strlen(entrada) > 0U ? fmemopen((void *)entrada, strlen(entrada), "r")
                              : fopen("/dev/null", "r");
    out = open_memstream(&saida, &tamanho);
    if (in == NULL || out == NULL) {
        if (in != NULL) fclose(in);
        if (out != NULL) { fclose(out); free(saida); }
        return NULL;
    }
    io.input = in; io.output = out;
    session_init(&sessao, io); error_list_init(&erros);
    ok = session_execute(&sessao, "principal.lume", codigo, strlen(codigo),
                         false, &fonte, &erros);
    if (!ok && erros.count > 0U) diagnostic_render(out, fonte, &erros.data[0]);
    fflush(out);
    /* So o caminho de erro devolve a posse da fonte. Quando a execucao da
       certo, a sessao a retem (programas com funcoes) ou ja a liberou. Liberar
       aqui nos dois casos e um double-free: nao aparece no primeiro programa,
       mas corrompe o heap e derruba a sessao alguns programas depois. */
    resposta = montar_resposta(saida, tamanho, (!ok && erros.count > 0U) ? &erros.data[0] : NULL,
                               NULL, 0U, false, false);
    if (!ok && fonte != NULL) { source_free(fonte); memory_free(fonte); }
    error_list_free(&erros); session_free(&sessao);
    fclose(out); fclose(in); free(saida);
    return resposta;              /* alocado pela libc: liberar com free */
}

/* Como lume_web_eval, mas devolve tambem a fita de execucao para o depurador
   visual. O JSON e { "saida": ..., "eventos": [...], "total": n, "truncado": b }. */
EMSCRIPTEN_KEEPALIVE
char *lume_web_trace(const char *codigo, const char *entrada) {
    ErrorList erros; Source fonte; RuntimeIO io;
    char *saida = NULL, *eventos = NULL, *resposta = NULL;
    size_t tamanho = 0U, total = 0U;
    bool truncado = false, ok;
    FILE *in, *out;
    if (codigo == NULL) codigo = "";
    if (entrada == NULL) entrada = "";
    in = strlen(entrada) > 0U ? fmemopen((void *)entrada, strlen(entrada), "r")
                              : fopen("/dev/null", "r");
    out = open_memstream(&saida, &tamanho);
    if (in == NULL || out == NULL) {
        if (in != NULL) fclose(in);
        if (out != NULL) { fclose(out); free(saida); }
        return NULL;
    }
    io.input = in; io.output = out;
    source_init(&fonte); error_list_init(&erros);
    ok = lume_trace_executar("principal.lume", codigo, strlen(codigo), &io,
                             &eventos, &total, &truncado, &fonte, &erros);
    if (!ok && erros.count > 0U) diagnostic_render(out, &fonte, &erros.data[0]);
    fclose(out); fclose(in);


    resposta = montar_resposta(saida, tamanho, (!ok && erros.count > 0U) ? &erros.data[0] : NULL,
                               eventos, total, truncado, true);
    memory_free(eventos); free(saida);
    error_list_free(&erros); source_free(&fonte);
    return resposta;
}

/* Roda a CLI inteira (projetos, modulos, --analisar, --explicar) sobre arquivos
   que o JavaScript escreveu no sistema de arquivos virtual. */
EMSCRIPTEN_KEEPALIVE
char *lume_web_cli(int argc, char **argv) {
    char *saida = NULL; size_t tamanho = 0U; RuntimeIO io;
    FILE *out = open_memstream(&saida, &tamanho);
    if (out == NULL) return NULL;
    io.input = stdin; io.output = out;
    (void)cli_run(argc, argv, io);
    fclose(out);
    return saida;
}

EMSCRIPTEN_KEEPALIVE
void lume_web_free(char *ponteiro) { free(ponteiro); }
