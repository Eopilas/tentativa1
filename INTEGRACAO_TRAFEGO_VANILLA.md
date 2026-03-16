# Integração com Sistema de Tráfego Vanilla - GTA SA
**Data**: 2026-03-16
**Branch**: claude/ler-historico-tasks-agent

---

## Objetivo

Fazer recrutas se comportarem **naturalmente como NPCs de tráfego vanilla** quando seguindo o player normalmente em estradas, mantendo a **responsividade** quando necessário.

---

## Análise: Tráfego Vanilla vs Mod Atual

### Sistema de Tráfego Vanilla (plugin-sdk/gta-reversed)

**Classes Principais**:
- `CCarCtrl` - Gerenciamento de tráfego e geração
- `CAutoPilot` - Sistema de autopilot dos veículos (0x98 bytes)
- `CPathFind / ThePaths` - Rede de navegação rodoviária
- `CCarAI` - Funções helper de alto nível

**Comportamento Padrão do Tráfego**:
```cpp
// CCarCtrl::GenerateRandomCars() configuração típica
ap.m_nCarMission      = MISSION_CRUISE;                    // 1
ap.m_nCarDrivingStyle = DRIVING_STYLE_STOP_FOR_CARS;       // 0
ap.m_nCruiseSpeed     = 13;                                 // ~46 km/h em velocidade real
ap.m_fMaxTrafficSpeed = 13.0f;                             // Limite dinâmico

// A cada 4 frames:
CCarCtrl::SlowCarOnRailsDownForTrafficAndLights(vehicle);  // Ajusta velocidade por semáforos/tráfego
```

**Características do Tráfego Vanilla**:
- ✅ **Segue estradas perfeitamente** - usa road-graph sempre
- ✅ **Para em semáforos e obstáculos** - STOP_FOR_CARS
- ✅ **Velocidade conservadora** - 13 units (seguro para curvas)
- ✅ **Navegação suave** - interpolação gradual de velocidade
- ✅ **Usa path array** - m_aPathFindNodesInfo[8] para lookahead de até 8 nodes
- ❌ **NÃO segue nenhum veículo específico** - apenas cruza

---

### Mod Atual (v3.4)

**Configuração Atual**:
```cpp
// grove_recruit_drive.cpp - SetupDriveMode()
case DriveMode::CIVICO_F:
    ap.m_nCarMission      = MC_ESCORT_REAR_FARAWAY;  // 67
    ap.m_pTargetCar       = playerCar;
    ap.m_nCruiseSpeed     = SPEED_CIVICO;            // 46
    ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS; // 2
    CCarCtrl::JoinCarWithRoadSystem(veh);
    break;

case DriveMode::CIVICO_H:
    ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;    // 52
    ap.m_pTargetCar       = playerCar;
    ap.m_nCruiseSpeed     = SPEED_CIVICO;            // 46
    ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS; // 2
    CCarCtrl::JoinCarWithRoadSystem(veh);
    break;
```

**Características do Mod Atual**:
- ✅ **Segue player especificamente** - MC_FOLLOWCAR_FARAWAY/MC_ESCORT_REAR_FARAWAY
- ✅ **Velocidade mais alta** - 46 units (3.5x mais rápido que tráfego)
- ✅ **Evita obstáculos ativamente** - AVOID_CARS desvia ao redor
- ✅ **Catchup dinâmico** - aumenta para 80-100 km/h quando longe
- ❌ **Menos "natural"** - comportamento mais agressivo que tráfego comum

---

## Diferenças Críticas (Vanilla vs Mod)

| Aspecto | Tráfego Vanilla | Mod Atual v3.4 | Impacto |
|---------|----------------|----------------|---------|
| **Mission** | MISSION_CRUISE (1) | MC_FOLLOWCAR_FARAWAY (52) | Vanilla não persegue, apenas cruza |
| **Driving Style** | STOP_FOR_CARS (0) | AVOID_CARS (2) | Vanilla para, mod desvia |
| **Cruise Speed** | 13 units | 46 units | Mod 3.5x mais rápido |
| **Target** | Nenhum | Player car | Vanilla não tem alvo específico |
| **Ajuste Dinâmico** | SlowCarOnRailsDownForTrafficAndLights() | Sistema catchup customizado | Vanilla responde a semáforos, mod responde a distância |
| **Path Lookahead** | 8 nodes (path array) | Similar (via CAutoPilot) | Ambos usam lookahead |

