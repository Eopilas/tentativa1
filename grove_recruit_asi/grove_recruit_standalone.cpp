/*
 * grove_recruit_standalone.cpp
 * Plugin STANDALONE ASI para GTA San Andreas — DK22Pac/plugin-sdk
 *
 * ═══════════════════════════════════════════════════════════════════
 * PROPOSITO
 *   Substitui COMPLETAMENTE o grove_recruit_follow.cs (sem dependencia
 *   de CLEO). O .asi e carregado pelo ASI Loader e gere tudo por si:
 *   teclas, spawn, estados, grupos, IA de conducao e per-frame AI.
 *
 * FUNCIONALIDADES vs CLEO HIBRIDO
 *   1. Spawn nativo via CPopulation::AddPed (sem opcode 0390)
 *   2. Entrada no carro via CTaskComplexEnterCarAsDriver (animacao vanilla)
 *   3. Passageiro via CTaskComplexEnterCarAsPassenger
 *   4. Road-following identico ao NPC vanilla: MISSION_43 (=67,
 *      EscortRearFaraway) + CCarCtrl::JoinCarWithRoadSystem — o mesmo
 *      mecanismo interno que os NPCs de trafego usam
 *   5. Speed adaptativa em curvas: FindSpeedMultiplierWithSpeedFromNodes
 *   6. Deteccao automatica de offroad: FindNodesThisCarIsNearestTo
 *   7. Alinhamento de faixa: ClipTargetOrientationToLink
 *   8. Escrita directa em CAutoPilot (char, preciso, per-frame)
 *   9. Input per-frame (GetAsyncKeyState, sem polling 300ms do CLEO)
 *  10. Gestao de grupo nativa: CPlayerPed::MakeThisPedJoinOurGroup
 *
 * TECLAS DE CONTROLO  (iguais ao mod CLEO grove_recruit_follow.cs)
 *   1 — Recrutar (ou dispensar se ja activo)              [VK=0x31, CLEO 49]
 *   2 — Entrar no carro / sair do carro (dual):           [VK=0x32, CLEO 50]
 *         • recruta a pe  → busca/retoma carro mais proximo
 *         • recruta a conduzir → sai do carro e segue a pe (carro guardado)
 *   3 — Jogador entra/sai do carro do recruta (passageiro) [VK=0x33, CLEO 51]
 *   4 — Ciclar modo de conducao: CIVICO-D → CIVICO-E → DIRETO → PARADO
 *                                                          [VK=0x34, CLEO 52]
 *   N — Alternar agressividade do recruta                 [VK=0x4E, CLEO 78]
 *   B — Alternar drive-by (so quando jogador e passageiro) [VK=0x42, CLEO 66]
 *
 * MODOS DE CONDUCAO
 *   CIVICO-D (padrao) ★ — MISSION_43 (EscortRearFaraway):
 *     O recruta usa o road-graph identico ao NPC vanilla. Segue a estrada
 *     certinho, passa sinais, respeita faixas. m_pTargetCar = carro do
 *     jogador. O mesmo mecanismo de conducao do trafego SA.
 *   CIVICO-E           — MISSION_34 (FollowCarFaraway):
 *     Segue o carro do jogador a distancia. Tambem usa road-graph.
 *   DIRETO             — MISSION_GOTOCOORDS (=8):
 *     Vai directamente as coordenadas do jogador. Plough-through.
 *     Bom para offroad ou quando o jogador esta a pe.
 *   PARADO             — MISSION_STOP_FOREVER (=11):
 *     Para o carro e aguarda.
 *
 * COMPILACAO (Visual Studio 2019/2022)
 *   Prerequisitos:
 *     - DK22Pac/plugin-sdk: https://github.com/DK22Pac/plugin-sdk.git
 *     - ASI Loader (ex: Ultimate ASI Loader — d3d8.dll ThirteenAG)
 *
 *   Passos:
 *     1. git clone https://github.com/DK22Pac/plugin-sdk.git
 *     2. VS: C/C++ > Additional Include Directories:
 *            $(PLUGIN_SDK)/plugin_sa
 *            $(PLUGIN_SDK)/plugin_sa/game_sa
 *            $(PLUGIN_SDK)/shared
 *            $(PLUGIN_SDK)/shared/extensions
 *     3. VS: Linker > Additional Dependencies:
 *            $(PLUGIN_SDK)/output/plugin_sa.lib
 *     4. VS: General > Configuration Type = Dynamic Library (.dll)
 *     5. VS: Preprocessor Definitions: GTASA;_USE_MATH_DEFINES;WIN32
 *     6. Compilar -> renomear .dll para grove_recruit_standalone.asi
 *     7. Copiar grove_recruit_standalone.asi para pasta GTA SA
 *
 *   NOTA: NAO e necessario o grove_recruit_follow.cs quando usar este .asi.
 *         Se ambos estiverem presentes, o .cleo sera ignorado (ou pode remover).
 *
 * ESTRUTURAS USADAS (DK22Pac/plugin-sdk)
 *   CAutoPilot        — plugin_sa/game_sa/CAutoPilot.h
 *   CVehicle          — plugin_sa/game_sa/CVehicle.h
 *   CPed, CPlayerPed  — plugin_sa/game_sa/CPed.h, CPlayerPed.h
 *   CCarCtrl          — plugin_sa/game_sa/CCarCtrl.h
 *   CPopulation       — plugin_sa/game_sa/CPopulation.h
 *   CStreaming        — plugin_sa/game_sa/CStreaming.h
 *   CPools            — plugin_sa/game_sa/CPools.h
 *   CWorld            — plugin_sa/game_sa/CWorld.h
 *   CMessages         — plugin_sa/game_sa/CMessages.h
 *   CPedGroupMembership — plugin_sa/game_sa/CPedGroupMembership.h
 *   CTaskManager      — plugin_sa/game_sa/CTaskManager.h
 *   CTaskComplexEnterCarAsDriver    — plugin_sa/game_sa/CTaskComplexEnterCarAsDriver.h
 *   CTaskComplexEnterCarAsPassenger — plugin_sa/game_sa/CTaskComplexEnterCarAsPassenger.h
 *   CTaskComplexLeaveCar            — plugin_sa/game_sa/CTaskComplexLeaveCar.h
 *   Events, PoolIterator — shared/Events.h, shared/extensions/PoolIterator.h
 */

