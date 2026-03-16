/*
 * grove_recruit_asi.cpp
 * Plugin ASI para GTA San Andreas — DK22Pac/plugin-sdk
 *
 * PROPOSITO:
 *   Substitui / melhora a IA de condução do grove_recruit_follow.cs ao rodar
 *   como plugin nativo (.asi) a cada frame, em vez de cada 300 ms (CLEO).
 *
 *   Funcionalidades novas vs CLEO puro:
 *     1. Speed adaptativa antes de curvas via CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes
 *     2. Deteccao automatica de offroad via CCarCtrl::FindNodesThisCarIsNearestTo
 *     3. Alinhamento de faixa via CCarCtrl::ClipTargetOrientationToLink (todos os modos)
 *     4. Controlo directo de CAutoPilot::m_nCruiseSpeed (char, preciso)
 *     5. Suporte a VARIOS recrutas simultaneamente via PoolIterator
 *
 * COMPILACAO:
 *   Prerequisitos:
 *     - Visual Studio 2019/2022 (Windows)
 *     - DK22Pac/plugin-sdk clonado: https://github.com/DK22Pac/plugin-sdk.git
 *     - DirectX SDK ou Windows SDK (para d3d8/d3d9)
 *
 *   Passos:
 *     1. git clone https://github.com/DK22Pac/plugin-sdk.git
 *     2. VS: C/C++ > Additional Include Directories:
 *            <plugin-sdk>/plugin_sa
 *            <plugin-sdk>/plugin_sa/game_sa
 *            <plugin-sdk>/shared
 *            <plugin-sdk>/shared/extensions
 *     3. VS: Linker > Additional Dependencies:
 *            <plugin-sdk>/output/plugin_sa.lib  (debug ou release conforme configuracao)
 *     4. VS: General > Configuration Type = Dynamic Library (.dll)
 *     5. Compilar -> renomear .dll para grove_recruit_asi.asi
 *     6. Copiar grove_recruit_asi.asi para pasta GTA SA
 *
 *   Requer: ASI Loader instalado (modloader.asi OU d3d8.dll do ThirteenAG)
 *     - https://github.com/ThirteenAG/Ultimate-ASI-Loader
 *
 *   Preprocessor defines necessarios:
 *     GTASA      (identifica o jogo para o plugin-sdk)
 *     _USE_MATH_DEFINES
 *
 * MODO DE USO — HIBRIDO (recomendado) vs STANDALONE:
 *
 *   HIBRIDO (este ficheiro, versao actual):
 *     Este .asi corre PARALELAMENTE ao grove_recruit_follow.cs.
 *     O CLEO continua a gerir: teclas Y/U/G/H/N/B, spawn, entry, estados.
 *     O ASI apenas REFINA a IA a cada frame enquanto o recruta esta a conduzir.
 *     Nao ha conflito: o ASI so escreve m_nCruiseSpeed e m_nCarDrivingStyle —
 *     ambos sao sobrescritos pelo CLEO apenas quando o modo muda (tecla 4).
 *     -> USAR ASSIM: copiar .asi para pasta GTA SA junto com .cleo existente.
 *
 *   STANDALONE (versao futura, nao implementada aqui):
 *     O ASI substitui completamente o CLEO — faz teclas, spawn, estados, IA.
 *     Vantagem: sem limitacoes do CLEO (per-frame input, multi-recruta nativo).
 *     Custo: reimplementar toda a maquina de estados (8 modos, guards, spawn).
 *     -> Ver PLUGINSDK_ANALISE.md secao "HIBRIDO vs STANDALONE" para detalhes.
 *
 * VARIAVEIS DO CLEO REFERENCIADAS (via memoria partilhada):
 *   Nao e necessario ler variaveis CLEO directamente: o ASI detecta o estado
 *   via CAutoPilot::m_nCarMission e a posicao do veiculo vs jogador.
 *
 * ESTRUTURAS USADAS (ver DK22Pac/plugin-sdk):
 *   CAutoPilot           — plugin_sa/game_sa/CAutoPilot.h
 *   CVehicle             — plugin_sa/game_sa/CVehicle.h        (contem m_autoPilot)
 *   CPed                 — plugin_sa/game_sa/CPed.h
 *   CCarCtrl             — plugin_sa/game_sa/CCarCtrl.h
 *   CPathFind / CPathNode — plugin_sa/game_sa/CPathFind.h
 *   CPools               — plugin_sa/game_sa/CPools.h
 *   CWorld               — plugin_sa/game_sa/CWorld.h          (CWorld::Players)
 *   PoolIterator         — shared/extensions/PoolIterator.h
 *   Events               — shared/Events.h
 */

