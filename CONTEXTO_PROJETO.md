# CONTEXTO COMPLETO DO PROJETO — Grove Street Recruit Mod (GTA SA)

> **Documento de handoff para outra IA (ex: Gemini).**  
> Este ficheiro documenta a linha de tempo completa, PRs, decisões técnicas, bugs descobertos,
> estado actual e próximos passos do projecto — suficiente para continuar o trabalho sem
> precisar de ler todos os commits.

---

## 1. DESCRIÇÃO DO PROJECTO

**O que é:** mod para GTA San Andreas (PC, versão 1.0 US) que substitui o comportamento
"ímã" dos companheiros de gangue por **IA veicular real**. O recruta dirige por conta própria,
segue o jogador em carro, navega no road-graph nativo do motor RenderWare, desvia de
obstáculos e mantém distância de segurança.

**Problema no jogo vanilla:** recrutas simplesmente teleportam ou correm em linha recta
para o jogador — sem lógica de rua, sem desvio de carros, sem respeito ao trânsito.

**Stack:**
- **GTA SA 1.0 US** — executável original sem patches
- **CLEO 4** — scripting via opcodes RenderWare nativos
- **Sanny Builder 3** — compilador de scripts CLEO
- **DK22Pac/plugin-sdk** — SDK C++ para criar ficheiros `.asi` (DLLs nativas)
- **ASI Loader** — injector de DLLs no processo do jogo
- **Visual Studio** — compilação do ASI (MSVC, Win32, Release/Debug)
- **Repositório:** `Reinoso-Pilado/tentativa1` (GitHub)

---

## 2. FICHEIROS PRINCIPAIS

| Ficheiro | Descrição |
|---|---|
| `grove_recruit_follow.cs` | Script CLEO principal (~1971 linhas). Controla recruta a pé + modos de condução. |
| `grove_recruit_asi/` | Pasta do projecto ASI (standalone, substitui CLEO em C++) |
| `grove_recruit_asi/teste/source/Main.cpp` | Código principal ASI (~1787 linhas). Toda a lógica aqui. |
| `grove_recruit_asi/grove_recruit_standalone.cpp` | Protótipo intermediário (antes do Main.cpp completo) |
| `grove_recruit_asi/grove_recruit_asi.cpp` | Protótipo inicial hybrid CLEO+ASI |
| `grove_recruit_asi/PLUGINSDK_ANALISE.md` | Análise comparativa CLEO vs plugin-sdk |
| `grove_recruit_asi/BUILD.md` | Guia de compilação Visual Studio |
| `grove_recruit_asi/grove_recruit.log` | Log de runtime produzido pelo ASI em jogo |
| `grove_recruit_asi/depuração.txt` | Notas de debug do utilizador (PT-BR) |
| `grove_weapon_assign.cs` | Mod auxiliar: atribuição de armas ao recruta |
| `grove_personal_car.cs` | Mod auxiliar: carro pessoal persistente do jogador |
| `README.md` | Documentação completa com arquitectura e controles |
| `SASCM.ini` / `commands.TXT` / `commands.def` | Referências de opcodes CLEO para Sanny Builder |
| `More docs/` | Classes, enums e opcodes nativos do jogo |

---

## 3. LINHA DE TEMPO COMPLETA (cronológica)

### Dia 1 — 12 de Março de 2026

**Início absoluto do projecto.** O utilizador criou o repositório com um único ficheiro `teste`
e começou a trabalhar no mod do zero.

| SHA (abrev) | Hora UTC | Evento |
|---|---|---|
| `bd1fc86` | 12/03 12:19 | **Initial commit** — repositório criado |
| `34687e2` | 12/03 12:20 | `teste` — ficheiro placeholder |
| `a2b6451` | 12/03 15:21 | **PR #1 — Plano inicial** do agente Copilot |
| `c6fde2c` | 12/03 15:43 | feat: arquitectura completa do mod — grove_recruit_follow.cs, README, SASCM.ini |
| `6292d08` | 12/03 16:28 | fix: `0ADE` opcode errado → `0ACD` |
| `0c8238e` | 12/03 16:42 | fix: `01CA` parâmetros demais → substituído por `0850` (TASK_FOLLOW_FOOTSTEPS) |
| `3c2c436` | 12/03 17:15 | fix: todos os opcodes via SASCM.ini (`0407/0176→0AB5`, `0615→05CB`, etc.) |
| `bd16920` | 12/03 18:02 | fix: crash em runtime — ordem errada de parâmetros em `0209/009A/04C4/03C0/0395` |
| `9a71cc9` | 12/03 18:35 | fix: crash `04C4` → substituído por `00A0+000B`; ambiguidade `0395/03C0` |
| `2dca1ac` | 12/03 18:57 | **fix crash #3:** `$PLAYER_ACTOR` era 0 em script externo → substituído por `01F5+3@` |
| `80593b2` | 12/03 16:17 | **Merge PR #1** (único PR merged até hoje) |

