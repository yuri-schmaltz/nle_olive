# Original User Request

## Initial Request — 2026-09-20T14:03:45Z

Implementar a suíte completa de modernização e paridade competitiva do Olive Video Editor (C++17 / Qt6), abrangendo Track Audio Mixer com equalizador paramétrico, detecção automática de corte de cena (Scene Cut Detection), intercâmbio de timeline (FCPXML/OTIO) e automação de empacotamento Linux.

Working directory: /home/yuri/Documentos/olive
Integrity mode: development

## Requirements

### R1. Módulo de Áudio Profissional & Mixagem
- Implementar interface visual de Track Audio Mixer com controles em tempo real de faders de volume, balanço/pan e medição de nível de áudio (VU meters) por trilha de áudio.
- Implementar nó de processamento de Equalizador Paramétrico (Parametric EQ) na biblioteca de nós de áudio com curvas ajustáveis por banda de frequência.

### R2. Visão Computacional & IA: Detecção Automática de Corte de Cena (Scene Cut Detection)
- Implementar analisador assíncrono em `TaskManager` capaz de detectar transições de tomadas e cortes através de análise de histograma / limiares de diferença de quadros em decodificação de vídeo.
- Integrar à timeline para permitir divisão automática de clipes selecionados nos pontos de corte identificados.

### R3. Intercâmbio de Linha de Tempo Editorial (FCPXML / OTIO)
- Habilitar e robustecer a importação e exportação de Final Cut Pro 7 XML e OpenTimelineIO, garantindo fidelidade de pontos de in/out, trilhas de vídeo/áudio e cortes ao migrar entre Olive, Kdenlive e Premiere.

### R4. Automação de Empacotamento Linux
- Criar script reprodutível de geração de AppImage autossuficiente integrando binário do Olive, FFmpeg, OCIO, OIIO e dependências Qt6.
- Adicionar manifesto e receita de empacotamento Flatpak para distribuição no ecossistema Linux.

## Acceptance Criteria

### Compilação e Qualidade de Código
- [ ] Todo o código deve ser C++17 nativo e Qt6 puro, sem erros de compilação ou avisos tratados como erro (`-pedantic-errors -Wall -Wextra`).
- [ ] Preservação rigorosa da arquitetura de nós DAG e thread-safety do playback/render.

### Portal de Qualidade do Gauntlet
- [ ] O projeto deve passar 100% no Gauntlet com AddressSanitizer (`python3 scripts/gauntlet.py --preset linux-asan --jobs 4`) com zero vazamentos de memória (0 memory leaks) e 0 falhas de asserção.
- [ ] Cada nova funcionalidade (áudio, detecção de cena, intercâmbio XML) deve possuir testes unitários automatizados integrados ao `ctest`.
