# Sistema de Gang Nativo - Integração Completa
**Data**: 2026-03-16
**Branch**: claude/ler-historico-tasks-agent

---

## Visão Geral

Este documento detalha o sistema nativo de gang recruitment do GTA SA baseado na análise completa dos repositórios **plugin-sdk** e **gta-reversed**, e como o mod `grove_recruit` deve se integrar adequadamente com ele.

---

## 1. Arquitetura do Sistema Nativo

### CPedGroups (Gerenciador Global)

**Arquivo**: `plugin_sa/game_sa/CPedGroups.h/cpp`

**Estrutura Estática**:
```cpp
class CPedGroups {
public:
    static short         ms_activeGroups[8];          // Flags de grupos ativos (0-7)
    static CPedGroup     ms_groups[8];                // Array de 8 grupos máximo
    static bool          ms_bIsPlayerOnAMission;      // Estado global de missão
    static unsigned int  ms_iNoOfPlayerKills;         // Contador de kills durante missão
    static short         ScriptReferenceIndex[8];     // Referências de script

    // Métodos principais
    static void AddGroup();
    static void RemoveGroup(int groupID);
    static void RemoveAllFollowersFromGroup(int groupID);
    static bool IsInPlayersGroup(CPed* ped);
    static bool AreInSameGroup(CPed* ped1, CPed* ped2);
    static bool IsGroupLeader(CPed* ped);
    static CPedGroup* GetPedsGroup(CPed* ped);
    static void Process();  // Loop principal (chamado todo frame)
};
```

**Limitações Críticas**:
- ❌ **Apenas 8 grupos simultâneos** no engine inteiro
- ❌ **Máximo 8 membros por grupo** (7 followers + 1 leader)
- ❌ **Separation distance 60m** - membros >60m do líder são removidos automaticamente
- ❌ **Grupos compartilhados** - missions, scripts, e player competem pelos 8 slots

---

### CPedGroup (Grupo Individual)

**Arquivo**: `gta-reversed/source/game_sa/PedGroup.h`

```cpp
class CPedGroup {
public:
    CPed*                 m_pPed;                             // Líder do grupo
    bool                  m_bMembersEnterLeadersVehicle;      // Auto-enter vehicle (padrão: true)
    CPedGroupMembership   m_groupMembership;                  // Gerenciador de membros
    CPedGroupIntelligence m_groupIntelligence;                // IA do grupo (eventos)
    bool                  m_bIsMissionGroup;                  // Grupo de missão?

    // Métodos
    void Flush();
    void RemoveAllFollowers(bool bCreatedByGameOnly);
    void Process();
    CPedIntelligence* GetIntelligence();
    CPed* GetMembership().GetLeader();
};
```

**CPedGroupMembership (Estrutura Interna)**:
```cpp
class CPedGroupMembership {
    std::array<CPed*, 8> m_members;          // 8 slots: índice 7 = leader, 0-6 = followers
    float                m_separationRange;   // 60.0f metros (padrão)
    CPedGroup*           m_pPedGroup;         // Back-reference ao grupo

    // Constantes
    static const int32 TOTAL_PED_GROUP_MEMBERS = 8;
    static const int32 TOTAL_PED_GROUP_FOLLOWERS = 7;
    static const int32 LEADER_MEM_ID = 7;    // Líder sempre no índice 7
};
```

---

## 2. Ciclo de Vida de Recrutamento

### 2.1. Adição de Membro (Recruitment)

**Opcode CLEO**: `0x836` (ADD_CHAR_TO_GROUP)