**O utilizador subiu logs de sucesso** (`f59c36c`) mostrando que o script compilava e corria.

---

**PR #2** foi aberto ainda no Dia 1 — `Fix CIVICO driving, car persistence, weapons, toggles`.

| SHA (abrev) | Hora UTC | Evento |
|---|---|---|
| `460c909` | 12/03 19:31 | Plano detalhado |
| `5b75e83` | 12/03 20:24 | docs: guardar contexto GTA SA em memória do agente |
| `bcaac47` | 12/03 20:33 | feat: integração de grupo vanilla, toggle U para saída, scan `092B` |
| `7b53084` | 12/03 20:46 | feat: **Estado 3** — recruta dirige (G pressionado), jogador passageiro |
| `3c79b09` | 12/03 21:15 | fix: dessinc Estado 3 em carros de 4 portas |
| `279122f` | 12/03 21:30 | fix: `06C9` antes de `05CB` — bug de saída instantânea em carros 4 portas |
| `f99b52d` | 12/03 21:59 | feat: remapear teclas 1/2/3/4, waypoint GPS, NPC traffic modes |
| `851b818` | 12/03 22:16 | feat: substituir 4 modos velocidade por toggle CIVICO/DIRETO/PARADO |
| `ef4319a` | 12/03 22:27 | fix: tecla 4 nunca disparava + `driver_behaviour` sobrescrevia traffic |
| `1dfe8ef` | 12/03 22:40 | fix: `00A9` cancelava `07F8` em CIVICO + limite 20 km/h errado em Estado 3 |

---

### Dia 2 — 13 de Março de 2026

Desenvolvimento intensivo de modos de condução CIVICO múltiplos e prototype ASI.

| SHA (abrev) | Hora UTC | Evento |
|---|---|---|
| `81ed669` | 13/03 00:32 | Plano detalhado para 4 requisitos novos |
| `13abd0c` | 13/03 00:41 | feat: fix CIVICO, force-respawn, teleport safety; weapon assign + personal car mods |
| `49dc1d6` | 13/03 02:18 | feat: car persistence, driveby toggle, aggression toggle, weapon selection por membro |
| `da297ad` | 13/03 13:38 | **PR #5 — Plano** (5 driving modes) |
| `b650eec` | 13/03 13:46 | feat: **5 driving modes** (CIVICO-0/6, HIBRIDO, DIRETO, PARADO) |
| `07b4455` | 13/03 13:59 | fix: key-chain bug em grove_weapon_assign + name_thread |
| `9a5ffa0` | 13/03 14:27 | fix: PARADO mode (`00A9` para cancelar follow task) + reduzir raio CIVICO |
| `355a50c` | 13/03 14:50 | refactor: comentário SF_DRIVE_MODE |
| `16151c0` | 13/03 14:57 | fix: CIVICO road nodes + detecção de dispand + crash personal car |
| `be9a1b7` | 13/03 15:07 | fix: remover `00A5` do path pessoal (crash no load) |
| `d497a5d` | 13/03 15:46 | fix: CIVICO follow lights + garage auto-detect |
| `5b88749` | 13/03 16:09 | feat: **3 variantes CIVICO** (A=06E1 faraway, B=06E1 close, C=07F8 reference) |
| `ebf3cea` | 13/03 16:51 | feat: `04D3` road-snap em STATE3 CIVICO + velocidades A=40/B=50/C=60 |
| `1e1f918` | 13/03 17:45 | feat: **8 driving modes** (CIVICO-0 a F + DIRETO + PARADO) |
| `13ca02c` | 13/03 18:44 | fix: CIVICO A/B correcto + CIVICO-F + deslocar tecla DIRETO→7 PARADO→8 |
| `5ca4f28` | 13/03 19:43 | Redesign modes B/C + docs road node + fix CIVICO-C sem `04D3` |
| `4bd9342` | 13/03 20:19 | Improve: anti-collision offset, `00AF=1`, CIVICO-F guard, DIRETO 60 km/h |
| `b3b2313` | 13/03 20:46 | fix: STATE3_CIV `00AF=0→1` (road-follow flag) |
| `39769eb` | 13/03 21:31 | fix: spinning parado, DIRETO offroad/canal, curvas invade faixas |
| `0007076` | 13/03 21:41 | docs: spinning guard + countdown DIRETO |
| `24a52ab` | 13/03 22:04 | fix: revert AvoidCars(2), reduzir follow distance, default→CIVICO-D |
| `ee2b89b` | 13/03 22:18 | fix: **guard 00EC 50m** — previne crazy behavior offroad em CIVICO |
| `56e5eaa` | 13/03 22:34 | **PR #7 — Plano:** protótipo ASI + análise plugin-sdk |
| `4a85b5b` | 13/03 22:44 | docs: hybrid vs standalone ASI (recomendação) |
| `167d22f` | 13/03 22:58 | feat: **standalone ASI prototype** + VS project + BUILD.md |
| `8332c96` | 13/03 23:26 | fix: build.bat vswhere + auto-build plugin_sa.lib |

