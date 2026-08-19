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
  'Olá, mundo': { codigo: 'variavel nome = "mundo"\nescreva("Olá, " + nome + "!")\n' },
  'Decisões': { codigo: 'variavel idade = 20\n\nse idade >= 18 {\n  escreva("maior de idade")\n} senao {\n  escreva("menor de idade")\n}\n' },
  'Repetição': { codigo: 'variavel soma = 0\n\npara i de 1 ate 10 {\n  soma = soma + i\n}\n\nescreva("A soma de 1 a 10 é " + texto(soma))\n' },
  'Listas': { codigo: 'variavel notas = [7, 9, 6, 10]\nvariavel total = 0\n\npara i de 0 ate tamanho(notas) - 1 {\n  total = total + notas[i]\n}\n\nescreva("Média: " + texto(total / tamanho(notas)))\n' },
  'Funções': { codigo: 'funcao dobro(x) {\n  retorne x * 2\n}\n\nfuncao saudacao(nome) {\n  retorne "Olá, " + nome + "!"\n}\n\nescreva(dobro(21))\nescreva(saudacao("Lume"))\n' },
  'Recursão': { codigo: '// Toda recursão precisa de um caso base.\nfuncao fatorial(n) {\n  se n <= 1 {\n    retorne 1\n  }\n  retorne n * fatorial(n - 1)\n}\n\nescreva(fatorial(10))\n' },
  'Lendo entrada': {
    codigo: '// Os números vêm do painel "Entrada do programa",\n// uma linha para cada leia().\nescreva("Digite dois números:")\n\nvariavel a = inteiro(leia())\nvariavel b = inteiro(leia())\n\nescreva("A soma é " + texto(a + b))\n',
    entrada: '20\n22\n'
  },
  'Biblioteca padrão': { codigo: 'importe "lume/matematica"\nimporte "lume/texto"\n\nescreva(matematica.raiz(144))\nescreva(texto.maiusculo("lume"))\n' }
};

const CODIGO_INICIAL = EXEMPLOS['Olá, mundo'].codigo;
const CHAVE_RASCUNHO = 'lume:rascunho';

const $ = (id) => document.getElementById(id);
const elCodigo = $('codigo'), elRealce = $('realce').firstElementChild;
const elNumeros = $('numeros'), elEntrada = $('entrada'), elSaida = $('saida');
const btRodar = $('rodar'), btParar = $('parar'), elEstado = $('estado');
const btDepurar = $('depurar'), elDepurador = $('depurador'), elTempo = $('linha-do-tempo');
const elDescricao = $('descricao-passo'), elQuadros = $('quadros');
const elLinhaAtual = $('linha-atual');

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

let worker = null, pronto = false, executando = false, modoAtual = 'normal';