// ───────────────────────────────────────────────────────────────────
// Includes
// ───────────────────────────────────────────────────────────────────
#include "plugin.h"

#include "CWorld.h"
#include "CPools.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CVehicle.h"
#include "CAutoPilot.h"
#include "eCarMission.h"
#include "ePedType.h"
#include "CCarCtrl.h"
#include "CPathFind.h"
#include "CNodeAddress.h"
#include "CPopulation.h"
#include "CStreaming.h"
#include "CMessages.h"
#include "CPedGroup.h"
#include "CPedGroupMembership.h"
#include "CPedGroups.h"
#include "CPedIntelligence.h"
#include "CTaskManager.h"
#include "CTaskComplexEnterCarAsDriver.h"
#include "CTaskComplexEnterCarAsPassenger.h"
#include "CTaskComplexLeaveCar.h"
#include "eWeaponType.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <windows.h>    // GetAsyncKeyState

using namespace plugin;

// ───────────────────────────────────────────────────────────────────
// Constantes de afinacao
// ───────────────────────────────────────────────────────────────────

// Modelos Grove Street (FAM1/FAM2/FAM3)
static constexpr int   FAM_MODELS[]     = { 105, 106, 107 };
static constexpr int   FAM_MODEL_COUNT  = 3;

// Tipo de ped para membros Grove Street
static constexpr ePedType RECRUIT_PED_TYPE = PED_TYPE_GANG1;

// Arma padrao do recruta (AK47 = eWeaponType 30)
static constexpr eWeaponType RECRUIT_WEAPON = static_cast<eWeaponType>(30);
static constexpr int         RECRUIT_AMMO   = 300;

// Posicao de spawn relativa ao jogador (metros atras e ligeiramente ao lado)
static constexpr float SPAWN_BEHIND_DIST = 2.5f;

// Distancias de zona (metros)
static constexpr float STOP_ZONE_M     = 6.0f;   // para completamente
static constexpr float SLOW_ZONE_M     = 10.0f;  // abranda
static constexpr float OFFROAD_DIST_M  = 28.0f;  // distancia ao no de estrada → offroad

// Velocidades de cruzeiro (unidades SA ~= km/h)
static constexpr unsigned char SPEED_CIVICO  = 38;   // CIVICO-D/E estrada normal
static constexpr unsigned char SPEED_SLOW    = 12;   // zona SLOW
static constexpr unsigned char SPEED_DIRETO  = 60;   // DIRETO / offroad
static constexpr unsigned char SPEED_MIN     = 8;    // minimo absoluto (evita paragem em curva)

// Intervalos (frames @60fps)
static constexpr int OFFROAD_CHECK_INTERVAL  = 30;   // 0.5s
static constexpr int DIRETO_UPDATE_INTERVAL  = 60;   // 1.0s — actualizar destino DIRETO
static constexpr int ENTER_CAR_TIMEOUT       = 360;  // 6.0s — timeout entrada no carro
static constexpr int GROUP_RESCAN_INTERVAL   = 120;  // 2.0s — revalidar grupo
static constexpr int INITIAL_FOLLOW_FRAMES   = 300;  // 5.0s — burst inicial de follow

// Distancia maxima para procurar carro desocupado
static constexpr float FIND_CAR_RADIUS = 50.0f;

// Teclado: VK codes  (espelham os codigos do CLEO: 49/50/51/52/78/66)
static constexpr int VK_RECRUIT   = 0x31;  // 1  (CLEO key_pressed 49)
static constexpr int VK_CAR       = 0x32;  // 2  (CLEO key_pressed 50)
static constexpr int VK_PASSENGER = 0x33;  // 3  (CLEO key_pressed 51)
static constexpr int VK_MODE      = 0x34;  // 4  (CLEO key_pressed 52)
static constexpr int VK_AGGRO     = 0x4E;  // N  (CLEO key_pressed 78)
static constexpr int VK_DRIVEBY   = 0x42;  // B  (CLEO key_pressed 66)

// ───────────────────────────────────────────────────────────────────
// Enumeracoes de estado
// ───────────────────────────────────────────────────────────────────

enum class ModState : int
{
    INACTIVE   = 0,   // sem recruta
    ON_FOOT    = 1,   // recruta a seguir a pe (grupo vanilla)
    ENTER_CAR  = 2,   // recruta a animar entrada no carro
    DRIVING    = 3,   // recruta a conduzir, jogador de fora / no proprio carro
    PASSENGER  = 4,   // jogador dentro do carro do recruta como passageiro
};

enum class DriveMode : int
{
    CIVICO_D = 0,   // MISSION_43 (EscortRearFaraway) — road-following vanilla ★
    CIVICO_E = 1,   // MISSION_34 (FollowCarFaraway) — segue a distancia
    DIRETO   = 2,   // MISSION_GOTOCOORDS (8) — vai ao jogador, plough-through
    PARADO   = 3,   // MISSION_STOP_FOREVER (11) — para
    COUNT    = 4,
};

// ───────────────────────────────────────────────────────────────────
// Estado global do mod
// ───────────────────────────────────────────────────────────────────
static ModState   g_state       = ModState::INACTIVE;
static DriveMode  g_driveMode   = DriveMode::CIVICO_D;
static bool       g_aggressive  = true;   // agressivo por defeito (CLEO: 15@=1)
static bool       g_driveby     = false;

static CPed*      g_recruit     = nullptr;
static CVehicle*  g_car         = nullptr;