---

### Dia 3 — 14 de Março de 2026

Foco total no ASI standalone. Múltiplos crashes, bugs profundos de engine descobertos.

| SHA (abrev) | Hora UTC | Evento |
|---|---|---|
| `ba14e71` | 14/03 03:56 | fix C2039: `m_pPlayerGroup` → `CPedGroups::ms_groups` |
| `f6ae17b` | 14/03 04:28 | fix: standalone ASI CLEO-parity — group follow, teclas 1/2/3/4, g_car, passive timer |
| `9be3a00` | 14/03 04:30 | fix: code review — comentários, IsCarValid, passive timer |
| `8a2d3a9` | 14/03 05:15 | **fix crash: key 1** — campo separação errado (`+0x30` vs `+0x2C`) crashava em `0x42C8021C` |
| `e986a41` | 14/03 06:44 | **fix: recruta congelado e auto-dismissed** — 4 bugs corrigidos + grove_debug.log |
| `e2d2d29` | 14/03 06:58 | **fix: 5 bugs** — `LoadAllRequestedModels(true)` bloqueante, `bDoesntListenToPlayerGroupCommands=0`, `AddFollower` backup, timers |
| `9cbb65e` | 14/03 14:45 | feat: **sistema de logging completo** — níveis EVENT/GROUP/TASK/DRIVE/AI/KEY/WARN/ERROR |
| `3696229` | 14/03 14:47 | refactor: `g_logAiTimer→g_logAiFrame`, `_IOLBF`, comentários |
| `7f0cbb6` | 14/03 15:12 | **fix CRÍTICO: `PED_TYPE_GANG1→GANG2`** (GSF=8, não 7) + logging activeTask/pedType/memberCount |
| `1d7b1d7` | 14/03 16:32 | fix: respect voiceline + logs heading/STAT_RESPECT/direcção |
| `a0a0bed` | 14/03 18:42 | **fix: boost de respeito PERSISTENTE** — `ActivateRespectBoost()/DeactivateRespectBoost()` |
| `9dfc501` | 14/03 18:53 | Utilizador sobe ficheiros (logs, exe crash reports, scrlog) |
| `1fb9566` | 14/03 19:xx | **Commit actual (esta PR):** PRE_JOIN scan, TASK_CHANGE tracker, POST_FOLLOW_CHECK, WRONG_DIR_START/END, JOIN_ROAD before/after |

---

## 4. PRs E ESTADO

| PR | Título | Estado | Notas |
|---|---|---|---|
| **#1** | Fix crash on Y key press | **Merged** (12/03) | Único PR merged. Fix `$PLAYER_ACTOR=0` em script externo. |
| **#2** | Fix CIVICO driving, car persistence, weapons, toggles | **Open** (não merged) | Muitos commits empilhados. Inclui toda a evolução do CLEO. |
| **#3** | Add ideias_imersao_gta_sa.md | Closed (sem merge) | Idéias de features futuras. |
| **#4** | [WIP] Explain traffic behaviour modes | Closed (sem merge) | Documentação intermedia. |
| **#5** | feat: 5 driving modes | Closed (sem merge) | Substituído por PR #2. |
| **#6** | ASI: persistent respect boost + CLEO 0850 parity | **Open** (não merged) | Contém `ActivateRespectBoost`. |
| **#7** | docs: plugin-sdk setup, CLEO vs ASI tradeoffs | **Open** (não merged) | Contém `PLUGINSDK_ANALISE.md`, `BUILD.md`. |
| **actual** | (branch `copilot/fix-issues-in-previous-branch`) | **Em progresso** | Diagnósticos avançados (TASK_CHANGE, PRE_JOIN, etc.) |

---

