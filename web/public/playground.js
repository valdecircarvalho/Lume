'use strict';

/* Palavras-chave e nativas extraidas do proprio interpretador:
   src/lexer.c (keywords), src/interpreter.c (install_native) e
   src/lume_stdlib.c (namespaces importados com `importe "lume/..."`). */
const PALAVRAS_CHAVE = new Set([
  'variavel', 'constante', 'se', 'senao', 'enquanto', 'para', 'de', 'ate',
  'funcao', 'retorne', 'verdadeiro', 'falso', 'nulo', 'e', 'ou', 'nao',
  'importe', 'exporte'
]);
const NATIVAS = new Set([
  'escreva', 'leia', 'texto', 'inteiro', 'decimal', 'tipo', 'tamanho',
  'adicione', 'remova', 'matematica', 'arquivo', 'tempo'
]);

const EXEMPLOS = {
  'Olá, mundo': 'variavel nome = "mundo"\nescreva("Olá, " + nome + "!")\n',
  'Decisões': 'variavel idade = 20\n\nse idade >= 18 {\n  escreva("maior de idade")\n} senao {\n  escreva("menor de idade")\n}\n',
  'Repetição': 'variavel soma = 0\n\npara i de 1 ate 10 {\n  soma = soma + i\n}\n\nescreva("A soma de 1 a 10 é " + texto(soma))\n',
  'Listas': 'variavel notas = [7, 9, 6, 10]\nvariavel total = 0\n\npara i de 0 ate tamanho(notas) - 1 {\n  total = total + notas[i]\n}\n\nescreva("Média: " + texto(total / tamanho(notas)))\n',
  'Funções': 'funcao dobro(x) {\n  retorne x * 2\n}\n\nfuncao saudacao(nome) {\n  retorne "Olá, " + nome + "!"\n}\n\nescreva(dobro(21))\nescreva(saudacao("Lume"))\n',
  'Recursão': '// Toda recursão precisa de um caso base.\nfuncao fatorial(n) {\n  se n <= 1 {\n    retorne 1\n  }\n  retorne n * fatorial(n - 1)\n}\n\nescreva(fatorial(10))\n',
  'Lendo entrada': '// Preencha o painel "Entrada do programa" ao lado,\n// uma linha para cada leia().\nescreva("Digite dois números:")\n\nvariavel a = inteiro(leia())\nvariavel b = inteiro(leia())\n\nescreva("A soma é " + texto(a + b))\n',
  'Biblioteca padrão': 'importe "lume/matematica"\nimporte "lume/texto"\n\nescreva(matematica.raiz(144))\nescreva(texto.maiusculo("lume"))\n'
};

const CODIGO_INICIAL = EXEMPLOS['Olá, mundo'];
const CHAVE_RASCUNHO = 'lume:rascunho';

const $ = (id) => document.getElementById(id);
const elCodigo = $('codigo'), elRealce = $('realce').firstElementChild;
const elNumeros = $('numeros'), elEntrada = $('entrada'), elSaida = $('saida');
const btRodar = $('rodar'), btParar = $('parar'), elEstado = $('estado');

/* ---------- realce de sintaxe ---------- */

const escapar = (t) => t.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));

/* Um passe unico sobre o texto. A ordem importa: comentario e string vencem
   qualquer coisa que pareca palavra-chave dentro deles. */
function realcar(fonte) {
  const padrao = /(\/\/[^\n]*)|("(?:[^"\\\n]|\\.)*"?)|(\b\d+(?:\.\d+)?\b)|([A-Za-z_][A-Za-z0-9_]*)/g;
  let saida = '', ultimo = 0, m;
  while ((m = padrao.exec(fonte)) !== null) {
    saida += escapar(fonte.slice(ultimo, m.index));
    const texto = escapar(m[0]);
    if (m[1]) saida += '<span class="tok-com">' + texto + '</span>';
    else if (m[2]) saida += '<span class="tok-str">' + texto + '</span>';
    else if (m[3]) saida += '<span class="tok-num">' + texto + '</span>';
    else if (PALAVRAS_CHAVE.has(m[4])) saida += '<span class="tok-kw">' + texto + '</span>';
    else if (NATIVAS.has(m[4])) saida += '<span class="tok-nat">' + texto + '</span>';
    else saida += texto;
    ultimo = m.index + m[0].length;
  }
  return saida + escapar(fonte.slice(ultimo));
}

