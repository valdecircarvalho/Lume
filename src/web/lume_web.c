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

/* Executa 'codigo' com 'entrada' fazendo o papel do stdin e devolve tudo o que
   o programa escreveu. O chamador libera com lume_web_free. */
EMSCRIPTEN_KEEPALIVE
char *lume_web_eval(const char *codigo, const char *entrada) {
    LumeSession sessao; ErrorList erros; Source *fonte = NULL; RuntimeIO io;
    char *saida = NULL; size_t tamanho = 0U; FILE *in, *out; bool ok;
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
    /* So o caminho de erro devolve a posse da fonte. Quando a execucao da
       certo, a sessao a retem (programas com funcoes) ou ja a liberou. Liberar
       aqui nos dois casos e um double-free: nao aparece no primeiro programa,
       mas corrompe o heap e derruba a sessao alguns programas depois. */
    if (!ok && fonte != NULL) { source_free(fonte); memory_free(fonte); }
    error_list_free(&erros); session_free(&sessao);
    fclose(out); fclose(in);
    return saida;                 /* alocado pela libc: liberar com free */
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
