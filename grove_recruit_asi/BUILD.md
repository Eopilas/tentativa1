# Como compilar o grove_recruit_standalone.asi

> **TL;DR — caminho mais fácil:**  
> Edite `setup_and_build.bat` com o caminho do seu GTA SA → clique duas vezes → pronto.

---

## O que é este ficheiro

`grove_recruit_standalone.asi` é uma DLL renomeada que o GTA SA carrega automaticamente na inicialização (via ASI Loader). Substitui completamente o `grove_recruit_follow.cs` — não precisa de CLEO para funcionar.

---

## Requisitos

| Ferramenta | Link | Notas |
|---|---|---|
| **Windows** (10 ou 11) | — | Necessário para compilar e jogar |
| **Visual Studio 2019 ou 2022** | https://visualstudio.microsoft.com/ | Gratuito (Community) |
| **Git** | https://git-scm.com | Para clonar o plugin-sdk |
| **ASI Loader** | https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases | Ver passo 5 |

No Visual Studio, instale o workload **"Desktop development with C++"**.

---

## Caminho rápido (script automático)

1. Abra `grove_recruit_asi\setup_and_build.bat` num editor de texto
2. Edite as duas linhas no topo:
   ```bat
   set GTA_SA_PATH=C:\Program Files (x86)\Rockstar Games\GTA San Andreas
   set PLUGIN_SDK_DIR=C:\dev\plugin-sdk
   ```
3. Guarde e clique duas vezes no ficheiro
4. O script clona o plugin-sdk, compila e copia o `.asi` automaticamente

---

## Caminho manual (passo a passo)

### Passo 1 — Clonar o plugin-sdk

Abra uma janela de comando (cmd ou PowerShell) e execute:

```bat
git clone https://github.com/DK22Pac/plugin-sdk.git C:\dev\plugin-sdk
```

Isso cria a pasta `C:\dev\plugin-sdk` com todos os headers necessários.

---

### Passo 2 — Abrir o projeto no Visual Studio

Abra o ficheiro:
```
grove_recruit_asi\grove_recruit_standalone.vcxproj
```

> **Primeira vez:** O VS pode pedir para "retargetar" o projeto. Aceite com os valores padrão.

---

### Passo 3 — Apontar para o plugin-sdk

O projeto lê o caminho do plugin-sdk de uma variável de ambiente ou de uma linha no `.vcxproj`.

**Opção A (recomendada) — variável de ambiente:**

```bat
REM Execute isto num cmd de administrador UMA VEZ, depois reinicie o VS
setx PLUGIN_SDK "C:\dev\plugin-sdk"
```

Depois feche e abra o Visual Studio.

**Opção B — editar o .vcxproj directamente:**

Abra `grove_recruit_standalone.vcxproj` num editor de texto e edite:
```xml
<PLUGIN_SDK_PATH Condition="'$(PLUGIN_SDK)'==''">C:\dev\plugin-sdk</PLUGIN_SDK_PATH>
```
Substitua `C:\dev\plugin-sdk` pelo caminho real onde clonou o plugin-sdk.

---

### Passo 4 — Compilar

No Visual Studio:
1. No topo, seleccione **Release** e **Win32** (não x64 — GTA SA é 32-bit!)
2. Menu **Build → Build Solution** (ou `Ctrl+Shift+B`)
3. Aguarde — deve aparecer `Build succeeded` na barra de estado

O ficheiro `.asi` é gerado em:
```
grove_recruit_asi\Release\grove_recruit_standalone.asi
```

---

### Passo 5 — Instalar o ASI Loader no GTA SA

O ASI Loader permite que o GTA SA carregue ficheiros `.asi`. Só é necessário fazer isto uma vez.

1. Vá a: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases
2. Descarregue `Ultimate-ASI-Loader.zip`
3. Extraia e copie **`d3d8.dll`** para a pasta raiz do GTA SA
   - Se o GTA SA já usa `d3d8.dll` para outro mod, use `dinput8.dll` em vez disso

---

### Passo 6 — Instalar o .asi

Copie `grove_recruit_standalone.asi` para a **pasta raiz do GTA SA**:
```
C:\Program Files (x86)\Rockstar Games\GTA San Andreas\grove_recruit_standalone.asi
```

> **Remover o .cleo antigo:** Se tiver o `grove_recruit_follow.cs` na pasta `CLEO\`, pode removê-lo — o standalone substitui-o completamente.

---

### Passo 7 — Testar no jogo

1. Inicie o GTA SA
2. Vá para Grove Street
3. Use as teclas:

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
| `Cannot open include file: 'plugin.h'` | O caminho do plugin-sdk está errado. Verifique o Passo 3. |
| `LNK1104: cannot open file 'plugin_sa.lib'` | O plugin-sdk não foi compilado. Vá à pasta plugin-sdk, abra `GTA SA.sln` e compile o projecto `plugin_sa` em Release/Win32 primeiro. |
| `.asi` não carrega no jogo | ASI Loader não está instalado (Passo 5). |
| Jogo crasha ao iniciar | Verifique se compilou para **Win32** (não x64). |
| `Build succeeded` mas não encontra o `.asi` | Procure em `Release\grove_recruit_standalone.asi` ou `Debug\` conforme a configuração escolhida. |

---

## Compilar o plugin_sa.lib (se necessário)

Se o `LNK1104` aparecer, precisa de compilar a lib do plugin-sdk uma vez:

```bat
cd C:\dev\plugin-sdk
REM Abra "GTA SA.sln" no Visual Studio
REM Seleccione "Release | Win32"
REM Clique com o botão direito em "plugin_sa" → Build
REM O ficheiro plugin_sa.lib é gerado em output\
```

---

## Estrutura de ficheiros

```
grove_recruit_asi\
├── grove_recruit_standalone.cpp    ← código fonte (editar aqui)
├── grove_recruit_standalone.vcxproj ← projecto Visual Studio
├── setup_and_build.bat             ← script automático
├── BUILD.md                        ← este ficheiro
└── PLUGINSDK_ANALISE.md            ← documentação técnica
```