function criarWorker() {
  pronto = false;
  worker = new Worker('worker.js');
  worker.onmessage = (evento) => {
    const dados = evento.data;
    if (dados.tipo === 'pronto') {
      pronto = true; executando = false;
      btRodar.disabled = false; btRodar.textContent = 'Executar';
      btDepurar.disabled = false;
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
      if (dados.modo === 'passo') abrirDepurador(dados);
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
  btRodar.disabled = true; btDepurar.disabled = true;
  btRodar.textContent = 'Reiniciando…';
  btParar.hidden = true;
  elEstado.textContent = mensagem || ''; elEstado.className = 'estado';
  criarWorker();
}

function terminarExecucao() {
  executando = false;
  btRodar.disabled = false; btRodar.textContent = 'Executar';
  btDepurar.disabled = false; btParar.hidden = true;
  elEstado.className = 'estado';
}

function executar(modo) {
  if (!pronto || executando) return;
  modoAtual = modo === 'passo' ? 'passo' : 'normal';
  executando = true;
  fecharDepurador();
  btRodar.disabled = true; btDepurar.disabled = true;
  btRodar.textContent = modoAtual === 'passo' ? 'Gravando…' : 'Executando…';
  btParar.hidden = false;
  elEstado.textContent = 'em execução'; elEstado.className = 'estado rodando';
  /* Sem isto, a saida da execucao anterior fica na tela durante a nova — e um
     programa que trava parece ter respondido o que o anterior respondeu. */
  limparSaida();
  worker.postMessage({ codigo: elCodigo.value, entrada: elEntrada.value, modo: modoAtual });
}

/* O motivo de tudo isto rodar num worker: aqui a execucao morre de verdade,
   mesmo dentro de `enquanto verdadeiro { }`. */
function parar() {
  if (!executando) return;
  mostrarSaida('Execução interrompida por você.', false);
  reiniciarWorker('interrompido');
}

function limparSaida() { elSaida.textContent = ''; elSaida.className = 'saida'; }

function mostrarSaida(texto, ehErro) {
  elSaida.textContent = texto;
  elSaida.className = ehErro ? 'saida erro' : 'saida';
}

/* ---------- depurador visual ---------- */

/* A fita inteira vem de uma execucao so (src/web/lume_trace.c), entao dá para
   andar para tras — coisa que o `--passo` da CLI nao faz, porque la o programa
   avanca junto com o aluno. */
let fita = [], passo = 0;

/* Frases em vez de nomes de evento: quem esta aprendendo nao deveria precisar
   traduzir "TRACE_WHILE_ITERATION" na cabeca. */
function descrever(evento) {
  const nome = evento.n ? '<code>' + escaparHtml(evento.n) + '</code>' : '';
  const valor = (v) => '<code>' + escaparHtml(v === undefined ? '?' : v) + '</code>';
  switch (evento.t) {
    case 'inicio': return 'O programa começou.';
    case 'fim': return 'O programa terminou.';
    case 'declara-variavel': return 'Criou a variável ' + nome + ' valendo ' + valor(evento.d) + '.';
    case 'declara-constante': return 'Criou a constante ' + nome + ' valendo ' + valor(evento.d) + '.';
    case 'declara-funcao': return 'Registrou a função ' + nome + '.';
    case 'atribui': return 'Mudou ' + nome + ' de ' + valor(evento.a) + ' para ' + valor(evento.d) + '.';
    case 'condicao-se': return 'Avaliou a condição do <code>se</code>: deu ' +
      valor(evento.b ? 'verdadeiro' : 'falso') + ', então ' + (evento.b ? 'entrou no bloco.' : 'pulou o bloco.');
    case 'condicao-enquanto': return 'Testou a condição do <code>enquanto</code>: deu ' +
      valor(evento.b ? 'verdadeiro' : 'falso') + (evento.b ? ', vai repetir.' : ', vai parar.');
    case 'volta-enquanto': return 'Volta ' + valor(evento.i) + ' do <code>enquanto</code>.';
    case 'fim-enquanto': return 'Terminou o <code>enquanto</code>.';
    case 'inicio-para': return 'Começou o <code>para</code> com ' + nome + '.';
    case 'volta-para': return 'Volta ' + valor(evento.i) + ' do <code>para</code>, com ' + nome + ' valendo ' + valor(evento.d) + '.';
    case 'fim-para': return 'Terminou o <code>para</code>.';
    case 'chama-funcao': return 'Chamou ' + nome + '.';
    case 'entra-funcao': return 'Entrou em ' + nome + ' — agora são ' + valor(evento.p) + ' chamada(s) empilhada(s).';
    case 'retorna-funcao': return nome + ' devolveu ' + valor(evento.d) + '.';
    case 'chama-nativa': return 'Chamou a função pronta ' + nome + '.';
    case 'escreve': return 'Escreveu na saída.';
    case 'cria-lista': return 'Criou uma lista.';
    case 'le-indice': return 'Leu a posição ' + valor(evento.x) + ' e achou ' + valor(evento.d) + '.';
    case 'escreve-indice': return 'Guardou ' + valor(evento.d) + ' na posição ' + valor(evento.x) + '.';
    case 'adiciona-lista': return 'Acrescentou ' + valor(evento.d) + ' à lista.';
    case 'remove-lista': return 'Tirou ' + valor(evento.a) + ' da lista.';
    case 'importa-modulo': return 'Importou ' + nome + '.';
    case 'modulo-carregado': return 'Carregou o módulo ' + nome + '.';
    default: return evento.t;
  }
}

const escaparHtml = (t) => String(t).replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));

function abrirDepurador(dados) {
  fita = dados.eventos || [];
  if (fita.length === 0) { elEstado.textContent = 'nada para percorrer'; return; }
  elDepurador.hidden = false;
  elTempo.max = String(fita.length - 1);
  elTempo.value = '0';
  if (dados.truncado) {
    elEstado.textContent = 'gravados os primeiros ' + fita.length + ' passos de ' + dados.total;
  } else {
    elEstado.textContent = fita.length + ' passos gravados';
  }
  irPara(0);
}

function fecharDepurador() {
  elDepurador.hidden = true;
  elLinhaAtual.hidden = true;
  fita = []; passo = 0;
}

function irPara(indice) {
  if (fita.length === 0) return;
  passo = Math.max(0, Math.min(indice, fita.length - 1));
  elTempo.value = String(passo);
  const evento = fita[passo];
  elDescricao.innerHTML = '<span class="passo-n">' + (passo + 1) + '/' + fita.length + '</span> &nbsp; ' + descrever(evento);
  destacarLinha(evento.l);
  mostrarQuadros(evento, passo > 0 ? fita[passo - 1] : null);
}

/* Posiciona a faixa pela altura de linha real, medida do elemento de numeros —
   assim o destaque nao sai do lugar se a fonte ou o zoom mudarem. */
