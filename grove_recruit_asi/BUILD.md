# Como compilar o grove_recruit_standalone.asi

> **TL;DR — caminho mais rápido:**
> 1. Instale o **Visual Studio Community 2022** (não VS Code!) com o workload **"Desktop development with C++"** → https://visualstudio.microsoft.com/
> 2. Instale o **Git** → https://git-scm.com
> 3. Edite as 2 linhas no topo do `setup_and_build.bat` com o caminho do seu GTA SA
> 4. Clique duas vezes no `setup_and_build.bat` → pronto

---

## ⚠️ VS Code vs Visual Studio — diferença importante

| | VS Code | Visual Studio |
|---|---|---|
| O que é | Editor de texto leve | IDE com compilador C++ |
| Compila C++? | ❌ Não sozinho | ✅ Sim (MSVC incluído) |
| O que precisas | — | **Este aqui** |

**VS Code sozinho não compila C++.** Para compilar este mod precisas do **Visual Studio Community** (gratuito) com o workload `Desktop development with C++`.

Podes ter os dois instalados ao mesmo tempo — não há conflito.

---

## O que é este ficheiro

`grove_recruit_standalone.asi` é uma DLL renomeada que o GTA SA carrega automaticamente na inicialização (via ASI Loader). Substitui completamente o `grove_recruit_follow.cs` — não precisa de CLEO para funcionar.

---

## Requisitos

| Ferramenta | Link | Notas |
|---|---|---|
| **Windows** (10 ou 11) | — | Necessário para compilar e jogar |
| **Visual Studio 2019 ou 2022** | https://visualstudio.microsoft.com/ | Gratuito (Community). Workload: **"Desktop development with C++"** |
| **Git** | https://git-scm.com | Para clonar o plugin-sdk automaticamente |
| **ASI Loader** | https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases | Ver passo 5 |

---

## Caminho rápido (script automático)

1. **Instale o Visual Studio Community 2022** se ainda não o fez:
   - Vai a https://visualstudio.microsoft.com/
   - Descarrega e instala o **Visual Studio Community**
   - Durante a instalação, selecciona o workload **"Desktop development with C++"**

2. **Instale o Git** se ainda não o tem:
   - Vai a https://git-scm.com e instala com as opções padrão

3. **Edita o `setup_and_build.bat`** num editor de texto:
   ```bat
   set GTA_SA_PATH=C:\Program Files (x86)\Rockstar Games\GTA San Andreas
   set PLUGIN_SDK_DIR=C:\dev\plugin-sdk
   ```
   Substitui pelo caminho real do teu GTA SA. O `PLUGIN_SDK_DIR` pode ficar em `C:\dev\plugin-sdk` (será criado automaticamente).

4. **Clica duas vezes** no `setup_and_build.bat`

O script faz automaticamente:
- Clona o DK22Pac/plugin-sdk
- Compila o `plugin_sa.lib` (necessário para o linker)
- Compila o `grove_recruit_standalone.asi`
- Copia o `.asi` para a pasta do GTA SA

---

## Caminho manual (passo a passo)

### Passo 1 — Instalar o Visual Studio

1. Vai a: https://visualstudio.microsoft.com/
2. Descarrega **Visual Studio Community 2022** (gratuito)
3. Durante a instalação, selecciona o workload:  
   **"Desktop development with C++"**
4. Clica em Instalar e aguarda (~5-10 GB de download)

> **Não confundir com VS Code** — são produtos diferentes. O VS Code é um editor de texto; o Visual Studio é uma IDE com compilador MSVC integrado.

---

### Passo 2 — Clonar o plugin-sdk

Abre uma janela de comando (cmd ou PowerShell) e executa:

```bat
git clone https://github.com/DK22Pac/plugin-sdk.git C:\dev\plugin-sdk
```

Isso cria a pasta `C:\dev\plugin-sdk` com todos os headers necessários.

---

### Passo 3 — Compilar o plugin_sa.lib (uma vez)

O `plugin_sa.lib` é necessário para o linker. Só precisas fazer isto uma vez.

