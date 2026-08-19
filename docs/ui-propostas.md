# Propostas para a interface do playground

Data: 2026-08-19 · Base: branch `web-wasm`, playground rodando em `localhost:8080`

Documento de decisão, não de implementação. Cada proposta traz a evidência que a
motivou — o que eu observei, onde, e o que isso implica para a Lume.

---

## 1. O benchmark

Visitei três sistemas e li o DOM deles, em vez de me basear em reputação.

### 1.1 Portugol Webstudio — [portugol.dev](https://portugol.dev/)

O par mais direto que existe: linguagem educacional em português, brasileira,
rodando no navegador, mesmo público. É a régua justa.

**O que tem:** IDE completa com abas de arquivo, editor Monaco, execução em Web
Worker (mesma decisão de arquitetura que tomei), biblioteca de exemplos, ajuda,
configurações, painel "Novidades" com changelog datado, e canais de dúvidas e
sugestões ligados ao GitHub Discussions.

**O que não tem: depurador passo a passo.** Executa e mostra a saída.

Isso reposiciona a conversa inteira. Não estamos correndo atrás do Portugol em
funcionalidade de IDE — estamos à frente exatamente na parte que **ensina**. O
esforço de UI deve proteger e ampliar essa vantagem, não gastar orçamento
replicando abas e gerenciador de arquivos.

Vale copiar duas coisas modestas: o painel de **novidades datado** (dá sinal de
vida ao projeto) e o **canal explícito de dúvidas** (um aluno travado precisa
saber para onde ir).

### 1.2 Python Tutor — [pythontutor.com](https://pythontutor.com/)

O padrão-ouro de visualização de execução, usado em milhares de cursos.

**O que observei no DOM:**

- **Duas marcações de linha simultâneas**, com legenda visível na tela:
  `line that just executed` e `next line to execute`.
- Navegação `<< First`, `< Prev`, `Next >`, `Last >>` e o contador `Step 1 of 18`.
- **Painel `Frames` com as variáveis agrupadas por chamada.** Em `fatorial(4)`,
  no meio da recursão, o painel mostrava literalmente:

  ```
  fatorial
    n   4
  fatorial
    n   3
  ```

- Painel `Objects` à direita, com o heap e setas ligando referências.
- Modos separados: visualizar é uma tela, editar é outra ("Edit this code").
- "Teacher Mode" com acesso gratuito para professores.

### 1.3 Rust Playground — [play.rust-lang.org](https://play.rust-lang.org/)

A régua de ergonomia de playground.

**O que observei:** barra superior enxuta com **um** botão primário forte (`RUN`),
e o resto secundário (`DEBUG`/`STABLE`, `SHARE`, `TOOLS`, `CONFIG`, ajuda). O
editor ocupa a tela toda; a saída aparece embaixo só depois de executar.
`SHARE` é cidadão de primeira classe, não item escondido em menu.

**O que não vale copiar:** o `Objects`/heap com setas do Python Tutor. A Lume não
tem referências observáveis por um iniciante — listas são valores, não há lição
de aliasing para dar, e o `value_format` já imprime a lista inteira. Desenhar um
grafo de heap seria imitar o benchmark em vez de servir a linguagem.

---

## 2. O problema mais sério: o inspetor está errado em recursão

Não é polimento. É exibição incorreta na funcionalidade que eu apresentei como
o diferencial.

Extraí a fita real de `fatorial(4)` (21 eventos) e olhei o que o inspetor mostra:

```
   # | linha | tipo             | prof | variáveis exibidas hoje
  ---+-------+------------------+------+-------------------------
   4 |   8   | entra-funcao     |  1   | n=4
   7 |   5   | entra-funcao     |  2   | n=3
  10 |   5   | entra-funcao     |  3   | n=2
  13 |   5   | entra-funcao     |  4   | n=1     <- só este
```

No passo 13, com quatro chamadas empilhadas, o aluno vê **um único `n`**. Os
outros três sumiram. O Python Tutor, no mesmo programa, mostra os quatro.