**Fluxo Interno** (`CPedGroupMembership::AddFollower`):
```cpp
void CPedGroupMembership::AddFollower(CPed* ped) {
    // 1. Resetar flag de drive task
    ped->bHasGroupDriveTask = false;

    // 2. Se líder é player, desabilitar afogamento
    if (const auto leader = GetLeader()) {
        if (leader->IsPlayer()) {
            ped->bDrownsInWater = false;  // Gang members não se afogam
        }
    }

    // 3. Verificar se já é membro
    if (IsFollower(ped) || GetLeader() == ped) {
        return;  // Já está no grupo
    }

    // 4. Encontrar slot disponível (0-6 para followers)
    const auto memId = FindIdForNewMember();
    if (memId == -1) {
        return;  // Grupo cheio (7 followers máximo)
    }

    // 5. Adicionar ao array
    AddMember(ped, memId);

    // 6. Dar objeto aleatório (gang sign, bebida, cigarro)
    GivePedRandomObjectToHold(ped);
}
```

**Flags Setadas Durante Recruitment**:
```cpp
ped->bHasGroupDriveTask = false;                // Não aplicar vanilla drive task
ped->bDrownsInWater = false;                    // Gang members não afogam
ped->bClearRadarBlipOnDeath = (context-dep);    // Limpar blip ao morrer
ped->GetIntelligence()->SetPedDecisionMakerType(
    DM_EVENT_DRAGGED_OUT_CAR                    // Event decision maker
);
```

---

### 2.2. Remoção de Membro

**Opcode CLEO**: `0x06C9` (REMOVE_CHAR_FROM_GROUP)

**Fluxo Interno** (`CPedGroupMembership::RemoveMember`):
```cpp
void CPedGroupMembership::RemoveMember(int32 memIdx) {
    const auto mem = m_members[memIdx];

    // 1. Limpar radar blip se flag ativa
    if (mem->bClearRadarBlipOnDeath) {
        CRadar::ClearBlipForEntity(mem);
        mem->bClearRadarBlipOnDeath = false;
    }

    // 2. Restaurar decision maker original
    mem->GetIntelligence()->RestorePedDecisionMakerType();

    // 3. Reabilitar afogamento
    mem->bDrownsInWater = true;

    // 4. Remover do array
    m_members[memIdx] = nullptr;
}
```

**Remoção Automática (Separation Distance)**:
```cpp
// CPedGroupMembership::Process() verifica todo frame
for (auto&& [memIdx, ped] : notNull(m_members)) {
    if (memIdx == LEADER_MEM_ID) continue;  // Líder não verifica distância

    const auto dist = DistanceBetweenPoints(ped->GetPosition(), leader->GetPosition());
    if (dist > m_separationRange) {  // 60.0f metros
        RemoveMember(memIdx);
    }
}
```

---

## 3. Integração com Sistema de Veículos

### 3.1. Auto-Entry em Veículos

**Flag**: `m_bMembersEnterLeadersVehicle = true` (padrão)

**Comportamento**:
- Quando líder entra em veículo, followers **automaticamente** recebem task `ENTER_CAR_AS_PASSENGER`
- Engine escolhe assentos disponíveis (passenger, rear_left, rear_right)
- Se veículo cheio, followers restantes aguardam ou seguem a pé

**Opcode Relacionado**: `05CB` (task_enter_car_as_driver) ou `05CA` (task_enter_car_as_passenger)

---

### 3.2. CAutoPilot Quando em Veículo

**Estrutura** (de `CVehicle.h`):
```cpp
struct CAutoPilot {
    CNodeAddress         m_currentAddress;           // Node atual
    CNodeAddress         m_aPathFindNodesInfo[8];    // Path array (lookahead)
    unsigned short       m_nPathFindNodesCount;      // Quantidade de nodes no path
    eCarMission          m_nCarMission;              // Mission type (1-67)
    eCarDrivingStyle     m_nCarDrivingStyle;         // Driving style (0-6)
    eAutoPilotTempAction m_nTempAction;              // Temporary action (reverse, wait, etc)
    float                m_fMaxTrafficSpeed;         // Limite de velocidade dinâmico
    char                 m_nCruiseSpeed;             // Velocidade base (0-127)
    float                m_speed;                    // Velocidade atual
    CVector              m_vecDestinationCoors;      // Destino
    CVehicle*            m_pTargetCar;               // Veículo alvo (para follow missions)
    // ... mais campos ...
};
```

