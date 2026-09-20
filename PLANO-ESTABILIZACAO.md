# Plano de estabilização e conclusão funcional do Olive

Data: 19/09/2026. Documento de planejamento; suas metas não representam validações já realizadas.

## 1. Resultado esperado e limites

Entregar um editor no qual o usuário consiga instalar, importar mídia, montar e
revisar uma timeline, salvar, recuperar seu trabalho e exportar o resultado de
forma previsível. “Completo” significa concluir e testar esse fluxo dentro de uma
matriz publicada de recursos, formatos e plataformas. Não significa suportar
qualquer codec, efeito, hardware ou projeto histórico.

Premissa inicial: Linux nativo é a plataforma de referência, pois é o ambiente
validado nesta sessão. Windows e macOS fazem parte da entrega multiplataforma,
mas só recebem status de suportados depois de testes e instalação em máquinas
reais. Essa ordem pode ser alterada conforme o público pretendido.

Não fazer uma reescrita geral nem atualizar todas as dependências simultaneamente.
Cada correção deve manter os projetos existentes legíveis ou incluir uma migração
verificada. Nenhum marco pode ser declarado concluído apenas por compilar.

## 2. Ponto de partida comprovado

- Código C++17/CMake, Qt, FFmpeg, OpenGL, OCIO/OIIO e submódulos core/KDDockWidgets.
- Build incremental Linux Release com Qt6 e USE_WERROR=OFF aprovado.
- Três executáveis de teste registrados, com 13 casos reais aprovados.
- Correção local da contagem de amostras e drenagem do filtro por clipe.
- Quatro testes novos exercitam a saída da track; um detectou o erro original
  quando ele foi reintroduzido temporariamente.
- Launcher local aponta para build-host; dependências antigas não são injetadas.
- Testes Windows reativados no workflow, sem execução Windows nesta sessão.
- Mudanças ainda locais e não consolidadas em commits.

Pendências confirmadas:

| Evidência | Consequência | Frente |
| --- | --- | --- |
| Processador de tempo local a cada solicitação, app/node/output/track/track.cpp | Continuidade entre pedidos de preview não garantida | A2 |
| tests/compositing/compositing-tests.cpp sem casos | Composição sem cobertura nessa suíte | A3 |
| app/dialog/color/colordialog.cpp e app/widget/colorwheel/colorvalueswidget.cpp | Transformação inversa de cor desativada por crash | A3 |
| app/widget/timelinewidget/tool/add.cpp | Geradores de barras e tom sem implementação | A7 |
| app/node/project/serializer/serializer.cpp | Projetos 0.1 sem importação implementada | A4/A7 |
| Somente build incremental e testes locais executados | Instalação, exportação, GUI e outras plataformas não comprovadas | A1/A8 |

Recuperação, salvamento, codecs, concorrência e desempenho são áreas a auditar;
este documento não presume que estejam ausentes ou quebradas. A lista acima não
é um inventário exaustivo: a primeira etapa deve inspecionar também funções vazias,
recursos desativados, caminhos de erro e a interface, não apenas TODO/FIXME.

## 3. Prioridades e regras de conclusão

- P0: perda/corrupção de projeto, acesso inválido à memória, deadlock, crash em
  fluxo básico, exportação silenciosamente incorreta. Bloqueia qualquer release.
- P1: falha em recurso prometido, áudio/vídeo fora de sincronia, cache desatualizado,
  operação principal inconsistente, instalação quebrada. Bloqueia versão estável.
- P2: desempenho abaixo da meta, ergonomia, acessibilidade, funções secundárias.
  Se o recurso fizer parte do contrato da release, sua falha também bloqueia.
- P3: expansão de formatos, integrações e capacidades fora do escopo acordado.

Cada item deve ter ID, responsável, prioridade, reprodução ou hipótese explícita,
projeto/mídia de teste, dependências, critério de aceitação, estimativa revisável,
PR/commit e evidência de validação. “Implementado” e “validado” são estados distintos.

Correções devem incluir regressão quando houver comportamento verificável. Testes
não podem aprovar por falta de casos nem apenas reproduzir o algoritmo implementado.
Flakiness precisa de diagnóstico; repetir até passar não constitui aprovação.

## 4. Marcos e dependências

