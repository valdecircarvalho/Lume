/* Regressao do ciclo de vida entre execucoes.
 *
 * A CLI usa um processo por programa, entao nada exercitava varias sessoes no
 * mesmo processo — que e exatamente o que o playground faz. Um double-free da
 * fonte so aparecia depois de uma dezena de programas, como
 * "table index is out of bounds" no navegador. Rode sob ASan:
 *
 *   make -f Makefile.wasm web-test-nativo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *lume_web_eval(const char *codigo, const char *entrada);
char *lume_web_trace(const char *codigo, const char *entrada);
void lume_web_free(char *ponteiro);

static int falhas = 0;

/* Recorta o primeiro evento da fita que contenha 'marca'. Os eventos sao objetos
   separados por "},{" no vetor, entao o recorte e simples e nao precisa de um
   parser de JSON aqui. O chamador libera com free. */
static char *recortar_evento(const char *fita, const char *marca) {
    const char *achou = strstr(fita, marca), *inicio, *fim;
    char *recorte; size_t tamanho;
    if (achou == NULL) return NULL;
    inicio = achou;
    while (inicio > fita && !(inicio[0] == '{' && inicio[1] == '"' && inicio[2] == 't')) inicio--;
    fim = strstr(achou, "},{\"t\"");
    if (fim == NULL) fim = fita + strlen(fita);
    tamanho = (size_t)(fim - inicio);
    recorte = malloc(tamanho + 1U);
    if (recorte == NULL) return NULL;
    memcpy(recorte, inicio, tamanho); recorte[tamanho] = '\0';
    return recorte;
}

static size_t conta_ocorrencias(const char *texto, const char *agulha) {
    size_t total = 0U; const char *atual = texto;
    while ((atual = strstr(atual, agulha)) != NULL) { total++; atual += strlen(agulha); }
    return total;
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); falhas++; } } while (0)

static void esperar(const char *rotulo, const char *codigo, const char *entrada, const char *esperado) {
    char *saida = lume_web_eval(codigo, entrada);
    CHECK(saida != NULL);
    if (saida != NULL && strstr(saida, esperado) == NULL) {
        fprintf(stderr, "FALHA %s: esperava conter \"%s\", veio \"%.90s\"\n", rotulo, esperado, saida);
        falhas++;
    }
    lume_web_free(saida);
}