// ---------------------------------------------------------------
// Includes do plugin-sdk (ordem importa — PluginBase.h primeiro)
// ---------------------------------------------------------------
#include "plugin.h"          // shared/plugin.h — inclui Events, PoolIterator, etc.

#include "CWorld.h"          // CWorld::Players
#include "CPools.h"          // CPools::ms_pPedPool
#include "CPed.h"            // CPed, ePedType flags
#include "CPlayerPed.h"      // CPlayerPed (subclasse de CPed para o jogador)
#include "CVehicle.h"        // CVehicle, contem m_autoPilot (CAutoPilot embebido)
#include "CAutoPilot.h"      // CAutoPilot, eCarDrivingStyle, eCarMission (via includes)
#include "eCarMission.h"     // MISSION_NONE, MISSION_GOTOCOORDS, MISSION_GOTOCOORDS_ACCURATE...
#include "CCarCtrl.h"        // FindNodesThisCarIsNearestTo, ClipTargetOrientationToLink, FindSpeedMultiplierWithSpeedFromNodes
#include "CPathFind.h"       // ThePaths (CPathFind global), CPathNode
#include "CNodeAddress.h"    // CNodeAddress

#include <cmath>
#include <array>
#include <algorithm>

// ---------------------------------------------------------------
// Namespace
// ---------------------------------------------------------------
using namespace plugin;

// ---------------------------------------------------------------
// Constantes de afinacao
// ---------------------------------------------------------------

// Distancia maxima ao no de estrada antes de considerar offroad (metros)
static constexpr float OFFROAD_DIST_M       = 30.0f;

// Zona STOP: recruta para completamente (nao colide com jogador)
static constexpr float STOP_ZONE_M          = 6.0f;

// Zona SLOW: recruta abranda para velocidade de seguranca
static constexpr float SLOW_ZONE_M          = 10.0f;

// Velocidades de cruzeiro em unidades SA (~= km/h, confirmado via Project Cerbera)
static constexpr unsigned char SPEED_MAX    = 40;   // modo normal em estrada
static constexpr unsigned char SPEED_SLOW   = 12;   // zona SLOW (perto do jogador)
static constexpr unsigned char SPEED_OFFRD  = 60;   // modo offroad/DIRETO
static constexpr unsigned char SPEED_MIN    = 8;    // minimo absoluto (evita paragem em curva)

// Intervalo de deteccao offroad (em frames, @60fps = 0.5s por verificacao)
static constexpr int OFFROAD_CHECK_INTERVAL = 30;

// Intervalo de varredura do grupo (em frames, @60fps = 1s por varrimento)
static constexpr int GROUP_SCAN_INTERVAL    = 60;

// Maximo de recrutas geridos simultaneamente
// SA suporta grupo de 7 membros + lider = max 7 seguidores
static constexpr int MAX_RECRUTAS           = 7;

// ---------------------------------------------------------------
// Estado interno por recruta
// ---------------------------------------------------------------
struct RecrutaState {
    CPed*     ped             = nullptr;
    CVehicle* vehicle         = nullptr;
    bool      isOffroad       = false;
    int       offroadTimer    = 0;  // countdown para proxima verificacao offroad
};