| Marco | Entrega | Condição para avançar |
| --- | --- | --- |
| M0 — Base reproduzível | A1, inventário e corpus inicial | Build limpo, testes reais e smoke de instalação Linux |
| M1 — Integridade | A4 e infraestrutura de testes A9 | Falhas de salvamento/recuperação testadas; nenhum P0 aberto nesses fluxos |
| M2 — Motor confiável | A2, A3 e A5 | Preview/exportação coerentes, testes de concorrência e orçamento de recursos |
| M3 — Fluxo editorial completo | A6 e escopo essencial de A7 | Cenários completos de edição e exportação aprovados |
| M4 — Beta distribuível | A8 e testes prolongados A9 | Pacotes instaláveis e matriz real de SO/GPU validada |
| M5 — Estável | A10 e critérios globais | Todos os critérios da seção 8 cumpridos |

A1 precede os demais. Depois de M0, A2/A3/A4 podem progredir independentemente,
com contratos claros. A6 depende das garantias de A4/A5; A8 deve começar cedo com
smokes, embora só termine após M3. Esta é uma estrutura de trabalho, não uma
instrução para executar agentes em paralelo.

## 5. Frentes de execução

### A1 — Build, dependências e linha de base (P0/P1)

1. Revisar e consolidar as alterações locais em unidades pequenas: áudio/testes,
   launcher/documentação e infraestrutura. Registrar hash e ambiente da base.
2. Configurar e compilar do zero em diretório novo e máquina/container limpo.
   Registrar compilador, Qt, FFmpeg, OCIO/OIIO, submódulos e flags.
3. Criar presets CMake para desenvolvimento, Release e sanitizers. Auditar a
   propagação das flags ao aplicativo, core e bibliotecas sob nosso controle.
4. Definir Qt principal e política explícita para a alternativa. A opção Qt6
   atual é experimental; não declará-la estabilizada por um único build.
5. Fixar versões de dependências e hashes de artefatos necessários. Verificar
   suporte das ferramentas/runners/actions antes de migrar o CI.
6. Separar jobs rápidos de PR, testes GPU, sanitizers e validação noturna.
7. Eliminar dependências acidentais de caminhos locais e vendor-libs; manter um
   procedimento documentado de aquisição/construção das dependências.
8. Auditar avisos; ativar Werror no código próprio nos toolchains suportados,
   sem transformar avisos arbitrários de terceiros em bloqueios permanentes.

Aceitação: clone limpo + instruções produzem binário executável e testes; nenhuma
biblioteca ausente; ambiente e comandos registrados; CI não aprova zero testes.

### A2 — Áudio, tempo e sincronização (P0/P1)

1. Criar reproduções determinísticas de cliques, truncamento e atraso: seno com
   fase contínua, impulsos, silêncio, fala e música com licença adequada.
2. Especificar propriedade do estado do filtro, latência, correspondência entre
   tempo da mídia e timeline, flush, reverse, seek, loop e cancelamento.
3. Prototipar renderização com preroll/overlap e descarte das bordas, comparando-a
   com sessões sequenciais por clipe. Não compartilhar filtros globalmente nem
   confiar na ordem dos pedidos; medir correção, custo e comportamento de seeks.
4. Validar a escolha com render integral e render dividido em blocos de tamanhos
   variados, pedidos fora de ordem, cache frio/quente e múltiplos workers.
5. Cobrir 0x, 0,5x, 1x, 2x e limites declarados; 44,1/48/96 kHz conforme suporte;
   mono/estéreo e layouts adicionais somente se prometidos; reverse e mudanças
   de velocidade. Definir comportamento para parâmetros inválidos.
6. Exercitar clips adjacentes de mídias diferentes, gaps, transições, entrada
   curta, falha de filtro, troca de dispositivo e esvaziamento do buffer.
7. Medir sincronia A/V ao iniciar, buscar, repetir, pausar e exportar longas sequências.

Aceitação: ausência de perda/duplicação de amostras nos casos com duração exata;
limites e compensação de latência documentados para time-stretch; comparação com
referência independente e tolerância justificada; nenhuma descontinuidade criada
nos sinais contínuos de teste; sem deriva acumulada além da meta da seção 8.
Igualdade bit a bit só é exigida onde o algoritmo a permite.

### A3 — Vídeo, composição e gestão de cor (P0/P1)

1. Criar casos reais de composição: ordem das camadas, alpha, crop, transform,
   transições, keyframes, títulos e conversões entre formatos de pixel.
