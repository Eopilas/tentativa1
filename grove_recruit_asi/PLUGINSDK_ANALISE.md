# grove_recruit_asi — Análise CLEO vs plugin-sdk e Protótipo ASI

## O que é o plugin-sdk

O [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk) é um SDK C++ que expõe as classes internas do GTA SA como headers C++ compiláveis.  
Permite criar ficheiros `.asi` (DLLs injectadas pelo ASI Loader) que correm como código nativo dentro do processo do jogo, com acesso total às estruturas internas.

---

## Comparação directa: CLEO vs plugin-sdk

| Capacidade | CLEO (actual) | plugin-sdk (ASI) |
|---|---|---|
| **Frequência de actualização** | ~300 ms (wait mínimo do loop) | Per-frame (~16 ms a 60 fps) |
| **Speed adaptativa para curvas** | ❌ Sem opcode para ler o multiplicador do link | ✅ `CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(m_nStraightLineDistance)` |
| **Detecção automática de offroad** | ⚠️ Só com guard 00EC + 04D3 (manual, 300 ms) | ✅ `CCarCtrl::FindNodesThisCarIsNearestTo` per-frame |
| **Alinhamento de faixa** | ⚠️ Só activo em DriveMode=Accurate (CIVICO-C) | ✅ `CCarCtrl::ClipTargetOrientationToLink` em qualquer modo |
| **Controlo de velocidade** | ⚠️ `00AD` (float arredondado, aplicado pelo CLEO) | ✅ `CAutoPilot::m_nCruiseSpeed` (char, escrita directa, precisa) |
| **Múltiplos recrutas** | ❌ 1 recruta (1 handle, 1 estado) | ✅ Até 7 via `PoolIterator<CPed>` |
| **Leitura de CarMission** | ❌ Sem opcode | ✅ `CAutoPilot::m_nCarMission` |
| **Detecção de highway** | ❌ Sem opcode | ✅ `CPathNode::m_bHighway` / `m_bNotHighway` |
| **Nível de tráfego do nó** | ❌ Sem opcode | ✅ `CPathNode::m_nTrafficLevel` |
| **Pathfinding completo** | ⚠️ 04D3 só o nó mais próximo | ✅ `CPathFind::DoPathSearch` (rota completa nó-a-nó) |
| **Coexistência com CLEO** | — | ✅ Corre em paralelo, sem conflito |

---

## Estruturas do plugin-sdk utilizadas no protótipo

### `CAutoPilot` — `plugin_sa/game_sa/CAutoPilot.h`
```
Offset  Campo                    Tipo              Descrição
0x14    m_nCurrentPathNodeInfo   CCarPathLinkAddress  Link actual (para ClipToLink e SpeedMult)
0x27    m_nCurrentLane           char              Faixa actual
0x29    m_nCarDrivingStyle       eCarDrivingStyle  0=StopForCars, 2=AvoidCars, 3=PloughThrough, 4=SFI
0x2A    m_nCarMission            eCarMission       Missão actual (8=GotoCoords, 12=Accurate, 11=StopForever...)
0x40    m_nCruiseSpeed           char              Velocidade de cruzeiro (escrita directa, ~km/h)
0x4D    m_nStraightLineDistance  char              Distância em linha recta ao nó seguinte
0x5C    m_vecDestinationCoors    CVector           Destino actual da IA
0x8C    m_pTargetCar             CVehicle*         Carro-alvo (para EscortRear/FollowFar)
```

### `eCarMission` — `plugin_sa/game_sa/eCarMission.h`
Missões relevantes para o nosso mod:
- `MISSION_NONE = 0`
- `MISSION_GOTOCOORDS = 8` — equivalente ao 00A7 do CLEO
- `MISSION_GOTOCOORDS_ACCURATE = 12` — equivalente ao 02C2 do CLEO
- `MISSION_STOP_FOREVER = 11` — parar completamente
- `MISSION_34 = 52` — FollowCarFaraway (06E1 CIVICO-E)
- `MISSION_43 = 67` — EscortRearFaraway (06E1 CIVICO-D)

