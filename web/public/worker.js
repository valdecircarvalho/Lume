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
  const { codigo, entrada } = evento.data;
  if (modulo === null) {
    postMessage({ tipo: 'resultado', saida: '', erro: 'O interpretador ainda esta carregando.' });
    return;
  }
  const inicio = performance.now();
  let ponteiro = 0;
  try {
    ponteiro = modulo.ccall('lume_web_eval', 'number', ['string', 'string'], [codigo, entrada]);
    const saida = ponteiro ? modulo.UTF8ToString(ponteiro) : '';
    postMessage({ tipo: 'resultado', saida: saida, ms: Math.round(performance.now() - inicio) });
  } catch (erro) {
    /* Chegar aqui significa que o wasm abortou (estouro de pilha, por exemplo).
       O modulo nao e mais confiavel, entao a pagina descarta este worker. */
    postMessage({ tipo: 'abortou', mensagem: String(erro) });
  } finally {
    if (ponteiro) modulo.ccall('lume_web_free', null, ['number'], [ponteiro]);
  }
};