**Quando Gang Member Está Dirigindo**:
- `CAutoPilot` **toma controle completo** da navegação
- `CPedIntelligence` ainda processa eventos (alertas, ameaças, etc)
- `CPedGroupIntelligence` propaga comandos do líder
- Missions típicas: `MC_FOLLOWCAR_FARAWAY(52)`, `MC_ESCORT_REAR_FARAWAY(67)`

---

### 3.3. Formation Tasks (Opcode 06E1)

**Missões de Formação**:
```cpp
// Opcode 06E1: task_car_drive_wander, task_car_mission
eCarMission::MC_ESCORT_REAR_FARAWAY  = 67  // Formação geométrica atrás (losango)
eCarMission::MC_FOLLOWCAR_CLOSE      = 53  // Follow próximo com road-graph
eCarMission::MC_FOLLOWCAR_FARAWAY    = 52  // Follow distante via road nodes
eCarMission::MC_ESCORT_REAR          = 31  // Escort próximo (tenta posicionamento exato)
```

**Limitações das Formation Tasks**:
- ❌ **INVALID_LINK em interseções** - pathfinding falha em áreas complexas
- ❌ **Não reagem a offroad** - continuam tentando usar road-graph inexistente
- ❌ **Velocidade fixa** - sem ajuste dinâmico de catchup
- ❌ **Colisões repetidas** - HEADON loop quando obstáculo na rota

**Por Isso o Mod Implementa Sistema Custom**:
- ✅ INVALID_LINK fallback (modo direto temporário)
- ✅ Player offroad detection (transita para GOTOCOORDS)
- ✅ Dynamic catchup speeds (80-100 km/h)
- ✅ HEADON loop prevention (STUCK_RECOVER inteligente)

---

## 4. Sistema de Progressão e Kill Counter

### 4.1. Kill Counter e Decision Makers

**Variável Global**: `CPedGroups::ms_iNoOfPlayerKills`

**Fluxo de Progressão** (`CPedGroups::Process`):
```cpp
void CPedGroups::Process() {
    // ... atualizar membership, remover membros distantes ...

    // Alterar decision maker baseado em estado de missão
    const auto SetGroupsDecisionMakerType = [](eDecisionMakerType dm) {
        for (auto&& [_, g] : GetActiveGroupsWithIDs()) {
            g.GetIntelligence().SetGroupDecisionMakerType(dm);
        }
    };

    if (CTheScripts::IsPlayerOnAMission() && !ms_bIsPlayerOnAMission) {
        // MISSÃO INICIADA
        ms_iNoOfPlayerKills = 0;
        SetGroupsDecisionMakerType(eDecisionMakerType::GROUP_RANDOM_PASSIVE);
        ms_bIsPlayerOnAMission = true;
    }
    else if (!CTheScripts::IsPlayerOnAMission() && ms_bIsPlayerOnAMission) {
        // MISSÃO TERMINADA
        SetGroupsDecisionMakerType(eDecisionMakerType::UNKNOWN);  // Retorna ao normal
        ms_bIsPlayerOnAMission = false;
    }
    else if (CTheScripts::IsPlayerOnAMission() && ms_iNoOfPlayerKills == 8) {
        // ESCALADA: 8 KILLS DURANTE MISSÃO
        SetGroupsDecisionMakerType(eDecisionMakerType::UNKNOWN);  // Agressivo
    }
}
```

**Progression Tiers**:
1. **Fora de Missão**: Decision maker padrão (UNKNOWN) - comportamento normal
2. **Missão Iniciada**: `GROUP_RANDOM_PASSIVE` - defensivo, prioriza proteção do player
3. **8 Kills Alcançados**: `UNKNOWN` - agressivo, prioriza eliminar ameaças
4. **Missão Terminada**: Volta ao padrão