function redesenhar() {
  const fonte = elCodigo.value;
  /* A quebra final garante que a ultima linha do <pre> tenha altura, senao o
     realce e o textarea saem de alinhamento no fim do arquivo. */
  elRealce.innerHTML = realcar(fonte) + '\n';
  const linhas = fonte.split('\n').length;
  elNumeros.textContent = Array.from({ length: linhas }, (_, i) => i + 1).join('\n');
  sincronizarRolagem();
  try { localStorage.setItem(CHAVE_RASCUNHO, fonte); } catch (_) { /* modo privado */ }
}

function sincronizarRolagem() {
  elRealce.parentElement.scrollTop = elCodigo.scrollTop;
  elRealce.parentElement.scrollLeft = elCodigo.scrollLeft;
  elNumeros.scrollTop = elCodigo.scrollTop;
}

/* Tab indenta em vez de sair do campo — sair do campo com Tab surpreende
   quem esta escrevendo codigo. Shift+Tab desindenta. */
function tratarTecla(evento) {
  if (evento.key !== 'Tab') return;
  evento.preventDefault();
  const ini = elCodigo.selectionStart, fim = elCodigo.selectionEnd, v = elCodigo.value;
  if (evento.shiftKey) {
    const inicioLinha = v.lastIndexOf('\n', ini - 1) + 1;
    if (v.startsWith('  ', inicioLinha)) {
      elCodigo.value = v.slice(0, inicioLinha) + v.slice(inicioLinha + 2);
      elCodigo.selectionStart = elCodigo.selectionEnd = Math.max(inicioLinha, ini - 2);
    }
  } else {
    elCodigo.value = v.slice(0, ini) + '  ' + v.slice(fim);
    elCodigo.selectionStart = elCodigo.selectionEnd = ini + 2;
  }
  redesenhar();
}

/* ---------- ciclo de vida do worker ---------- */

let worker = null, pronto = false, executando = false;

function criarWorker() {
  pronto = false;
  worker = new Worker('worker.js');
  worker.onmessage = (evento) => {
    const dados = evento.data;
    if (dados.tipo === 'pronto') {
      pronto = true; executando = false;
      btRodar.disabled = false; btRodar.textContent = 'Executar';
      btParar.hidden = true; elEstado.textContent = ''; elEstado.className = 'estado';
      return;
    }
    if (dados.tipo === 'falha-ao-carregar') {
      btRodar.textContent = 'Indisponível';
      mostrarSaida('Não foi possível carregar o interpretador.\n' + dados.mensagem, true);
      return;
    }
    if (dados.tipo === 'resultado') {
      terminarExecucao();
      mostrarSaida(dados.saida, /Erro (de|lexico|interno)/.test(dados.saida));
      elEstado.textContent = dados.ms >= 200 ? 'concluído em ' + dados.ms + ' ms' : 'concluído';
      return;
    }
    if (dados.tipo === 'abortou') {
      /* Depois de um abort do wasm o modulo nao e confiavel: troca por um novo. */
      mostrarSaida('A execução foi interrompida pelo navegador.\n\n' + dados.mensagem, true);
      reiniciarWorker('interpretador reiniciado');
    }
  };
  worker.onerror = () => reiniciarWorker('interpretador reiniciado');
}

function reiniciarWorker(mensagem) {
  if (worker) worker.terminate();
  executando = false;
  btRodar.disabled = true; btRodar.textContent = 'Reiniciando…';
  btParar.hidden = true;
  elEstado.textContent = mensagem || ''; elEstado.className = 'estado';
  criarWorker();
}

