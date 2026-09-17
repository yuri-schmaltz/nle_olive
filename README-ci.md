# Olive gauntlet — reprodução da verificação

Este README documenta como reproduzir o estado verificado na sessão de bug-fixing
(16 commits em `master`, build Release limpo e ctest 3/3 verde), sem depender do
ambiente de CI com OpenGL.

## Pré-requisitos

- Docker com a imagem oficial `olivevideoeditor/ci-olive:2022.3`
  (coloque os arquivos que serão compilados em `/src` dentro do container).
- CPU com 4+ cores; 16GB RAM; ~60GB livres para a imagem do toolchain.

## 1. Configurar

```sh
root_olive=# caminho onde está a árvore do Olive (ex.: ~/Documentos/olive)

docker run --rm -v "$root_olive":/src -w /src olivevideoeditor/ci-olive:2022.3 \
  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DUSE_WERROR=ON -DBUILD_TESTS=ON -GNinja
```

## 2. Compilar

```sh
docker run --rm -v "$root_olive":/src -w /src/build-release \
  olivevideoeditor/ci-olive:2022.3 ninja -j$(nproc)
```

Um build Release limpo passa com clang 15 e `-Werror -pedantic-errors` (zero
warnings). O binário fica em `build-release/app/olive-editor`.

## 3. Testar (execução real)

```sh
docker run --rm -v "$root_olive":/src -w /src/build-release \
  olivevideoeditor/ci-olive:2022.3 ctest
```

Esperado: 3/3 testes passam (`compositing`, `common`, `timeline`).

## 4. Rodar o editor (requer GPU/desktop real)

O `olive-editor` é um aplicativo Qt/OpenGL 3.2+ e **não abre janela em host
headless/software** (a imagem CI é CentOS 7.9 EOL, sem drivers DRI utilizáveis).
Numa estação com GPU e drivers Mesa, execute:

```sh
./build-release/app/olive-editor
```

Em ambientes CI/headless esta limitação é esperada e não indica falha do código;
use `xvfb-run` + mesa para um smoke de inicialização de software.