## 5. BUGS DESCOBERTOS E ESTADO DE RESOLUÇÃO

### 5.1 Bug Crítico: Recruta Congelado (on-foot, NÃO RESOLVIDO)

**Sintoma:** recruta spawna, fica parado (task=203=STAND_STILL), nunca segue o jogador.
`TellGroupToStartFollowingPlayer` é chamado 15+ vezes sem efeito.

**Causa raiz descoberta pelos logs:**
1. `MakeThisPedJoinOurGroup(g_recruit)` falha silenciosamente (**SEMPRE**, mesmo com pedType=8 e respect=1000)
2. `AddFollower` backup adiciona o ped ao array do grupo mas NÃO configura o Decision Maker (DM)
3. Sem DM configurado, `TellGroupToStartFollowingPlayer` → `CPedGroupDefaultTaskAllocatorFollowAnyMeans::CreateFirstSubTask` (0x666160) → **no-op**
4. Task fica em 400 (desconhecida, possivelmente walk/spawn AI) e depois cai para 203 (STAND_STILL)

**Hipótese principal (não confirmada, aguarda PRE_JOIN log):**
O ped spawna como PED_TYPE_GANG2 (=8, GSF). GTA SA automaticamente coloca GSF NPCs em grupos de gang internos. Quando tentamos adicionar ao grupo do jogador (CPedGroups::ms_groups[0]), o jogo recusa porque o ped já pertence a outro grupo slot.

**Fix tentado e FALHOU:**
- Boost de respeito persistente para 1000 (`ActivateRespectBoost`) — não ajudou pois o problema é o DM, não o respeito
- Alterar pedType=GANG1→GANG2 — ajudou a não ser tratado como inimigo mas não resolveu o freeze
- 5 bugs de spawn sequence corrigidos — não resolveram o freeze

**Fix correcto provável para próxima iteração:**
Implementar o equivalente ASI do opcode CLEO `0850 AS_actor recruit follow_actor player`.
O CLEO 0850 cria `CTaskComplexGangFollower` DIRECTAMENTE na task manager do ped, bypassando completamente o chain Group → DM → Respects → CreateFirstSubTask.

```cpp
// Endereço da função interna: CreateFirstSubTask(0x666160)
// OU: usar CPed::GetIntelligence()->AddTaskComplexGangFollower(player)
// OU: encontrar endereço de CTaskComplexGangFollower::ctor e atribuir directamente
```

**Prova de diagnóstico esperada no próximo log (com PRE_JOIN + TASK_CHANGE):**
```
PRE_JOIN: ped_em_grupo=X(slot=Y) FindMaxGroupMembers=Z respect=1000
TASK_CHANGE: 400 -> 203  (spawn AI -> stand still, TellGroup foi no-op)
POST_FOLLOW_CHECK(3fr): activeTask=203 (PROBLEMA: TellGroup foi no-op)
```

---

### 5.2 Bug Moderado: Recruta Conduz na Direcção Errada (NÃO RESOLVIDO)

**Sintoma:** recruta em carro entra na estrada mas vai para o sentido contrário, não segue
o road-graph como NPC vanilla. Erra curvas, sobe passeios.

**Log evidence (actual):**
```
heading=0.824  targetH=2.552  deltaH=1.728(WRONG_DIR!)
# targetH ≈ 2.552 rad é CONSTANTE através de muitos frames
# speedMult=1.00 SEMPRE (never detects curves)
# linkId=4294966814 = 0xFFFFFFFE = INVALID (após JoinRoadSystem em CIVICO_E)
```

**Causas descobertas:**
1. **targetH constante:** `ClipTargetOrientationToLink` devolve sempre ≈2.55 rad (a direcção do nó ao qual o carro snapou inicialmente). Não actualiza conforme o carro avança no road-graph.
2. **Mission flip:** CIVICO_D (EscortRearFaraway=67) e CIVICO_E (FollowCarFaraway=52) sobrescrevem `m_nCarMission` para 11 (STOP_FOREVER) quando o player está distante ou quando o road-graph não encontra path. Isto faz o carro parar completamente em vez de seguir.
3. **linkId inválido:** `JoinCarWithRoadSystem` por vezes produz `linkId=0xFFFFFFFE` — o carro não ficou ancorado num nó válido.
4. **speedMult sempre 1.00:** `FindSpeedMultiplierWithSpeedFromNodes` recebe `m_nStraightLineDistance=20` fixo — o parâmetro não está a ser actualizado com a distância real ao próximo nó.