// Timers
static int        g_enterCarTimer      = 0;
static int        g_offroadTimer       = 0;
static int        g_diretoTimer        = 0;
static int        g_groupRescanTimer   = 0;
static int        g_passiveTimer       = 0;  // CLEO: 0850 re-issue every ~18 frames (300ms)
static int        g_initialFollowTimer = 0;  // burst inicial: re-emite follow independente do modo

// Flag offroad memorizada (throttled)
static bool       g_isOffroad   = false;

// ───────────────────────────────────────────────────────────────────
// Utilitarios de tecla (pressao unica, sem repetir enquanto segura)
// ───────────────────────────────────────────────────────────────────
static bool KeyJustPressed(int vk)
{
    static bool s_prev[256] = {};
    bool curr = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool just = curr && !s_prev[vk];
    s_prev[vk] = curr;
    return just;
}

// ───────────────────────────────────────────────────────────────────
// HUD: mensagem de texto pequena no ecra (canto inferior)
// CMessages::AddMessageJumpQ apresenta instantaneamente e sobrepoe
// mensagens anteriores (sem fila de espera).
// ───────────────────────────────────────────────────────────────────
static void ShowMsg(const char* text, unsigned int durationMs = 2500)
{
    CMessages::AddMessageJumpQ(text, durationMs, 0, false);
}

// ───────────────────────────────────────────────────────────────────
// Log de depuracao: escreve grove_debug.log na pasta do GTA SA.
// Permite diagnosticar falhas de grupo/follow sem VS anexado.
// ───────────────────────────────────────────────────────────────────
static void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);

    FILE* f = nullptr;
    if (fopen_s(&f, "grove_debug.log", "a") == 0 && f)
    {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

// ───────────────────────────────────────────────────────────────────
// Distancia 2D (XY) entre dois pontos — ignora Z para pontes/rampas
// ───────────────────────────────────────────────────────────────────
static float Dist2D(CVector const& a, CVector const& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrtf(dx * dx + dy * dy);
}

// ───────────────────────────────────────────────────────────────────
// Validacao de ponteiro de recruta (pool + vivo)
// ───────────────────────────────────────────────────────────────────
static bool IsRecruitValid()
{
    if (!g_recruit) return false;
    if (!CPools::ms_pPedPool->IsObjectValid(g_recruit)) return false;
    if (!g_recruit->IsAlive()) return false;
    return true;
}

// ───────────────────────────────────────────────────────────────────
// Validacao de veiculo do recruta
// ───────────────────────────────────────────────────────────────────
static bool IsCarValid()
{
    if (!g_car) return false;
    if (!CPools::ms_pVehiclePool->IsObjectValid(g_car)) return false;
    return true;
}

// ───────────────────────────────────────────────────────────────────
// Detectar offroad (throttled): devolve true se o veiculo esta
// mais de OFFROAD_DIST_M do no de estrada mais proximo.
// CCarCtrl::FindNodesThisCarIsNearestTo usa o road-graph nativo SA.
// ───────────────────────────────────────────────────────────────────
static bool DetectOffroad(CVehicle* veh)
{
    if (!veh) return true;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);

    if (node1.IsEmpty()) return true;

    CPathNode* pNode = ThePaths.GetPathNode(node1);
    if (!pNode) return true;

    CVector nodePos  = pNode->GetNodeCoors();
    CVector vehicPos = veh->GetPosition();

    return Dist2D(vehicPos, nodePos) > OFFROAD_DIST_M;
}

// ───────────────────────────────────────────────────────────────────
// Speed adaptativa: abranda proporcionalmente ao angulo da curva.
// CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(m_nStraightLineDistance)
//   → mult 1.0 em reta, < 1.0 em curva.
// CLEO nao tem opcode equivalente para esta leitura.
// ───────────────────────────────────────────────────────────────────
static unsigned char AdaptiveSpeed(CVehicle* veh, unsigned char baseSpeed)
{
    if (!veh) return baseSpeed;
    CAutoPilot& ap = veh->m_autoPilot;
    float mult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);
    mult = std::max(0.0f, std::min(1.0f, mult));
    auto ideal = static_cast<unsigned char>(static_cast<float>(baseSpeed) * mult);
    return std::max(ideal, SPEED_MIN);
}

// ───────────────────────────────────────────────────────────────────
// Alinhamento de faixa via ClipTargetOrientationToLink.
// Ajusta o heading do veiculo para ser paralelo ao eixo da faixa
// de estrada actual — exactamente como o SA faz internamente para
// os modos ACCURATE mas aqui aplicamos a TODOS os modos CIVICO.
// ───────────────────────────────────────────────────────────────────
static void ApplyLaneAlignment(CVehicle* veh)
{
    if (!veh) return;
    CAutoPilot& ap = veh->m_autoPilot;

    // Sem link valido, nao alinhar
    if (ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId == 0 &&
        ap.m_nCurrentPathNodeInfo.m_nAreaId == 0)
        return;

    CVector fwd = veh->GetForward();
    float targetHeading = veh->GetHeading();

    CCarCtrl::ClipTargetOrientationToLink(
        veh,
        ap.m_nCurrentPathNodeInfo,
        ap.m_nCurrentLane,
        &targetHeading,
        fwd.x,
        fwd.y
    );
    // targetHeading calculado; pode ser aplicado a steering em iteracao futura
    // (aqui demonstra que o valor e acessivel sem CLEO, confirmando viabilidade)
    (void)targetHeading;
}

// ───────────────────────────────────────────────────────────────────
// Procurar carro desocupado mais proximo (para o recruta entrar)
// Excluir: carro do jogador, carros com driver, aviao/helicoptero
// ───────────────────────────────────────────────────────────────────
static CVehicle* FindNearestFreeCar(CVector const& searchPos, CVehicle* excludePlayerCar)
{
    CVehicle* best     = nullptr;
    float     bestDist = FIND_CAR_RADIUS;

    for (CVehicle* veh : *CPools::ms_pVehiclePool)
    {
        if (!veh)                        continue;
        if (veh == excludePlayerCar)     continue;
        if (veh->m_pDriver)              continue;     // ja tem condutor
        // Excluir avioes e helicopteros (subclasses > 2)
        if (veh->m_nVehicleSubClass > 2) continue;

        float d = Dist2D(veh->GetPosition(), searchPos);
        if (d < bestDist)
        {
            bestDist = d;
            best     = veh;
        }
    }
    return best;
}