**Por quê:** o ambiente de uma chamada tem como pai o ambiente de *fechamento*
(o global), não o do chamador. Então `environment_visit_current` subindo pelos
pais nunca alcança os quadros de fora. Não é bug do interpretador — é a
semântica correta de escopo léxico. O que falta é o coletor guardar a pilha.

**A recursão é o módulo 7 do currículo e o conceito em que iniciantes mais
travam.** Mostrar um `n` só quando existem quatro é pior do que não mostrar nada,
porque parece que a variável está sendo sobrescrita — exatamente o mal-entendido
que a visualização deveria desfazer.

**Correção viável inteiramente dentro de `src/web/lume_trace.c`:** manter uma
pilha própria de `const Environment *`, empilhando em `entra-funcao` e
desempilhando em `retorna-funcao`. Os ambientes continuam vivos enquanto
aninhados, então os ponteiros são válidos. A cada evento, serializar todos os
quadros:

```json
"v": [
  { "q": "principal", "vars": [...] },
  { "q": "fatorial", "vars": [{"n":"n","v":"4"}] },
  { "q": "fatorial", "vars": [{"n":"n","v":"3"}] }
]
```

Custo estimado: ~40 linhas em C, mais a renderização agrupada. É a proposta de
maior valor pedagógico por linha escrita.

---

## 3. Propostas, ordenadas por valor de ensino

### P1 — Variáveis agrupadas por quadro da pilha ⭐ — ✅ FEITO

Descrito acima. Corrige exibição errada, não adiciona enfeite.

Junto vem uma consequência de layout: o painel de variáveis e o de pilha viram
**um só**, porque a pilha passa a ser o eixo de organização das variáveis — que é
como o Python Tutor faz. Sobra espaço em vez de faltar.

### P2 — Erros marcados no editor, não só no painel de saída — ✅ FEITO

Hoje o diagnóstico aparece como texto na saída. O aluno lê `linha 2, coluna 9` e
precisa contar linhas com o dedo.

Todo `LumeError` já carrega `span.start.line/column` e `span.end` — é o que o
`diagnostic_render` usa para desenhar o caret. **O dado já está lá**, só não
chega ao editor.

Proposta: sublinhado ondulado vermelho no trecho exato, com a mensagem e a dica
aparecendo ao passar o mouse ou ao clicar. A saída continua mostrando o
diagnóstico completo — o sublinhado é adicional, não substituto.

Nenhum dos três sistemas que olhei faz isso bem em português. É a maior lacuna
de experiência para iniciantes e a mais barata de fechar com o que já existe.

### P3 — Primeira visita não pode ser uma caixa de código — ✅ FEITO

Hoje quem chega vê um editor com um "Olá, mundo" e dois botões. Não há nada
dizendo o que é a Lume, o que dá para fazer, nem que existe um modo passo a passo
— que é justamente a razão de o site existir.

O Portugol resolve com um painel inicial antes do editor (Novo / Abrir / Exemplo
/ Ajuda). Dá para resolver mais barato:

- os exemplos saem do `<select>` e viram **cartões visíveis**, com uma linha
  dizendo o que cada um ensina;
- uma frase única acima do editor: *"clique em Passo a passo para ver o programa
  executar linha a linha"*;
- atalhos de teclado visíveis em algum canto, em vez de secretos.

### P4 — Espaço: hoje o depurador e a saída competem

Ao abrir o passo a passo, o depurador nasce embaixo do editor e a saída fica na
coluna lateral, longe da linha do tempo. O olho salta.

O Python Tutor separa em dois modos (editar / visualizar) e ganha a tela inteira
para cada um. Proposta mais leve para a Lume: ao entrar em passo a passo, a
coluna lateral passa a mostrar **estado da execução** (quadros + saída até
aquele passo), e o editor vira leitura com a linha destacada. Sair volta ao
layout normal. Sem tela nova, sem rota nova.

