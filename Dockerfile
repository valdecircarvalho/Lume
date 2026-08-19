# Estagio 1: compila a Lume para WebAssembly. O Emscripten fica so aqui.
FROM emscripten/emsdk:latest AS build
ENV EM_CACHE=/tmp/emcache
WORKDIR /src
COPY Makefile.wasm ./
COPY src ./src
COPY web ./web

# A prova sob Node cobre o bundle; o teste nativo sob AddressSanitizer/UBSan
# cobre o ciclo de vida entre execucoes, os quadros por chamada e o span dos
# erros. Rodar os dois aqui e o que impede uma regressao de passar despercebida
# — a mesma licao do 'make sanitize' que faltava no CI do upstream.
RUN make -f Makefile.wasm web \
 && node web/tools/prova.cjs web/dist/lume.js \
 && make -f Makefile.wasm web-test-nativo

# Estagio 2: serve o bundle estatico.
FROM caddy:alpine
COPY --from=build /src/web/dist /srv
COPY Caddyfile /etc/caddy/Caddyfile
EXPOSE 8080