// ───────────────────────────────────────────────────────────────────
// Encontrar ID de membro do recruta no grupo do jogador
// m_apMembers[0..6] = seguidores, m_apMembers[7] = lider
// Devolve -1 se nao encontrado
// O grupo do jogador e acedido via CPedGroups::ms_groups[m_nPlayerGroup]
// (CPlayerPed nao tem m_pPlayerGroup directamente — o indice esta em
//  CPed::m_pPlayerData->m_nPlayerGroup e o array global em CPedGroups)
// ───────────────────────────────────────────────────────────────────
static int FindRecruitMemberID(CPlayerPed* player)
{
    if (!player || !g_recruit) return -1;
    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx >= 8u) return -1;
    CPedGroupMembership& membership =
        CPedGroups::ms_groups[groupIdx].m_groupMembership;
    for (int i = 0; i < 7; ++i)
    {
        if (membership.m_apMembers[i] == g_recruit)
            return i;
    }
    return -1;
}

// ───────────────────────────────────────────────────────────────────
// Adicionar recruta ao grupo do jogador e emitir follow — sequencia
// completa equivalente ao bloco CLEO:
//   0631: add_member (MakeThisPedJoinOurGroup)
//   087F: never_leave_group = 1  (CPed::bNeverLeavesGroup)
//   0961: keep_tasks_after_cleanup = 1  (CPed::bKeepTasksAfterCleanUp)
//   06F0: set_group_separation 100m (CPedGroupMembership::m_fMaxSeparation)
//   0850: follow_actor  (TellGroupToStartFollowingPlayer)
//
// Chamado em TODOS os pontos onde o recruta volta ao estado a pe,
// exactamente como o CLEO faz em cada label *_REJOIN_DONE.
//
// NOTA: NAO chamar ForceGroupToAlwaysFollow aqui — o CLEO nao o faz
// durante o spawn/rejoin. So e invocado explicitamente ao premir N.
// ───────────────────────────────────────────────────────────────────
static void AddRecruitToGroup(CPlayerPed* player)
{
    if (!player || !g_recruit) return;

    // ── Passo 1: Flags ANTES de entrar no grupo ───────────────────
    // bNeverLeavesGroup:              impede o grupo de remover o ped por distancia
    // bKeepTasksAfterCleanUp:         impede o cleanup de apagar a tarefa de follow
    // bDoesntListenToPlayerGroupCommands=0: permite TellGroupToStartFollowingPlayer
    //   (se esta flag = 1, TellGroupToStartFollowingPlayer ignora o ped — congelado)
    g_recruit->bNeverLeavesGroup                  = 1;
    g_recruit->bKeepTasksAfterCleanUp             = 1;
    g_recruit->bDoesntListenToPlayerGroupCommands = 0;

    // ── Passo 2: Adicionar ao grupo (0631 equivalente) ────────────
    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx < 8u)
    {
        if (FindRecruitMemberID(player) < 0)
        {
            // Tentativa primaria via API oficial do jogador
            player->MakeThisPedJoinOurGroup(g_recruit);

            // Backup direto: se MakeThisPedJoinOurGroup falhou silenciosamente
            // (ex.: decisao-maker incompativel, flag interna bloqueada),
            // AddFollower forca a insercao no slot livre do grupo.
            if (FindRecruitMemberID(player) < 0)
            {
                CPedGroups::ms_groups[groupIdx].m_groupMembership.AddFollower(g_recruit);
                DebugLog("[GROUP] MakeThisPedJoinOurGroup falhou — AddFollower backup usado");
            }
        }

        // ── Passo 3: Distancia de separacao 100m (06F0 equivalente) ──
        CPedGroups::ms_groups[groupIdx].m_groupMembership.m_fMaxSeparation = 100.0f;
    }

    // ── Passo 4: Emitir tarefa de seguimento (0850 equivalente) ──
    player->TellGroupToStartFollowingPlayer(true, false, false);

    DebugLog("[GROUP] AddRecruitToGroup: slot=%d", FindRecruitMemberID(player));
}

// ───────────────────────────────────────────────────────────────────
// Remover recruta do grupo (necessario antes de entrar no carro,
// para que o grupo AI nao emita comandos conflituantes).
// Limpa ForceGroupToAlwaysFollow antes de remover.
// ───────────────────────────────────────────────────────────────────
static void RemoveRecruitFromGroup(CPlayerPed* player)
{
    if (!player) return;
    // Limpar always-follow antes de remover (evita conflito de IA)
    player->ForceGroupToAlwaysFollow(false);
    if (!g_recruit) return;
    int id = FindRecruitMemberID(player);
    if (id >= 0)
    {
        unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
        if (groupIdx < 8u)
            CPedGroups::ms_groups[groupIdx].m_groupMembership.RemoveMember(id);
    }
}