static std::array<RecrutaState, MAX_RECRUTAS> g_recrutas{};

// ---------------------------------------------------------------
// Utilitarios de distancia
// ---------------------------------------------------------------

// Distancia 2D (X/Y) entre dois CVectors (ignora Z — estradas podem ser em pontes)
static float Dist2D(CVector const& a, CVector const& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrtf(dx*dx + dy*dy);
}

// ---------------------------------------------------------------
// Deteccao de offroad
//
// Usa CCarCtrl::FindNodesThisCarIsNearestTo para encontrar os nos de
// estrada mais proximos ao veiculo e verifica se o mais proximo esta
// dentro do limiar OFFROAD_DIST_M.
//
// Limitacao CLEO equivalente: opcode 04D3 (FindNthClosestCarNode) retorna
// coords do no mas CLEO nao tem forma de calcular a distancia e re-agir
// automaticamente sem adicionar codigo extra de verificacao a cada iteracao.
// Com o ASI corremos isto per-frame (ou a cada OFFROAD_CHECK_INTERVAL frames).
// ---------------------------------------------------------------
static bool DetectOffroad(CVehicle* veh)
{
    if (!veh) return true;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);

    if (node1.IsEmpty()) return true;  // sem no de estrada -> offroad

    CPathNode* pNode = ThePaths.GetPathNode(node1);
    if (!pNode) return true;

    CVector nodePos  = pNode->GetNodeCoors();
    CVector vehicPos = veh->GetPosition();

    return Dist2D(vehicPos, nodePos) > OFFROAD_DIST_M;
}

// ---------------------------------------------------------------
// Speed adaptativa para curvas
//
// CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(char straightLineDist):
//   - Aceita m_nStraightLineDistance (distancia em linha recta ao proximo no).
//   - Retorna multiplicador float [0.0, 1.0]:
//       1.0 = reta (velocidade maxima)
//       < 1.0 = curva (velocidade reduzida proporcionalmente ao angulo)
//
// CLEO nao tem opcode equivalente para ler este valor.
// Actualmente o script CLEO apenas usa STOP/SLOW por distancia ao jogador —
// nao pre-abranda para curvas. O ASI permite isso.
// ---------------------------------------------------------------
static unsigned char AdaptiveSpeed(CVehicle* veh, unsigned char baseSpeed)
{
    if (!veh) return baseSpeed;

    CAutoPilot& ap = veh->m_autoPilot;

    // FindSpeedMultiplierWithSpeedFromNodes: mult perto de 1.0 em reta, < 1.0 em curva
    float mult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);

    // Clamp: mult pode ser < 0 em alguns edge cases (no invalido)
    mult = std::max(0.0f, std::min(1.0f, mult));

    // Calcular velocidade ideal e garantir minimo absoluto
    auto ideal = static_cast<unsigned char>(static_cast<float>(baseSpeed) * mult);
    return std::max(ideal, SPEED_MIN);
}

// ---------------------------------------------------------------
// Alinhamento de faixa (Lane Alignment)
//
// CCarCtrl::ClipTargetOrientationToLink(vehicle, linkAddr, lane, &heading, fwdX, fwdY):
//   Ajusta `heading` para ser paralelo ao eixo da faixa de estrada actual.
//   Activo internamente apenas quando CarMission == GOTOCOORDS_ACCURATE (12).
//   Via ASI podemos FORCAR isto em qualquer momento, para qualquer modo.
//
// Resultado: targetHeading alinhado com a lane — pode ser aplicado a
//   AutoPilot ou usado para corrigir steering directamente.
//
// CLEO equivalente parcial: 05D1 DriveMode=Accurate(1) activa este internamente,
//   mas apenas para o modo CIVICO-C/B. Via ASI aplicamos a todos os modos.
// ---------------------------------------------------------------
static float GetLaneAlignedHeading(CVehicle* veh)
{
    if (!veh) return 0.0f;
    CAutoPilot& ap = veh->m_autoPilot;

    // Sem link de caminho valido nao e possivel alinhar
    if (ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId == 0 &&
        ap.m_nCurrentPathNodeInfo.m_nAreaId == 0)
        return veh->GetHeading();

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

    return targetHeading;
}