---

## Problema Identificado pelo Usuário

> "nos momentos que eu so quero q o recruta me siga usando a rua normalmente"

**Situação Desejada**:
- Player dirigindo **normalmente** em estradas padrão (40-60 km/h)
- Recruta deveria se comportar como **NPC de tráfego comum seguindo atrás**
- Sem ultrapassagens agressivas, sem desvios bruscos
- Comportamento **elegante e natural**

**Situação Atual (Problema)**:
- Recruta usa MC_FOLLOWCAR que é **mission de chase/perseguição**
- AVOID_CARS faz recruta **desviar ativamente** ao redor de obstáculos
- Velocidade base 46 + catchup até 100 = **muito agressivo** para seguir casualmente
- Parece mais "perseguição policial" que "seguir tranquilamente"

---

## Solução Proposta: Sistema Híbrido Contextual

### Conceito: 3 Modos de Comportamento

#### 1. MODO TRÁFEGO (TRAFFIC_FOLLOW) - NOVO
**Quando usar**: Player dirigindo normalmente (40-70 km/h), em estrada padrão, distância 15-40m

**Configuração**:
```cpp
ap.m_nCarMission      = MISSION_GOTOCOORDS;              // 8 - pathfinding direto
ap.m_vecDestinationCoors = playerPos;                    // Destino = posição player
ap.m_nCruiseSpeed     = 18;                              // ~40% mais rápido que tráfego (13)
ap.m_nCarDrivingStyle = DRIVING_STYLE_STOP_FOR_CARS;     // 0 - comportamento tráfego
ap.m_fMaxTrafficSpeed = 18.0f;                           // Limite dinâmico conservador
CCarCtrl::JoinCarWithRoadSystem(veh);
```

**Comportamento Esperado**:
- ✅ Segue estradas elegantemente
- ✅ Para em semáforos (se próximo ao player parado)
- ✅ Não desvia agressivamente
- ✅ Velocidade conservadora mas suficiente para acompanhar
- ✅ **Parece NPC de tráfego comum que coincidentemente vai na mesma direção**

#### 2. MODO CIVICO (CIVICO_H/F) - MANTIDO COM AJUSTES
**Quando usar**: Player acelerando (70-100 km/h), curvas, distância 40-80m

**Configuração** (ajustada):
```cpp
ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;            // 52
ap.m_pTargetCar       = playerCar;
ap.m_nCruiseSpeed     = 25;                              // REDUZIDO de 46 para 25
ap.m_nCarDrivingStyle = DRIVINGSTYLE_SLOW_DOWN_FOR_CARS; // 1 - meio termo
CCarCtrl::JoinCarWithRoadSystem(veh);
```

**Comportamento Esperado**:
- ✅ Segue player ativamente
- ✅ Desacelera para carros mas não para completamente
- ✅ Velocidade moderada (25 base + catchup até 60)
- ✅ Mais responsivo que TRAFFIC_FOLLOW mas ainda elegante

#### 3. MODO CATCHUP (FAR_CATCHUP) - MANTIDO
**Quando usar**: Distância >80m, player muito rápido (>100 km/h)

**Configuração** (atual v3.4):
```cpp
ap.m_nCarMission      = MISSION_GOTOCOORDS;              // 8 - direto
ap.m_vecDestinationCoors = playerPos;
ap.m_nCruiseSpeed     = 80-100;                          // Catchup agressivo
ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;         // 2 - desviar
```

**Comportamento Esperado**:
- ✅ Alcança player rapidamente
- ✅ Usa navegação direta se INVALID_LINK
- ✅ Evita obstáculos ativamente
- ❌ Menos elegante mas **necessário** para não perder player