function destacarLinha(linha) {
  if (!linha || linha < 1) { elLinhaAtual.hidden = true; return; }
  const total = elCodigo.value.split('\n').length;
  if (linha > total) { elLinhaAtual.hidden = true; return; }
  const estilo = getComputedStyle(elCodigo);
  const alturaLinha = parseFloat(estilo.lineHeight);
  const topoTexto = parseFloat(estilo.paddingTop);
  elLinhaAtual.style.top = (topoTexto + (linha - 1) * alturaLinha - elCodigo.scrollTop) + 'px';
  elLinhaAtual.style.height = alturaLinha + 'px';
  elLinhaAtual.hidden = false;
  /* Traz a linha para a area visivel se o programa for maior que o editor. */
  const alvo = topoTexto + (linha - 1) * alturaLinha;
  if (alvo < elCodigo.scrollTop || alvo > elCodigo.scrollTop + elCodigo.clientHeight - alturaLinha * 2) {
    elCodigo.scrollTop = Math.max(0, alvo - elCodigo.clientHeight / 2);
    sincronizarRolagem();
    elLinhaAtual.style.top = (topoTexto + (linha - 1) * alturaLinha - elCodigo.scrollTop) + 'px';
  }
}

/* Um bloco por chamada em andamento, do mais externo para o mais interno — como
   o Python Tutor faz. Uma lista unica seria enganosa em recursao: em
   fatorial(4) o aluno veria um `n` so, parecendo que a variavel foi
   sobrescrita, quando na verdade existem quatro, um por chamada. */
function mostrarQuadros(evento, anterior) {
  const quadros = evento.v || [];
  if (quadros.length === 0) { elQuadros.innerHTML = '<span class="vazio">nada em execução</span>'; return; }
  /* Compara com o passo anterior pelo par (posição do quadro, nome) para
     destacar o que mudou sem confundir variaveis homonimas de quadros
     diferentes — o caso exato da recursao. */
  const antes = new Map();
  (anterior && anterior.v ? anterior.v : []).forEach((quadro, i) => {
    (quadro.vars || []).forEach((v) => antes.set(i + '\u0000' + v.n, v.v));
  });
  elQuadros.innerHTML = quadros.map((quadro, i) => {
    const atual = i === quadros.length - 1 && quadros.length > 1;
    const vars = (quadro.vars || []).map((v) => {
      const chave = i + '\u0000' + v.n;
      const mudou = antes.has(chave) ? antes.get(chave) !== v.v : anterior !== null;
      return '<div class="par' + (mudou ? ' mudou' : '') + '">' +
        '<span class="nome">' + escaparHtml(v.n) + '</span>' +
        '<span class="valor">' + escaparHtml(v.v) + '</span></div>';
    }).join('') || '<div class="par vazio">sem variáveis</div>';
    return '<div class="quadro' + (atual ? ' quadro-atual' : '') + '">' +
      '<div class="quadro-titulo">' + escaparHtml(quadro.q) +
      (atual ? '<span class="etiqueta">executando</span>' : '') + '</div>' +
      vars + '</div>';
  }).join('');
}

elTempo.addEventListener('input', () => irPara(Number(elTempo.value)));
$('passo-anterior').addEventListener('click', () => irPara(passo - 1));
$('passo-proximo').addEventListener('click', () => irPara(passo + 1));
$('passo-inicio').addEventListener('click', () => irPara(0));
$('passo-fim').addEventListener('click', () => irPara(fita.length - 1));
$('fechar-passo').addEventListener('click', fecharDepurador);
document.addEventListener('keydown', (evento) => {
  if (elDepurador.hidden) return;
  if (document.activeElement === elCodigo || document.activeElement === elEntrada) return;
  if (evento.key === 'ArrowLeft') { evento.preventDefault(); irPara(passo - 1); }
  if (evento.key === 'ArrowRight') { evento.preventDefault(); irPara(passo + 1); }
});

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
    const exemplo = EXEMPLOS[seletor.value];
    elCodigo.value = exemplo.codigo;
    /* A entrada de exemplo vive junto do codigo: se o rotulo do menu mudar, os
       dois nao podem sair de sincronia. */
    elEntrada.value = exemplo.entrada || '';
    seletor.value = ''; redesenhar(); limparSaida();
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
elCodigo.addEventListener('input', () => { redesenhar(); if (!elDepurador.hidden) fecharDepurador(); });
elCodigo.addEventListener('scroll', sincronizarRolagem);
elCodigo.addEventListener('keydown', tratarTecla);
btRodar.addEventListener('click', () => executar('normal'));
btDepurar.addEventListener('click', () => executar('passo'));
btParar.addEventListener('click', parar);
$('limpar').addEventListener('click', limparSaida);
$('compartilhar').addEventListener('click', compartilhar);

/* Ctrl/Cmd+Enter executa de dentro do editor. */
document.addEventListener('keydown', (evento) => {
  if ((evento.ctrlKey || evento.metaKey) && evento.key === 'Enter') {
    evento.preventDefault(); executar(evento.shiftKey ? 'passo' : 'normal');
  }
});

montarExemplos();
redesenhar();
criarWorker();