**Implicação para o Mod**:
- Gang members podem mudar de comportamento **automaticamente** durante missões
- Mod deve **respeitar decision maker** setado pelo engine
- Se mod quiser override, deve usar `SetPedDecisionMakerType()` explicitamente

---

### 4.2. Radar Blips e Tracking

**Flag**: `ped->bClearRadarBlipOnDeath`

**Comportamento**:
- Se `true`, radar blip é removido quando ped morre
- Usado para gang members temporários (mission-specific)
- Gang members permanentes (recruits) geralmente têm `false`

**Criação de Blip** (script):
```
0187: blip = create_radar_marker_without_sphere $recruta[i]
018A: blip color $recruta[i] 4  // Verde (gang color)
```

---

## 5. Integração Adequada do Mod com Sistema Nativo

### 5.1. O Que o Mod JÁ Faz Corretamente

✅ **Usa CAutoPilot diretamente** - não interfere com CPedGroups
✅ **Chama JoinCarWithRoadSystem()** - conecta ao road-graph
✅ **Seta missions válidas** - MC_FOLLOWCAR_FARAWAY, MC_ESCORT_REAR_FARAWAY
✅ **Respeita driving styles** - AVOID_CARS, STOP_FOR_CARS
✅ **Não modifica CPedGroupMembership** - deixa engine gerenciar grupo

---

### 5.2. O Que o Mod Deve EVITAR

❌ **Não interferir com separation distance** (60m)
   - Se mod quer manter recrutas >60m, deve setar `ped->bNeverLeavesGroup = true`

❌ **Não sobrescrever decision maker durante missões**
   - Engine muda automaticamente (PASSIVE → AGGRESSIVE)
   - Sobrescrever pode quebrar progressão

❌ **Não adicionar ao CPedGroups se não necessário**
   - Mod pode gerenciar próprio array de recrutas
   - Apenas adicionar ao CPedGroups se quiser integração com opcodes vanilla

❌ **Não usar slots de grupo desnecessariamente**
   - Apenas 8 grupos no engine - missions precisam deles
   - Se mod gerencia próprio grupo, liberar slot quando não em uso

---

### 5.3. Melhorias de Integração Recomendadas

#### Opção 1: Integração Completa (Usar CPedGroups)

**Vantagens**:
- ✅ Funciona com opcodes vanilla (ADD_CHAR_TO_GROUP, etc)
- ✅ Engine gerencia radar blips automaticamente
- ✅ Decision makers mudam automaticamente durante missões
- ✅ Separation distance enforcement automático

**Desvantagens**:
- ❌ Limite de 7 followers (pode ser insuficiente)
- ❌ Separation distance 60m pode remover recrutas prematuramente
- ❌ Compete por slots de grupo com missions

**Implementação**:
```cpp
// grove_recruit_init.cpp (novo)

int g_playerGroupID = -1;

void InitializePlayerGroup() {
    if (g_playerGroupID == -1) {
        g_playerGroupID = CPedGroups::AddGroup();  // Criar grupo do player
        CPedGroup* group = &CPedGroups::ms_groups[g_playerGroupID];
        group->m_groupMembership.SetLeader(FindPlayerPed());
        group->m_bMembersEnterLeadersVehicle = true;
        group->m_groupMembership.m_separationRange = 100.0f;  // Aumentar para 100m
    }
}

void RecruitPed(CPed* ped) {
    if (g_playerGroupID == -1) {
        InitializePlayerGroup();
    }

    CPedGroup* group = &CPedGroups::ms_groups[g_playerGroupID];
    group->m_groupMembership.AddFollower(ped);
    ped->bNeverLeavesGroup = true;  // Não remover por distância

    // Mod continua gerenciando CAutoPilot customizado
}
```