---

## Transições Entre Modos (State Machine)

```
┌─────────────────────────────────────────────────────────────┐
│                    TRAFFIC_FOLLOW (Novo)                     │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ • MISSION_GOTOCOORDS                                 │  │
│  │ • STOP_FOR_CARS                                      │  │
│  │ • Speed: 18 units (~65 km/h real)                    │  │
│  │ • Comportamento: Tráfego elegante                    │  │
│  └──────────────────────────────────────────────────────┘  │
│  Condições:                                                  │
│  • dist: 15-40m                                              │
│  • playerSpeed: 40-70 km/h                                   │
│  • Em estrada padrão (não offroad)                           │
│  • Sem INVALID_LINK prolongado                               │
└───────┬────────────────────────────────────────┬─────────────┘
        │ dist > 40m OU                          │
        │ playerSpeed > 70 km/h                  │ dist < 15m OU
        ↓                                        │ player parado
┌───────────────────────────┐                    ↓
│    CIVICO_H/F (Ajustado)  │          ┌─────────────────────┐
│  ┌──────────────────────┐ │          │   CLOSE_RANGE       │
│  │ • MC_FOLLOWCAR       │ │          │  (Modo especial)    │
│  │ • SLOW_DOWN_FOR_CARS │ │          │  • MISSION_STOP     │
│  │ • Speed: 25 base     │ │          │    ou STOP_FOREVER  │
│  │ • Catchup: até 60    │ │          └─────────────────────┘
│  └──────────────────────┘ │
│  Condições:                │
│  • dist: 40-80m            │
│  • playerSpeed: 70-100     │
└───────┬────────────────────┘
        │ dist > 80m OU
        │ playerSpeed > 100 km/h OU
        │ INVALID_LINK burst >20
        ↓
┌─────────────────────────────┐
│   FAR_CATCHUP (Mantido)     │
│  ┌────────────────────────┐ │
│  │ • MISSION_GOTOCOORDS   │ │
│  │ • AVOID_CARS           │ │
│  │ • Speed: 80-100        │ │
│  │ • Fallback: GOTOCOORDS │ │
│  │   direto se INVALID    │ │
│  └────────────────────────┘ │
│  Condições:                  │
│  • dist: >80m                │
│  • Ou INVALID_LINK storm     │
└──────────────────────────────┘
```

---

## Implementação: Novo Enum e Constantes

### grove_recruit_config.h

```cpp
// Novo enum para modo de comportamento
enum class BehaviorMode : unsigned char
{
    TRAFFIC_FOLLOW = 0,  // Comportamento tráfego vanilla
    CIVICO_NORMAL  = 1,  // Comportamento cívico moderado
    FAR_CATCHUP    = 2,  // Catchup agressivo
    CLOSE_RANGE    = 3,  // Próximo demais (parar/esperar)
    OFFROAD_DIRECT = 4   // Player offroad (navegação direta)
};

// Constantes para TRAFFIC_FOLLOW (novo)
static constexpr unsigned char SPEED_TRAFFIC_FOLLOW        = 18;  // ~65 km/h real
static constexpr float         TRAFFIC_FOLLOW_DIST_MIN     = 15.0f;
static constexpr float         TRAFFIC_FOLLOW_DIST_MAX     = 40.0f;
static constexpr float         TRAFFIC_FOLLOW_SPEED_MIN    = 40.0f;  // km/h
static constexpr float         TRAFFIC_FOLLOW_SPEED_MAX    = 70.0f;  // km/h

// Ajuste constantes CIVICO (reduzir velocidade base)
static constexpr unsigned char SPEED_CIVICO                = 25;  // era 46, reduzido!
static constexpr unsigned char SPEED_CIVICO_CATCHUP        = 60;  // novo: limite catchup em civico

// Thresholds de transição
static constexpr float BEHAVIOR_TRANSITION_DIST_CLOSE      = 15.0f;
static constexpr float BEHAVIOR_TRANSITION_DIST_MEDIUM     = 40.0f;
static constexpr float BEHAVIOR_TRANSITION_DIST_FAR        = 80.0f;
```