1. Abre `C:\dev\plugin-sdk\plugin_sa\plugin_sa.vcxproj` no Visual Studio
2. Selecciona **Release** e **Win32** no topo
3. Menu **Build → Build Project**
4. Aguarda — o ficheiro `plugin_sa.lib` é gerado em `C:\dev\plugin-sdk\output\`

> Se usares o `setup_and_build.bat`, este passo é feito automaticamente.

---

### Passo 4 — Abrir o projeto no Visual Studio

Abre o ficheiro:
```
grove_recruit_asi\grove_recruit_standalone.vcxproj
```

> **Primeira vez:** O VS pode pedir para "retargetar" o projeto. Aceita com os valores padrão.

---

### Passo 5 — Apontar para o plugin-sdk

O projeto lê o caminho do plugin-sdk de uma variável de ambiente.

**Opção A (recomendada) — variável de ambiente:**

```bat
REM Execute isto num cmd de administrador UMA VEZ, depois reinicie o VS
setx PLUGIN_SDK "C:\dev\plugin-sdk"
```

Depois fecha e abre o Visual Studio.

**Opção B — editar o .vcxproj directamente:**

Abre `grove_recruit_standalone.vcxproj` num editor de texto e edita:
```xml
<PLUGIN_SDK_PATH Condition="'$(PLUGIN_SDK)'==''">C:\dev\plugin-sdk</PLUGIN_SDK_PATH>
```
Substitui `C:\dev\plugin-sdk` pelo caminho real onde clonaste o plugin-sdk.

---

### Passo 6 — Compilar

No Visual Studio:
1. No topo, selecciona **Release** e **Win32** (não x64 — GTA SA é 32-bit!)
2. Menu **Build → Build Solution** (ou `Ctrl+Shift+B`)
3. Aguarda — deve aparecer `Build succeeded` na barra de estado

O ficheiro `.asi` é gerado em:
```
grove_recruit_asi\Release\grove_recruit_standalone.asi
```

---

### Passo 7 — Instalar o ASI Loader no GTA SA

O ASI Loader permite que o GTA SA carregue ficheiros `.asi`. Só é necessário fazer isto uma vez.

1. Vai a: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
2. Descarrega `Ultimate-ASI-Loader.zip`
3. Extrai e copia **`d3d8.dll`** para a pasta raiz do GTA SA
   - Se o GTA SA já usa `d3d8.dll` para outro mod, usa `dinput8.dll` em vez disso

---

### Passo 8 — Instalar o .asi

Copia `grove_recruit_standalone.asi` para a **pasta raiz do GTA SA**:
```
C:\Program Files (x86)\Rockstar Games\GTA San Andreas\grove_recruit_standalone.asi
```

> **Remover o .cleo antigo:** Se tiveres o `grove_recruit_follow.cs` na pasta `CLEO\`, podes removê-lo — o standalone substitui-o completamente.

---

### Passo 9 — Testar no jogo

1. Inicia o GTA SA
2. Vai para Grove Street
3. Usa as teclas:

| Tecla | Acção |
|---|---|
| **Y** | Recrutar membro do Grove (ou dispensar se já activo) |
| **U** | Mandar recruta entrar no carro mais próximo |
| **G** | Entrar/sair do carro do recruta como passageiro |
| **H** | Ciclar modo de condução |
| **N** | Alternar agressividade |
| **B** | Alternar drive-by |

#### Modos de condução (tecla H)

| Modo | Comportamento |
|---|---|
| **CIVICO-D** ★ | Road-following idêntico ao NPC vanilla — segue a estrada certinho |
| **CIVICO-E** | Segue o carro do jogador a distância, também via road-graph |
| **DIRETO** | Vai directamente às coordenadas do jogador (bom offroad) |
| **PARADO** | Para o carro e aguarda |

---

## Problemas comuns

| Erro | Solução |
|---|---|
| Script bat fecha imediatamente sem fazer nada | Abre o bat num editor de texto e verifica as paths. Para ver os erros: abre o **cmd** (Iniciar → cmd), navega até à pasta (`cd grove_recruit_asi`) e executa `setup_and_build.bat` directamente — a janela fica aberta e mostra os erros. |
| `Visual Studio nao encontrado` | Instala o Visual Studio Community 2022 com workload "Desktop development with C++" |
| `Git nao encontrado` | Instala o Git em https://git-scm.com |
| `Cannot open include file: 'plugin.h'` | O caminho do plugin-sdk está errado. Verifica o Passo 5. |
| `LNK1104: cannot open file 'plugin_sa.lib'` | O plugin-sdk não foi compilado. Faz o Passo 3 (ou volta a correr o bat — faz isso automaticamente). |
| `.asi` não carrega no jogo | ASI Loader não está instalado (Passo 7). |
| Jogo crasha ao iniciar | Verifica se compilaste para **Win32** (não x64). |
| `Build succeeded` mas não encontra o `.asi` | Procura em `Release\grove_recruit_standalone.asi` ou `Debug\` conforme a configuração escolhida. |

---

## Estrutura de ficheiros

```
grove_recruit_asi\
├── grove_recruit_standalone.cpp    ← código fonte (editar aqui)
├── grove_recruit_standalone.vcxproj ← projecto Visual Studio
├── setup_and_build.bat             ← script automático (clonar + compilar + copiar)
├── BUILD.md                        ← este ficheiro
└── PLUGINSDK_ANALISE.md            ← documentação técnica
```