---

#### Opção 2: Sistema Híbrido (Recomendado)

**Conceito**:
- Mod gerencia **próprio array de recrutas** (não usa CPedGroups)
- Apenas **adiciona ao CPedGroups quando necessário** (ex: durante missões)
- **Remove de CPedGroups** quando missão termina (libera slot)

**Vantagens**:
- ✅ Sem limite de 7 followers (mod gerencia quantos quiser)
- ✅ Sem separation distance enforcement (mod controla)
- ✅ Não compete por slots de grupo fora de missões
- ✅ Ainda compatível com opcodes vanilla quando em grupo

**Implementação**:
```cpp
// grove_recruit_manager.cpp (ajuste)

struct RecruitData {
    CPed* ped;
    CVehicle* vehicle;
    bool inNativeGroup;    // Novo: flag se está em CPedGroups
    int nativeGroupMemIdx; // Índice no CPedGroupMembership
    // ... resto dos campos ...
};

void OnMissionStart() {
    // Adicionar todos recrutas ao CPedGroups
    InitializePlayerGroup();
    for (auto& recruit : g_recruits) {
        if (!recruit.inNativeGroup) {
            CPedGroup* group = &CPedGroups::ms_groups[g_playerGroupID];
            group->m_groupMembership.AddFollower(recruit.ped);
            recruit.inNativeGroup = true;
        }
    }
}

void OnMissionEnd() {
    // Remover todos recrutas de CPedGroups (liberar slots)
    for (auto& recruit : g_recruits) {
        if (recruit.inNativeGroup) {
            CPedGroup* group = &CPedGroups::ms_groups[g_playerGroupID];
            group->m_groupMembership.RemoveFollower(recruit.ped);
            recruit.inNativeGroup = false;
        }
    }

    // Mod continua gerenciando recrutas fora de CPedGroups
}
```

---

#### Opção 3: Sistema Independente (Atual)

**Conceito**:
- Mod **NÃO usa CPedGroups** de forma alguma
- Gerencia 100% próprio sistema de recrutas
- Apenas interage com `CAutoPilot` de veículos

**Vantagens**:
- ✅ Máxima flexibilidade (sem limites engine)
- ✅ Não compete por recursos
- ✅ Controle total sobre comportamento

**Desvantagens**:
- ❌ Não funciona com opcodes vanilla (ADD_CHAR_TO_GROUP, etc)
- ❌ Precisa gerenciar radar blips manualmente
- ❌ Decision makers não mudam automaticamente

**Status Atual**: Mod já usa essa abordagem. **Funciona bem** se não houver necessidade de integração com scripts vanilla.

---

## 6. Comandos de Grupo - Expansão Imersiva

### 6.1. Comandos Nativos (Via CPedGroupIntelligence)

**Eventos Suportados**:
- `EVENT_LEADER_ENTRY_EXIT` - Líder entra/sai de veículo
- `EVENT_LEADER_QUIT_ENTERING_CAR_AS_DRIVER` - Líder cancela entrada
- `EVENT_LEADER_EXITED_CAR_AS_DRIVER` - Líder sai como driver
- `EVENT_LEADER_IN_CAR` - Líder dentro de veículo

**Propagação Automática**:
- Followers recebem eventos do líder via `CPedGroupIntelligence::ComputeEventResponseTasks`
- Exemplo: Líder entra em carro → Followers recebem task `ENTER_CAR_AS_PASSENGER`

---

### 6.2. Comandos Customizados (Expansão do Mod)

**Keybinds Propostos**:
```cpp
// grove_recruit_config.h

static constexpr int KEY_COMMAND_FOLLOW       = VK_KEY_G;  // "Follow me"
static constexpr int KEY_COMMAND_HOLD         = VK_KEY_H;  // "Hold position"
static constexpr int KEY_COMMAND_SCATTER      = VK_KEY_J;  // "Scatter/Disperse"
static constexpr int KEY_COMMAND_ATTACK       = VK_KEY_K;  // "Attack target"
static constexpr int KEY_COMMAND_ENTER_VEHICLE = VK_KEY_L;  // "Enter vehicle"
```