### grove_recruit_drive.cpp - Nova Função

```cpp
BehaviorMode DetermineBehaviorMode(float dist2D, float playerSpeed, bool offroad, int invalidLinkBurst)
{
    // 1. Offroad priority
    if (offroad) {
        return BehaviorMode::OFFROAD_DIRECT;
    }

    // 2. INVALID_LINK storm → FAR_CATCHUP com fallback
    if (invalidLinkBurst > 20) {
        return BehaviorMode::FAR_CATCHUP;
    }

    // 3. Close range
    if (dist2D < BEHAVIOR_TRANSITION_DIST_CLOSE) {
        return BehaviorMode::CLOSE_RANGE;
    }

    // 4. Far catchup
    if (dist2D > BEHAVIOR_TRANSITION_DIST_FAR) {
        return BehaviorMode::FAR_CATCHUP;
    }

    // 5. Player muito rápido → FAR_CATCHUP
    if (playerSpeed > 100.0f) {
        return BehaviorMode::FAR_CATCHUP;
    }

    // 6. TRAFFIC_FOLLOW (sweet spot)
    if (dist2D >= TRAFFIC_FOLLOW_DIST_MIN && dist2D <= TRAFFIC_FOLLOW_DIST_MAX &&
        playerSpeed >= TRAFFIC_FOLLOW_SPEED_MIN && playerSpeed <= TRAFFIC_FOLLOW_SPEED_MAX)
    {
        return BehaviorMode::TRAFFIC_FOLLOW;
    }

    // 7. Default: CIVICO_NORMAL
    return BehaviorMode::CIVICO_NORMAL;
}

void ApplyBehaviorMode(CVehicle* veh, CPlayerPed* player, BehaviorMode mode)
{
    CAutoPilot& ap = veh->m_autoPilot;
    CVehicle* playerCar = player->m_pVehicle;
    CVector playerPos = player->GetPosition();

    switch (mode)
    {
        case BehaviorMode::TRAFFIC_FOLLOW:
        {
            // NOVO: Comportamento tráfego vanilla
            ap.m_nCarMission         = MISSION_GOTOCOORDS;
            ap.m_vecDestinationCoors = playerPos;  // Destino = player
            ap.m_pTargetCar          = nullptr;    // Sem targetCar (como tráfego)
            ap.m_nCruiseSpeed        = SPEED_TRAFFIC_FOLLOW;  // 18
            ap.m_nCarDrivingStyle    = DRIVING_STYLE_STOP_FOR_CARS;  // 0
            ap.m_fMaxTrafficSpeed    = SPEED_TRAFFIC_FOLLOW;  // 18.0f
            CCarCtrl::JoinCarWithRoadSystem(veh);

            // Log
            // LogTrafficFollowStart(...)
            break;
        }

        case BehaviorMode::CIVICO_NORMAL:
        {
            // AJUSTADO: Velocidade reduzida, driving style mais suave
            ap.m_nCarMission         = MC_FOLLOWCAR_FARAWAY;  // 52
            ap.m_pTargetCar          = playerCar;
            ap.m_nCruiseSpeed        = SPEED_CIVICO;  // 25 (reduzido!)
            ap.m_nCarDrivingStyle    = DRIVING_STYLE_SLOW_DOWN_FOR_CARS;  // 1 (meio termo!)
            ap.m_fMaxTrafficSpeed    = SPEED_CIVICO_CATCHUP;  // 60 (limite)
            CCarCtrl::JoinCarWithRoadSystem(veh);
            break;
        }

        case BehaviorMode::FAR_CATCHUP:
        {
            // MANTIDO: Catchup agressivo (v3.4)
            ap.m_nCarMission         = MISSION_GOTOCOORDS;
            ap.m_vecDestinationCoors = playerPos;
            ap.m_pTargetCar          = nullptr;
            ap.m_nCruiseSpeed        = (dist2D > 100.0f) ? SPEED_CATCHUP_VERY_FAR : SPEED_CATCHUP_FAR;
            ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS;  // 2
            CCarCtrl::JoinCarWithRoadSystem(veh);
            break;
        }

        case BehaviorMode::CLOSE_RANGE:
        {
            // MANTIDO: Sistema CLOSE_BLOCKED
            // [código atual do CLOSE_BLOCKED]
            break;
        }

        case BehaviorMode::OFFROAD_DIRECT:
        {
            // MANTIDO: Sistema PLAYER_OFFROAD
            // [código atual do PLAYER_OFFROAD_DIRECT]
            break;
        }
    }
}
```