**Comportamento observado por modo:**
- `CIVICO_D (MISSION_43=EscortRearFaraway)`: começa OK (mission=67), degrada para mission=11 após atingir a posição do jogador — fica parado. Descrição do utilizador: *"ele só vai onde eu tava quando activo e depois para"*.
- `CIVICO_E (MISSION_34=FollowCarFaraway)`: mesmo problema mas parece comportar-se ligeiramente melhor em curvas depois do WRONG_DIR inicial.
- `DIRETO (MISSION_8=GotoCoords)`: vai para a posição do jogador e para — também fica parado (mesma queixa).
- `OFFROAD auto`: quando offroad é detectado e mission muda para GOTOCOORDS(8) + PloughThrough(3), o heading alinha-se correctamente. Isto sugere que o problema é específico às missions de "escort/follow".

**Fix provável para próxima iteração:**
Usar `MISSION_8 (GotoCoords_Accurate=12)` com actualização da coordenada destino por frame:
```cpp
// Em ProcessDrivingAI, cada frame:
CVector playerPos = player->GetPosition();
CVector offset = {0, -10.0f, 0}; // 10m atrás
ap.m_vecDestinationCoors = playerPos + offset;
ap.m_nCarMission = MISSION_GOTOCOORDS; // ou MISSION_12=Accurate
```
Alternativamente, investigar porque `EscortRearFaraway` degenera para STOP_FOREVER e como os NPCs vanilla evitam este problema.

---

### 5.3 Bug Resolvido: Crash ao Pressionar Tecla 1

`m_fSeparationRange` (+0x30 em CPedGroup) era interpretado como ponteiro para `CPedGroupIntelligence*`.
Escrever `100.0f = 0x42C80000` causava crash em `0x42C8021C`.
**Fix:** usar `m_groupMembership.m_fMaxSeparation` (+0x2C). — Resolvido em commit `8a2d3a9`.

---

### 5.4 Bug Resolvido: PED_TYPE errado (GANG1 vs GANG2)

Recrutas spawnavam como `PED_TYPE_GANG1=7` (Ballas/Vagos). GSF = `PED_TYPE_GANG2=8`.
Com tipo errado: `MakeThisPedJoinOurGroup` falha silenciosamente, DM nunca configura,
recruta era tratado como inimigo por outros membros GSF.
**Fix:** usar `PEDTYPE_GANG2=8`. — Resolvido em commit `7f0cbb6`.

---

### 5.5 Bug Resolvido: Recruta Auto-Dispensado

`g_groupRescanTimer` não era reiniciado no spawn. Na primeira iteração de rescan (120 frames),
`FindRecruitMemberID` podia retornar <0 se o grupo ainda não estava completamente inicializado,
causando DismissRecruit automático.
**Fix:** `g_groupRescanTimer=0` no spawn e no dismiss. — Resolvido em commit `e2d2d29`.

---

## 6. ARQUITECTURA DO ASI (Main.cpp)

### Estado da máquina de estados (enum `ModState`)

```
INACTIVE  → (KEY 1) →  ON_FOOT
ON_FOOT   → (KEY 2, recruta entra em carro) → ENTER_CAR
ENTER_CAR → (animação completa) → DRIVING
DRIVING   → (KEY 2, sai do carro) → ON_FOOT
DRIVING   → (KEY 3, jogador entra) → PASSENGER
PASSENGER → (KEY 3, jogador sai) → DRIVING
Qualquer estado → (recruta morre) → INACTIVE (DismissRecruit)
Qualquer estado → (KEY 1 novamente) → INACTIVE (DismissRecruit)
```

### Modos de condução (`enum DriveMode`)

| Enum | CLEO equiv. | Mission ID | Comportamento |
|---|---|---|---|
| `CIVICO_D` | `06E1` | `MISSION_43=67` (EscortRearFaraway) | Escolta atrás, usa road-graph. DEFAULT. |
| `CIVICO_E` | `06E1` | `MISSION_34=52` (FollowCarFaraway) | Segue à distância, road-graph. |
| `DIRETO` | `00A7` | `MISSION_8=8` (GotoCoords) | Vai directamente, sem road-graph. |
| `PARADO` | `00A9` | `MISSION_11=11` (StopForever) | Para completamente. |

### Sequência de spawn correcta (em HandleKeys KEY 1)