**Implementação**:
```cpp
// grove_recruit_commands.cpp (novo arquivo)

enum class GroupCommand : unsigned char {
    FOLLOW = 0,
    HOLD_POSITION,
    SCATTER,
    ATTACK_TARGET,
    ENTER_VEHICLE
};

void ExecuteGroupCommand(GroupCommand cmd) {
    CPlayerPed* player = FindPlayerPed();

    switch (cmd) {
        case GroupCommand::FOLLOW:
            for (auto& recruit : g_recruits) {
                recruit.commandMode = CommandMode::FOLLOW;
                recruit.holdPosition = CVector(0,0,0);  // Clear hold
                // Retornar a modo TRAFFIC_FOLLOW ou CIVICO
            }
            CHud::SetHelpMessage("Gang members following", false, false, false);
            break;

        case GroupCommand::HOLD_POSITION:
            for (auto& recruit : g_recruits) {
                recruit.commandMode = CommandMode::HOLD_POSITION;
                recruit.holdPosition = recruit.vehicle->GetPosition();
                recruit.vehicle->m_autoPilot.m_nCarMission = MISSION_STOP_FOREVER;
            }
            CHud::SetHelpMessage("Gang members holding position", false, false, false);
            break;

        case GroupCommand::SCATTER:
            for (auto& recruit : g_recruits) {
                recruit.commandMode = CommandMode::SCATTER;

                // Direção aleatória 360 graus
                float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * 3.14159f;
                CVector scatterDir(cos(angle), sin(angle), 0.0f);
                CVector scatterPos = recruit.vehicle->GetPosition() + scatterDir * 50.0f;

                recruit.vehicle->m_autoPilot.m_nCarMission = MISSION_GOTOCOORDS;
                recruit.vehicle->m_autoPilot.m_vecDestinationCoors = scatterPos;
                recruit.vehicle->m_autoPilot.m_nCruiseSpeed = 60;  // Scatter rápido
            }
            CHud::SetHelpMessage("Gang members scattering", false, false, false);
            break;

        case GroupCommand::ATTACK_TARGET:
            // Implementar sistema de targeting (raycast do player)
            // Recrutas mudam para MISSION_RAMCAR ou MISSION_BLOCKCAR
            break;

        case GroupCommand::ENTER_VEHICLE:
            // Se player está em veículo com assentos livres, recrutas entram
            if (player->m_pVehicle) {
                CVehicle* playerVeh = player->m_pVehicle;
                int availableSeats = playerVeh->m_nMaxPassengers - playerVeh->m_nNumPassengers;
                // Task ENTER_CAR_AS_PASSENGER para recrutas a pé
            }
            break;
    }
}
```

---

### 6.3. Feedback Visual (HUD Messages)

**Implementação**:
```cpp
// grove_recruit_hud.cpp (novo)

void ShowCommandFeedback(GroupCommand cmd, int recruitCount) {
    char msg[128];

    switch (cmd) {
        case GroupCommand::FOLLOW:
            sprintf(msg, "~g~%d gang members~w~ following", recruitCount);
            break;
        case GroupCommand::HOLD_POSITION:
            sprintf(msg, "~y~%d gang members~w~ holding position", recruitCount);
            break;
        case GroupCommand::SCATTER:
            sprintf(msg, "~r~%d gang members~w~ scattering", recruitCount);
            break;
    }

    CHud::SetHelpMessage(msg, false, false, false);
}
```

---

## 7. Compatibilidade com Missions e Scripts

### 7.1. Detecção de Missões

