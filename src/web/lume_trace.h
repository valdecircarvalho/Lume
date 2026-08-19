#ifndef LUME_WEB_TRACE_H
#define LUME_WEB_TRACE_H
#include "error.h"
#include "runtime_io.h"
#include "source.h"

/* Executa o programa registrando a fita completa de eventos em JSON.
 * 'fonte' e preenchida pelo chamador e continua valida no retorno, para que o
 * diagnostico possa ser renderizado. O chamador libera *eventos_json. */
bool lume_trace_executar(const char *nome, const char *codigo, size_t comprimento,
                         RuntimeIO *io, char **eventos_json, size_t *total, bool *truncado,
                         Source *fonte, ErrorList *erros);
#endif