```
1. CPopulation::AddPed(modelo, PED_TYPE_GANG2=8, posição)   ← pedType=8 CRÍTICO
2. SetCharCreatedBy(g_recruit, 2=MISSION_PED)
3. GiveWeapon + Ammo
4. bNeverLeavesGroup = 1
5. bKeepTasksAfterCleanUp = 1
6. bDoesntListenToPlayerGroupCommands = 0                   ← =0 CRÍTICO
7. ActivateRespectBoost()                                   ← CStats::SetStatValue(68, 1000)
8. AddRecruitToGroup:
   a. MakeThisPedJoinOurGroup(g_recruit)                    ← SEMPRE FALHA
   b. AddFollower backup                                    ← funciona como fallback
   c. m_groupMembership.m_fMaxSeparation = 100.0f
   d. TellGroupToStartFollowingPlayer(aggr, false, false)   ← no-op (DM não configurado)
9. g_initialFollowTimer = 300  (burst de 5s a re-emitir TellGroup)
```

### Funções internas usadas (endereços hardcoded GTA SA 1.0 US)

```cpp
// CPedIntelligence::Respects (verifica nível de respeito para seguir)
typedef bool (__thiscall* FnRespects_t)(void*, void*);
// addr: 0x601C90

// FindMaxNumberOfGroupMembers (baseado em STAT_RESPECT=68, retorna max slots)
typedef int (__cdecl* FnFindMaxGroupMembers_t)();
// addr: 0x559A50

// CreateFirstSubTask (cria CTaskComplexGangFollower no ped — o que 0850 faz)
// addr: 0x666160  ← PRÓXIMA ITERAÇÃO: usar este directamente
```

### Globals de tracking (para debugging)

```cpp
static int  g_prevRecruitTaskId = -999;  // rastreio TASK_CHANGE per-frame
static int  g_postFollowTimer   = 0;     // POST_FOLLOW_CHECK 3 frames após TellGroup
static bool g_wasWrongDir       = false; // rastreio WRONG_DIR_START/END
```

---

## 7. SISTEMA DE LOGGING

**Ficheiro:** `grove_recruit.log` (na pasta do GTA SA, truncado em cada sessão)

**Formato:** `[FRAME_HEX][NÍVEL] mensagem`

**Níveis:**
| Nível | Uso |
|---|---|
| `EVENT` | Eventos de estado (spawn, dismiss, mode change) |
| `GROUP` | Operações de grupo (MakeThisPedJoin, AddFollower, RESCAN) |
| `TASK` | Operações de tarefa (TellGroupFollow, TASK_CHANGE, POST_FOLLOW_CHECK) |
| `DRIVE` | Modos de condução (SetupDriveMode, WRONG_DIR_START/END, JOIN_ROAD) |
| `AI` | Dump periódico (ON_FOOT cada 2s, DRIVING cada 2s) |
| `KEY` | Teclas pressionadas |
| `WARN` | Avisos (MakeThisPedJoin falhou, etc.) |
| `ERROR` | Erros críticos (recruta inválido, carro inválido) |

**Eventos críticos de diagnóstico (adicionados na iteração actual):**

```
PRE_JOIN: ped_em_grupo=GI(slot=SI) FindMaxGroupMembers=N respect=R playerGrp=X
→ GI>=0 significa que o ped JÁ está noutro grupo! Esta é provável causa do freeze.

TASK_CHANGE: oldId -> newId (OLD_NAME -> NEW_NAME)
→ Detecta transições em tempo real. Crítico para ver se 1207 aparece alguma vez.
→ 203→1207 = follow aceite (OK)
→ qualquer→203 = freeze (problema)

POST_FOLLOW_CHECK(3fr): activeTask=N (diagnóstico)
→ 3 frames após TellGroupToStartFollowingPlayer, mostra se teve efeito.

WRONG_DIR_START: heading=H targetH=T deltaH=D modo=M mission=N linkId=L
WRONG_DIR_END:   heading=H targetH=T deltaH=D
→ Só log em transições, não cada frame.

JOIN_ROAD: linkId A->B areaId C->D heading_pre=X heading_post=Y playerHeading=Z (status)
→ Compara estado antes/após JoinCarWithRoadSystem.
→ "linkId nao mudou" = JoinRoadSystem não encontrou nó válido próximo.
```

**IDs de tarefa relevantes:**
```
-1  = sem tarefa       200 = TASK_NONE
203 = STAND_STILL (congelado!)
264 = BE_IN_GROUP (no grupo mas sem follow)
400 = desconhecida (aparece após spawn, antes de 203)
709 = CAR_DRIVE (a conduzir, OK)
1207 = GANG_FOLLOWER (a seguir, OK — alvo do fix)
1500 = GROUP_FOLLOW_ANY_MEANS (alternativa ao 1207)
```

---

## 8. ANÁLISE DO LOG REAL (última sessão de teste)