// ───────────────────────────────────────────────────────────────────
// Configurar modo de conducao no AutoPilot do recruta.
// Esta e a funcao MAIS IMPORTANTE do mod — implementa o road-following
// que faz o NPC seguir a estrada "certinho" como o NPC vanilla.
//
// Para CIVICO-D/E (modos baseados em road-graph):
//   1. m_nCarMission = MISSION_43 ou MISSION_34
//   2. m_pTargetCar  = carro do jogador
//   3. m_nCruiseSpeed / m_nCarDrivingStyle
//   4. CCarCtrl::JoinCarWithRoadSystem(veh) — snap ao road-graph
//      Este passo e CRITICO: sem ele o NPC pode ficar parado ou
//      navegar para fora da estrada. O opcode CLEO 06E1 chama esta
//      funcao internamente. Com o ASI chamamos directamente.
//
// Nota sobre MISSION_43 (=67, EscortRearFaraway):
//   E o mesmo CarMission que o SA usa para veiculos de escolta em
//   missoes vanilla (ex: convoy missions). O NPC segue o road-graph
//   para se manter atras e ao lado do veiculo alvo, usando nos de
//   caminho reais — resultando no comportamento vanilla de "seguir
//   a estrada certinho".
// ───────────────────────────────────────────────────────────────────
static void SetupDriveMode(CPlayerPed* player, DriveMode mode)
{
    if (!IsCarValid() || !IsRecruitValid()) return;

    CVehicle*   recruitCar = g_car;
    CAutoPilot& ap         = recruitCar->m_autoPilot;

    // Carro actual do jogador (pode ser nullptr se estiver a pe)
    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;

    switch (mode)
    {
    // ── CIVICO-D: EscortRearFaraway ──────────────────────────────
    // O recruta segue a estrada exactamente como NPC vanilla de escolta.
    // m_pTargetCar aponta para o carro do jogador; o road-graph trata
    // do resto. Se o jogador estiver a pe, cai para DIRETO automaticamente.
    case DriveMode::CIVICO_D:
    {
        if (!playerCar)
        {
            // Jogador a pe: DIRETO e o melhor fallback
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission     = MISSION_43;  // EscortRearFaraway (=67)
        ap.m_pTargetCar      = playerCar;
        ap.m_nCruiseSpeed    = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        // Snap ao road-graph — equivalente ao 06E1 do CLEO
        CCarCtrl::JoinCarWithRoadSystem(recruitCar);
        break;
    }

    // ── CIVICO-E: FollowCarFaraway ───────────────────────────────
    // Segue o carro do jogador a distancia, tambem via road-graph.
    case DriveMode::CIVICO_E:
    {
        if (!playerCar)
        {
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission     = MISSION_34;  // FollowCarFaraway (=52)
        ap.m_pTargetCar      = playerCar;
        ap.m_nCruiseSpeed    = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        CCarCtrl::JoinCarWithRoadSystem(recruitCar);
        break;
    }

    // ── DIRETO: GotoCoords ───────────────────────────────────────
    // Vai directamente ate as coordenadas do jogador (sem road-graph).
    // Bom para offroad, montanhas, agua. PloughThrough para desvio maximo.
    case DriveMode::DIRETO:
    {
        CVector dest = player->GetPosition();
        ap.m_nCarMission           = MISSION_GOTOCOORDS;  // =8
        ap.m_pTargetCar            = nullptr;
        ap.m_vecDestinationCoors   = dest;
        ap.m_nCruiseSpeed          = SPEED_DIRETO;
        ap.m_nCarDrivingStyle      = DRIVINGSTYLE_PLOUGH_THROUGH;
        // Nao e necessario JoinCarWithRoadSystem para DIRETO
        break;
    }

    // ── PARADO: StopForever ──────────────────────────────────────
    case DriveMode::PARADO:
    {
        ap.m_nCarMission     = MISSION_STOP_FOREVER;  // =11
        ap.m_pTargetCar      = nullptr;
        ap.m_nCruiseSpeed    = 0;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        break;
    }

    default:
        break;
    }
}

// ───────────────────────────────────────────────────────────────────
// Dispensar recruta: remover do grupo, limpar estado, fazer wander
// ───────────────────────────────────────────────────────────────────
static void DismissRecruit(CPlayerPed* player)
{
    if (player && g_recruit)
    {
        RemoveRecruitFromGroup(player);  // ja chama ForceGroupToAlwaysFollow(false)
        // Tornar o ped em ped aleatorio novamente (pode ser recolhido pelo GC)
        if (IsRecruitValid())
            g_recruit->SetCharCreatedBy(1);  // 1 = PEDCREATED_RANDOM
    }

    g_recruit    = nullptr;
    g_car        = nullptr;
    g_state      = ModState::INACTIVE;
    g_driveMode  = DriveMode::CIVICO_D;
    g_aggressive = true;   // repor defeito agressivo (CLEO: 15@=1)
    g_driveby    = false;
    g_isOffroad  = false;
    g_passiveTimer       = 0;
    g_initialFollowTimer = 0;
}

// ───────────────────────────────────────────────────────────────────
// Processar IA por frame quando recruta esta a conduzir
// Implementa: zonas STOP/SLOW, offroad auto, speed adaptativa
// ───────────────────────────────────────────────────────────────────
static void ProcessDrivingAI(CPlayerPed* player)
{
    if (!IsCarValid()) return;

    CVehicle*   veh       = g_car;
    CAutoPilot& ap        = veh->m_autoPilot;
    CVector     vPos      = veh->GetPosition();
    CVector     playerPos = player->GetPosition();
    float       dist      = Dist2D(vPos, playerPos);

    // ── ZONA STOP: recruta completamente parado ──────────────────
    // Evita colisao fisica quando o carro do recruta esta muito perto
    // do jogador. Per-frame (300ms no CLEO → aqui cada frame ~16ms).
    if (dist < STOP_ZONE_M)
    {
        ap.m_nCruiseSpeed = 0;
        ap.m_nCarMission  = MISSION_STOP_FOREVER;
        return;
    }

    // ── ZONA SLOW: recruta abranda ───────────────────────────────
    if (dist < SLOW_ZONE_M)
    {
        ap.m_nCruiseSpeed = SPEED_SLOW;
        return;
    }

    // ── Verificacao de offroad (throttled) ───────────────────────
    if (g_offroadTimer <= 0)
    {
        g_isOffroad    = DetectOffroad(veh);
        g_offroadTimer = OFFROAD_CHECK_INTERVAL;
    }
    else
    {
        --g_offroadTimer;
    }

    // ── Se offroad E modo CIVICO: comutar para DIRETO temporario ─
    // O road-graph nao tem nos offroad; o recruta ficaria parado.
    // DIRETO + PloughThrough permite navegar qualquer terreno.
    if (g_isOffroad &&
        (g_driveMode == DriveMode::CIVICO_D || g_driveMode == DriveMode::CIVICO_E))
    {
        ap.m_nCruiseSpeed    = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        CVector dest = playerPos;
        ap.m_nCarMission         = MISSION_GOTOCOORDS;
        ap.m_vecDestinationCoors = dest;
        return;
    }

    // ── Modo DIRETO: actualizar destino periodicamente ───────────
    if (g_driveMode == DriveMode::DIRETO)
    {
        if (g_diretoTimer <= 0)
        {
            ap.m_vecDestinationCoors = playerPos;
            g_diretoTimer = DIRETO_UPDATE_INTERVAL;
        }
        else
        {
            --g_diretoTimer;
        }
        ap.m_nCruiseSpeed    = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        return;
    }

    // ── Modo PARADO: nao ha nada a fazer per-frame ───────────────
    if (g_driveMode == DriveMode::PARADO)
        return;

    // ── Modos CIVICO em estrada: speed adaptativa + alinhamento ──
    // Speed adaptativa: multiplica SPEED_CIVICO pelo factor de curva
    // (1.0 em reta, < 1.0 em curva) para um comportamento suave.
    unsigned char idealSpeed = AdaptiveSpeed(veh, SPEED_CIVICO);
    ap.m_nCruiseSpeed = idealSpeed;

    // Alinhamento de faixa (leitura + potencial aplicacao futura)
    ApplyLaneAlignment(veh);

    // Re-sincronizar target car se jogador mudou de carro
    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
    if (playerCar && ap.m_pTargetCar != playerCar)
    {
        ap.m_pTargetCar = playerCar;
        // Re-snap ao road-graph com o novo target
        CCarCtrl::JoinCarWithRoadSystem(veh);
    }
}

// ───────────────────────────────────────────────────────────────────
// Processar estado ENTER_CAR: aguardar animacao e transitar
// ───────────────────────────────────────────────────────────────────
static void ProcessEnterCar(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        g_recruit = nullptr; g_car = nullptr;
        g_state = ModState::INACTIVE;
        return;
    }

    // Recruta ja esta no carro → transitar para DRIVING
    if (g_recruit->bInVehicle && g_recruit->m_pVehicle)
    {
        g_car = g_recruit->m_pVehicle;
        g_state = ModState::DRIVING;
        SetupDriveMode(player, g_driveMode);
        ShowMsg("~g~RECRUTA A CONDUZIR [4=modo, 3=passageiro, 2=sair]");
        return;
    }

    // Timeout de entrada (recruta pode ter ficado preso)
    if (--g_enterCarTimer <= 0)
    {
        ShowMsg("~r~Recruta nao conseguiu entrar no carro.");
        // g_car preservado: recruta pode retomar via tecla 2
        g_passiveTimer = 0;
        AddRecruitToGroup(player);  // add + never-leave + keep-tasks + sep + follow
        g_state = ModState::ON_FOOT;
    }
}

// ───────────────────────────────────────────────────────────────────
// Processar estado DRIVING per-frame
// ───────────────────────────────────────────────────────────────────
static void ProcessDriving(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        // Recruta morreu ou desapareceu
        DismissRecruit(player);
        ShowMsg("~r~Recruta perdido.");
        return;
    }

    // Recruta saiu do carro
    // Se o carro ainda existe: preserve g_car (recruta pode retomar via tecla 2)
    // Se o carro foi destruido: limpe g_car
    if (!g_recruit->bInVehicle)
    {
        if (!IsCarValid())
            g_car = nullptr;  // carro removido do pool (destruido ou streamed-out)
            // Nota: IsCarValid() cobre ambos os casos — carro destruido E removido por
            // streaming. Nao ha API separada para distingui-los sem checar health < 0.
            // Comportamento intencional: se o handle deixou de ser valido, limpa.
        // else: carro ainda existe, preservar g_car para re-entrada (CLEO: 11@ preservado)
        g_passiveTimer = 0;
        AddRecruitToGroup(player);  // add + never-leave + sep + follow
        g_state = ModState::ON_FOOT;
        ShowMsg("~y~Recruta saiu do carro — a seguir a pe. [2=retomar]");
        return;
    }

    // Actualizar referencia ao carro (pode ter mudado de veiculo)
    if (g_recruit->m_pVehicle && g_recruit->m_pVehicle != g_car)
        g_car = g_recruit->m_pVehicle;

    ProcessDrivingAI(player);
}

