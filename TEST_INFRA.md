# Verificação do Olive

Os resultados do CTest descrevem os testes executados, não a conclusão de todos
os requisitos do produto. A antiga contagem de 130 casos incluía simulações de
DSP, XML, botões e tarefas sem chamar as implementações correspondentes. Esses
casos foram substituídos por integrações reais; não são evidência de cobertura
completa de mixer, medidores, OTIO, interface ou pacotes finais.

## Executar

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
python3 tests/e2e/run_e2e.py --preset linux-release

cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

O runner E2E exige o build solicitado e pelo menos um teste registrado. Não usa
outro build como fallback. Escreve JUnit em `qa-results/e2e-*/junit.xml`.
`--tier 4` seleciona o fluxo editorial; os antigos filtros 1/2/3 não separavam
casos internos e foram removidos. `ctest -N` lista os alvos da configuração real.

Os testes geram mídia temporária e exigem o executável FFmpeg com libx264/AAC.
Os testes comuns usam QCoreApplication; o alvo GPU usa QGuiApplication e precisa
de uma sessão gráfica com OpenGL funcional.

## Cobertura implementada

| Área | Evidência executável | Limite |
|---|---|---|
| Equalizador | `equalizer-tests`: resposta, bypass e limites pelo EqualizerNode | Não mede continuidade de todas as combinações de playback |
| Áudio por track | `track-audio-tests`: volume, pan, solo/mute e saída de amostras | Não verifica um mixer gráfico |
| Integração áudio | `e2e-audio-tests`: EQ → volume/pan; bypass; ganho após split/undo | Não passa pelo dispositivo de áudio |
| Detecção | `scenecut-tests`: detector real; `e2e-scenecut-tests`: decodificação de vídeo com cortes conhecidos, sinal, cancelamento prévio e mídia inválida | Cancelamento durante playback concorrente ainda pendente |
| Timeline | Testes de split, undo/redo, vínculos, tempos e limites | Não substituem interação manual com a interface |
| Projeto | `project-tests`: serialização, erros e recuperação de snapshots | Não comprova recuperação após todos os tipos de crash |
| FCP7 XML | `fcpxml-tests` e `e2e-interchange-tests`: importação → split A/V → exportação → reimportação, tempos/vínculos e falha de destino | Não promete preservar todo efeito de outros editores |
| Fluxo editorial | `e2e-workflow-tests`: importar MP4 gerado, detectar cortes, split/undo/redo, salvar OVE, reabrir e exportar/reimportar FCP7 XML | A exportação desse cenário é de timeline, não de vídeo renderizado |
| Exportação de áudio | `export-tests`: WAV real, decodificado por FFmpeg e comparado por amostra | Enumeração NVENC/VAAPI não comprova encoding no hardware |
| GPU | `gpu-tests`, quando habilitado: composição, barras e pixels de Color Wheels com alfa | Não é um teste completo de preview da interface |
| Packaging | `e2e-packaging-validation`: scripts e manifesto | Não instala nem executa o pacote em outra máquina |
| OTIO | `otio-tests`, somente com OpenTimelineIO encontrado pelo CMake | Ausência do alvo significa recurso não validado |

## GPU e evidências

```sh
cmake --preset linux-release -DBUILD_GPU_TESTS=ON
cmake --build --preset linux-release
ctest --preset linux-release
python3 scripts/gauntlet.py --preset linux-release --gpu --install --jobs 2
```

O gauntlet registra configuração, build, inventário, testes, instalação e versão.
Use `--fresh` para comprovar build limpo; um build incremental não é equivalente.
Os relatórios são locais e ignorados pelo Git. Consulte o inventário junto ao
resultado: um conjunto reduzido de recursos pode passar sem validar os opcionais.

## Requisitos ainda sem validação completa

- Mixer gráfico com faders, master e medidores por track.
- Backend real de IA, transcrição e inserção de legendas na timeline.
- Preview/playback com áudio real, desempenho e cancelamento concorrente.
- Exportação de vídeo por NVENC/VAAPI em hardware compatível e tratamento de indisponibilidade.
- OTIO quando suas dependências não estiverem instaladas.
- Fluxo manual importar → editar → salvar → reabrir → exportar vídeo no pacote final.
- Instalação e execução dos pacotes finais em máquinas limpas, Windows e macOS.

Esses itens não devem receber status PASS a partir de simulações ou de testes de
outros componentes. Os requisitos de produto continuam em `ORIGINAL_REQUEST.md`,
`PROJECT.md` e `PLANO-ESTABILIZACAO.md`.
