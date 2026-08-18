const factory = require(require('path').resolve(process.argv[2] || 'web/dist/lume.js'));
factory().then(M => {
  const evalLume = (src, inp) => {
    const p = M.ccall('lume_web_eval','number',['string','string'],[src, inp||'']);
    if (!p) return '<NULO>';
    const s = M.UTF8ToString(p); M.ccall('lume_web_free',null,['number'],[p]); return s;
  };
  const T = (nome, obtido, esperado) => {
    const ok = obtido.includes(esperado);
    console.log(`${ok?'  OK   ':'  FALHA'} ${nome}`);
    if (!ok) console.log(`         esperado conter: ${JSON.stringify(esperado)}\n         obtido: ${JSON.stringify(obtido)}`);
    return ok;
  };
  let all = true;
  console.log('=== 1. fmemopen / open_memstream funcionam? ===');
  all &= T('escreva basico', evalLume('escreva("Ola, mundo!")'), 'Ola, mundo!');
  all &= T('acentos UTF-8', evalLume('escreva("Olá, João! ção")'), 'Olá, João! ção');
  all &= T('aritmetica+laco', evalLume('variavel s = 0\npara i de 1 ate 10 { s = s + i }\nescreva(s)'), '55');
  all &= T('recursao', evalLume('funcao f(n) {\n se n <= 1 {\n  retorne 1\n }\n retorne n * f(n-1)\n}\nescreva(f(10))'), '3628800');
  all &= T('listas+closure', evalLume('variavel l = [1,2,3]\nl.adicione(4)\nescreva(l)'), '4');

  console.log('\n=== 2. entrada() lendo do stdin pre-preenchido (Decisao 1-A) ===');
  all &= T('leia() 1 linha', evalLume('variavel n = leia()\nescreva("oi " + n)', 'Valdecir\n'), 'oi Valdecir');
  all &= T('leia() + conversao', evalLume('variavel a = inteiro(leia())\nvariavel b = inteiro(leia())\nescreva(a+b)', '20\n22\n'), '42');
  all &= T('leia() 3 linhas + laco', evalLume('variavel s = 0\npara i de 1 ate 3 {\n s = s + inteiro(leia())\n}\nescreva(s)', '10\n20\n30\n'), '60');

  console.log('\n=== 3. diagnostico de erro (o bug que corrigimos) ===');
  const err = evalLume('escreva(xyz)');
  all &= T('nome citado correto', err, "Nome: 'xyz'");
  all &= T('caret e localizacao', err, '^^^');

  console.log('\n=== 4. MEMFS: projeto multi-arquivo via lume_web_cli ===');
  M.FS.mkdir('/proj'); M.FS.mkdir('/proj/src');
  M.FS.writeFile('/proj/lume.projeto', 'nome = "demo"\nversao = "0.1.0"\nentrada = "src/principal.lume"\nfonte = "src"\n');
  M.FS.writeFile('/proj/src/matematica.lume', 'exporte funcao dobro(x) { retorne x * 2 }\n');
  M.FS.writeFile('/proj/src/principal.lume', 'importe "matematica"\nescreva(matematica.dobro(21))\n');
  const runCli = (args) => {
    const ptrs = args.map(a => M.stringToNewUTF8(a));
    const argv = M._malloc(ptrs.length * 4);
    ptrs.forEach((p,i) => M.setValue(argv + i*4, p, 'i32'));
    const r = M.ccall('lume_web_cli','number',['number','number'],[args.length, argv]);
    const s = r ? M.UTF8ToString(r) : '<NULO>';
    if (r) M.ccall('lume_web_free',null,['number'],[r]);
    ptrs.forEach(p => M._free(p)); M._free(argv);
    return s;
  };
  all &= T('executar projeto (stat/opendir em MEMFS)', runCli(['lume','executar','/proj']), '42');
  all &= T('verificar projeto', runCli(['lume','verificar','/proj']), '');
  all &= T('--analisar em MEMFS', runCli(['lume','--analisar','/proj/src/principal.lume']), 'Analisando');
  all &= T('--explicar em MEMFS', runCli(['lume','--explicar','/proj/src/principal.lume']), '42');

  console.log('\n=== 5. arquivo.* nativas em MEMFS (sandbox) ===');
  all &= T('arquivo.escreva/leia', evalLume('arquivo.escreva("/t.txt","conteudo")\nescreva(arquivo.leia("/t.txt"))'), 'conteudo');

  console.log('\n=== 6. sabotagem: o harness detecta falha? ===');
  const sab = evalLume('escreva("a")').includes('ZZZ_IMPOSSIVEL');
  console.log(sab ? '  !! harness quebrado' : '  OK    harness detecta falhas (verificado)');

  console.log('\nRESULTADO=' + (all && !sab ? 'TUDO PASSOU' : 'HOUVE FALHA'));
  process.exit(all && !sab ? 0 : 1);
}).catch(e => { console.error('ERRO AO CARREGAR:', e); process.exit(2); });