function terminarExecucao() {
  executando = false;
  btRodar.disabled = false; btRodar.textContent = 'Executar';
  btParar.hidden = true;
  elEstado.className = 'estado';
}

function executar() {
  if (!pronto || executando) return;
  executando = true;
  btRodar.disabled = true; btRodar.textContent = 'Executando…';
  btParar.hidden = false;
  elEstado.textContent = 'em execução'; elEstado.className = 'estado rodando';
  elSaida.className = 'saida';
  worker.postMessage({ codigo: elCodigo.value, entrada: elEntrada.value });
}

/* O motivo de tudo isto rodar num worker: aqui a execucao morre de verdade,
   mesmo dentro de `enquanto verdadeiro { }`. */
function parar() {
  if (!executando) return;
  mostrarSaida('Execução interrompida por você.', false);
  reiniciarWorker('interrompido');
}

function mostrarSaida(texto, ehErro) {
  elSaida.textContent = texto;
  elSaida.className = ehErro ? 'saida erro' : 'saida';
}

/* ---------- exemplos, rascunho e link ---------- */

function montarExemplos() {
  const seletor = $('exemplos');
  for (const nome of Object.keys(EXEMPLOS)) {
    const opcao = document.createElement('option');
    opcao.value = nome; opcao.textContent = nome;
    seletor.appendChild(opcao);
  }
  seletor.onchange = () => {
    if (!seletor.value) return;
    elCodigo.value = EXEMPLOS[seletor.value];
    if (seletor.value !== 'Lendo entrada') elEntrada.value = '';
    else elEntrada.value = '20\n22\n';
    seletor.value = ''; redesenhar(); elSaida.textContent = '';
  };
}

/* O codigo vai no fragmento da URL (#), que nunca chega a servidor nenhum. */
const paraBase64 = (t) => btoa(String.fromCharCode(...new TextEncoder().encode(t)));
const deBase64 = (t) => new TextDecoder().decode(Uint8Array.from(atob(t), (c) => c.charCodeAt(0)));

function codigoInicial() {
  const hash = location.hash.slice(1);
  if (hash.startsWith('c=')) {
    try { return deBase64(decodeURIComponent(hash.slice(2))); } catch (_) { /* link corrompido */ }
  }
  try {
    const salvo = localStorage.getItem(CHAVE_RASCUNHO);
    if (salvo !== null && salvo.trim() !== '') return salvo;
  } catch (_) { /* modo privado */ }
  return CODIGO_INICIAL;
}

function compartilhar() {
  const url = location.origin + location.pathname + '#c=' + encodeURIComponent(paraBase64(elCodigo.value));
  const avisar = (texto) => {
    const antes = $('compartilhar').textContent;
    $('compartilhar').textContent = texto;
    setTimeout(() => { $('compartilhar').textContent = antes; }, 1600);
  };
  if (navigator.clipboard) {
    navigator.clipboard.writeText(url).then(() => avisar('Link copiado'), () => {
      location.hash = 'c=' + encodeURIComponent(paraBase64(elCodigo.value));
      avisar('Link na barra');
    });
  } else {
    location.hash = 'c=' + encodeURIComponent(paraBase64(elCodigo.value));
    avisar('Link na barra');
  }
}

/* ---------- ligacao ---------- */

elCodigo.value = codigoInicial();
elCodigo.addEventListener('input', redesenhar);
elCodigo.addEventListener('scroll', sincronizarRolagem);
elCodigo.addEventListener('keydown', tratarTecla);
btRodar.addEventListener('click', executar);
btParar.addEventListener('click', parar);
$('limpar').addEventListener('click', () => { elSaida.textContent = ''; elSaida.className = 'saida'; });
$('compartilhar').addEventListener('click', compartilhar);

/* Ctrl/Cmd+Enter executa de dentro do editor. */
document.addEventListener('keydown', (evento) => {
  if ((evento.ctrlKey || evento.metaKey) && evento.key === 'Enter') { evento.preventDefault(); executar(); }
});

montarExemplos();
redesenhar();
criarWorker();
