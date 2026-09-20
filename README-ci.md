# Compilação e verificação local do Olive

## Build nativo Linux

O launcher `./run-olive-desktop.sh` executa `build-host/app/olive-editor` e
encaminha seus argumentos ao editor. Ele usa as bibliotecas do sistema, sem
injetar as dependências antigas de `vendor-libs/` em `LD_LIBRARY_PATH`.

Pré-requisitos: compilador C++17, CMake, ferramentas de desenvolvimento Qt6
(incluindo LinguistTools e OpenGLWidgets), FFmpeg, OpenColorIO, OpenImageIO,
OpenEXR e PortAudio. Os submódulos `ext/core` e `ext/KDDockWidgets` devem estar
inicializados (`git submodule update --init --recursive`). As versões mínimas e
as dependências opcionais estão no `CMakeLists.txt` da raiz. Qt6 ainda é uma
opção experimental do projeto.

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_QT6=ON -DBUILD_TESTS=ON -DUSE_WERROR=OFF
cmake --build build-host -j2
ctest --test-dir build-host --output-on-failure
./run-olive-desktop.sh
```

Para verificar uma compilação limpa, use outro diretório de build com as mesmas
opções. Um build incremental bem-sucedido não comprova uma compilação limpa nem
uma compilação com `USE_WERROR=ON`.

## Testes

São registrados três executáveis com casos reais: `common-tests`,
`timeline-tests` e `TempoStream`. O alvo de composição é omitido enquanto seu
arquivo não tiver casos `OLIVE_ADD_TEST`; o CMake informa isso na configuração.

`TempoStream` verifica a continuidade do filtro isolado e a saída real da track
para alteração de velocidade, entrada curta, reprodução reversa e limites entre
clipes e gaps. Os testes do Windows também estão habilitados no workflow.

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