Um detalhe que a fita revelou e vale resolver junto: a saída hoje aparece toda
de uma vez, mesmo no passo a passo. Como existe o evento `escreve`, dá para
mostrar **a saída até o passo atual** — o aluno vê o texto aparecendo conforme
avança.

### P5 — Marcação de "linha atual" continua única, e isso é decisão, não preguiça

Testei a hipótese de copiar as duas cores do Python Tutor derivando a "próxima
linha" do evento seguinte da fita. Não sai limpo:

- em `condicao-se` funciona bem (passo 5: linha 2, próxima 5 — a condição foi
  falsa e o fluxo pulou o bloco, exatamente certo);
- em `retorna-funcao` quebra, porque o span do evento é o **local da chamada**,
  não o `retorne`;
- a linha 3 (`retorne 1`, o caso base) **nunca é destacada** — não há evento
  próprio para o `retorne` dentro do corpo.

Duas cores que às vezes mentem são piores que uma cor honesta. Recomendo manter
a linha única somada à frase descritiva, que já diz em português o que aconteceu
— provavelmente mais claro para um iniciante do que duas cores que ele precisa
aprender a distinguir.

Fica registrado que emitir um evento para `retorne` no interpretador resolveria
os dois problemas de uma vez. Seria mudança no upstream, e portanto candidata a
PR próprio, não a este trabalho.

### P6 — "Veja por dentro": tokens e árvore sintática

O maior diferencial possível, e o maior trabalho. Precedente: o Compiler
Explorer. A ideia é um painel mostrando como o texto vira tokens e como os
tokens viram árvore — a aula de "como a linguagem que você está aprendendo é
feita".

**Deixo por último de propósito.** É superfície nova, exige expor lexer e parser
ao JavaScript, e serve mais ao curso (Fase 2) do que ao playground de hoje.

### P7 — Celular: reconhecer, não projetar

O layout já colapsa para uma coluna. Um aluno digitando `variavel` numa tela de
telefone não é o caso de uso, e fingir que é custa complexidade de layout.
Proposta: garantir que **ler e executar** funcionem no celular — para abrir um
link que um colega mandou — e parar por aí.

---

## 4. Fatia recomendada — ✅ ENTREGUE

**P1 + P2 + P3 foram implementados nessa ordem**, cada um validado antes de o
seguinte começar: `make` sem warnings, `make test` 15/15, `make sanitize` 15/15
limpo, o wrapper sob ASan/UBSan, e verificação no navegador.

Dois aprendizados do caminho valem registro:

- **O primeiro teste do P1 não pegava o bug.** Ele procurava os quatro valores
  de `n` na fita inteira, e eles apareciam mesmo com o defeito — em eventos
  diferentes. Só depois de recortar **um evento** e exigir quatro quadros dentro
  dele o teste ficou vermelho sem a correção. Um teste que não falha com o bug
  presente não vale nada.
- **P2 não precisou de dado novo.** O `span` já estava em todo `LumeError`, para
  o caret do terminal. Era ligar o que estava desligado.

O raciocínio original da escolha:

1. **P1** porque corrige algo errado, não porque enfeita. Enquanto não for feito,
   o passo a passo mente em recursão.
2. **P2** porque é a maior lacuna para iniciantes e o dado já existe — é ligar o
   que está desligado.
3. **P3** porque hoje o visitante não descobre sozinho o recurso que justifica o
   site.

P4 vem naturalmente junto com P1 (o painel unificado já reorganiza o espaço).
P5 é uma decisão registrada, sem trabalho. P6 e P7 ficam para depois.

---

## 5. Nota sobre URLs

O compartilhamento usa fragmento (`#c=...`), que nunca chega ao servidor. Isso
sobreviveu intacto ao episódio do 404 em `localhost:8080/#/` — o fragmento não
teve participação nenhuma no problema. Se alguma proposta futura pedir rotas
(trilhas de lição, por exemplo), manter fragmento em vez de caminhos evita
depender de configuração do servidor.
