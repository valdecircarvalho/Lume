#include "web/lume_json.h"
#include <stdio.h>
#include <string.h>
#include "memory.h"

void json_buffer_init(JsonBuffer *buffer) {
    if (buffer == NULL) return;
    buffer->dados = NULL; buffer->tamanho = 0U; buffer->capacidade = 0U; buffer->ok = true;
}
void json_buffer_free(JsonBuffer *buffer) {
    if (buffer == NULL) return;
    memory_free(buffer->dados); json_buffer_init(buffer);
}
static bool reservar(JsonBuffer *buffer, size_t extra) {
    size_t nova; char *maior;
    if (!buffer->ok) return false;
    if (buffer->tamanho + extra + 1U <= buffer->capacidade) return true;
    nova = buffer->capacidade < 4096U ? 4096U : buffer->capacidade;
    while (nova < buffer->tamanho + extra + 1U) {
        if (nova > (size_t)-1 / 2U) { buffer->ok = false; return false; }
        nova *= 2U;
    }
    maior = memory_reallocate_array(buffer->dados, nova, 1U);
    if (maior == NULL) { buffer->ok = false; return false; }
    buffer->dados = maior; buffer->capacidade = nova;
    return true;
}
void json_bytes(JsonBuffer *buffer, const char *bytes, size_t comprimento) {
    if (buffer == NULL || bytes == NULL || !reservar(buffer, comprimento)) return;
    memcpy(buffer->dados + buffer->tamanho, bytes, comprimento);
    buffer->tamanho += comprimento; buffer->dados[buffer->tamanho] = '\0';
}
void json_texto(JsonBuffer *buffer, const char *texto) {
    if (texto != NULL) json_bytes(buffer, texto, strlen(texto));
}
void json_numero(JsonBuffer *buffer, size_t valor) {
    char temporario[32];
    int escrito = snprintf(temporario, sizeof(temporario), "%zu", valor);
    if (escrito > 0) json_bytes(buffer, temporario, (size_t)escrito);
}
/* Bytes de controle viram \u00XX; o resto de UTF-8 passa direto, porque JSON
   aceita UTF-8 cru. */
void json_string(JsonBuffer *buffer, const char *bytes, size_t comprimento) {
    size_t indice;
    json_texto(buffer, "\"");
    for (indice = 0U; bytes != NULL && indice < comprimento; indice++) {
        unsigned char c = (unsigned char)bytes[indice];
        switch (c) {
            case '"':  json_texto(buffer, "\\\""); break;
            case '\\': json_texto(buffer, "\\\\"); break;
            case '\n': json_texto(buffer, "\\n"); break;
            case '\r': json_texto(buffer, "\\r"); break;
            case '\t': json_texto(buffer, "\\t"); break;
            default:
                if (c < 0x20U) {
                    char escapado[7];
                    int escrito = snprintf(escapado, sizeof(escapado), "\\u%04x", c);
                    if (escrito > 0) json_bytes(buffer, escapado, (size_t)escrito);
                } else {
                    json_bytes(buffer, (const char *)&c, 1U);
                }
        }
    }
    json_texto(buffer, "\"");
}