---

## Integração com Sistema de Gang Nativo

### Estrutura Nativa (CPedGroups)

**Achados do gta-reversed**:
```cpp
class CPedGroup {
    CPed* m_pPeds[8];        // Máximo 8 membros (incluindo líder)
    CPed* m_pLeader;
    bool  m_bMembersEnterLeadersVehicle;
    // ...
};

class CTaskComplexGangFollower : public CTaskComplex {
    CPedGroup* m_PedGroup;
    CPed*      m_Leader;
    // Usado para gang members A PÉ, não em veículos!
};
```

**Sistema Nativo de Recrutamento**:
- Comando `RECRUIT_ACTOR` (opcode 0x4E2) adiciona ped ao grupo do player
- Comando `REMOVE_CHAR_FROM_GROUP` (opcode 0x4E3) remove ped do grupo
- Gang members usam `CTaskComplexGangFollower` quando **a pé**
- Quando entram em veículo, usam `CAutoPilot` system (como qualquer NPC dirigindo)

**Implicação**: O mod **JÁ está integrado corretamente** ao usar `CAutoPilot` para recrutas em veículos. Não há conflito com sistema nativo de gang a pé.

### Recomendação: Comandos de Grupo

Para melhorar imersão, adicionar comandos de controle de grupo:

```cpp
// grove_recruit_commands.cpp (novo arquivo)

void CommandGroup_FollowMe(CPlayerPed* player)
{
    // Todos recrutas: modo TRAFFIC_FOLLOW ou CIVICO
    ForEachRecruit([](RecruitData& r) {
        r.commandMode = CommandMode::FOLLOW;
        r.allowTrafficFollow = true;  // Permite usar modo tráfego
    });
}

void CommandGroup_HoldPosition(CPlayerPed* player)
{
    // Todos recrutas: MISSION_STOP_FOREVER na posição atual
    ForEachRecruit([](RecruitData& r) {
        r.commandMode = CommandMode::HOLD_POSITION;
        r.holdPosition = r.vehicle->GetPosition();
    });
}

void CommandGroup_Scatter(CPlayerPed* player)
{
    // Recrutas dispersam em direções aleatórias
    ForEachRecruit([](RecruitData& r) {
        r.commandMode = CommandMode::SCATTER;
        r.scatterDirection = GetRandomDirection();
    });
}
```

**Keybinds Sugeridos** (adicionar ao config):
- `KEY_G` - "Follow me" (já existe como toggle recruit, expandir)
- `KEY_H` - "Hold position"
- `KEY_J` - "Scatter" (dispersar)

---

## Testes Recomendados

### Teste 1: Traffic Follow Natural
**Setup**: Spawnar recruta, dirigir normalmente em Ganton (40-60 km/h)

**Esperado**:
- [ ] Recruta entra em modo TRAFFIC_FOLLOW
- [ ] Velocidade conservadora (~18 units base)
- [ ] Segue estradas suavemente
- [ ] Para em semáforos se player parar
- [ ] **Parece NPC comum seguindo atrás, não perseguição**
- [ ] Log mostra `BEHAVIOR_MODE_CHANGE: -> TRAFFIC_FOLLOW`

### Teste 2: Transição para Civico
**Setup**: Acelerar gradualmente de 60 → 90 km/h em Jefferson