2. Construir frames sintéticos com resultados analíticos e imagens de referência;
   registrar versão OCIO, config, GPU e tolerância por operação.
3. Reproduzir o crash da transformação inversa de cor. Diferenciar configuração
   inválida e transformações não inversíveis de falhas de implementação; nesses
   casos manter a operação indisponível com explicação útil ao usuário.
4. Verificar gestão de cor entre importação, viewer, cache e exportação: range,
   transfer function, primárias, gamma, profundidade e alpha premultiplicado.
5. Testar criação/destruição de contexto OpenGL, redimensionamento, múltiplos
   viewers e drivers selecionados. Tratar ausência de GPU compatível claramente.

Aceitação: suíte de composição real registrada; referências aprovadas no hardware
suportado; exportação e preview consistentes dentro de tolerâncias documentadas;
falha de configuração não encerra o aplicativo.

### A4 — Projetos, persistência e recuperação (P0)

1. Mapear e testar o salvamento existente, temporários, substituição de arquivos,
   erros de escrita, autosave/backup e estado dirty; implementar o que faltar.
2. Garantir que falhas não destruam a última versão válida. Testar disco cheio,
   permissão negada, destino removido e encerramento durante gravação.
3. Testar round-trip do grafo: mídia, caminhos, nós, links, keyframes, cores,
   sequências, legendas e parâmetros. Comparar significado, não só XML textual.
4. Criar fixtures de cada versão efetivamente suportada e testar migrações;
   arquivos futuros/corrompidos devem falhar de forma segura e explicativa.
5. Verificar recuperação após crash, retenção de backups e apresentação das
   versões recuperáveis sem sobrescrever o original silenciosamente.
6. Testar mídia ausente/relink, Unicode, caminhos relativos e movimentação do
   projeto entre máquinas. Auditar acessos concorrentes ao grafo ao salvar.
7. Aplicar fuzzing direcionado e limites de recursos nos leitores próprios de
   projetos/metadados; não assumir que todo arquivo de entrada é válido.

Aceitação: injeções de falha preservam um projeto anterior válido; recuperação
apresenta e abre o último snapshot íntegro; nenhuma regressão nos formatos
prometidos; nenhum crash/hang nas entradas inválidas do corpus.

### A5 — Concorrência, cache e recursos (P0/P1)

1. Documentar quem possui cada grafo, decoder, contexto GPU, buffer e job.
2. Exercitar editar durante caching/exportação, seek rápido, cancelamento,
   fechamento da janela, remoção de mídia e troca de sequência.
3. Validar versionamento/invalidação de cache: resultado de job antigo nunca
   substitui dados de uma versão mais recente do grafo.
4. Auditar locks, sinais Qt entre threads, callbacks tardios e destruição de objetos.
5. Executar ASan/UBSan e TSan em jobs separados e suportados, registrando
   supressões justificadas de terceiros. Não combinar sanitizers incompatíveis.
6. Implementar/verificar limites de RAM, VRAM, cache em disco, concorrência e
   filas; exercitar pressão de memória e disco cheio.

Aceitação: nenhum erro dos sanitizers nos fluxos cobertos; cancelamento e shutdown
terminam dentro do prazo estabelecido; nenhuma publicação de cache obsoleto;
consumo retorna ao patamar esperado após ciclos repetidos.

### A6 — Importação, timeline e exportação (P0/P1)

1. Publicar matriz de codecs/contêineres e recursos realmente disponíveis no build.
2. Testar CFR/VFR, FPS fracionário, streams com diferentes inícios, rotação,
   duração desconhecida, arquivos truncados e ausência de áudio/vídeo.
3. Verificar seleção, trim, split, ripple, roll, slip/slide se expostos, snapping,
   drag-and-drop, múltiplas tracks, transições e links A/V.
4. Testar undo/redo por invariantes: desfazer restaura o estado anterior; refazer
   restaura o posterior; nunca deixa referência pendente. Incluir sequências
   aleatórias reproduzíveis de operações.
5. Exportar projetos de referência: H.264/AAC como candidato inicial, codecs
   adicionais e duas passagens somente conforme o encoder presente no pacote.
6. Decodificar exports com ferramenta/processo separado; medir frames, amostras,
   timestamps, duração, sincronia e imagem. Arquivo existente não é sucesso.