**Verificar Estado**:
```cpp
bool isOnMission = CTheScripts::IsPlayerOnAMission();

if (isOnMission && !g_wasOnMission) {
    // Missão iniciada
    OnMissionStart();
    g_wasOnMission = true;
}
else if (!isOnMission && g_wasOnMission) {
    // Missão terminada
    OnMissionEnd();
    g_wasOnMission = false;
}
```

**Comportamento Durante Missões**:
- **Opção A**: Desabilitar mod temporariamente (para evitar conflitos)
- **Opção B**: Adicionar recrutas ao CPedGroups (integração completa)
- **Opção C**: Manter mod ativo mas com behavior modificado (ex: mais defensivo)

---

### 7.2. Interação com Territory Wars

**Sistema de Gangwar** (`CGangWars.h`):
- Engine gerencia territory control (verde, roxo, amarelo, etc)
- Gangwar ativa quando player entra em territory rival
- Gang members spawnam automaticamente para defender/atacar

**Integração do Mod**:
```cpp
// grove_recruit_gangwar.cpp (novo)

bool IsInActiveGangWar() {
    // Verificar se gangwar está ativa
    return CGangWars::bGangWarsActive;
}

void OnGangWarStart() {
    // Mudar behavior de recrutas para agressivo
    for (auto& recruit : g_recruits) {
        recruit.vehicle->m_autoPilot.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        recruit.isInCombatMode = true;
    }
}

void OnGangWarEnd() {
    // Retornar a comportamento normal
    for (auto& recruit : g_recruits) {
        recruit.vehicle->m_autoPilot.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        recruit.isInCombatMode = false;
    }
}
```

---

## 8. Melhorias de Imersão

### 8.1. Formação Customizada

**Padrões de Formação**:
```cpp
enum class FormationType {
    COLUMN,        // Fila indiana (um atrás do outro)
    WEDGE,         // Formação V (como pássaros)
    LINE_ABREAST,  // Linha horizontal (lado a lado)
    DIAMOND        // Losango (líder no centro)
};

CVector CalculateFormationPosition(int recruitIdx, FormationType formation) {
    CVector playerPos = FindPlayerPed()->GetPosition();
    float playerHeading = FindPlayerPed()->GetHeading();

    switch (formation) {
        case FormationType::COLUMN:
            // Recruta 0: 10m atrás, recruta 1: 20m atrás, etc
            return playerPos - GetForwardVector(playerHeading) * (10.0f + recruitIdx * 10.0f);

        case FormationType::WEDGE:
            // Recrutas alternados à esquerda/direita, cada vez mais atrás
            float lateralOffset = (recruitIdx % 2 == 0) ? 5.0f : -5.0f;
            float backOffset = 10.0f + (recruitIdx / 2) * 10.0f;
            return playerPos - GetForwardVector(playerHeading) * backOffset +
                   GetRightVector(playerHeading) * lateralOffset;

        // ... outros padrões
    }
}
```

---

### 8.2. Ranking e Progressão de Recrutas

**Sistema de XP**:
```cpp
struct RecruitData {
    // ... campos existentes ...

    int xp;              // Experience points
    int rank;            // 0=Recruit, 1=Soldier, 2=Lieutenant, 3=OG
    int killCount;       // Kills deste recruta
    float loyalty;       // 0.0-1.0 (afeta behavior)
};

void OnRecruitKill(RecruitData& recruit) {
    recruit.killCount++;
    recruit.xp += 10;

    // Level up
    if (recruit.xp >= 100 && recruit.rank < 3) {
        recruit.rank++;
        recruit.loyalty = min(1.0f, recruit.loyalty + 0.1f);

        char msg[128];
        sprintf(msg, "~g~Gang member~w~ promoted to ~y~%s~w~!", GetRankName(recruit.rank));
        CHud::SetHelpMessage(msg, false, false, false);
    }
}

const char* GetRankName(int rank) {
    switch (rank) {
        case 0: return "Recruit";
        case 1: return "Soldier";
        case 2: return "Lieutenant";
        case 3: return "OG";
        default: return "Unknown";
    }
}
```