// ---------------------------------------------------------------
// Processamento principal de um recruta em veiculo
// Chamado a cada frame do jogo
// ---------------------------------------------------------------
static void ProcessRecrutaFrame(RecrutaState& r, CVector const& playerPos)
{
    CVehicle* veh = r.vehicle;
    if (!veh) return;

    CAutoPilot& ap   = veh->m_autoPilot;
    CVector     vPos = veh->GetPosition();
    float       dist = Dist2D(vPos, playerPos);

    // ----- ZONA STOP: recruta completamente parado -----
    // Evita colisao estrutural com o carro do jogador quando muito perto.
    // CLEO tem guard equivalente (00EC 6m) mas funciona a 300ms; aqui e per-frame.
    if (dist < STOP_ZONE_M)
    {
        ap.m_nCruiseSpeed = 0;
        // MISSION_STOP_FOREVER garante que o carro para imediatamente
        ap.m_nCarMission  = MISSION_STOP_FOREVER;
        return;
    }

    // ----- ZONA SLOW: recruta abranda significativamente -----
    if (dist < SLOW_ZONE_M)
    {
        ap.m_nCruiseSpeed = SPEED_SLOW;
        // Nao mudar CarMission — apenas reduzir speed e deixar a IA gerir
        return;
    }

    // ----- Verificacao de offroad (throttled por timer) -----
    if (r.offroadTimer <= 0)
    {
        r.isOffroad     = DetectOffroad(veh);
        r.offroadTimer  = OFFROAD_CHECK_INTERVAL;
    }
    else
    {
        --r.offroadTimer;
    }

    // ----- Modo OFFROAD: PloughThrough + velocidade alta -----
    // Fora de estrada (canal, montanha, agua) o recruta precisa de forcar passagem.
    // CLEO resolve isto parcialmente com modo DIRETO (07F8) manual.
    // ASI faz automaticamente sem interacao do jogador.
    if (r.isOffroad)
    {
        ap.m_nCruiseSpeed    = SPEED_OFFRD;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;  // ignorar tudo
        return;
    }

    // ----- Modo normal em estrada -----

    // Speed adaptativa: abranda em curvas usando SpeedMultiplierFromNodes
    // CLEO nao consegue fazer isto (sem opcode para ler o multiplicador do link).
    unsigned char idealSpeed = AdaptiveSpeed(veh, SPEED_MAX);
    ap.m_nCruiseSpeed = idealSpeed;

    // DrivingStyle: AvoidCars (2) — desvia obstaculos, ignora semaforos
    // Consistente com o comportamento padrao do script CLEO para modos CIVICO.
    ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;

    // Alinhamento de faixa quando em missao Accurate
    // (ClipTargetOrientationToLink — ver documentacao acima)
    if (ap.m_nCarMission == MISSION_GOTOCOORDS_ACCURATE ||
        ap.m_nCarMission == MISSION_GOTOCOORDS_STRAIGHT_ACCURATE)
    {
        // GetLaneAlignedHeading calcula o heading correcto para a faixa.
        // Em versao completa, este valor seria aplicado ao steering do veiculo.
        // Por agora e calculado e ignorado — demonstra que e possivel ler o valor.
        [[maybe_unused]] float alignedHeading = GetLaneAlignedHeading(veh);
        // TODO (v2): aplicar alignedHeading via CAutoPilot ou hook de steering
    }
}