O utilizador testou em 14/03/2026. Extracto anotado:

```
[0000471] KEY 1: spawn modelo=107 pedType=8 respect=0
[0000471] RESPECT_BOOST: ACTIVADO 0 -> 1000
[0000471] WARN: MakeThisPedJoinOurGroup FALHOU; AddFollower (backup) slot=0
[0000471] TellGroupToStartFollowingPlayer (burst)
...
[0000590] RESCAN activeTask=400 ← task misteriosa (spawn AI?)
...
[0000710] RESCAN activeTask=203 ← STAND_STILL: recruta congelado
[TellGroup chamado ~15 vezes — nenhum efeito]
...
[0007141] KEY 2: recruta entra em carro → estado DRIVING, modo=CIVICO_D
[0007141] CIVICO_D sem carro jogador → fallback DIRETO (jogador estava a pé!)
...
[0009129] DRIVING mission=67 heading=0.752 targetH=2.552 deltaH=1.800 WRONG_DIR!
[0009412] DRIVING mission=11 ← STOP_FOREVER! jogo sobrescreveu a mission
[0009412..0009892] mission=11 persistente (carro parado)
...
[0010132] CIVICO_E heading=1.162 targetH=2.553 deltaH=1.390 desalinhado
[0010432] CIVICO_E heading=1.532 deltaH=1.022 desalinhado
[0010552] CIVICO_E heading=1.730 deltaH=0.821 — a melhorar gradualmente!
...
[0014613] linkId=4294966814 ← 0xFFFFFFFE = INVÁLIDO após JoinRoadSystem!
...
[0015112] Offroad: NAO -> SIM
[0015391] Offroad: SIM -> NAO
[0015772] DRIVING mission=8 heading=2.608 targetH=2.510 deltaH=-0.098 OK!
← quando offroad activa GOTOCOORDS, o heading alinha-se correctamente!
```

**Conclusões chave do log:**
1. **Recruta SEMPRE congela** — task 400→203, nunca 1207. Fix DM necessário.
2. **CIVICO_D degrada para STOP_FOREVER** — o game overrides a mission quando não há path.
3. **targetH ≈ 2.552 constante** — ClipTargetOrientationToLink não actualiza (ou o link não muda).
4. **GOTOCOORDS funciona bem para direcção** — quando offroad activa mission=8, heading alinha.
5. **Task 400** é desconhecida — nova iteração deve adicionar lookup de nome por ID.

---

## 9. REFERÊNCIAS TÉCNICAS IMPORTANTES

### GTA SA Internals (1.0 US)
```
CPedIntelligence::Respects          0x601C90
FindMaxNumberOfGroupMembers         0x559A50
CCarCtrl::JoinCarWithRoadSystem     (no plugin-sdk via CCarCtrl.h)
CCarCtrl::ClipTargetOrientationToLink  0x422760
CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes  0x424130
CreateFirstSubTask (CTaskComplexGangFollower)  0x666160
STAT_RESPECT = 68                   (em eStats.h do plugin-sdk)
GSF PED_TYPE = GANG2 = 8            (NÃO GANG1=7 que é Ballas)
CPedGroups::ms_groups[0]            = grupo do jogador (índice 0)
m_groupMembership.m_fMaxSeparation  = +0x2C (NÃO +0x30 que é o CPedGroupIntelligence*)
```

### Opcodes CLEO relevantes
```
0850  AS_actor A1 follow_actor A2    → cria GANG_FOLLOWER directamente (bypass DM)
0631  add_member A1 to_group A2      → MakeThisPedJoinOurGroup
087F  set_char A1 never_leave_group A2
0961  set_char A1 keep_tasks_after_cleanup A2
06F0  set_group_separation A1 A2    → m_fMaxSeparation
04D3  AS_car A1 find_nearest_road_node  → JoinCarWithRoadSystem equivalent
06E1  AS_car A1 escort A2 rear_faraway  → MISSION_43
00EC  is_point A1 near_point A2 radius → proximity check (road-node guard)
```

### Plugin-SDK URLs
- SDK: https://github.com/DK22Pac/plugin-sdk
- Headers úteis: `plugin_sa/game_sa/CAutoPilot.h`, `CCarCtrl.h`, `CPedGroup.h`, `CPed.h`
- Engine source: https://github.com/jte/GTASA (arquivado Jan 2025, classes completas em Docs/graph.txt)

---

## 10. PRÓXIMA ITERAÇÃO — O QUE FAZER

### Prioridade 1 (blocker): Fix recruta a pé (on-foot freeze)

