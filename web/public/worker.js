/* Executa a Lume fora da thread principal.
 *
 * A razao de existir deste arquivo e uma so: um aluno vai escrever
 * `enquanto verdadeiro { }` no primeiro dia. Na thread principal isso congela
 * a aba e o unico recurso e fecha-la. Aqui, a pagina chama worker.terminate()
 * e a execucao morre na hora.
 */
importScripts('lume.js');

let modulo = null;

createLume().then((M) => {
  modulo = M;
  postMessage({ tipo: 'pronto' });
}).catch((erro) => {
  postMessage({ tipo: 'falha-ao-carregar', mensagem: String(erro) });
});

onmessage = (evento) => {
  const { codigo, entrada, modo } = evento.data;
  if (modulo === null) {
    postMessage({ tipo: 'resultado', saida: '', erro: 'O interpretador ainda esta carregando.' });
    return;
  }
  const inicio = performance.now();
  let ponteiro = 0;
  try {
    const funcao = modo === 'passo' ? 'lume_web_trace'
                 : modo === 'estrutura' ? 'lume_web_estrutura' : 'lume_web_eval';
    /* lume_web_estrutura nao executa nada, entao nao recebe entrada. */
    ponteiro = modo === 'estrutura'
      ? modulo.ccall(funcao, 'number', ['string'], [codigo])
      : modulo.ccall(funcao, 'number', ['string', 'string'], [codigo, entrada]);
    const bruto = ponteiro ? modulo.UTF8ToString(ponteiro) : '';
    /* A string ja foi copiada para JS aqui, entao liberar antes de responder e
       seguro — e garante que nada fica pendurado se o postMessage falhar. */
    if (ponteiro) { modulo.ccall('lume_web_free', null, ['number'], [ponteiro]); ponteiro = 0; }
    const ms = Math.round(performance.now() - inicio);
    /* Os dois modos respondem o mesmo formato: { saida, erro, ... }. O erro vem
       com a localizacao exata, para o editor sublinhar o trecho. */
    const r = JSON.parse(bruto);
    postMessage({ tipo: 'resultado', modo: modo || 'normal', ms: ms,
                  saida: r.saida, erro: r.erro, eventos: r.eventos,
                  total: r.total, truncado: r.truncado,
                  tokens: r.tokens, arvore: r.arvore });
  } catch (erro) {
    /* Chegar aqui significa que o wasm abortou (estouro de pilha, por exemplo).
       O modulo nao e mais confiavel: nao se chama free sobre a heap dele, porque
       'ponteiro' pode ter ficado com lixo. A pagina descarta este worker. */
    ponteiro = 0;
    postMessage({ tipo: 'abortou', mensagem: String(erro) });
  }
};