**Benefícios por Rank**:
- **Recruit (0)**: Velocidade base, pode desertar se loyalty baixa
- **Soldier (1)**: +10% velocidade, não deserta
- **Lieutenant (2)**: +20% velocidade, melhor pathfinding (menos INVALID_LINK)
- **OG (3)**: +30% velocidade, imune a fear events

---

### 8.3. Personalização de Veículos

**Cores de Gang**:
```cpp
void ApplyGangVehicleCustomization(CVehicle* veh) {
    // Grove Street: Verde
    veh->m_nPrimaryColor = 126;   // Verde escuro
    veh->m_nSecondaryColor = 126; // Verde escuro

    // Adicionar nitro (easter egg)
    if (rand() % 10 == 0) {  // 10% chance
        veh->m_nNitroCount = 1;
    }

    // Hydraulics (para Lowriders)
    if (veh->m_nModelIndex == 536 || veh->m_nModelIndex == 575) {  // Blade, Broadway
        veh->m_vehicleSpecialCollidingStuff.bHydraulics = true;
    }
}
```

---

## 9. Conclusão e Recomendação Final

### Sistema Recomendado: **Híbrido**

**Por Quê?**
1. ✅ **Flexibilidade**: Mod gerencia próprio array (sem limite de 7 followers)
2. ✅ **Compatibilidade**: Adiciona a CPedGroups apenas durante missões
3. ✅ **Recursos**: Não compete por slots de grupo fora de missões
4. ✅ **Imersão**: Integra com decision makers e progression nativa

**Estrutura Proposta**:
```
┌─────────────────────────────────────────────────────────┐
│              MOD GROVE RECRUIT (Custom)                 │
│  ┌─────────────────────────────────────────────────┐  │
│  │ • Array próprio de recrutas (ilimitado)         │  │
│  │ • CAutoPilot management customizado              │  │
│  │ • Traffic-like behavior system (TRAFFIC_FOLLOW)  │  │
│  │ • INVALID_LINK fallback                          │  │
│  │ • Dynamic catchup speeds                         │  │
│  │ • Ranking e progression system                   │  │
│  └─────────────────────────────────────────────────┘  │
│                          ↕                              │
│          (Adiciona a CPedGroups apenas quando          │
│           missão ativa ou comando explícito)           │
│                          ↕                              │
└─────────────────────────────────────────────────────────┘
                          ↕
┌─────────────────────────────────────────────────────────┐
│           SISTEMA NATIVO CPedGroups (Engine)            │
│  ┌─────────────────────────────────────────────────┐  │
│  │ • Decision maker progression                     │  │
│  │ • Radar blip management                          │  │
│  │ • Event propagation (leader → followers)         │  │
│  │ • Auto-entry em veículos                         │  │
│  └─────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

**Próximos Passos de Implementação**:
1. [ ] Adicionar flag `inNativeGroup` a `RecruitData`
2. [ ] Implementar `OnMissionStart()` e `OnMissionEnd()` hooks
3. [ ] Implementar comandos de grupo (FOLLOW, HOLD, SCATTER)
4. [ ] Adicionar sistema de ranking/XP
5. [ ] Testar compatibilidade com missions vanilla

---

**Arquivos Principais Referenciados**:
- `plugin_sa/game_sa/CPedGroups.h/cpp` - Gerenciador global
- `gta-reversed/source/game_sa/PedGroup.h/cpp` - Grupo individual
- `gta-reversed/source/game_sa/PedGroupMembership.h/cpp` - Gerenciamento de membros
- `gta-reversed/source/game_sa/PedGroupIntelligence.h/cpp` - Event handling
- `plugin_sa/game_sa/CVehicle.h` - CAutoPilot structure
- `grove_recruit_drive.cpp` - Implementação atual do mod
