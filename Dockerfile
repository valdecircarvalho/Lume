# Estagio 1: compila a Lume para WebAssembly. O Emscripten fica so aqui.
FROM emscripten/emsdk:latest AS build
ENV EM_CACHE=/tmp/emcache
WORKDIR /src
COPY Makefile.wasm ./
COPY src ./src
COPY web ./web
RUN make -f Makefile.wasm web && node web/tools/prova.cjs web/dist/lume.js

# Estagio 2: serve o bundle estatico.
FROM caddy:alpine
COPY --from=build /src/web/dist /srv
COPY Caddyfile /etc/caddy/Caddyfile
EXPOSE 8080