**Esperado**:
- [ ] Transição suave TRAFFIC_FOLLOW → CIVICO_NORMAL aos ~70 km/h
- [ ] Velocidade aumenta gradualmente (25 base + catchup)
- [ ] Driving style muda para SLOW_DOWN_FOR_CARS (ainda elegante)
- [ ] Mantém distância 40-60m
- [ ] Log mostra `BEHAVIOR_MODE_CHANGE: TRAFFIC_FOLLOW -> CIVICO_NORMAL`

### Teste 3: Catchup Agressivo
**Setup**: Acelerar a 120 km/h em rodovia (freeway)

**Esperado**:
- [ ] Transição CIVICO_NORMAL → FAR_CATCHUP aos 80m de distância
- [ ] Recruta acelera para 80-100 km/h
- [ ] Usa AVOID_CARS (desvia obstáculos)
- [ ] Alcança player em <15 segundos
- [ ] Retorna a CIVICO_NORMAL quando dist <40m novamente

### Teste 4: Offroad Handling
**Setup**: Entrar em canal de Los Santos (sem road-graph)

**Esperado**:
- [ ] Transição automática para OFFROAD_DIRECT
- [ ] Navegação direta com AVOID_CARS
- [ ] Velocidade conservadora (SPEED_CIVICO = 25)
- [ ] Não bate em paredes/props
- [ ] Retorna a TRAFFIC_FOLLOW ao sair do canal

### Teste 5: Estradas Não-Padrão
**Setup**: Parking lot do aeroporto LS (área sem road-graph denso)

**Esperado**:
- [ ] INVALID_LINK burst detectado
- [ ] Transição para FAR_CATCHUP com fallback direto
- [ ] Usa AVOID_CARS (evita pilares/props)
- [ ] Após 5s, retorna ao modo anterior
- [ ] NÃO fica preso em loop INVALID_LINK

---

## Próximos Passos

### Fase 1: Implementação Básica (1-2 dias)
- [ ] Adicionar `BehaviorMode` enum a `grove_recruit_config.h`
- [ ] Implementar `DetermineBehaviorMode()` em `grove_recruit_drive.cpp`
- [ ] Implementar `ApplyBehaviorMode()` com case TRAFFIC_FOLLOW
- [ ] Ajustar `SPEED_CIVICO` de 46 → 25
- [ ] Testar transições básicas TRAFFIC_FOLLOW ↔ CIVICO

### Fase 2: Refinamento (2-3 dias)
- [ ] Adicionar hysteresis para transições (evitar oscilação)
- [ ] Implementar logging de mudanças de modo
- [ ] Ajustar thresholds baseado em testes
- [ ] Implementar smooth speed interpolation entre modos
- [ ] Testar cenários edge case (curvas, interseções, offroad)

### Fase 3: Comandos de Grupo (1-2 dias)
- [ ] Criar `grove_recruit_commands.cpp`
- [ ] Implementar CommandGroup_FollowMe/HoldPosition/Scatter
- [ ] Adicionar keybinds configuráveis
- [ ] Testar comandos com múltiplos recrutas
- [ ] Adicionar feedback visual (HUD messages)

### Fase 4: Polimento e Testes (2-3 dias)
- [ ] Testes extensivos em todas as áreas de San Andreas
- [ ] Ajustes finais de velocidades/thresholds
- [ ] Verificar performance (múltiplos recrutas)
- [ ] Documentação final
- [ ] Update README com novo sistema

---

## Conclusão

Esta integração trará o **melhor dos dois mundos**:

✅ **Elegância do tráfego vanilla** - quando player dirige normalmente
✅ **Responsividade do mod** - quando player acelera ou vai offroad
✅ **Transições suaves** - state machine contextual
✅ **Imersão aumentada** - recrutas parecem NPCs comuns quando apropriado

**Filosofia Final**: "Recruta deve se comportar como tráfego comum quando possível, e como perseguidor responsivo quando necessário, com transições suaves e naturais entre os dois."