### `eCarDrivingStyle` — `plugin_sa/game_sa/CAutoPilot.h`
```cpp
DRIVINGSTYLE_STOP_FOR_CARS              = 0   // pára para carros, respeita semáforos
DRIVINGSTYLE_SLOW_DOWN_FOR_CARS         = 1
DRIVINGSTYLE_AVOID_CARS                 = 2   // desvia, ignora semáforos (CIVICO-D/E/DIRETO)
DRIVINGSTYLE_PLOUGH_THROUGH             = 3   // ignora tudo (offroad)
DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS= 4   // pára obstáculos, ignora semáforos (CIVICO-0/A/B/C/F)
```

### `CCarCtrl` — `plugin_sa/game_sa/CCarCtrl.h`

**`FindNodesThisCarIsNearestTo(CVehicle*, CNodeAddress&, CNodeAddress&)`**  
Encontra os 2 nós de estrada mais próximos ao carro.  
Se o nó mais próximo está a >30 m → carro está fora de estrada (canal, montanha).  
CLEO não tem equivalente directo.

**`ClipTargetOrientationToLink(CVehicle*, CCarPathLinkAddress, char lane, float* heading, float fwdX, float fwdY)`**  
Ajusta `heading` para ser paralelo à faixa de estrada do link actual.  
Internamente activo só em `DriveMode=Accurate`. Via ASI aplicamos a qualquer modo.  
Resultado: recruta não corta curvas, mantém-se na faixa correcta.

**`FindSpeedMultiplierWithSpeedFromNodes(char straightLineDist)`**  
Lê o limite de velocidade do link para a curva actual.  
Retorna `float [0.0, 1.0]`: 1.0 = reta, <1.0 = curva (proporcional ao ângulo).  
Permite pre-abrandar antes da curva — impossível em CLEO.

**`StopCarIfNodesAreInvalid(CVehicle*)`**  
Para o carro imediatamente se os nós do seu AutoPilot estão inválidos.  
Útil como guard de segurança quando offroad.

### `CPathNode` — `plugin_sa/game_sa/CPathNode.h`
```
m_bHighway          — nó de auto-estrada (permitir velocidades mais altas)
m_bNotHighway       — explicitamente não-highway
m_nTrafficLevel     — densidade de tráfego [0-3]
m_bWaterNode        — nó aquático (recruta nunca deve navegar aqui)
m_bDontWander       — nó que NPCs civis evitam
m_nNumLinks         — número de ligações do nó
```

### `CPools` / `PoolIterator` — `plugin_sa/game_sa/CPools.h` + `shared/extensions/PoolIterator.h`
```cpp
// Iterar todos os peds activos no jogo (range-based for):
for (CPed* ped : *CPools::ms_pPedPool) {
    // ped é garantidamente válido dentro do loop
}
```
CLEO não tem equivalente: a varredura manual com `0A8D` é propensa a crashes.

### `CWorld::Players` — `plugin_sa/game_sa/CWorld.h`
```cpp
CPlayerPed* player = CWorld::Players[0].m_pPed;  // jogador 0 (CJ)
```

### `Events::gameProcessEvent` — `shared/Events.h`
```cpp
Events::gameProcessEvent += []() {
    // Corre a cada frame do game loop de SA (~60 fps)
    // Equivalente a estar dentro de CGame::Process()
};
```

---

## O que o protótipo implementa

O ficheiro `grove_recruit_asi.cpp` demonstra:

1. **Detecção automática de offroad** (`DetectOffroad`)  
   - `FindNodesThisCarIsNearestTo` → nó mais próximo → distância  
   - Se >30 m: muda para `PloughThrough` + velocidade 60

2. **Speed adaptativa para curvas** (`AdaptiveSpeed`)  
   - `FindSpeedMultiplierWithSpeedFromNodes(m_nStraightLineDistance)`  
   - Multiplica a velocidade base pelo factor da curva