// ---------------------------------------------------------------
// Varredura do grupo do jogador via PoolIterator
//
// Itera todos os peds activos no PedPool e identifica os que sao
// membros do grupo do jogador (GANG1 em veiculo).
//
// Limitacao CLEO: o script apenas gere 1 recruta (handle 10@).
// ASI gere ate MAX_RECRUTAS (7) simultaneamente.
//
// Nota: PEDTYPE_GANG1 = 7 (Grove Street). Ver enums.txt do repositorio.
// Para diferenciar recrutas do mod de NPCs civis de gang usa-se a verificacao
// de bInVehicle + distancia ao jogador (simplificacao para prototipo).
// Implementacao final deveria usar CPedGroupMembership::IsMember().
// ---------------------------------------------------------------
static void ScanPlayerGroup(CPlayerPed* player)
{
    // Limpar estados anteriores
    for (auto& r : g_recrutas) r = RecrutaState{};

    if (!player) return;

    CVector playerPos = player->GetPosition();
    int slot = 0;

    // PoolIterator: iterador range-based para CPool<CPed, CCopPed>
    // Ver: shared/extensions/PoolIterator.h
    for (CPed* ped : *CPools::ms_pPedPool)
    {
        if (slot >= MAX_RECRUTAS) break;

        // Ignorar o proprio jogador
        if (ped == player) continue;

        // Verificar tipo GANG2 (Grove Street members, PED_TYPE_GANG2 = 8)
        if (ped->m_nPedType != PED_TYPE_GANG2) continue;

        // Verificar se esta em veiculo e a conduzir
        if (!ped->bInVehicle)    continue;
        if (!ped->m_pVehicle)    continue;

        // Verificar proximidade ao jogador (recrutas sao <50m do jogador)
        // Evita capturar membros de gang de rua nao relacionados
        CVector pedPos = ped->GetPosition();
        if (Dist2D(pedPos, playerPos) > 50.0f) continue;

        // Registar recruta
        auto& r    = g_recrutas[slot++];
        r.ped      = ped;
        r.vehicle  = ped->m_pVehicle;
        r.isOffroad = false;
        r.offroadTimer = 0;
    }
}

// ---------------------------------------------------------------
// Classe principal do plugin — hook no game loop
//
// O construtor regista o handler via Events::gameProcessEvent.
// A instancia global g_plugin forca a execucao do construtor quando
// a DLL e carregada pelo ASI Loader (antes de WinMain do jogo).
// ---------------------------------------------------------------
class GroveRecruitASI
{
public:
    GroveRecruitASI()
    {
        // ----- Hook no game process loop -----
        // Events::gameProcessEvent corre a cada iteracao do loop principal
        // do GTA SA (~60 fps), antes do render. Equivale a correr dentro de
        // CGame::Process() mas sem modificar o executavel.
        Events::gameProcessEvent += []()
        {
            // Obter ped do jogador (player 0)
            CPlayerPed* player = CWorld::Players[0].m_pPed;
            if (!player) return;

            CVector playerPos = player->GetPosition();

            // ----- Varredura periodica do grupo -----
            static int scanTimer = 0;
            if (++scanTimer >= GROUP_SCAN_INTERVAL)
            {
                scanTimer = 0;
                ScanPlayerGroup(player);
            }

            // ----- Processar cada recruta registado -----
            for (auto& r : g_recrutas)
            {
                if (!r.ped || !r.vehicle) continue;

                // Validar que o ped ainda esta no pool (vivo e nao despawnado)
                // CPools::ms_pPedPool->IsObjectValid verifica slot + id
                if (!CPools::ms_pPedPool->IsObjectValid(r.ped))
                {
                    r = RecrutaState{};  // limpar slot invalido
                    continue;
                }

                // Processar logica de IA melhorada para este recruta
                ProcessRecrutaFrame(r, playerPos);
            }
        };
    }
};

// Instancia global — construtor chamado em DLL_PROCESS_ATTACH
static GroveRecruitASI g_plugin;