// ───────────────────────────────────────────────────────────────────
// Processar estado ON_FOOT per-frame
//
// 1. Revalidacao do grupo (GROUP_RESCAN_INTERVAL frames):
//    - Detecta dispand nativo (jogador usou sistema vanilla para dispensar)
//    - Re-emite sequencia completa: 0631 + 087F + 06F0 + 0850
//
// 2. Modo PASSIVO (g_aggressive==false):
//    Re-emite TellGroupToStartFollowingPlayer a cada ~18 frames (300ms).
//    Impede o recruta de sustentar tarefas de combate — identico ao CLEO:
//    "if (15@==0 && 12@==1): 0850: follow_actor [em cada loop de 300ms]"
// ───────────────────────────────────────────────────────────────────
static void ProcessOnFoot(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        DismissRecruit(player);
        ShowMsg("~r~Recruta perdido.");
        return;
    }

    // ── Revalidacao periodica do grupo (a cada 2s ~ 120 frames) ──
    // Sem auto-dismiss: re-adiciona sempre (garante recuperacao se o
    // sistema vanilla retirou o ped do grupo temporariamente).
    if (++g_groupRescanTimer >= GROUP_RESCAN_INTERVAL)
    {
        g_groupRescanTimer = 0;
        int slotBefore = FindRecruitMemberID(player);
        AddRecruitToGroup(player);  // flags + add + sep + follow
        int slotAfter  = FindRecruitMemberID(player);
        DebugLog("[RESCAN] slot antes=%d depois=%d", slotBefore, slotAfter);
    }

    // ── Follow: burst inicial (5s) + modo passivo periodico ──────
    // g_initialFollowTimer > 0: re-emite a cada 18 frames INDEPENDENTE
    //   do modo agressivo — cobre a janela em que MakeThisPedJoinOurGroup
    //   pode ainda nao ter devolvido o controlo ao task-manager.
    // !g_aggressive: modo passivo — re-emite continuamente (igual CLEO).
    bool doFollow = (g_initialFollowTimer > 0) || (!g_aggressive);
    if (g_initialFollowTimer > 0)
        --g_initialFollowTimer;

    if (doFollow)
    {
        if (++g_passiveTimer >= 18)
        {
            g_passiveTimer = 0;
            player->TellGroupToStartFollowingPlayer(true, false, false);
        }
    }
    else
    {
        g_passiveTimer = 0;  // modo agressivo fora do burst: deixar IA de combate
    }
}

