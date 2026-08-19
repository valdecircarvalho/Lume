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
void lume_web_free(char *ponteiro);

static int falhas = 0;
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
    if (falhas == 0) { puts("Sequencia de execucoes no mesmo processo: tudo passou."); return 0; }
    fprintf(stderr, "%d verificacao(oes) falharam.\n", falhas); return 1;
}