int main(void) {
    int volta;
    /* A ordem importa: programas com funcao fazem a sessao reter a fonte, e e
       nesse caminho que a posse era duplicada. */
    esperar("texto",     "escreva(\"ola\")\n", "", "ola");
    esperar("entrada",   "variavel a = inteiro(leia())\nescreva(a * 2)\n", "21\n", "42");
    esperar("sem entrada", "escreva(1 + 1)\n", "", "2");
    esperar("funcao",    "funcao f(n) {\n retorne n * 2\n}\nescreva(f(21))\n", "", "42");
    esperar("recursao",  "funcao f(n) {\n se n <= 1 {\n  retorne 1\n }\n retorne n * f(n-1)\n}\nescreva(f(10))\n", "", "3628800");
    esperar("erro nome", "escreva(xyz)\n", "", "Nome: 'xyz'");
    esperar("erro sintaxe", "variavel = 1\n", "", "Erro de sintaxe");
    esperar("recursao infinita", "funcao g() {\n retorne g()\n}\nescreva(g())\n", "", "vezes demais");
    esperar("lista",     "variavel l = [1,2,3]\nescreva(tamanho(l))\n", "", "3");
    esperar("stdlib",    "importe \"lume/matematica\"\nescreva(matematica.raiz(144))\n", "", "12");

    /* O sintoma original so aparecia depois de uma dezena de programas com
       funcao, entao o teste insiste bem alem disso. */
    for (volta = 0; volta < 40; volta++) {
        esperar("repeticao", "funcao f(n) {\n retorne n + 1\n}\nescreva(f(41))\n", "", "42");
        esperar("repeticao-erro", "funcao f() {\n retorne inexistente\n}\nescreva(f())\n", "", "Erro de nome");
    }
    /* O modo passo a passo grava a fita inteira em memoria; e o caminho que
       mais aloca, entao vale exercita-lo no mesmo processo tambem. */
    for (volta = 0; volta < 20; volta++) {
        char *fita = lume_web_trace("variavel s = 0\npara i de 1 ate 5 {\n s = s + i\n}\nescreva(s)\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) {
            CHECK(strstr(fita, "\"saida\":\"15") != NULL);
            CHECK(strstr(fita, "declara-variavel") != NULL);
            CHECK(strstr(fita, "volta-para") != NULL);
            CHECK(strstr(fita, "\"truncado\":false") != NULL);
        }
        lume_web_free(fita);
    }
    { /* erro no modo passo: a fita para, mas a resposta continua valida */
        char *fita = lume_web_trace("escreva(xyz)\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) CHECK(strstr(fita, "Nome: ") != NULL);
        lume_web_free(fita);
    }
    { /* Quadros por chamada: em fatorial(4) os quatro n precisam estar visiveis
         AO MESMO TEMPO, dentro de um unico evento. O ambiente de uma chamada tem
         como pai o de fechamento, entao sem a pilha propria do coletor so o n
         mais interno apareceria. Verificar a fita inteira nao serve: os quatro
         valores apareceriam mesmo assim, em eventos diferentes. */
        char *fita = lume_web_trace("funcao f(n) {\n se n <= 1 {\n  retorne 1\n }\n retorne n * f(n-1)\n}\nescreva(f(4))\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) {
            char *evento = recortar_evento(fita, "\"p\":4");
            CHECK(evento != NULL);
            if (evento != NULL) {
                CHECK(strstr(evento, "\"q\":\"principal\"") != NULL);
                CHECK(conta_ocorrencias(evento, "\"q\":\"f\"") == 4U);
                CHECK(strstr(evento, "\"v\":\"4\"") != NULL);
                CHECK(strstr(evento, "\"v\":\"3\"") != NULL);
                CHECK(strstr(evento, "\"v\":\"2\"") != NULL);
                CHECK(strstr(evento, "\"v\":\"1\"") != NULL);
                free(evento);
            }
        }
        lume_web_free(fita);
    }
    { /* Global nao pode ser repetida dentro do quadro da funcao. */
        char *fita = lume_web_trace("variavel g = 10\nfuncao f(a) {\n retorne a + g\n}\nescreva(f(5))\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) {
            const char *entrou = strstr(fita, "entra-funcao");
            CHECK(entrou != NULL);
            if (entrou != NULL) {
                const char *quadro_f = strstr(entrou, "\"q\":\"f\"");
                CHECK(quadro_f != NULL);
                /* depois do quadro de f, ate o fim do evento, nao deve haver g */
                if (quadro_f != NULL) {
                    const char *fim = strchr(quadro_f, '}');
                    CHECK(fim != NULL);
                    if (fim != NULL) {
                        char recorte[512]; size_t n = (size_t)(fim - quadro_f);
                        if (n >= sizeof(recorte)) n = sizeof(recorte) - 1U;
                        memcpy(recorte, quadro_f, n); recorte[n] = '\0';
                        CHECK(strstr(recorte, "\"n\":\"g\"") == NULL);
                        CHECK(strstr(recorte, "\"n\":\"a\"") != NULL);
                    }
                }
            }
        }
        lume_web_free(fita);
    }
    { /* No topo do programa, bloco e global sao o mesmo quadro: i e s juntos. */
        char *fita = lume_web_trace("variavel s = 0\npara i de 1 ate 2 {\n s = s + i\n}\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) {
            CHECK(strstr(fita, "\"q\":\"f\"") == NULL);
            CHECK(strstr(fita, "\"n\":\"i\"") != NULL);
            CHECK(strstr(fita, "\"n\":\"s\"") != NULL);
        }
        lume_web_free(fita);
    }
    { /* recursao: a profundidade tem de aparecer na fita */
        char *fita = lume_web_trace("funcao f(n) {\n se n <= 1 {\n  retorne 1\n }\n retorne n * f(n-1)\n}\nescreva(f(5))\n", "");
        CHECK(fita != NULL);
        if (fita != NULL) { CHECK(strstr(fita, "entra-funcao") != NULL); CHECK(strstr(fita, "\"p\":4") != NULL); }
        lume_web_free(fita);
    }
    if (falhas == 0) { puts("Sequencia de execucoes no mesmo processo: tudo passou."); return 0; }
    fprintf(stderr, "%d verificacao(oes) falharam.\n", falhas); return 1;
}