3. **Zona STOP/SLOW per-frame** (`ProcessRecrutaFrame`)  
   - Escreve `m_nCruiseSpeed = 0` quando dist < 6 m  
   - Escreve `m_nCruiseSpeed = 12` quando dist < 10 m  
   - CLEO faz isto a 300 ms; ASI faz per-frame (16 ms)

4. **Alinhamento de faixa** (`GetLaneAlignedHeading`)  
   - `ClipTargetOrientationToLink` → heading alinhado à faixa  
   - Por implementar (v2): aplicar ao steering do veículo directamente

5. **Multi-recruta via PoolIterator** (`ScanPlayerGroup`)  
   - Itera `CPools::ms_pPedPool`  
   - Filtra `m_nPedType == 7` (GANG1) + `bInVehicle` + proximidade  
   - Gere até 7 recrutas em simultâneo

---

## O que falta para versão completa (v2)

1. **Comunicação bidirecional CLEO ↔ ASI**  
   Usar `0A8C` (write_memory) no CLEO para escrever flags numa zona de memória partilhada que o ASI lê. Ex: flag do modo activo (29@) → ASI adapta a sua lógica.

2. **Aplicar heading de ClipTargetOrientationToLink ao steering**  
   Requer hook em `CAutomobile::ProcessControl` ou escrita directa em `m_fSteerAngle`.

3. **Verificação de membership via `CPedGroupMembership::IsMember`**  
   Em vez de apenas verificar `m_nPedType == 7`, verificar que o ped pertence ao grupo nativo do jogador.

4. **Detecção de highway → velocidade mais alta**  
   Ler `CPathNode::m_bHighway` do nó actual → se highway, `SPEED_MAX = 80`.

5. **Pathfinding completo com `CPathFind::DoPathSearch`**  
   Calcular rota óptima nó-a-nó em vez de depender do 04D3 (nó mais próximo).

---

## Como compilar

```
Requisitos:
  - Windows com Visual Studio 2019/2022
  - git clone https://github.com/DK22Pac/plugin-sdk.git

Visual Studio > Propriedades do Projecto:
  C/C++ > Additional Include Directories:
    $(plugin_sdk)\plugin_sa
    $(plugin_sdk)\plugin_sa\game_sa
    $(plugin_sdk)\shared
    $(plugin_sdk)\shared\extensions

  Linker > Additional Dependencies:
    $(plugin_sdk)\output\plugin_sa.lib

  General > Configuration Type: Dynamic Library (.dll)

  C/C++ > Preprocessor > Preprocessor Definitions:
    GTASA
    _USE_MATH_DEFINES

Compilar → grove_recruit_asi.dll → renomear para grove_recruit_asi.asi
Copiar para pasta do GTA SA (onde está o gta_sa.exe)

Requer ASI Loader:
  https://github.com/ThirteenAG/Ultimate-ASI-Loader
  (instalar como d3d8.dll ou usar ModLoader)
```

---

## HÍBRIDO (CLEO + ASI) vs STANDALONE (ASI puro) — qual é melhor?

### Resposta curta
**Agora: híbrido (CLEO + ASI).  
A longo prazo: standalone ASI puro.**

---

### Opção A — Híbrido: ASI ao lado do CLEO ✅ (recomendado agora)

O ASI corre **em paralelo** com o `grove_recruit_follow.cs` sem substituí-lo.

| Responsabilidade | Quem gere |
|---|---|
| Teclas Y/U/G/H/N/B (recrutar, armar, etc.) | **CLEO** |
| Spawn do recruta, entrada no carro | **CLEO** |
| Modo de seguimento (tecla 4): CIVICO/DIRETO/PARADO | **CLEO** |
| Velocidade per-frame, offroad automático | **ASI** |
| Speed adaptativa para curvas | **ASI** |
| Alinhamento de faixa | **ASI** |

**Porquê é o melhor agora:**
- O `grove_recruit_follow.cs` já funciona com a máquina de estados completa (8 modos, guards, etc.)
- O ASI apenas **refina** o que o CLEO não consegue fazer (per-frame, leitura de estruturas internas)
- Zero risco de quebrar o que já funciona
- Não é preciso reescrever nada — instalas o `.asi` e fica logo melhor

