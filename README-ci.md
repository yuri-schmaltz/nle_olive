# Compilação e verificação local do Olive

## Build nativo Linux

O launcher `./run-olive-desktop.sh` executa `build-linux-release/app/olive-editor` e
encaminha seus argumentos ao editor. Ele usa as bibliotecas do sistema, sem
injetar as dependências antigas de `vendor-libs/` em `LD_LIBRARY_PATH`.

Pré-requisitos: compilador C++17, CMake, ferramentas de desenvolvimento Qt6
(incluindo LinguistTools e OpenGLWidgets), FFmpeg, OpenColorIO, OpenImageIO,
OpenEXR e PortAudio. Os submódulos `ext/core` e `ext/KDDockWidgets` devem estar
inicializados (`git submodule update --init --recursive`). As versões mínimas e
as dependências opcionais estão no `CMakeLists.txt` da raiz. Qt6 ainda é uma
opção experimental do projeto.

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
./run-olive-desktop.sh
```

Para verificar uma compilação limpa, use outro diretório de build com as mesmas
opções. Um build incremental bem-sucedido não comprova uma compilação limpa nem
uma compilação com `USE_WERROR=ON`.

## Testes

A configuração registra testes de áudio, timeline, projeto, detecção de cortes,
FCP7 XML, renderização e exportação, além de integrações E2E e verificações
estáticas de packaging. Use `ctest --preset linux-release -N` para consultar o
inventário real. Os testes de OTIO dependem da biblioteca opcional; os testes GPU
exigem `-DBUILD_GPU_TESTS=ON` e um display OpenGL funcional.

Consulte `TEST_INFRA.md` para o escopo e as lacunas de cobertura. O resultado
positivo de uma suíte não valida automaticamente mixer, IA, codecs por hardware
ou pacotes finais.

Os testes não substituem validação manual de importação, preview, abertura e
salvamento de projetos e exportação. A janela exige um ambiente Qt/OpenGL
compatível; sucesso nos testes não comprova funcionamento da interface.

O filtro de tempo é drenado por clipe e por solicitação de renderização. A
continuidade entre solicitações independentes de preview ainda exige contexto
adicional no renderizador (preroll/overlap). Não há garantia de eliminar estalos
nesses limites.

## Build Docker legado

A configuração histórica usa a imagem `olivevideoeditor/ci-olive:2022.3`:

```sh
root_olive="$PWD"
docker run --rm -v "$root_olive":/src -w /src olivevideoeditor/ci-olive:2022.3 \
  cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DUSE_WERROR=ON -DBUILD_TESTS=ON -GNinja
docker run --rm -v "$root_olive":/src -w /src olivevideoeditor/ci-olive:2022.3 \
  cmake --build build-release -j2
docker run --rm -v "$root_olive":/src -w /src olivevideoeditor/ci-olive:2022.3 \
  ctest --test-dir build-release --output-on-failure
```

Esse build é separado do nativo e não é usado pelo launcher. A imagem e seus
resultados precisam ser verificados no ambiente Docker; não se deve inferir
sucesso a partir do build nativo. `vendor-libs/` contém dependências locais
antigas, é ignorado pelo Git e não faz parte do procedimento nativo.