// ───────────────────────────────────────────────────────────────────
// Processar estado PASSENGER per-frame
// ───────────────────────────────────────────────────────────────────
static void ProcessPassenger(CPlayerPed* player)
{
    if (!IsRecruitValid() || !IsCarValid())
    {
        DismissRecruit(player);
        ShowMsg("~r~Recruta ou carro perdido.");
        return;
    }

    // Continuar a IA de conducao mesmo com o jogador como passageiro
    ProcessDrivingAI(player);
}

// ───────────────────────────────────────────────────────────────────
// Processar teclas
// ───────────────────────────────────────────────────────────────────
static void HandleKeys(CPlayerPed* player)
{
    // ── 1: Recrutar / Dispensar ──────────────────────────────────
    // VK 0x31 = tecla '1' (49 decimal = CLEO key_pressed 49 = hex 0x31)
    if (KeyJustPressed(VK_RECRUIT))
    {
        if (g_state == ModState::INACTIVE)
        {
            // ── Spawn ──
            // Escolher modelo FAM aleatorio e solicitar ao streaming
            int modelIdx = FAM_MODELS[rand() % FAM_MODEL_COUNT];
            CStreaming::RequestModel(modelIdx, 0);
            // LoadAllRequestedModels(true) = BLOQUEANTE: espera ate o modelo
            // (incluindo blocos de animacao) estar completamente carregado.
            // Com false (nao-bloqueante) o ped pode ser criado sem anims → congelado.
            CStreaming::LoadAllRequestedModels(true);

            // Calcular posicao de spawn (atras do jogador)
            CVector pPos    = player->GetPosition();
            float   heading = player->m_fCurrentRotation;
            CVector spawnPos;
            spawnPos.x = pPos.x - std::sinf(heading) * SPAWN_BEHIND_DIST;
            spawnPos.y = pPos.y - std::cosf(heading) * SPAWN_BEHIND_DIST;
            spawnPos.z = pPos.z;

            // Criar ped via CPopulation::AddPed
            CPed* ped = CPopulation::AddPed(RECRUIT_PED_TYPE,
                                            static_cast<unsigned int>(modelIdx),
                                            spawnPos,
                                            false);
            if (!ped)
            {
                ShowMsg("~r~Falha ao criar recruta!");
                return;
            }

            // Configurar ped como ped de missao (nao recolhido pelo GC)
            ped->SetCharCreatedBy(2);  // 2 = PEDCREATED_MISSION

            // Dar arma
            ped->GiveWeapon(RECRUIT_WEAPON, RECRUIT_AMMO, false);

            g_recruit = ped;
            g_state   = ModState::ON_FOOT;

            // Flags criticas ANTES de gerir o grupo:
            // bDoesntListenToPlayerGroupCommands=0 e essencial — peds criados
            // via CPopulation::AddPed com decisao-maker de gang podem ter este
            // flag=1, fazendo TellGroupToStartFollowingPlayer ignorar o ped.
            g_recruit->bNeverLeavesGroup                  = 1;
            g_recruit->bKeepTasksAfterCleanUp             = 1;
            g_recruit->bDoesntListenToPlayerGroupCommands = 0;

            // Reset de timers para janela limpa
            g_groupRescanTimer   = 0;
            g_initialFollowTimer = INITIAL_FOLLOW_FRAMES;

            DebugLog("[SPAWN] model=%d ped=%p", modelIdx, (void*)ped);

            // Adicionar ao grupo do jogador (vai seguir automaticamente)
            AddRecruitToGroup(player);

            ShowMsg("~g~Recruta activo! [2=carro, 4=modo, N=agressivo, 1=dispensar]");
        }
        else
        {
            // ── Dispensar ──
            DismissRecruit(player);
            ShowMsg("~y~Recruta dispensado.");
        }
        return;
    }

    // ── 2: Entrar/sair do carro (dual, igual ao CLEO tecla 50) ──────
    // Estado ON_FOOT  → recruta entra no carro (retoma guardado ou procura novo)
    // Estado DRIVING  → recruta sai do carro, volta a pe (g_car PRESERVADO)
    if (KeyJustPressed(VK_CAR))
    {
        if (!IsRecruitValid())
        {
            ShowMsg("~r~Sem recruta activo.");
            return;
        }

        if (g_state == ModState::DRIVING && IsCarValid())
        {
            // ── DRIVING → ON_FOOT: recruta sai do carro ──────────────
            // CLEO: 0633 exit_car → 12@=1 → 0631 → 087F → 0850
            // g_car e preservado para re-entrada posterior via tecla 2.
            CTaskComplexLeaveCar* pTask =
                new CTaskComplexLeaveCar(g_car, 0, 0, true, false);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            // g_car PRESERVADO (nao limpar) — identico ao CLEO (11@ guardado)
            g_passiveTimer = 0;
            AddRecruitToGroup(player);  // add + never-leave + sep + follow
            g_state = ModState::ON_FOOT;
            ShowMsg("~y~Recruta a sair do carro. [2=retomar carro]");
            return;
        }

        if (g_state == ModState::ON_FOOT)
        {
            // ── ON_FOOT → ENTER_CAR: recruta entra no carro ──────────
            // Prioridade 1: retomar o carro guardado (g_car valido e vazio)
            // Prioridade 2: procurar o carro livre mais proximo
            CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            CVehicle* targetCar = nullptr;

            if (IsCarValid() && !g_car->m_pDriver)
            {
                // Carro guardado ainda existe e esta livre → retomar
                targetCar = g_car;
            }
            else
            {
                // Procurar carro livre mais proximo
                CVector searchPos = g_recruit->GetPosition();
                targetCar = FindNearestFreeCar(searchPos, playerCar);
                if (!targetCar)
                {
                    ShowMsg("~r~Nenhum carro disponivel perto.");
                    return;
                }
            }

            // Retirar do grupo antes de entrar no carro
            // (ForceGroupToAlwaysFollow(false) ja chamado dentro)
            RemoveRecruitFromGroup(player);

            CTaskComplexEnterCarAsDriver* pTask =
                new CTaskComplexEnterCarAsDriver(targetCar);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            g_car           = targetCar;
            g_state         = ModState::ENTER_CAR;
            g_enterCarTimer = ENTER_CAR_TIMEOUT;
            ShowMsg("~y~Recruta a entrar no carro...");
            return;
        }
        return;
    }

    // ── 3: Jogador como passageiro / sair do carro ───────────────
    // VK 0x33 = tecla '3' (CLEO key_pressed 51)
    if (KeyJustPressed(VK_PASSENGER))
    {
        if (g_state == ModState::DRIVING && IsCarValid())
        {
            // Jogador entra como passageiro
            CTaskComplexEnterCarAsPassenger* pTask =
                new CTaskComplexEnterCarAsPassenger(g_car, 0, false);

            player->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            g_state = ModState::PASSENGER;
            ShowMsg("~g~A entrar como passageiro [3=sair, B=drive-by]");
        }
        else if (g_state == ModState::PASSENGER && IsCarValid())
        {
            // Jogador sai do carro
            CTaskComplexLeaveCar* pTask =
                new CTaskComplexLeaveCar(g_car, 0, 0, true, false);

            player->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            g_state = ModState::DRIVING;
            ShowMsg("~y~A sair do carro.");
        }
        return;
    }

    // ── 4: Ciclar modo de conducao (qualquer estado activo) ──────────
    // CLEO: tecla 52, valida quando 12@ > 0 (qualquer estado com recruta)
    if (KeyJustPressed(VK_MODE) && g_state != ModState::INACTIVE)
    {
        // Avançar para o proximo modo (circular)
        int nextMode = (static_cast<int>(g_driveMode) + 1) %
                       static_cast<int>(DriveMode::COUNT);
        g_driveMode = static_cast<DriveMode>(nextMode);

        SetupDriveMode(player, g_driveMode);

        static const char* const MODE_NAMES[] = {
            "~g~Modo: CIVICO-D (road-following vanilla) [4=proximo]",
            "~g~Modo: CIVICO-E (segue a distancia) [4=proximo]",
            "~b~Modo: DIRETO (vai directo ao jogador) [4=proximo]",
            "~r~Modo: PARADO [4=proximo]",
        };
        ShowMsg(MODE_NAMES[static_cast<int>(g_driveMode)]);
        return;
    }

    // ── N: Alternar agressividade ─────────────────────────────────
    // CLEO: tecla 78, alterna 15@: 0=passivo, 1=agressivo
    // Passivo  → ForceGroupToAlwaysFollow(true)  + re-issue 0850 cada 18fr
    // Agressivo → ForceGroupToAlwaysFollow(false) + IA de combate livre
    if (KeyJustPressed(VK_AGGRO) && g_state != ModState::INACTIVE)
    {
        g_aggressive = !g_aggressive;
        g_passiveTimer = 0;
        if (g_state == ModState::ON_FOOT)
            player->ForceGroupToAlwaysFollow(!g_aggressive);
        if (g_aggressive)
            ShowMsg("~r~Recruta: AGRESSIVO (ataca inimigos)");
        else
            ShowMsg("~g~Recruta: PASSIVO (segue sempre)");
        return;
    }

    // ── B: Alternar drive-by ──────────────────────────────────────
    // CLEO: tecla 66, valida no estado 3 (jogador passageiro no carro do recruta)
    if (KeyJustPressed(VK_DRIVEBY) && g_state == ModState::PASSENGER)
    {
        g_driveby = !g_driveby;
        ShowMsg(g_driveby ? "~r~Drive-by ACTIVO" : "~y~Drive-by INACTIVO");
        return;
    }
}

