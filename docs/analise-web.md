# Lume no browser — análise de viabilidade e plano em duas fases

Data: 2026-08-18 · Base analisada: `main` @ `eaa3eea`

---

## Sumário executivo

**Portar a Lume para o browser via WebAssembly é altamente viável — provavelmente o
port mais fácil que um interpretador em C pode ter.** A superfície de plataforma é
mínima, a I/O já está abstraída atrás de uma struct, e `session.h` já é, na prática,
a API de embedding que o playground precisa. A Fase 1 exige **zero mudanças** nos
`.c` existentes: apenas um wrapper novo e um alvo de build.

Encontrei **um use-after-free real e reproduzível no caminho de erro** — a mensagem
que mais importa para um iniciante saía corrompida (§1.2). Já está **corrigido e
enviado ao upstream** ([PR #1](https://github.com/Ultra332/Lume/pull/1)), com
`make sanitize` verde nas 15 suítes pela primeira vez.

A Fase 2 (programa de estudos) é **majoritariamente trabalho de conteúdo, não de
engenharia**, e é a maior das duas fases.

---

## 1. Estado atual do repositório (verificado, não lido da documentação)

### 1.1 O que está genuinamente bom

Verificado empiricamente nesta análise:

| Checagem | Resultado |
|---|---|
| `make clean && make` com `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` | **0 warnings** |
| `make test` — 15 suítes | **15/15 passam** (345 asserções via macro `CHECK`) |
| `main()` das suítes retorna != 0 em falha | **Sim, todas as 15** — o verde do CI é real |
| 20 exemplos em `exemplos/` executados | **Todos produzem a saída esperada** |
| Dependências externas | **Nenhuma** além da libc + `-lm` |

A arquitetura é limpa e bem separada: `source → lexer → parser → ast → analyzer →
interpreter`, com `environment`, `module`, `project`, `dependency` e
`lume_stdlib` ao redor. Isso é o que torna o port viável.

### 1.2 Bug de memória no caminho de erro — **CORRIGIDO**

> **Status (2026-08-18):** corrigido e enviado ao upstream em
> [Ultra332/Lume#1](https://github.com/Ultra332/Lume/pull/1). Com a correção,
> `make sanitize` passa limpo nas 15 suítes pela primeira vez, confirmado tanto
> em macOS/clang quanto no CI em Ubuntu/GCC. O texto abaixo fica como registro do
> diagnóstico.

`make sanitize` está **vermelho no `main`**, e o CI nunca o executa
(`.github/workflows/ci.yml` roda só `make clean && make && make test`).

Ao rodar o sanitizer encontrei **dois problemas distintos**:

**(a) Bug no teste — trivial.** `tests/test_diagnostics.c:7` passa `28U` como
comprimento de um literal de 25 bytes:

```c
source_from_bytes(&source, "exemplo.lume", "variavel x = 10\nx + \"oi\"\n", 28U)
```

→ `global-buffer-overflow` em `memory_copy_string` (`src/memory.c:32`). A biblioteca
está correta; o teste mentiu o tamanho. Correção: `25U` ou `strlen(...)`.
Como o ASan aborta no primeiro erro, tudo depois de `test_diagnostics` (suíte 8 de 15)
nunca havia sido verificado.

**(b) Bug real no interpretador — `heap-use-after-free`.** Corrigindo (a) e
re-executando, aparece o problema sério:

```
READ of size 7 ... heap-use-after-free
  #4 diagnostic_render        src/diagnostic.c:22
  #5 run_text                 src/cli.c:21
freed by:
  #1 expr_free                src/ast.c:77
  #5 program_free             src/ast.c:124
  #6 session_execute_internal src/session.c:113
```

O que acontece:

1. `session_execute_internal` falha e faz `program_free(program)` (`session.c:113`),
   liberando a AST inteira.
2. `run_text` (`cli.c:20-21`) chama **depois** `diagnostic_render(...)`.
3. `diagnostic.c:22` faz `fwrite(error->subject, 1U, error->subject_length, stream)`
   — e `error->subject` aponta para dentro da AST já liberada.

**Não é teórico e não é raro. É o erro mais comum de um iniciante:**

```sh
$ echo 'escreva(xyz)' > erro.lume && lume erro.lume
erro.lume:1:9

escreva(xyz)
        ^^^

Erro de nome:
A variavel usada nao foi definida.

Nome: '   '        ← deveria ser 'xyz'; é memória liberada
```

A mensagem que mais importa pedagogicamente está corrompida. Em WebAssembly a
memória liberada é reutilizada de forma diferente da nativa — o sintoma pode ir de
"nome errado" a lixo binário na tela do aluno.

**O bug está confinado a `cli.c:run_text`** — verifiquei que `--explicar` imprime
`Nome: 'xyz'` corretamente, porque `education.c:212` só chama `program_free` **depois**
do `diagnostic_render`. Ou seja: a ordem certa já existe no próprio repositório; é
`run_text` que está fora de passo. Isso importa duplamente aqui, porque `run_text` é
exatamente o caminho que o wrapper web vai espelhar (§2.3) — copiar a ordenação dele
levaria o bug para dentro do `.wasm`.

**Correção:** fazer `error->subject` ser dono da própria string (copiar em
`error_list_add`), ou apenas adiar o `program_free` de `session.c:113`. A primeira é a
correta — hoje `LumeError` guarda ponteiros emprestados de três donos diferentes
(AST, Source, literais estáticos), o que é frágil por construção.

**Estado após diagnosticar (b):** `make sanitize` **continua vermelho**. Nunca vi uma
passagem limpa do sanitizer neste código, e as três últimas suítes —
`test_project`, `test_dependencies`, `test_stdlib` — permanecem território
desconhecido, porque o ASan ainda aborta antes de chegar nelas.

### 1.3 Legibilidade do código vs. objetivo declarado

O `README.md` diz "pequena o bastante para ser estudada" e o `CONTRIBUTING.md` diz
"implementação C **pequena e legível**". Os números não sustentam isso:

| Arquivo | Bytes | Linhas | Maior linha |
|---|---:|---:|---:|
| `src/analyzer.c` | 19.896 | 38 | **4.789 chars** |
| `src/project.c` | 8.419 | 36 | 3.099 |
| `src/module.c` | 16.436 | 32 | 2.479 |
| `src/dependency.c` | 8.936 | 24 | 1.589 |
| **total `src/`** | **276.649** | **3.622** | — |

São 277 KB de C, não um projeto de 3.6k linhas. A compressão é **progressiva**:
`src/lexer.c`, `src/value.c`, `tests/test_lexer.c` estão formatados normalmente;
`src/analyzer.c`, `src/module.c`, `tests/test_modules.c` estão minificados. Isso
sugere que uma meta de contagem de linhas foi sendo perseguida ao longo do
desenvolvimento — não um estilo de casa.

Consequência prática para este projeto web: **um site que ensina programação e cujo
próprio código-fonte é ilegível perde o maior ativo que tem** — "leia como a
linguagem que você está aprendendo é implementada" é uma aula inteira da Fase 2 que
hoje não é possível dar. Rodar um `clang-format` no `src/` é reversível, não muda
comportamento, e é verificável (`make test` continua verde).

---

## 2. Fase 1 — Rodar no browser

### 2.1 Por que é fácil: as três propriedades que decidem o port

**(a) A superfície de plataforma é mínima e toda coberta pelo Emscripten.**

Headers usados em todo o `src/`: `string.h`, `stdio.h`, `stdlib.h`, `stdint.h`,
`math.h`, `time.h`, `ctype.h`, `errno.h` + POSIX `unistd.h`, `sys/stat.h`,
`dirent.h`. Chamadas de sistema: `fopen/fread/fwrite/fclose/fgets/fputs`,
`stat/mkdir/opendir/readdir/remove`, `time`.

Tudo isso existe na musl do Emscripten, e o sistema de arquivos é atendido por
**MEMFS**. Os dois `#include <windows.h>` estão dentro de `#ifdef _WIN32` e
simplesmente desaparecem. **Não há threads, sockets, `dlopen`, nem `fork`.**

**(b) A I/O já está abstraída — captura de saída sai de graça.**

```c
/* src/runtime_io.h */
typedef struct { FILE *input; FILE *output; } RuntimeIO;
```

Essa struct é passada por todo o interpretador, REPL, CLI e stdlib. No browser:

```c
FILE *in  = fmemopen((void *)stdin_text, strlen(stdin_text), "r");
FILE *out = open_memstream(&buffer, &size);
RuntimeIO io = { in, out };
```

Isso é o que torna a captura de saída gratuita: **nenhuma linha dos `.c` existentes
muda.** Esta é, de longe, a decisão de design mais valiosa que o projeto já tomou.

*A confirmar no spike (§2.6):* a disponibilidade de `fmemopen`/`open_memstream` na
musl do Emscripten. Caso alguma falte, o plano B mantém a mesma propriedade — um
`FILE *` por callback (`funopen`) ou um arquivo temporário em MEMFS, com o wrapper
absorvendo a diferença e os `.c` existentes ainda intocados.

**(c) `session.h` já é a API de embedding.**

```c
void        session_init(LumeSession *, RuntimeIO);
InputStatus session_classify(const char *text, size_t length);   /* COMPLETE/INCOMPLETE/INVALID */
bool        session_execute(LumeSession *, name, text, len, print_expr, Source **, ErrorList *);
bool        session_execute_repl(LumeSession *, ...);
```

`session_classify` é exatamente a lógica de "o usuário terminou de digitar ou
precisa de mais uma linha?" que um REPL no browser precisa. Não há API para
projetar — há uma API existente para exportar.

### 2.2 Arquitetura proposta

```
┌─ Página estática (sem backend) ──────────────────────────────┐
│                                                              │
│  Editor: CodeMirror 6 + modo Lume       Painel de saída       │
│  Textarea "Entrada do programa"         Botão ▶ / ⏹          │
│                        ↕ postMessage                          │
│  ┌─ Web Worker ──────────────────────────────────────────┐   │
│  │  lume.wasm  (Emscripten, ~250–400 KB)                 │   │
│  │    lume_eval(src, stdin) → { saida, erro, eventos[] } │   │
│  │    MEMFS: /proj/src/*.lume, /proj/lume.projeto        │   │
│  └───────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

**Por que Web Worker é obrigatório, não opcional:** o loop infinito. Um aluno vai
escrever `enquanto verdadeiro { }` no primeiro dia. Na CLI ele aperta Ctrl-C; no
browser, sem worker, a aba congela e o único recurso é fechá-la. Com worker,
`worker.terminate()` mata a execução instantaneamente. **É o item de segurança
número 1 do playground.**

Bônus: o interpretador já tem cancelamento cooperativo embutido —
`RuntimeTrace.stop_requested`, verificado a cada statement em
`src/interpreter.c:1361`. Instalando um trace vazio, dá para parar com uma flag
sem matar o worker (preservando o estado do REPL). `terminate()` fica como
garantia de último recurso.

### 2.3 O que precisa ser escrito

**Adições apenas — nenhum arquivo existente é modificado.**

| Novo arquivo | Tamanho estimado | Papel |
|---|---|---|
| `src/web/lume_web.c` | ~120 linhas | wrapper `EMSCRIPTEN_KEEPALIVE`: `lume_eval`, `lume_repl_eval`, `lume_classify`, `lume_write_file` |
| `Makefile.web` | ~20 linhas | alvo `emcc` com `-Os -sMODULARIZE -sEXPORTED_RUNTIME_METHODS=...` |
| `web/` | — | página, editor, worker glue |

Esboço do wrapper:

```c
EMSCRIPTEN_KEEPALIVE
char *lume_eval(const char *src, const char *entrada) {
    LumeSession sessao; ErrorList erros; Source *fonte = NULL;
    char *buf = NULL; size_t n = 0;
    FILE *in  = fmemopen((void *)entrada, strlen(entrada), "r");
    FILE *out = open_memstream(&buf, &n);
    RuntimeIO io = { in, out };
    session_init(&sessao, io); error_list_init(&erros);
    bool ok = session_execute(&sessao, "principal.lume", src, strlen(src),
                              false, &fonte, &erros);
    if (!ok && erros.count > 0) diagnostic_render(out, fonte, &erros.data[0]);
    fclose(out); fclose(in);
    /* ... session_free, empacota buf + ok em JSON ... */
    return json;   /* liberado pelo JS via lume_free */
}
```

### 2.4 As decisões que precisam ser tomadas

#### Decisão 1 — `entrada()` bloqueante (a única dificuldade real)

`src/interpreter.c:463` implementa `entrada()` como
`while ((c = fgetc(runtime->io->input)) != EOF && c != '\n')`. Uma leitura
**síncrona e bloqueante** — algo que o browser, por design, não tem.

| Opção | Custo | Veredicto |
|---|---|---|
| **A. stdin pré-preenchido** — textarea "Entrada do programa", `fmemopen` | **Zero mudanças no C.** Não é interativo. | **Recomendado para a Fase 1.** É exatamente o que Ideone / Judge0 / a maioria dos juízes online fazem. Cobre 100% dos exercícios de "leia N e calcule". |
| **B. Asyncify** | ~2× no tamanho do `.wasm`, perda de performance, build mais frágil | Só se a Fase 1.5 provar que interatividade real é necessária |
| **C. SharedArrayBuffer + `Atomics.wait`** | stdin verdadeiramente interativo | **Força uma escolha de hospedagem** — ver §2.5 |

Recomendação: **A**, e projetar a UI para que isso pareça natural (o painel de
entrada fica sempre visível ao lado do editor, como num juiz online), não como
uma limitação.

#### Decisão 2 — `--passo` e `--explicar` no browser: transformar limitação em vantagem

Este é o argumento de produto mais forte da análise e vale mais que o playground em si.

`src/trace.h` expõe uma infraestrutura de trace **genérica e plugável**:

```c
typedef struct {
    TraceEventType type;        /* 25 tipos: DECLARE_VARIABLE, ASSIGN, IF_CONDITION,
                                   WHILE_ITERATION, FUNCTION_ENTER, FUNCTION_RETURN,
                                   OUTPUT, INDEX_WRITE, MODULE_IMPORT, ... */
    SourceSpan span;            /* linha/coluna exatas */
    const char *name; size_t name_length;
    const Value *before, *after, *arguments;
    const Environment *environment;
    bool decision; size_t iteration; size_t call_depth; int64_t index;
} TraceEvent;
typedef void (*TraceCallback)(void *context, const TraceEvent *event);
```

Na CLI, `education.c` consome esses eventos e imprime texto — e pausa com
`fgets(...)` (`education.c:55`), que é o mesmo problema bloqueante do stdin.

**No browser, não se porta o renderizador de texto: instala-se um callback
próprio que serializa os eventos em JSON.** O programa roda uma vez, do início ao
fim, e o JS recebe a fita completa de execução. A UI então oferece:

- uma **linha do tempo** com scrub para frente **e para trás** (a CLI só vai para frente);
- **destaque da linha atual** no editor via `span`;
- **inspetor de variáveis** com `before`/`after` a cada passo;
- **pilha de chamadas** via `call_depth`;
- **contador de iterações** de laços via `iteration`.

Isso **elimina o problema de bloqueio do `--passo`** e entrega um depurador visual
**melhor do que a CLI nativa consegue ser**. É a resposta para "por que usar o site
em vez de instalar o `lume`".

Cuidado técnico: o comentário em `trace.h` avisa que os campos são *borrowed* e só
valem durante o callback síncrono — a serialização tem de copiar os valores na
hora, não guardar ponteiros.

#### Decisão 3 — comportamentos secundários

- **`durma(ms)`** — usa `usleep`/`Sleep`. Dentro de um Worker o Emscripten resolve
  isso sem travar a UI. Aceitável.
- **`arquivo.*`** (`existe`, `leia`, `escreva`, `adicione`, `remova`) — mapeiam para
  MEMFS. Funciona e fica **sandboxed por construção**: um aluno pode aprender I/O de
  arquivo sem qualquer risco. É um ganho, não uma perda.
- **Módulos e projetos** — escrever os `.lume` e o `lume.projeto` em MEMFS antes de
  chamar `cli_run`. Todo o suporte a `importe`, dependências e `lume testar`
  funciona sem alteração. Isso viabiliza exercícios multi-arquivo na Fase 2.
- **UTF-8** — a saída sai como bytes UTF-8; decodificar no JS com
  `new TextDecoder('utf-8')`. Os identificadores da linguagem são sem acento, mas as
  strings têm ("Olá, mundo!"), então isso importa.
- **Tamanho** — 277 KB de C com `-Os`. Estimativa: **250–400 KB de `.wasm`,
  ~120–180 KB com Brotli**. A confirmar no spike (§2.6).

### 2.5 Hospedagem — decidido: Docker + Caddy, com Cloudflare Pages como espelho

**A percepcao que simplifica a decisao:** o build produz um bundle estatico
(`.html` + `.js` + `.wasm`). **Cloudflare Pages, um container Docker e a VM servem
o artefato identico.** Nao existe port, variante de build ou diferenca de codigo
entre eles — a unica coisa que muda e a configuracao de cabecalhos HTTP.

Isso troca a pergunta "qual deles?" por "quais cabecalhos?", e a resposta depende
de uma unica decisao ja escopada (§2.4, Decisao 1):

| | Sem SharedArrayBuffer (stdin pre-preenchido) | Com SharedArrayBuffer (stdin interativo) |
|---|---|---|
| **Cabecalhos** | MIME `application/wasm` + Brotli | + `COOP: same-origin` e `COEP: require-corp` |
| **Cloudflare Pages** | funciona | funciona (via `_headers`) |
| **Docker / VM (Caddy, nginx)** | funciona | funciona (2 linhas de config) |
| **GitHub Pages** | funciona | **impossivel** — nao permite cabecalhos |

**Recomendacao para "a melhor experiencia possivel": auto-hospedar na VM atras do
Caddy.** HTTPS automatico, cabecalhos em duas linhas, `Dockerfile` trivial, e
controle total — o que mantem a porta do SAB aberta sem depender de ninguem.
Cloudflare Pages fica como espelho publico de custo zero do mesmo bundle, se
quiser um endereco publico.

O ciclo Docker-primeiro-VM-depois esta certo: a mesma imagem roda nos dois
lugares, e o **Emscripten vive dentro da imagem** (estagio de build
`emscripten/emsdk`), entao nenhum SDK de ~1 GB precisa ser instalado no Mac nem
na VM.

```dockerfile
FROM emscripten/emsdk:latest AS build
WORKDIR /src
COPY . .
RUN make -f Makefile.web

FROM caddy:alpine
COPY --from=build /src/web/dist /srv
COPY Caddyfile /etc/caddy/Caddyfile
```

```
# Caddyfile
:8080
root * /srv
encode zstd gzip
header {
    Cross-Origin-Opener-Policy   same-origin
    Cross-Origin-Embedder-Policy require-corp
    Cache-Control                "public, max-age=31536000, immutable"
}
header /index.html Cache-Control "no-cache"
file_server
```

**Uma consequencia a registrar agora, nao depois:** o isolamento cross-origin
(COOP/COEP) **quebra conteudo de terceiros embutido** — iframes de YouTube,
fontes externas, imagens de CDN — a menos que cada um envie cabecalhos CORP.
Isso e uma restricao real para o site do projeto (§5), nao um rodape.

> **Status:** o `Dockerfile` e o `Caddyfile` acima estao **projetados, nao
> testados.** Eles referenciam `Makefile.web` e `web/dist`, que ainda nao
> existem — nada foi construido nem executado. So da para testar o container
> depois que o bundle wasm existir (§2.6).

### 2.6 Próximo passo concreto — o spike, dentro do proprio Docker

O Emscripten **nao esta instalado** nesta maquina, e nao precisa estar: o mesmo
raciocinio de §2.5 vale aqui — o toolchain mora na imagem. O spike roda com um
comando, sem instalar SDK de ~1 GB nem no Mac nem na VM:

```sh
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:latest \
  emcc -Isrc -Os src/*.c src/web/lume_web.c -o web/dist/lume.mjs \
       -sMODULARIZE -sEXPORT_ES6 -sEXPORTED_FUNCTIONS=_lume_eval,_lume_free \
       -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS -sALLOW_MEMORY_GROWTH
```

### RESULTADO DO SPIKE (executado em 2026-08-18) — ✅ tudo confirmado

| Medida | Estimativa anterior | **Real** |
|---|---|---|
| `.wasm` | 250–400 KB | **154 KB** |
| gzip | 120–180 KB | **68 KB** (71 KB na rede) |
| glue `.js` | — | 66 KB |
| imagem Docker final | — | **85 MB** |

**Validado sob Node:** as **15 suítes de teste compilam e passam em wasm32**, com
**zero warnings** sob `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.
`fmemopen`/`open_memstream` existem e funcionam — o "zero mudanças nos `.c`
existentes" está **confirmado**. MEMFS atende `stat`/`opendir`, então projetos
multi-arquivo, `importe`, `--analisar`, `--explicar` e as nativas `arquivo.*`
funcionam sem alteração.

**Validado em navegador real** (container local, Caddy): acentos UTF-8, `leia()`
lendo do stdin pré-preenchido, diagnósticos com caret, e
`crossOriginIsolated === true` com `SharedArrayBuffer` disponível — ou seja, a
Fase 1.5 (stdin interativo) já está desbloqueada pela config do Caddyfile.

#### Duas armadilhas que o spike encontrou

**1. `-D_POSIX_C_SOURCE=200809L` é obrigatório.** Com `-std=c11` estrito, a musl
do Emscripten não declara `localtime_r` nem `nanosleep`, usados por
`lume_stdlib.c`. Sem a macro, o build nem começa. Não aparece no macOS/glibc
porque esses headers são mais permissivos.

**2. `-sSTACK_SIZE=4MB` é obrigatório — e esta é séria.** O padrão do Emscripten
é **64 KB**, e cada chamada de função Lume consome vários KB de pilha C. Com o
padrão, recursão de profundidade ~16 **corrompe a memória em silêncio**:
`fat(19)` devolve vazio e `fat(20)` inventa um erro de sintaxe em um código sem
erro nenhum. Recursão é o módulo 7 do currículo (§3.3) e o conceito em que
iniciantes mais travam — teria sido um desastre descobrir isso depois.

Investigando a causa, apareceu um **bug pré-existente no interpretador nativo**:
não havia teto de profundidade de chamada, então recursão sem caso base
derrubava o processo com **SIGSEGV e nenhuma mensagem** (profundidade ~460 no
macOS). Corrigido na branch `limite-recursao` — o contador `runtime->call_depth`
já existia para os eventos de trace, então bastou consultá-lo. Detalhes em §4.

### 2.6.1 Riscos que o spike descartou:

1. `fmemopen` / `open_memstream` funcionam de fato na musl do Emscripten (risco baixo, impacto alto);
2. tamanho real do `.wasm`;
3. `stat`/`opendir` sobre MEMFS atendem `project.c` e `dependency.c` sem `#ifdef`;
4. os avisos de `-Wconversion` continuam limpos sob clang/wasm32 (ponteiros de 32 bits podem revelar conversões que o arm64 escondia).

**Isso já foi feito:** o use-after-free do §1.2 está corrigido e o `make sanitize`
está verde, então o spike parte de uma base limpa — nenhum bug de memória conhecido
para confundir o diagnóstico através de duas camadas novas de toolchain.

---

## 3. Fase 2 — Programa de estudos

### 3.1 O diagnóstico honesto: isto é trabalho de conteúdo, e é a fase maior

Inventário do que existe hoje em `docs/`:

| Arquivo | Tamanho |
|---|---:|
| `LANGUAGE.md` | 15.376 B — **substancial**, é a referência real |
| `docs/guia-de-uso.md` | 3.015 B |
| `docs/biblioteca-padrao.md` | 2.429 B |
| `docs/projetos.md` | 1.764 B |
| `docs/algoritmos.md` | 1.469 B — tem seção "Exercícios" |
| `docs/comecando.md` | 1.192 B |
| `docs/para-professores.md` | 941 B |
| `docs/modos-educacionais.md` | 804 B |
| `docs/depois-da-lume.md` | 722 B |
| **`docs/solucoes.md`** | **571 B — praticamente vazio** |

Fora o `LANGUAGE.md`, os documentos têm 1–3 KB: são esboços, não um curso. Um
programa de estudos completo é **dezenas de milhares de palavras de conteúdo
pedagógico revisado**. Vale dimensionar isso corretamente desde já — é mais trabalho
que a Fase 1 inteira, e é trabalho de professor, não de programador.

**O que já existe e serve de matéria-prima:** os ~20 programas de `exemplos/` são
todos funcionais e já cobrem a progressão natural — `ola.lume`, `variaveis.lume`,
`condicional.lume`, `repeticao.lume`, `funcoes.lume`, `recursao.lume`, `listas.lume`,
`closure.lume`, `entrada.lume`, mais `algoritmos/` (busca linear, maior número,
média) e `modulos/`. **São sementes de aula prontas, testadas e executáveis.**

### 3.2 A engenharia da Fase 2 é pequena e específica

Só três peças, e uma delas já existe:

**(a) Verificação automática de exercícios.** O modelo já está no projeto:
`lume testar` descobre `tests/*.lume` e roda. No browser, cada exercício vira
`{ enunciado, código inicial, entrada, saída esperada }` e a verificação é
comparação de saída — a mesma máquina do `lume testar`, sem backend.

**(b) Persistência de progresso.** `localStorage` com o código de cada aluno e as
lições concluídas. Sem login, sem servidor, sem LGPD. Exportar/importar um JSON
resolve troca de máquina e entrega para o professor.

**(c) Deep-link e modo professor.** URL que carrega código pré-preenchido
(`/?lc=aula-3&code=...`) para o professor mandar um exercício pronto no quadro ou
no grupo da turma. É barato e é o que faz a ferramenta circular.

### 3.3 Espinha curricular proposta

Cada lição segue o mesmo esqueleto: **conceito → exemplo executável (do `exemplos/`)
→ exercício com verificação automática → "espie por dentro" com o depurador visual
do §2.4**.

| Módulo | Lições | Base já existente |
|---|---|---|
| 1. Primeiros passos | saída, strings, comentários | `ola.lume`, `basico/ola_mundo.lume` |
| 2. Dados e nomes | variáveis, constantes, tipos, `entrada()` | `variaveis.lume`, `entrada.lume` |
| 3. Decisões | `se`/`senao`, booleanos, `e`/`ou`/`nao` | `condicional.lume` |
| 4. Repetição | `enquanto`, `para`, acumuladores | `repeticao.lume` |
| 5. Listas | índices, mutação, percurso | `listas.lume`, `algoritmo_listas.lume` |
| 6. Funções | parâmetros, retorno, escopo | `funcoes.lume` |
| 7. Recursão | caso base, pilha (**usar o inspetor de `call_depth`**) | `recursao.lume` |
| 8. Algoritmos clássicos | busca, máximo, média, ordenação | `algoritmos/*.lume` |
| 9. Programas maiores | módulos, `importe`, projetos | `modulos/`, `projeto/` |
| 10. Depois da Lume | ponte para Python/JS | `docs/depois-da-lume.md` |

O módulo 7 é onde o depurador visual (§2.4) se justifica sozinho: recursão é o
conceito em que iniciantes mais travam, e ver a pilha crescer e desmontar passo a
passo, para frente e para trás, é algo que nenhum tutorial em texto oferece.

**Trilha do professor** (`docs/para-professores.md` tem hoje 941 B): turmas via
deep-link, gabaritos, e uma sequência de aula por módulo. É o que faz a Lume entrar
numa escola em vez de ser um brinquedo individual.

---

## 4. Riscos e ordem recomendada

| # | Risco | Severidade | Mitigação |
|---|---|---|---|
| ~~1~~ | ~~`heap-use-after-free` no caminho de erro (§1.2b)~~ | — | **Resolvido** — PR [#1](https://github.com/Ultra332/Lume/pull/1) |
| ~~2~~ | ~~`make sanitize` quebrado e ausente do CI~~ | — | **Resolvido** — mesmo PR |
| 3 | Loop infinito trava a aba | Alta | Web Worker + `terminate()` + `stop_requested` |
| 4 | `entrada()` bloqueante | Média | stdin pré-preenchido (Decisão 1-A) |
| 5 | Escolha de hospedagem fecha a porta do SAB | Média | Começar em Cloudflare Pages |
| 6 | Código-fonte minificado impede a aula "veja por dentro" | Média | `clang-format` no `src/` |
| 7 | Subestimar o volume de conteúdo da Fase 2 | Média | Dimensionar como projeto próprio |
| 8 | `-Wconversion` pode acusar em wasm32 | Baixa | Descoberto no spike |

### Ordem sugerida

**Fase 0 — higiene — ✅ CONCLUÍDA** (PR [Ultra332/Lume#1](https://github.com/Ultra332/Lume/pull/1))
1. ✅ Comprimento em `tests/test_diagnostics.c` agora vem de `strlen`.
2. ✅ Use-after-free corrigido: a `ErrorList` passou a ser dona da cópia de `subject`.
3. ✅ Job `sanitize` adicionado ao `.github/workflows/ci.yml` — verde no CI.
4. ⏸ `clang-format` no `src/` — movido para o backlog (§5.2), deliberadamente fora do PR.

**Fase 1 — playground (≈1–2 semanas)**
5. Spike de compilação Emscripten (§2.6).
6. `src/web/lume_web.c` + `Makefile.web`.
7. Worker + editor + painéis de saída/entrada.
8. Callback de trace → JSON → linha do tempo do depurador visual (§2.4).
9. Deploy no Cloudflare Pages.

**Fase 2 — curso (projeto próprio)**
10. Motor de lições + verificação de saída + `localStorage`.
11. Autoria das 10 trilhas sobre a base de `exemplos/`.
12. Trilha e materiais do professor.


---

## 5. Backlog

Itens acordados como "segundo momento", registrados para nao se perderem.

### 5.1 Site do projeto

Uma pagina propria para a Lume, separada do playground (§2) e do curso (§3),
ainda que hospedada no mesmo dominio e servida pelo mesmo container.

Escopo a definir, mas o minimo util: identidade visual (o `assets/lume.png` e o
icone ja existem), um "experimente agora" que leva direto ao playground com
codigo pre-carregado, a referencia da linguagem renderizada a partir do
`LANGUAGE.md`, e as instrucoes de download para quem quiser a CLI nativa.

**Restricao herdada:** se o playground rodar com COOP/COEP (§2.5), qualquer
embed de terceiro no site — video, fonte de CDN, badge externo — precisa enviar
cabecalhos CORP ou sera bloqueado. A saida usual e servir o site institucional
sem isolamento e o playground em um caminho ou subdominio isolado.

### 5.2 Legibilidade do codigo-fonte

`clang-format` no `src/` e nos testes (§1.3). Reversivel, sem mudanca de
comportamento, verificavel com `make test`. Habilita a aula "veja como a
linguagem que voce esta aprendendo e feita por dentro", hoje impossivel.

Deliberadamente **fora** do PR de correcao enviado ao upstream: reformatar
277 KB junto com uma correcao de bug e a melhor forma de um PR bom ser recusado.

### 5.3 Fase 2 — programa de estudos

Detalhado em §3. Adiado por decisao explicita; a espinha curricular de 10 modulos
e o inventario de material existente ficam registrados la para quando for a hora.
