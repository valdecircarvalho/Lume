#ifndef LUME_WEB_ESTRUTURA_H
#define LUME_WEB_ESTRUTURA_H
#include "common.h"

/* Devolve, em JSON, como o texto do programa vira tokens e como os tokens
 * viram arvore. E a aula de "como a linguagem que voce esta aprendendo e feita
 * por dentro". O chamador libera com free. */
char *lume_estrutura_json(const char *codigo);

#endif
