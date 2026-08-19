#ifndef LUME_WEB_JSON_H
#define LUME_WEB_JSON_H
#include "common.h"

/* Buffer de texto que cresce sozinho, usado para montar as respostas em JSON.
 * Em caso de falha de alocacao ele marca 'ok' e para de escrever, entao o
 * chamador so precisa conferir uma vez no fim, em vez de a cada append. */
typedef struct { char *dados; size_t tamanho; size_t capacidade; bool ok; } JsonBuffer;

void json_buffer_init(JsonBuffer *buffer);
void json_buffer_free(JsonBuffer *buffer);
void json_bytes(JsonBuffer *buffer, const char *bytes, size_t comprimento);
void json_texto(JsonBuffer *buffer, const char *texto);
void json_numero(JsonBuffer *buffer, size_t valor);
/* Escreve os bytes como string JSON, entre aspas e com os escapes necessarios. */
void json_string(JsonBuffer *buffer, const char *bytes, size_t comprimento);

#endif