**Conflito?** Não, porque:
- O ASI escreve `m_nCruiseSpeed` e `m_nCarDrivingStyle` a cada frame
- O CLEO reescreve esses mesmos campos **apenas quando o modo muda** (tecla 4)
- Nos frames entre mudanças de modo (que são a maioria), o ASI afina continuamente

---

### Opção B — Standalone: ASI substitui o CLEO inteiramente 🔮 (versão futura)

Nesta opção o `.asi` faz **tudo**: teclas, spawn, IA, modos, guards.

**Vantagens sobre o híbrido:**
- Sem limitações do CLEO: teclas per-frame (sem wait 300ms), múltiplos recrutas nativamente
- Sem necessidade de ter CLEO instalado (menos dependências para o utilizador final)
- Acesso a `CKeyboard`/`CControllerConfigManager` para input mais responsivo
- `CPedGroupMembership::IsMember` para membership correcto (sem hack de `m_nPedType == 7`)
- Pathfinding completo com `CPathFind::DoPathSearch` para rota real nó-a-nó

**O que faltaria implementar (não trivial):**
1. Sistema de teclas: `GetAsyncKeyState` ou hook em `CControllerConfigManager`
2. Spawn do recruta: `CCarGenerator` / `CPopulation::AddPed`
3. Máquina de estados (8 modos, guards STOP/SLOW, 00EC offroad 50m)
4. Dialog/texto de "Press Y to enter vehicle"
5. Sistema de armas: `GiveWeapon`
6. Cleanup na morte/sair do carro

**Conclusão:** Possível e seria a solução mais limpa, mas é semanas de trabalho.  
O híbrido permite chegar lá **iterativamente** sem reescrever tudo de uma vez.

---

### Recomendação prática

```
Fase 1 (agora): CLEO + ASI híbrido
  grove_recruit_follow.cs  →  gestão de estados, input, spawn
  grove_recruit_asi.asi    →  velocidade per-frame, offroad, curvas

Fase 2 (opcional): migrar estado por estado para o ASI
  Começar pelo input (teclas) → depois spawn → depois modos
  Cada migração desactiva a secção equivalente no CLEO

Fase 3 (versão final): standalone .asi puro, CLEO removido
```

---

## Coexistência CLEO + ASI (modo híbrido actual)

O ASI **não substitui** o script CLEO — funciona **ao lado** dele:

- **CLEO** gere: teclas Y/U/G/H/N/B, spawn, entrada no carro, estados, modo actual (29@)
- **ASI** gere: velocidade per-frame, offroad auto, alinhamento de faixa

Não há conflito porque:
- O ASI escreve apenas `m_nCruiseSpeed` e `m_nCarDrivingStyle`
- O CLEO reescreve estes campos quando o modo muda (tecla 4), sobrepondo-se
- Entre mudanças de modo, o ASI afina continuamente

---

## Diagrama de fluxo do ASI

```
[Events::gameProcessEvent] → a cada frame (~60fps)
  │
  ├─ [60 frames] → ScanPlayerGroup()
  │    └─ PoolIterator → encontrar peds GANG1 em veículo próximo
  │         └─ preencher g_recrutas[0..6]
  │
  └─ [cada frame] → para cada recruta em g_recrutas:
       ├─ dist < 6m  → CruiseSpeed=0, MISSION_STOP_FOREVER
       ├─ dist < 10m → CruiseSpeed=12 (SLOW)
       ├─ [30 frames] → DetectOffroad()
       │    └─ FindNodesThisCarIsNearestTo → dist > 30m?
       │         ├─ offroad=true  → CruiseSpeed=60, PloughThrough
       │         └─ offroad=false → AdaptiveSpeed() + AvoidCars
       │                └─ FindSpeedMultiplierWithSpeedFromNodes
       └─ se missão Accurate → GetLaneAlignedHeading()
            └─ ClipTargetOrientationToLink → heading da faixa
```