**Abordagem recomendada:** implementar equivalente de CLEO 0850 directamente.

```cpp
// Opção A: usar CreateFirstSubTask directamente
// Opção B: criar CTaskComplexGangFollower via ctor e atribuir
// Opção C: chamar GetIntelligence()->ForceTask() ou equivalente

// O que 0850 faz internamente (de acordo com análise CLEO):
// - Não usa o grupo do jogador em absoluto
// - Cria directamente CTaskComplexGangFollower(player) na task manager do ped
// - O ped passa a "seguir" o player como se fosse vanilla, mas iniciado externamente
```

**Depois de aplicar a fix:** re-compilar, testar, verificar no log:
```
TASK_CHANGE: 203 -> 1207  (ou 1500)  ← sinal de sucesso
POST_FOLLOW_CHECK(3fr): activeTask=1207 — follow bem sucedido
```

### Prioridade 2: Fix direcção de condução

**Abordagem recomendada:** actualizar coordenada destino por frame com offset do player.
```cpp
// Em ProcessDrivingAI, cada frame quando em modo CIVICO/DIRETO:
CVector dest = player->GetPosition();
// Calcular offset de 10-15m atrás baseado no heading do player
ap.m_vecDestinationCoors = dest; // + offset calculado
ap.m_nCarMission = MISSION_GOTOCOORDS_ACCURATE; // ou MISSION_GOTOCOORDS
```

**Investigar também:** porque `EscortRearFaraway(67)` reverte para `STOP_FOREVER(11)`.
Possível causa: `m_pTargetCar` perde o lock ou o road-graph não consegue calcular rota.

### Prioridade 3: Task ID 400

Identificar o que é task ID 400. Verificar em `eTaskTypes.h` do plugin-sdk se existe.
Ou fazer raw lookup por nome via `CTask::GetTaskType()` name table.

### Para diagnosticar na próxima sessão:

1. Correr a versão actual (com PRE_JOIN + TASK_CHANGE + POST_FOLLOW_CHECK)
2. Procurar no log:
   - `PRE_JOIN: ped_em_grupo=X(slot=Y)` — X>=0 confirma hipótese de grupo existente
   - `TASK_CHANGE: 203 -> ?` — ver o que acontece à task após burst inicial
   - `POST_FOLLOW_CHECK` — confirmar se TellGroup é no-op
3. Se PRE_JOIN mostra X>=0: antes de MakeThisPedJoinOurGroup, remover o ped do grupo existente
4. Se PRE_JOIN mostra X=-1: o problema é o DM — implementar 0850 direct

---

## 11. AMBIENTE DE DESENVOLVIMENTO

- **OS:** Windows (PT-BR, utilizador brasileiro)
- **GTA SA instalado em:** `C:\Program Files (x86)\Rockstar Games\GTA San Andreas\`
- **IDE:** Visual Studio (detectado via vswhere em setup_and_build.bat)
- **Build:** `grove_recruit_asi/setup_and_build.bat` — auto-detecta VS, compila plugin_sa.lib e grove_recruit_standalone.dll
- **Output:** `grove_recruit_standalone.dll` → renomear para `.asi` → copiar para pasta GTA SA
- **CLEO:** `CLEO 4` com plugins `CLEO+`, `FileSystemOperations`, `IniFiles`, `IntOperations`
- **Outros mods activos:** `modloader.asi`, `CrashInfo.SA.asi`, `_noDEP.asi`
- **Versão confirmada:** `SA 1.0 US` (confirmado no modloader.log)

---

## 12. CONVENÇÕES DO CÓDIGO

- **Logging:** usar macros `LogEvent(fmt,...)`, `LogGroup(...)`, `LogTask(...)`, `LogDrive(...)`, `LogAI(...)`, `LogKey(...)`, `LogWarn(...)`, `LogError(...)` — **nunca printf directo**
- **Novos eventos de diagnóstico:** sempre log em transições, não per-frame; sempre incluir identificadores chave (taskId, pedType, respect, modo)
- **Globals:** prefixo `g_`, ex: `g_recruit`, `g_state`, `g_driveMode`; statics locais de função usam `s_` ou são declarados `static`
- **Endereços hardcoded:** declarar como `static const FnTipo s_Nome = (FnTipo)0xADDRESS;` perto do ponto de uso
- **Memória:** nunca escrever em offsets não verificados no plugin-sdk. Sempre confirmar com jte/GTASA source ou plugin-sdk headers antes de usar offset raw

---

*Última actualização: 14/03/2026 — gerado automaticamente pelo GitHub Copilot Agent*