7. Testar cancelamento, falha de encoder, disco cheio, arquivo existente e opção
   incompatível; diferenciar arquivo completo de saída parcial.

Aceitação: fluxo importar → editar → desfazer/refazer → salvar → reabrir →
exportar aprovado com referências conhecidas; erros acionáveis; nenhum resultado
silenciosamente incompleto; codecs ausentes não aparecem como disponíveis.

### A7 — Funcionalidade e experiência de uso (P1/P2)

1. Auditar todos os menus, ferramentas e opções visíveis. Classificar cada uma:
   implementada/testada, experimental, indisponível ou fora do escopo.
2. Implementar barras e tom se fizerem parte da versão-alvo; caso contrário,
   remover a promessa da interface até implementação. Cliques não podem criar
   silenciosamente um objeto vazio que aparenta ser o recurso solicitado.
3. Concluir títulos, legendas, keyframes, efeitos e proxies que estejam no contrato
   da versão. Cada recurso precisa de criação, edição, persistência e exportação.
4. Decidir explicitamente migração 0.1: projeto separado com conversor e fixtures,
   ou não suportada com mensagem clara. Não confundir com leitura de versões atuais.
5. Revisar navegação por teclado, foco, HiDPI, contraste, traduções, progresso,
   mensagens de erro e recuperação de ações malsucedidas.
6. Criar guia de início rápido baseado no pacote distribuído e no fluxo validado.

Aceitação: nenhum controle visível sem efeito ou resultado enganoso; o roteiro
editorial pode ser completado sem terminal; funcionalidades prometidas possuem
validação end-to-end e limitações publicadas.

### A8 — Empacotamento e plataformas (P1)

1. Criar pacotes por plataforma e testar em usuário/máquina limpa sem SDK instalado.
2. Definir matriz de versões de SO, CPU e GPU a partir de usuários-alvo e ensaios;
   verificar requisitos atuais dos serviços e ferramentas antes de fixá-los.
3. Incluir plugins Qt, codecs permitidos, configs OCIO, traduções, fontes/recursos
   necessários, licenças e inventário de dependências.
4. Testar instalação, primeira abertura, atualização, configurações antigas,
   associação de projeto e desinstalação sem apagar projetos pessoais.
5. Produzir símbolos de depuração e identificação exata de versão/build para
   diagnosticar crashes. Assinar/notarizar onde aplicável à distribuição.
6. Validar o mesmo fluxo de referência no pacote final de cada plataforma.

Aceitação: artefato instalado funciona fora da máquina de desenvolvimento; logs
identificam o build; pacote e checksum arquivados; limitações por plataforma claras.
Credenciais de assinatura, hardware e publicação são dependências operacionais,
registradas no backlog; não são motivos para parar o trabalho local independente.

### A9 — Testes, corpus e desempenho (transversal)

Criar mídia sintética curta e determinística para PRs; manter corpus maior,
licenciado e identificado por hash para testes noturnos. Abranger áudio, vídeo,
imagens, alpha, fontes, legendas, mídia faltante e entradas inválidas.

Camadas: testes unitários de tempo/serialização; integração de track/render/cache;
GUI com eventos e espera por condições; smokes de pacote; sessões manuais e testes
prolongados com GPU real. Capturar logs, versão, seed e artefatos em toda falha.

Medir p50/p95 e picos para abertura, importação, seek, preview, exportação, RAM/VRAM
e disco. Usar máquina e projeto fixos, separar cache frio/quente e medir overhead
de instrumentação. Definir orçamento antes de otimizar; usar perfis, não intuição.

### A10 — Beta e manutenção (P1)

Congelar funcionalidades para a candidata; aceitar correções e regressões apenas.
Distribuir beta com roteiro, coleta voluntária de diagnóstico e instruções para
anonimizar projetos/mídias. Não transmitir dados sem consentimento.

Triar falhas por severidade, publicar limitações e manter candidato anterior para
rollback de distribuição. Testar se projetos gravados pela versão nova permanecem
compatíveis; não prometer downgrade quando houver mudança de formato.

Após a versão estável, manter corpus de regressões, política de dependências,
checagem de vulnerabilidades, compatibilidade de projetos e releases de correção.

## 6. Primeiro ciclo de execução