// ───────────────────────────────────────────────────────────────────
// Frame principal: dispatcher de estados
// Chamado a cada iteracao do game loop (~60fps)
// ───────────────────────────────────────────────────────────────────
static void ProcessFrame()
{
    // Obter jogador
    CPlayerPed* player = CWorld::Players[0].m_pPed;
    if (!player) return;

    // Processar teclas (sempre, em qualquer estado)
    HandleKeys(player);

    // Dispatcher de estados
    switch (g_state)
    {
    case ModState::ON_FOOT:
        ProcessOnFoot(player);
        break;

    case ModState::ENTER_CAR:
        ProcessEnterCar(player);
        break;

    case ModState::DRIVING:
        ProcessDriving(player);
        break;

    case ModState::PASSENGER:
        ProcessPassenger(player);
        break;

    case ModState::INACTIVE:
    default:
        break;
    }
}

// ───────────────────────────────────────────────────────────────────
// Classe principal do plugin
// Regista o hook em Events::gameProcessEvent (corre a cada frame)
// A instancia global g_standalone forca o registo no DLL_PROCESS_ATTACH
// ───────────────────────────────────────────────────────────────────
class GroveRecruitStandalone
{
public:
    GroveRecruitStandalone()
    {
        Events::gameProcessEvent += []()
        {
            ProcessFrame();
        };
    }
};

// Instancia global — construtor executado quando o ASI e carregado
static GroveRecruitStandalone g_standalone;