| Ordem | Item | Evidência de conclusão |
| --- | --- | --- |
| 1 | BASE-001: revisar/consolidar mudanças locais e inventário | Diff revisado, base identificada, backlog com responsáveis |
| 2 | BUILD-001: build limpo Linux em ambiente isolado | Log, versões, testes e binário reproduzidos |
| 3 | TEST-001: corpus mínimo e cenário editorial automatizável | Fixtures com resultados e licenças definidos |
| 4 | SAVE-001: auditoria e injeção de falha no salvamento | Projeto anterior preservado e recuperação exercitada |
| 5 | AUDIO-001: reprodução de limites de preview | Falha reproduzível com posição/seed e métrica |
| 6 | AUDIO-002: experimento de contexto temporal | Comparação integral/blocos/seek e decisão documentada |
| 7 | RENDER-001: testes reais de composição | Casos analíticos registrados e executados com GPU |
| 8 | EXPORT-001: primeiro fluxo completo | Export decodificado e validado externamente |
| 9 | PACKAGE-001: instalação Linux limpa | Mesmo fluxo executado pelo pacote final |

Essa ordem prioriza reprodutibilidade e proteção do trabalho do usuário. Um P0
novo interrompe expansão funcional e assume prioridade de correção.

## 7. Dimensionamento e acompanhamento

Não há informação suficiente sobre equipe, dedicação e hardware para prometer
uma data robusta. Primeiro executar M0 e os experimentos de salvamento/áudio;
depois estimar cada item em intervalos e recalcular o caminho crítico.

Papéis necessários (uma pessoa pode acumular): responsável por release/build,
render/áudio, modelo de projeto/timeline e validação/UX. Cada item tem um único
responsável pela conclusão, mesmo com vários colaboradores.

Acompanhar semanalmente: P0/P1 abertos e reabertos, cenários aprovados por
plataforma, crashes reproduzíveis, testes instáveis, orçamento de desempenho e
bloqueios externos. Não usar número de commits, TODOs removidos ou cobertura
percentual isolada como medida de estabilidade.

## 8. Critérios propostos para declarar uma versão estável

Os números abaixo são metas iniciais a ratificar em M0 com hardware e escopo
fixados. Mudanças precisam ser justificadas antes da avaliação da candidata.

1. Zero P0/P1 conhecidos abertos dentro do escopo suportado.
2. 100% dos cenários essenciais aprovados por plataforma anunciada, incluindo
   importação, edição, undo/redo, persistência, recuperação e exportação.
3. Build limpo e instalação limpa aprovados para todos os artefatos publicados.
4. Zero erro atribuível ao código próprio nas suítes instrumentadas; supressões
   e áreas não instrumentadas explicitadas.
5. Teste de 8 horas de edição/playback automatizado por plataforma, sem crash,
   deadlock nem crescimento sustentado inexplicado de memória; complementar com
   pelo menos 100 ciclos de abrir/editar/salvar/fechar na configuração de referência.
6. Falhas de gravação preservam o último projeto válido. Proposta inicial de
   recuperação: autosave a cada 60 s com snapshots íntegros; medir atraso real e
   declarar a janela máxima de trabalho que pode ser perdida.
7. Em sequência de uma hora, erro A/V medido nos pontos de referência não excede
   um frame da sequência após compensações documentadas de codec/latência, sem
   deriva acumulada; pipelines PCM com duração exata têm contagem exata de amostras.
8. Metas iniciais de UX: feedback de operações em até 100 ms p95; cancelamento
   reconhecido em até 1 s e conclusão em até 5 s nos casos do corpus. Operações
   externas não interrompíveis precisam de tratamento e limite explícitos.
9. Projeto de referência 1080p30 reproduz em tempo real após cache/proxy na máquina
   definida, sem underruns de áudio nem perda contínua de frames. Publicar limites
   de efeitos/mídia em vez de prometer tempo real irrestrito.
10. Cada recurso visível funciona, informa indisponibilidade ou está claramente
    marcado como experimental. Nenhum recurso experimental é necessário ao fluxo básico.
11. Release candidate passa duas rodadas completas consecutivas sem regressão
    bloqueante, incluindo uma rodada com os pacotes finais.
12. Limitações, notas de versão, recuperação, suporte e procedimento de diagnóstico
    publicados e consistentes com os artefatos entregues.

Cumprir esses critérios reduz o risco dentro da matriz testada; não constitui
prova de ausência de defeitos em todas as mídias, drivers e combinações possíveis.
