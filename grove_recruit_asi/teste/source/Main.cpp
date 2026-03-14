/*
 * grove_recruit_standalone.cpp  (com sistema de logging detalhado)
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
 *   CStats                          — plugin_sa/game_sa/CStats.h
 *   eStats                          — plugin_sa/game_sa/eStats.h  (STAT_RESPECT=68)
 *   Events, PoolIterator — shared/Events.h, shared/extensions/PoolIterator.h
 *
 * MECANICA DE RESPEITO (GTA SA vanilla)
 *   TellGroupToStartFollowingPlayer (0x60A1D0)
 *     └→ CTaskComplexGangFollower::CreateFirstSubTask (0x666160)
 *          └→ CPedIntelligence::Respects (0x601C90)
 *               └→ CStats::GetStatValue(STAT_RESPECT=68)
 *   Se respeito < threshold do Decision Maker do ped:
 *     - Ped diz voiceline GANG_RECRUIT_REFUSE
 *     - Fica em TASK_SIMPLE_STAND_STILL (203) — recruta congelado!
 *   CStats::FindMaxNumberOfGroupMembers() tambem lê STAT_RESPECT para
 *   limitar o tamanho do grupo (respeito=0 → max=0 slots).
 *   FIX (BOOST PERSISTENTE): ActivateRespectBoost() elevada durante TODA
 *   a sessao (spawn→dismiss) via CStats::SetStatValue(68, 1000.0f).
 *   Cobre: MakeThisPedJoinOurGroup + CreateFirstSubTask (deferred
 *   ao proxframe pelo task manager) + FindMaxNumberOfGroupMembers.
 *   DeactivateRespectBoost() em DismissRecruit restaura o valor original.
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
#include "CStats.h"
#include "eStats.h"
#include "eWeaponType.h"
#include "eTaskType.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <windows.h>    // GetAsyncKeyState

using namespace plugin;

// ───────────────────────────────────────────────────────────────────
// Sistema de Logging
//
// Grava grove_recruit.log na pasta do GTA SA (diretorio de trabalho).
// Modo "w": ficheiro e truncado em cada sessao (log sempre fresco).
// Buffer de linha (_IOLBF): flush automatico em cada '\n' — crash-safe
// equivalente a _IONBF mas com melhor desempenho em sistemas Windows.
//
// Niveis:
//   [EVENT] — acoes do jogador, transicoes de estado, spawn/dismiss
//   [GROUP] — operacoes de grupo: join, leave, rescan, flags
//   [TASK ] — tarefas de IA: follow, enter-car, leave-car
//   [DRIVE] — IA de conducao: missao, velocidade, offroad
//   [AI   ] — dump per-frame (throttled, a cada ~2s = 120 frames)
//   [KEY  ] — teclas pressionadas
//   [WARN ] — situacoes inesperadas recuperaveis
//   [ERROR] — falhas criticas (spawn falhou, recruta desapareceu)
//
// Formato: [FFFFFFF][NIVEL] mensagem
//   FFFFFFF = numero do frame (contador interno do plugin)
//
// Campos uteis nos dumps AI/GROUP para diagnostico rapido:
//   pedType    — tipo do ped: 8=PED_TYPE_GANG2=GSF (CORRECTO); 7=GANG1=Ballas=ERRADO
//                Se pedType!=8: MakeThisPedJoinOurGroup falha, recruta congela e e inimigo
//   activeTask — ID da tarefa activa (eTaskType.h):
//                  -1=sem tarefa | 200=TASK_NONE | 264=TASK_COMPLEX_BE_IN_GROUP
//                  1207=TASK_COMPLEX_GANG_FOLLOWER (a seguir OK)
//                  1500=TASK_GROUP_FOLLOW_LEADER_ANY_MEANS
//                  709=TASK_SIMPLE_CAR_DRIVE (a conduzir OK)
//   membros    — nr de membros no grupo excl. lider (deve ser 1 apos spawn correcto)
// ───────────────────────────────────────────────────────────────────
static FILE* g_logFile     = nullptr;
static int   g_logFrame    = 0;  // incrementado em cada ProcessFrame()
static int   g_logAiFrame  = 0;  // frame counter para dump AI throttled (reset a cada 120fr)

// Abre (ou recria) o ficheiro de log. Modo "w" trunca na sessao nova.
static void LogInit()
{
    if (fopen_s(&g_logFile, "grove_recruit.log", "w") != 0)
    {
        g_logFile = nullptr;
        return;
    }
    // Buffer de linha: flush automatico a cada '\n' (quase identico a
    // _IONBF em termos de crash-safety, mas com melhor desempenho)
    setvbuf(g_logFile, NULL, _IOLBF, 1024);
    fprintf(g_logFile,
        "===== grove_recruit_standalone.asi — log iniciado =====\n"
        "  Formato: [FFFFFFF][NIVEL] mensagem\n"
        "  Niveis: EVENT GROUP TASK DRIVE AI    KEY   WARN  ERROR\n"
        "\n"
        "  DIAGNOSTICO ON-FOOT (campos-chave para depurar congelamento):\n"
        "  activeTask: -1=sem tarefa | 200=TASK_NONE | 203=STAND_STILL(congelado!)\n"
        "              264=BE_IN_GROUP | 400=GANG_SPAWN_AI(spawn_init,antes_de_203)\n"
        "              1207=GANG_FOLLOWER(a seguir OK) | 1500=GROUP_FOLLOW_ANY_MEANS(OK)\n"
        "              709=CAR_DRIVE(a conduzir OK)\n"
        "  pedType:  8=GANG2=GSF(OK)  7=GANG1=Ballas(ERRADO->congelado e inimigo)\n"
        "  respeito: STAT_RESPECT=68 lido por CPedIntelligence::Respects (0x601C90)\n"
        "            e FindMaxNumberOfGroupMembers (0x559A50).\n"
        "            Se respect<threshold: voiceline REFUSE + activeTask=203.\n"
        "  BOOST PERSISTENTE: ActivateRespectBoost() activo durante TODA a sessao\n"
        "    (spawn->dismiss). Procurar 'RESPECT_BOOST: ACTIVADO/DESACTIVADO'.\n"
        "\n"
        "  NOVOS EVENTOS DE DIAGNOSTICO (adicionados para proxima iteracao):\n"
        "  PRE_JOIN: dump antes de MakeThisPedJoinOurGroup — ped_em_grupo=GI(slot=SI)\n"
        "            indica se o ped JA esta num grupo de gang (causa provavel do falho).\n"
        "            FindMaxGroupMembers=N confirma o limite de grupo com respect actual.\n"
        "  TASK_CHANGE: mudanca real-time da tarefa activa do recruta (cada frame).\n"
        "            Ex: TASK_CHANGE: 203 -> 1207 indica que o follow foi aceite.\n"
        "            Se nao aparece '-> 1207' apos TellGroup, o bypass e necessario.\n"
        "  POST_FOLLOW_CHECK: tarefa 3 frames apos TellGroupToStartFollowingPlayer.\n"
        "            Confirma se CreateFirstSubTask (deferred) criou 1207 ou ficou 203.\n"
        "  WRONG_DIR_START/END: transicoes de direcao errada na conducao (nao per-frame).\n"
        "            Reduz ruido; mostra exactamente quando o recruta inverte.\n"
        "  INVALID_LINK: linkId>50000 detectado (ex: 0xFFFFFE1E visto em log).\n"
        "            Causa WRONG_DIR persistente. Fix: fallback DIRETO temporario\n"
        "            + re-snap JoinRoadSystem quando link valido restaurado.\n"
        "  MISSION_RECOVERY: missao CIVICO sobrescrita para STOP_FOREVER(11).\n"
        "            Causa o bug 'so vai onde eu estava e para'. Fix: restaurar\n"
        "            directamente MISSION_43/34 com cooldown de 30fr.\n"
        "  SLOW_ZONE: missao CIVICO restaurada dentro da zona SLOW (dist<10m).\n"
        "            Garante retoma do road-follow ao sair da zona SLOW.\n"
        "\n"
        "  DIAGNOSTICO CONDUCAO:\n"
        "  heading:  rads do heading do veiculo (GetHeading()). targetH=\n"
        "            heading clipado ao eixo da faixa via ClipTargetOrientationToLink.\n"
        "            deltaH>1.5rad = WRONG_DIR (sentido contrario na estrada).\n"
        "            speedMult=factor de curva 0.0-1.0 (<0.6 = curva acentuada).\n"
        "  JOIN_ROAD: diff de linkId e heading antes/apos JoinCarWithRoadSystem.\n"
        "            Se linkId nao muda: JoinRoadSystem nao snap ao road-graph.\n"
        "========================================================\n\n");
}

// Funcao interna de escrita
static void LogWrite(const char* level, const char* fmt, va_list ap)
{
    if (!g_logFile) return;
    fprintf(g_logFile, "[%07d][%s] ", g_logFrame, level);
    vfprintf(g_logFile, fmt, ap);
    fputc('\n', g_logFile);
}

// Funcoes publica por nivel
static void LogEvent(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("EVENT", fmt, a); va_end(a); }
static void LogGroup(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("GROUP", fmt, a); va_end(a); }
static void LogTask (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("TASK ", fmt, a); va_end(a); }
static void LogDrive(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("DRIVE", fmt, a); va_end(a); }
static void LogAI   (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("AI   ", fmt, a); va_end(a); }
static void LogKey  (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("KEY  ", fmt, a); va_end(a); }
static void LogWarn (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("WARN ", fmt, a); va_end(a); }
static void LogError(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("ERROR", fmt, a); va_end(a); }

// ───────────────────────────────────────────────────────────────────
// Constantes de afinacao
// ───────────────────────────────────────────────────────────────────

// Modelos Grove Street (FAM1/FAM2/FAM3)
static constexpr int   FAM_MODELS[] = { 105, 106, 107 };
static constexpr int   FAM_MODEL_COUNT = 3;

// Tipo de ped para membros Grove Street
// PED_TYPE_GANG2 = 8 = Grove Street Families (SASCM pedtype 8)
// ATENCAO: PED_TYPE_GANG1 = 7 = Ballas/Vagos — NAO usar para GSF!
//   Com GANG1: MakeThisPedJoinOurGroup falha silenciosamente,
//   DM de grupo nunca e configurado, TellGroupToStartFollowingPlayer
//   e um no-op, recruta fica congelado E e reconhecido como inimigo.
static constexpr ePedType RECRUIT_PED_TYPE = PED_TYPE_GANG2;

// Arma padrao do recruta (AK47 = eWeaponType 30)
static constexpr eWeaponType RECRUIT_WEAPON = static_cast<eWeaponType>(30);
static constexpr int         RECRUIT_AMMO = 300;

// Posicao de spawn relativa ao jogador (metros atras e ligeiramente ao lado)
static constexpr float SPAWN_BEHIND_DIST = 2.5f;

// Distancias de zona (metros)
static constexpr float STOP_ZONE_M = 6.0f;   // para completamente
static constexpr float SLOW_ZONE_M = 10.0f;  // abranda
static constexpr float OFFROAD_DIST_M = 28.0f;  // distancia ao no de estrada → offroad

// Velocidades de cruzeiro (unidades SA ~= km/h)
static constexpr unsigned char SPEED_CIVICO = 38;   // CIVICO-D/E estrada normal
static constexpr unsigned char SPEED_SLOW = 12;   // zona SLOW
static constexpr unsigned char SPEED_DIRETO = 60;   // DIRETO / offroad
static constexpr unsigned char SPEED_MIN = 8;    // minimo absoluto (evita paragem em curva)

// Intervalos (frames @60fps)
static constexpr int OFFROAD_CHECK_INTERVAL  = 30;   // 0.5s
static constexpr int DIRETO_UPDATE_INTERVAL  = 60;   // 1.0s — actualizar destino DIRETO
static constexpr int ENTER_CAR_TIMEOUT       = 360;  // 6.0s — timeout entrada no carro
static constexpr int GROUP_RESCAN_INTERVAL   = 120;  // 2.0s — revalidar grupo
static constexpr int INITIAL_FOLLOW_FRAMES   = 300;  // 5.0s — burst inicial de follow

// Distancia maxima para procurar carro desocupado
static constexpr float FIND_CAR_RADIUS = 50.0f;

// Limiar de validacao de CCarPathLinkAddress.
// Links validos sao indices pequenos no road-graph (tipicamente 0-5000 por area).
// 0xFFFFFE1E (= 4294966814) foi visto em log real apos JoinCarWithRoadSystem
// quando o carro nao ficou ancorado a nenhum no valido.
// Qualquer valor acima deste limiar e tratado como invalido → fallback DIRETO.
static constexpr unsigned MAX_VALID_LINK_ID = 50000u;

// Teclado: VK codes  (espelham os codigos do CLEO: 49/50/51/52/78/66)
static constexpr int VK_RECRUIT = 0x31;  // 1  (CLEO key_pressed 49)
static constexpr int VK_CAR = 0x32;  // 2  (CLEO key_pressed 50)
static constexpr int VK_PASSENGER = 0x33;  // 3  (CLEO key_pressed 51)
static constexpr int VK_MODE = 0x34;  // 4  (CLEO key_pressed 52)
static constexpr int VK_AGGRO = 0x4E;  // N  (CLEO key_pressed 78)
static constexpr int VK_DRIVEBY = 0x42;  // B  (CLEO key_pressed 66)

// ───────────────────────────────────────────────────────────────────
// Enumeracoes de estado
// ───────────────────────────────────────────────────────────────────

enum class ModState : int
{
    INACTIVE = 0,   // sem recruta
    ON_FOOT = 1,   // recruta a seguir a pe (grupo vanilla)
    ENTER_CAR = 2,   // recruta a animar entrada no carro
    DRIVING = 3,   // recruta a conduzir, jogador de fora / no proprio carro
    PASSENGER = 4,   // jogador dentro do carro do recruta como passageiro
};

enum class DriveMode : int
{
    CIVICO_D = 0,   // MISSION_43 (EscortRearFaraway) — road-following vanilla ★
    CIVICO_E = 1,   // MISSION_34 (FollowCarFaraway) — segue a distancia
    DIRETO = 2,   // MISSION_GOTOCOORDS (8) — vai ao jogador, plough-through
    PARADO = 3,   // MISSION_STOP_FOREVER (11) — para
    COUNT = 4,
};

// Nomes legiveis para os enums (usados pelo logger)
static const char* StateName(ModState s)
{
    switch (s) {
        case ModState::INACTIVE:  return "INACTIVE";
        case ModState::ON_FOOT:   return "ON_FOOT";
        case ModState::ENTER_CAR: return "ENTER_CAR";
        case ModState::DRIVING:   return "DRIVING";
        case ModState::PASSENGER: return "PASSENGER";
        default:                  return "UNKNOWN";
    }
}
static const char* DriveModeName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_D: return "CIVICO_D(MISSION_43)";
        case DriveMode::CIVICO_E: return "CIVICO_E(MISSION_34)";
        case DriveMode::DIRETO:   return "DIRETO(MISSION_8)";
        case DriveMode::PARADO:   return "PARADO(MISSION_11)";
        default:                  return "UNKNOWN";
    }
}
// Auxiliar: true se o modo usa road-graph CIVICO (CIVICO_D ou CIVICO_E).
// Centraliza a verificacao para garantir consistencia.
static inline bool IsCivicoMode(DriveMode m)
{
    return m == DriveMode::CIVICO_D || m == DriveMode::CIVICO_E;
}

// ───────────────────────────────────────────────────────────────────
// Estado global do mod
// ───────────────────────────────────────────────────────────────────
static ModState   g_state = ModState::INACTIVE;
static DriveMode  g_driveMode = DriveMode::CIVICO_D;
static bool       g_aggressive = true;   // agressivo por defeito (CLEO: 15@=1)
static bool       g_driveby = false;

static CPed* g_recruit = nullptr;
static CVehicle* g_car = nullptr;

// Timers
static int        g_enterCarTimer = 0;
static int        g_offroadTimer = 0;
static int        g_diretoTimer = 0;
static int        g_groupRescanTimer = 0;
static int        g_passiveTimer = 0;       // CLEO: 0850 re-issue every ~18 frames (300ms)
static int        g_initialFollowTimer = 0; // burst inicial: re-emite follow independente do modo

// Flag offroad memorizada (throttled)
static bool       g_isOffroad = false;

// Rastreio em tempo real de mudancas de tarefa do recruta (a-pe)
// Inicializado com sentinela -999 para forcar log na primeira frame.
// Apenas mudancas sao registadas, sem ruido em frames sem alteracao.
static int        g_prevRecruitTaskId = -999;

// Timer de verificacao pos-TellGroup: 3 frames apos TellGroupToStartFollowingPlayer
// logamos a tarefa activa para confirmar se 1207 foi atribuida.
// Evitar inundar o log: so dispara quando g_postFollowTimer > 0.
static int        g_postFollowTimer = 0;

// Rastreio de estado WRONG_DIR na conducao: logamos apenas nas transicoes
// (inicio e fim), nao a cada frame, para reduzir ruido no log.
static bool       g_wasWrongDir = false;

// Rastreio de link invalido na conducao (logamos apenas na transicao).
static bool       g_wasInvalidLink = false;

// Timer de cooldown para recuperacao de MISSION_STOP_FOREVER inesperado
// em modos CIVICO. Evita re-emissao excessiva (backoff 30 frames = 0.5s).
static int        g_missionRecoveryTimer = 0;

// Boost persistente de respeito para teste
// -1.0f = inactivo; >= 0.0f = boost activo com valor original guardado.
// Mantido elevado durante TODA a sessao do recruta (spawn → dismiss) para
// garantir que CPedIntelligence::Respects e FindMaxNumberOfGroupMembers
// passam sempre — incluindo CreateFirstSubTask (deferred) e rescan frames.
static float      g_savedRespect = -1.0f;

// ───────────────────────────────────────────────────────────────────
// Funcoes internas GTA SA acessadas directamente por endereco
//
// FindMaxNumberOfGroupMembers (0x559A50):
//   Le STAT_RESPECT internamente e devolve o numero maximo de membros
//   permitidos no grupo do jogador (0=nenhum, ate 7=maximo).
//   Chamada aqui APENAS para diagnostico no log — sem efeito lateral.
//   Nota: e a mesma funcao que TellGroupToStartFollowingPlayer e
//   MakeThisPedJoinOurGroup chamam para validar a capacidade do grupo.
// ───────────────────────────────────────────────────────────────────
typedef int (__cdecl* FnFindMaxGroupMembers_t)();
static const FnFindMaxGroupMembers_t s_FindMaxGroupMembers =
    reinterpret_cast<FnFindMaxGroupMembers_t>(0x559A50);

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

    CVector nodePos = pNode->GetNodeCoors();
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
// Devolve o targetHeading (heading clipado ao eixo da faixa actual),
// ou o heading actual do veiculo se sem link valido.
// O valor e usado pelo dump AI throttled para diagnostico de direcao.
// (Aplicacao directa ao steering reservada para iteracao futura.)
// ───────────────────────────────────────────────────────────────────
static float ApplyLaneAlignment(CVehicle* veh)
{
    if (!veh) return 0.0f;
    CAutoPilot& ap = veh->m_autoPilot;

    float currentHeading = veh->GetHeading();

    // Sem link valido, nao ha faixa para alinhar
    if (ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId == 0 &&
        ap.m_nCurrentPathNodeInfo.m_nAreaId == 0)
        return currentHeading;

    CVector fwd = veh->GetForward();
    float targetHeading = currentHeading;

    CCarCtrl::ClipTargetOrientationToLink(
        veh,
        ap.m_nCurrentPathNodeInfo,
        ap.m_nCurrentLane,
        &targetHeading,
        fwd.x,
        fwd.y
    );
    // targetHeading = heading ideal para a faixa actual segundo o road-graph.
    // Devolvido para logging throttled em ProcessDrivingAI (dump AI a cada ~2s).
    return targetHeading;
}

// ───────────────────────────────────────────────────────────────────
// Procurar carro desocupado mais proximo (para o recruta entrar)
// Excluir: carro do jogador, carros com driver, aviao/helicoptero
// ───────────────────────────────────────────────────────────────────
static CVehicle* FindNearestFreeCar(CVector const& searchPos, CVehicle* excludePlayerCar)
{
    CVehicle* best = nullptr;
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
            best = veh;
        }
    }

    if (best)
        LogEvent("FindNearestFreeCar: encontrado veh=%p dist=%.1fm pos=(%.1f,%.1f,%.1f)",
            static_cast<void*>(best), bestDist,
            best->GetPosition().x, best->GetPosition().y, best->GetPosition().z);
    else
        LogWarn("FindNearestFreeCar: nenhum carro livre num raio de %.0fm", FIND_CAR_RADIUS);

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
// Boost PERSISTENTE de respeito para sessao de teste.
//
// POR QUE boost PERSISTENTE (nao temporario por call):
//   TellGroupToStartFollowingPlayer (0x60A1D0) cria CTaskComplexGangFollower
//   cujo CreateFirstSubTask (0x666160) e chamado NO FRAME SEGUINTE pelo
//   task manager — DEPOIS do boost temporario ja ter sido restaurado.
//   Alem disso, MakeThisPedJoinOurGroup pode triggerar CPedIntelligence::Respects
//   internamente, ANTES do nosso boost manual.
//   FindMaxNumberOfGroupMembers (0x559A50) le STAT_RESPECT em frames
//   periodicos para validar slots de grupo — se restaurado, ejecta o recruta.
//
//   SOLUCAO: boost activo durante TODA a sessao (spawn → dismiss).
//     ActivateRespectBoost()  — chamada UMA VEZ no spawn (KEY 1)
//     DeactivateRespectBoost() — chamada UMA VEZ no dismiss
//
// MECANICA GTA SA (confirmada via plugin-sdk):
//   CPedIntelligence::Respects (0x601C90) → CStats::GetStatValue(STAT_RESPECT=68)
//   Se respeito < threshold do DM: voiceline GANG_RECRUIT_REFUSE + TASK_STAND_STILL(203)
//   FindMaxNumberOfGroupMembers (0x559A50): respeito=0 → max=0 slots (ejecta recruta)
// ───────────────────────────────────────────────────────────────────
static constexpr float RESPECT_TEST_BOOST = 1000.0f;  // acima de qualquer threshold

// Activa o boost persistente: eleva STAT_RESPECT para RESPECT_TEST_BOOST e
// guarda o valor original em g_savedRespect para restauracao no dismiss.
static void ActivateRespectBoost()
{
    float current = CStats::GetStatValue(STAT_RESPECT);
    if (current < RESPECT_TEST_BOOST)
    {
        g_savedRespect = current;
        CStats::SetStatValue(STAT_RESPECT, RESPECT_TEST_BOOST);
        LogEvent("RESPECT_BOOST: ACTIVADO %.0f -> %.0f "
                 "(persistente: cobre MakeThisPedJoinOurGroup + CreateFirstSubTask deferred + rescan frames)",
            current, RESPECT_TEST_BOOST);
    }
    else
    {
        g_savedRespect = -1.0f;  // ja suficiente, nao precisa boost nem restore
        LogEvent("RESPECT_BOOST: desnecessario (respect=%.0f >= %.0f)", current, RESPECT_TEST_BOOST);
    }
}

// Restaura o STAT_RESPECT ao valor original guardado por ActivateRespectBoost.
// Chamada em DismissRecruit — garante que o save do jogador nao fica alterado.
static void DeactivateRespectBoost()
{
    if (g_savedRespect >= 0.0f)
    {
        CStats::SetStatValue(STAT_RESPECT, g_savedRespect);
        LogEvent("RESPECT_BOOST: DESACTIVADO -> %.0f restaurado", g_savedRespect);
        g_savedRespect = -1.0f;
    }
}

// Wrapper para TellGroupToStartFollowingPlayer com logging de respeito.
// O boost ja esta activo (via ActivateRespectBoost no spawn) — sem boost/restore aqui.
static void TellGroupFollowWithRespect(CPlayerPed* player, bool aggressive, bool verbose = true)
{
    if (!player) return;

    if (verbose)
    {
        float respect = CStats::GetStatValue(STAT_RESPECT);
        LogTask("TellGroupToStartFollowingPlayer: respect=%.0f boost_persistente=%s aggr=%d",
            respect,
            (g_savedRespect >= 0.0f) ? "SIM" : "NAO(ja_suficiente)",
            (int)aggressive);
    }

    player->TellGroupToStartFollowingPlayer(aggressive, false, false);

    // Armar verificacao diferida: 3 frames apos esta chamada, logamos a tarefa
    // activa do recruta para confirmar se CreateFirstSubTask (deferred) atribuiu
    // TASK_COMPLEX_GANG_FOLLOWER(1207) ou ficou em TASK_SIMPLE_STAND_STILL(203).
    // Nao re-armar se ja ha uma verificacao pendente (evita reset prematuro).
    if (g_postFollowTimer <= 0)
        g_postFollowTimer = 3;
}

// ───────────────────────────────────────────────────────────────────
// Adicionar recruta ao grupo do jogador e emitir follow — sequencia
// equivalente ao bloco CLEO (mas com abordagem diferente — ver nota 0850):
//   0850: AS_actor recruit follow_actor player  ← CLEO faz ANTES de 0631!
//   0631: add_member (MakeThisPedJoinOurGroup)
//   087F: never_leave_group = 1  (CPed::bNeverLeavesGroup)
//   0961: keep_tasks_after_cleanup = 1  (CPed::bKeepTasksAfterCleanUp)
//   06F0: set_group_separation 100m (CPedGroupMembership::m_fMaxSeparation)
//
// NOTA CRITICA — CLEO 0850 vs TellGroupToStartFollowingPlayer:
//   0850 parametros: P1=actor_handle(recruit), P2=actor_handle(player)
//   0850 NAO e TellGroupToStartFollowingPlayer (0x60A1D0) que toma (bool,bool,bool).
//   0850 cria CTaskComplexGangFollower DIRECTAMENTE no ped SEM check de respeito.
//   CLEO chama 0850 ANTES de 0631 (ped nao esta no grupo ainda), logo nao pode
//   ser TellGroupToStartFollowingPlayer (que so itera membros do grupo activo).
//   
//   No ASI usamos TellGroupToStartFollowingPlayer apos 0631, que internamente
//   cria CTaskComplexGangFollower via CreateFirstSubTask (0x666160), o qual
//   verifica CPedIntelligence::Respects (0x601C90) → CStats::GetStatValue(68).
//   O BYPASS e garantido pelo boost PERSISTENTE de STAT_RESPECT activo desde
//   o spawn (ActivateRespectBoost) ate o dismiss (DeactivateRespectBoost).
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

    // ── Passo 1: Flags ANTES de entrar no grupo ──────────────────
    // bNeverLeavesGroup:              impede o grupo de remover o ped por distancia
    // bKeepTasksAfterCleanUp:         impede o cleanup de apagar a tarefa de follow
    // bDoesntListenToPlayerGroupCommands=0: permite TellGroupToStartFollowingPlayer
    //   (se esta flag = 1, TellGroupToStartFollowingPlayer ignora o ped — congelado)
    //   Peds criados via CPopulation::AddPed com tipo PED_TYPE_GANG2 (=8, GSF)
    //   podem ter esta flag=1 por defeito, bloqueando todos os comandos de grupo.
    g_recruit->bNeverLeavesGroup                  = 1;
    g_recruit->bKeepTasksAfterCleanUp             = 1;
    g_recruit->bDoesntListenToPlayerGroupCommands = 0;

    // ── Passo 2: Adicionar ao grupo (0631 equivalente) ────────────
    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx < 8u)
    {
        int slotBefore = FindRecruitMemberID(player);
        if (slotBefore < 0)
        {
            // ── PRE_JOIN: diagnostico — o ped ja esta noutro grupo? ──────
            // MakeThisPedJoinOurGroup falha silenciosamente se o ped JA
            // pertence a um CPedGroup (ex.: grupo de gang GSF automatico).
            // Varredura de todos os 8 grupos para detectar essa situacao.
            {
                int existGi = -1, existSi = -1;
                for (int gi = 0; gi < 8 && existGi < 0; ++gi)
                    for (int si = 0; si < 7; ++si)
                        if (CPedGroups::ms_groups[gi].m_groupMembership.m_apMembers[si] == g_recruit)
                        { existGi = gi; existSi = si; break; }

                int maxMem = s_FindMaxGroupMembers();
                float resp  = CStats::GetStatValue(STAT_RESPECT);
                LogGroup("PRE_JOIN: ped_em_grupo=%d(slot=%d) FindMaxGroupMembers=%d respect=%.0f playerGrp=%u "
                         "(%s)",
                    existGi, existSi, maxMem, resp, groupIdx,
                    existGi >= 0 ? "ATENCAO: ped JA tem grupo — MakeThisPedJoinOurGroup FALHARA!" :
                                   "ped sem grupo (OK para MakeThisPedJoinOurGroup)");
            }

            // Tentativa primaria via API oficial do jogador
            player->MakeThisPedJoinOurGroup(g_recruit);
            int slotAfter = FindRecruitMemberID(player);
            if (slotAfter < 0)
            {
                // Backup direto: se MakeThisPedJoinOurGroup falhou silenciosamente
                // (ex.: pedType errado — GANG1=7 em vez de GANG2=8, ou DM incompativel),
                // AddFollower forca a insercao no slot livre do grupo.
                // NOTA: com pedType errado o DM de grupo NAO e configurado, logo
                // TellGroupToStartFollowingPlayer continuara a ser no-op mesmo apos AddFollower.
                CPedGroups::ms_groups[groupIdx].m_groupMembership.AddFollower(g_recruit);
                slotAfter = FindRecruitMemberID(player);
                if (slotAfter < 0)
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup E AddFollower falharam — recruta fora do grupo!");
                else
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup falhou (pedType=%d, GSF=8); AddFollower (backup) -> slot=%d. "
                            "PROVAVEL_CAUSA: ver PRE_JOIN acima (ped ja em grupo de gang?)",
                        (int)g_recruit->m_nPedType, slotAfter);
            }
            else
            {
                LogGroup("AddRecruitToGroup: MakeThisPedJoinOurGroup OK -> slot=%d pedType=%d",
                    slotAfter, (int)g_recruit->m_nPedType);
            }
        }
        else
        {
            LogGroup("AddRecruitToGroup: recruta ja no grupo slot=%d (sem re-join)", slotBefore);
        }

        // ── Passo 3: Distancia de separacao 100m (06F0 equivalente) ──
        // CLEO: 06F0: 24@ 100.0 → CPedGroupMembership::m_fMaxSeparation
        // ATENCAO: usar m_groupMembership.m_fMaxSeparation (offset +0x2C
        // em CPedGroup). NAO usar m_fSeparationRange (+0x30): esse campo
        // e interpretado internamente como ponteiro para CPedGroupIntelligence
        // por TellGroupToStartFollowingPlayer, e escrever 100.0f = 0x42C80000
        // ai causa violacao de acesso a 0x42C8021C (crash ao apertar 1).
        CPedGroups::ms_groups[groupIdx].m_groupMembership.m_fMaxSeparation = 100.0f;

        // Logar contagem de membros para confirmar insercao
        int memberCount = CPedGroups::ms_groups[groupIdx].m_groupMembership.CountMembersExcludingLeader();
        LogGroup("AddRecruitToGroup: grupo=%u membros=%d (excl. lider)", groupIdx, memberCount);
    }
    else
    {
        LogWarn("AddRecruitToGroup: groupIdx=%u invalido (>=8)!", groupIdx);
    }

    // Logar flags criticas apos operacoes de grupo
    LogGroup("AddRecruitToGroup: flags ped=%p bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d bInVeh=%d",
        static_cast<void*>(g_recruit),
        (int)g_recruit->bNeverLeavesGroup,
        (int)g_recruit->bKeepTasksAfterCleanUp,
        (int)g_recruit->bDoesntListenToPlayerGroupCommands,
        (int)g_recruit->bInVehicle);

    // ── Passo 4: Emitir tarefa de seguimento (equivalente a 0850) ──
    // TellGroupToStartFollowingPlayer → CreateFirstSubTask (0x666160)
    //   → CPedIntelligence::Respects (0x601C90) → CStats::GetStatValue(68).
    // O boost persistente (ActivateRespectBoost, activo desde o spawn) garante
    // que Respects retorna true → TASK_COMPLEX_GANG_FOLLOWER(1207) em vez de
    // TASK_SIMPLE_STAND_STILL(203) com voiceline GANG_RECRUIT_REFUSE.
    // Diferente do CLEO 0850 que cria CTaskComplexGangFollower directamente sem check.
    TellGroupFollowWithRespect(player, g_aggressive);
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
    LogGroup("RemoveRecruitFromGroup: ForceGroupToAlwaysFollow(false)");
    if (!g_recruit) return;
    int id = FindRecruitMemberID(player);
    if (id >= 0)
    {
        unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
        if (groupIdx < 8u)
        {
            CPedGroups::ms_groups[groupIdx].m_groupMembership.RemoveMember(id);
            LogGroup("RemoveRecruitFromGroup: slot=%d removido do grupo %u", id, groupIdx);
        }
    }
    else
    {
        LogGroup("RemoveRecruitFromGroup: recruta nao estava no grupo (slot=-1)");
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

    CVehicle* recruitCar = g_car;
    CAutoPilot& ap = recruitCar->m_autoPilot;

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
            LogDrive("SetupDriveMode: CIVICO_D sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission = MISSION_43;  // EscortRearFaraway (=67)
        ap.m_pTargetCar = playerCar;
        ap.m_nCruiseSpeed = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        // Snap ao road-graph — equivalente ao 06E1 do CLEO
        // Guardamos linkId/heading ANTES para comparar com APOS e confirmar
        // que JoinCarWithRoadSystem de facto snap ao road-graph.
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            float    playerH  = playerCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPost = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_D mission=43 speed=%d driveStyle=AVOID_CARS playerCar=%p "
                     "JOIN_ROAD: linkId %u->%u areaId %u->%u heading_pre=%.3f heading_post=%.3f lane=%d "
                     "playerHeading=%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost, (int)ap.m_nCurrentLane,
                playerH,
                (linkPre == linkPost ? "ATENCAO: linkId nao mudou apos JoinRoad!" : "JoinRoad OK snap"));
        }
        break;
    }

    // ── CIVICO-E: FollowCarFaraway ───────────────────────────────
    // Segue o carro do jogador a distancia, tambem via road-graph.
    case DriveMode::CIVICO_E:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_E sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission = MISSION_34;  // FollowCarFaraway (=52)
        ap.m_pTargetCar = playerCar;
        ap.m_nCruiseSpeed = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            float    playerH  = playerCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPost = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_E mission=34 speed=%d driveStyle=AVOID_CARS playerCar=%p "
                     "JOIN_ROAD: linkId %u->%u areaId %u->%u heading_pre=%.3f heading_post=%.3f lane=%d "
                     "playerHeading=%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost, (int)ap.m_nCurrentLane,
                playerH,
                (linkPre == linkPost ? "ATENCAO: linkId nao mudou apos JoinRoad!" : "JoinRoad OK snap"));
        }
        break;
    }

    // ── DIRETO: GotoCoords ───────────────────────────────────────
    // Vai directamente ate as coordenadas do jogador (sem road-graph).
    // Bom para offroad, montanhas, agua. PloughThrough para desvio maximo.
    case DriveMode::DIRETO:
    {
        CVector dest = player->GetPosition();
        ap.m_nCarMission = MISSION_GOTOCOORDS;  // =8
        ap.m_pTargetCar = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        // Nao e necessario JoinCarWithRoadSystem para DIRETO
        LogDrive("SetupDriveMode: DIRETO mission=8 speed=%d dest=(%.1f,%.1f,%.1f)",
            (int)ap.m_nCruiseSpeed, dest.x, dest.y, dest.z);
        break;
    }

    // ── PARADO: StopForever ──────────────────────────────────────
    case DriveMode::PARADO:
    {
        ap.m_nCarMission = MISSION_STOP_FOREVER;  // =11
        ap.m_pTargetCar = nullptr;
        ap.m_nCruiseSpeed = 0;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        LogDrive("SetupDriveMode: PARADO mission=11 speed=0");
        break;
    }

    default:
        LogWarn("SetupDriveMode: modo desconhecido %d", (int)mode);
        break;
    }
}

// ───────────────────────────────────────────────────────────────────
// Dispensar recruta: remover do grupo, limpar estado, fazer wander
// ───────────────────────────────────────────────────────────────────
static void DismissRecruit(CPlayerPed* player)
{
    LogEvent("DismissRecruit: estado_anterior=%s ped=%p carro=%p",
        StateName(g_state),
        static_cast<void*>(g_recruit),
        static_cast<void*>(g_car));

    // Restaurar respeito ANTES de qualquer outra operacao de grupo
    DeactivateRespectBoost();

    if (player && g_recruit)
    {
        RemoveRecruitFromGroup(player);  // ja chama ForceGroupToAlwaysFollow(false)
        // Tornar o ped em ped aleatorio novamente (pode ser recolhido pelo GC)
        if (IsRecruitValid())
            g_recruit->SetCharCreatedBy(1);  // 1 = PEDCREATED_RANDOM
    }

    g_recruit = nullptr;
    g_car = nullptr;
    g_state = ModState::INACTIVE;
    g_driveMode = DriveMode::CIVICO_D;
    g_aggressive = true;   // repor defeito agressivo (CLEO: 15@=1)
    g_driveby = false;
    g_isOffroad = false;
    g_passiveTimer       = 0;
    g_groupRescanTimer   = 0;
    g_initialFollowTimer = 0;
    g_prevRecruitTaskId  = -999;  // reset rastreio de tarefa
    g_postFollowTimer    = 0;
    g_wasWrongDir        = false;
    g_wasInvalidLink     = false;
    g_missionRecoveryTimer = 0;
    LogEvent("DismissRecruit: estado resetado para INACTIVE");
}

// ───────────────────────────────────────────────────────────────────
// Processar IA por frame quando recruta esta a conduzir
// Implementa: zonas STOP/SLOW, offroad auto, speed adaptativa
// ───────────────────────────────────────────────────────────────────
static void ProcessDrivingAI(CPlayerPed* player)
{
    if (!IsCarValid()) return;

    CVehicle* veh = g_car;
    CAutoPilot& ap = veh->m_autoPilot;
    CVector     vPos = veh->GetPosition();
    CVector     playerPos = player->GetPosition();
    float       dist = Dist2D(vPos, playerPos);

    // ── ZONA STOP: recruta completamente parado ──────────────────
    // Evita colisao fisica quando o carro do recruta esta muito perto
    // do jogador. Per-frame (300ms no CLEO → aqui cada frame ~16ms).
    if (dist < STOP_ZONE_M)
    {
        ap.m_nCruiseSpeed = 0;
        ap.m_nCarMission = MISSION_STOP_FOREVER;
        return;
    }

    // ── ZONA SLOW: recruta abranda ───────────────────────────────
    // IMPORTANTE: a STOP zone (acima) define mission=STOP_FOREVER e retorna.
    // Quando o jogador se afasta da STOP zone, o carro entra na SLOW zone.
    // A SLOW zone SO alterava a velocidade — nao restaurava a missao CIVICO.
    // Resultado: carro ficava em mission=11 (STOP_FOREVER) mesmo na SLOW zone
    // e continuava parado para sempre ao sair da SLOW zone.
    // FIX: restaurar a missao CIVICO aqui para que o road-follow retome
    // assim que o carro sair da SLOW zone (dist > SLOW_ZONE_M).
    if (dist < SLOW_ZONE_M)
    {
        ap.m_nCruiseSpeed = SPEED_SLOW;
        // Restaurar missao correcta nos modos CIVICO se foi sobrescrita
        if (IsCivicoMode(g_driveMode))
        {
            eCarMission expectedM = (g_driveMode == DriveMode::CIVICO_D) ? MISSION_43 : MISSION_34;
            if (ap.m_nCarMission != expectedM)
            {
                CVehicle* pCar = player->bInVehicle ? player->m_pVehicle : nullptr;
                ap.m_nCarMission = expectedM;
                if (pCar) ap.m_pTargetCar = pCar;
                LogDrive("SLOW_ZONE: missao restaurada STOP_FOREVER->%s (road-follow pronto a retomar)",
                    (expectedM == MISSION_43) ? "MISSION_43" : "MISSION_34");
            }
        }
        return;
    }

    // ── Verificacao de offroad (throttled) ───────────────────────
    if (g_offroadTimer <= 0)
    {
        bool wasOffroad = g_isOffroad;
        g_isOffroad = DetectOffroad(veh);
        g_offroadTimer = OFFROAD_CHECK_INTERVAL;
        // Logar apenas quando o estado de offroad muda
        if (g_isOffroad != wasOffroad)
            LogDrive("Offroad: %s -> %s (modo=%s)",
                wasOffroad ? "SIM" : "NAO",
                g_isOffroad ? "SIM" : "NAO",
                DriveModeName(g_driveMode));
    }
    else
    {
        --g_offroadTimer;
    }

    // ── Se offroad E modo CIVICO: comutar para DIRETO temporario ─
    // O road-graph nao tem nos offroad; o recruta ficaria parado.
    // DIRETO + PloughThrough permite navegar qualquer terreno.
    if (g_isOffroad && IsCivicoMode(g_driveMode))
    {
        ap.m_nCruiseSpeed = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        CVector dest = playerPos;
        ap.m_nCarMission = MISSION_GOTOCOORDS;
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
        ap.m_nCruiseSpeed = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        return;
    }

    // ── Modo PARADO: nao ha nada a fazer per-frame ───────────────
    if (g_driveMode == DriveMode::PARADO)
        return;

    // ── Modos CIVICO em estrada: speed adaptativa + alinhamento ──
    // ── Guard: link ID invalido ──────────────────────────────────────────────
    // JoinCarWithRoadSystem pode produzir linkId=0xFFFFFE1E (visto em log real).
    // Com link invalido, ClipTargetOrientationToLink devolve lixo → WRONG_DIR
    // persistente → carro vai na direcao errada indefinidamente.
    // Detectar e fallback temporario para DIRETO ate o link estabilizar.
    {
        unsigned linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        if (linkId > MAX_VALID_LINK_ID)  // links validos sao indices pequenos (< MAX_VALID_LINK_ID)
        {
            if (!g_wasInvalidLink)
            {
                LogDrive("INVALID_LINK: linkId=%u (invalido! MAX_VALID_LINK_ID=%u) "
                         "— fallback DIRETO temporario para evitar WRONG_DIR",
                    linkId, MAX_VALID_LINK_ID);
                g_wasInvalidLink = true;
            }
            ap.m_nCruiseSpeed = SPEED_DIRETO;
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
            ap.m_nCarMission = MISSION_GOTOCOORDS;
            ap.m_vecDestinationCoors = playerPos;
            return;
        }
        if (g_wasInvalidLink)
        {
            LogDrive("INVALID_LINK: linkId=%u — link valido restaurado, retomando CIVICO e re-snap road-graph",
                linkId);
            g_wasInvalidLink = false;
            // Re-snap ao road-graph com heading actual apos link invalido
            CCarCtrl::JoinCarWithRoadSystem(veh);
        }
    }

    // ── Recuperacao de MISSION_STOP_FOREVER inesperado ──────────────────────
    // Alem da STOP zone (que ja restaura na SLOW zone acima), o proprio motor
    // do jogo pode sobrescrever m_nCarMission para 11 (STOP_FOREVER) quando:
    //   - o road-graph nao encontra rota valida para o target car
    //   - o target car (m_pTargetCar) se torna nullptr (jogador mudou de carro)
    //   - EscortRearFaraway/FollowCarFaraway "chegou" a posicao alvo e pousou
    // Sem recuperacao aqui, o recruta para definitivamente neste frame e nao
    // retoma mesmo que o jogador se afaste — o bug "so vai onde eu estava".
    if (g_missionRecoveryTimer > 0) --g_missionRecoveryTimer;
    {
        eCarMission expectedMission = (g_driveMode == DriveMode::CIVICO_D) ? MISSION_43 : MISSION_34;
        if (ap.m_nCarMission != expectedMission && g_missionRecoveryTimer <= 0)
        {
            CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            LogDrive("MISSION_RECOVERY: missao_atual=%d esperada=%d (modo=%s) "
                     "targetCar=%s — restaurar directamente (sem JoinRoadSystem)",
                (int)ap.m_nCarMission, (int)expectedMission,
                DriveModeName(g_driveMode),
                playerCar ? "valido" : "nullptr(jogador a pe)");
            ap.m_nCarMission = expectedMission;
            if (playerCar) ap.m_pTargetCar = playerCar;
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
            g_missionRecoveryTimer = 30;  // backoff 0.5s — evitar re-emit spam
        }
    }

    // Speed adaptativa: multiplica SPEED_CIVICO pelo factor de curva
    // (1.0 em reta, < 1.0 em curva) para um comportamento suave.
    unsigned char idealSpeed = AdaptiveSpeed(veh, SPEED_CIVICO);
    ap.m_nCruiseSpeed = idealSpeed;

    // Alinhamento de faixa: calcula targetHeading (clipado ao road-graph).
    // Devolvido para logging throttled abaixo.
    float targetHeading = ApplyLaneAlignment(veh);

    // Re-sincronizar target car se jogador mudou de carro
    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
    if (playerCar && ap.m_pTargetCar != playerCar)
    {
        ap.m_pTargetCar = playerCar;
        // Re-snap ao road-graph com o novo target
        CCarCtrl::JoinCarWithRoadSystem(veh);
        LogDrive("ProcessDrivingAI: target_car atualizado para %p e JoinRoadSystem re-emitido",
            static_cast<void*>(playerCar));
    }

    // ── Deteccao de WRONG_DIR por transicao (nao throttled) ──────
    // Logamos APENAS quando o estado de direcao errada muda, para
    // ver exactamente em que frame o problema começa ou termina.
    // (O dump throttled abaixo continua a mostrar os valores periodicamente.)
    {
        float vH  = veh->GetHeading();
        float tH  = targetHeading;
        float dH  = tH - vH;
        while (dH >  3.14159f) dH -= 6.28318f;
        while (dH < -3.14159f) dH += 6.28318f;
        float absDH = dH < 0.0f ? -dH : dH;
        bool isWrong = (absDH > 1.5f);
        if (isWrong != g_wasWrongDir)
        {
            if (isWrong)
                LogDrive("WRONG_DIR_START: heading=%.3f targetH=%.3f deltaH=%.3f "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d straightLine=%d",
                    vH, tH, dH,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane,
                    (int)ap.m_nStraightLineDistance);
            else
                LogDrive("WRONG_DIR_END:   heading=%.3f targetH=%.3f deltaH=%.3f "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d",
                    vH, tH, dH,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane);
            g_wasWrongDir = isWrong;
        }
    }

    // ── Dump AI throttled a cada ~2s (120 frames) ─────────────
    if (++g_logAiFrame >= 120)
    {
        g_logAiFrame = 0;
        // Tarefa activa do condutor: TASK_SIMPLE_CAR_DRIVE=709 = conduzindo normalmente
        CTask* pActiveTask = g_recruit->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int taskId = pActiveTask ? (int)pActiveTask->GetId() : -1;
        // Heading e dados de caminho para depuracao da direcao do recruta:
        //   heading       = orientacao actual do veiculo (radianos, 0=Norte)
        //   targetH       = heading clipado ao eixo da faixa (road-graph)
        //   deltaH        = diff heading-targetH (>1.5rad = WRONG_DIR: sentido contrario!)
        //   speedMult     = factor de curva 0.0-1.0 (AdaptiveSpeed, <0.6 = curva acentuada)
        //   straightLine  = distancia linha recta ao proximo no (CAutoPilot)
        //   lane          = indice faixa actual (0=direita, 1=esquerda, etc.)
        //   linkId/areaId = troco de estrada actual no road-graph
        float vehHeading = veh->GetHeading();
        float deltaH = targetHeading - vehHeading;
        // Normalizar para [-pi, pi]
        while (deltaH >  3.14159f) deltaH -= 6.28318f;
        while (deltaH < -3.14159f) deltaH += 6.28318f;
        float absDeltaH = deltaH < 0.0f ? -deltaH : deltaH;
        float speedMult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);
        LogAI("DRIVING: dist=%.1fm speed_ap=%d mission=%d driveStyle=%d offroad=%d modo=%s "
              "heading=%.3f targetH=%.3f deltaH=%.3f(%s) speedMult=%.2f straightLine=%d "
              "lane=%d linkId=%u areaId=%u activeTask=%d car=%p",
            dist, (int)ap.m_nCruiseSpeed, (int)ap.m_nCarMission,
            (int)ap.m_nCarDrivingStyle, (int)g_isOffroad,
            DriveModeName(g_driveMode),
            vehHeading, targetHeading, deltaH,
            (absDeltaH > 1.5f) ? "WRONG_DIR!" : (absDeltaH > 0.3f) ? "desalinhado" : "OK",
            speedMult,
            (int)ap.m_nStraightLineDistance,
            (int)ap.m_nCurrentLane,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
            taskId,
            static_cast<void*>(veh));
    }
}

// ───────────────────────────────────────────────────────────────────
// Processar estado ENTER_CAR: aguardar animacao e transitar
// ───────────────────────────────────────────────────────────────────
static void ProcessEnterCar(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        LogError("ProcessEnterCar: recruta invalido/morto durante ENTER_CAR — resetar para INACTIVE");
        g_recruit = nullptr; g_car = nullptr;
        g_state = ModState::INACTIVE;
        return;
    }

    // Recruta ja esta no carro → transitar para DRIVING
    if (g_recruit->bInVehicle && g_recruit->m_pVehicle)
    {
        g_car = g_recruit->m_pVehicle;
        g_state = ModState::DRIVING;
        LogEvent("ProcessEnterCar: recruta entrou no carro %p -> estado DRIVING, modo=%s",
            static_cast<void*>(g_car), DriveModeName(g_driveMode));
        SetupDriveMode(player, g_driveMode);
        ShowMsg("~g~RECRUTA A CONDUZIR [4=modo, 3=passageiro, 2=sair]");
        return;
    }

    // Timeout de entrada (recruta pode ter ficado preso)
    if (--g_enterCarTimer <= 0)
    {
        LogWarn("ProcessEnterCar: TIMEOUT apos %d frames — recruta nao conseguiu entrar. Voltando a ON_FOOT.",
            ENTER_CAR_TIMEOUT);
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
        LogError("ProcessDriving: recruta invalido/morto — dismiss");
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
        {
            LogEvent("ProcessDriving: recruta saiu do carro e carro foi destruido/removido — g_car=null");
            g_car = nullptr;
        }
        else
        {
            LogEvent("ProcessDriving: recruta saiu do carro %p (preservado para re-entrada)", static_cast<void*>(g_car));
        }
        g_passiveTimer = 0;
        AddRecruitToGroup(player);  // add + never-leave + sep + follow
        g_state = ModState::ON_FOOT;
        ShowMsg("~y~Recruta saiu do carro — a seguir a pe. [2=retomar]");
        return;
    }

    // Actualizar referencia ao carro (pode ter mudado de veiculo)
    if (g_recruit->m_pVehicle && g_recruit->m_pVehicle != g_car)
    {
        LogEvent("ProcessDriving: recruta mudou de carro %p -> %p",
            static_cast<void*>(g_car), static_cast<void*>(g_recruit->m_pVehicle));
        g_car = g_recruit->m_pVehicle;
    }

    ProcessDrivingAI(player);
}

// ───────────────────────────────────────────────────────────────────
// Processar estado ON_FOOT per-frame
//
// 1. Revalidacao do grupo (GROUP_RESCAN_INTERVAL frames):
//    - Detecta dispand nativo (jogador usou sistema vanilla para dispensar)
//    - Re-emite sequencia completa: 0631 + 087F + 06F0 + 0850
//
// 2. Burst inicial (g_initialFollowTimer > 0):
//    Re-emite TellGroupFollowWithRespect a cada 18 frames nos primeiros
//    300 frames (5s) apos o spawn. Garante que o ped começa a seguir mesmo
//    que algo interrupta o primeiro follow. Identico ao comportamento do CLEO
//    nos primeiros loops apos o spawn.
//
// 3. Modo PASSIVO (g_aggressive==false):
//    Re-emite TellGroupFollowWithRespect a cada ~18 frames (300ms).
//    Impede o recruta de sustentar tarefas de combate — identico ao CLEO:
//    "if (15@==0 && 12@==1): 0850: follow_actor [em cada loop de 300ms]"
// ───────────────────────────────────────────────────────────────────
static void ProcessOnFoot(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        LogError("ProcessOnFoot: recruta invalido/morto — dismiss");
        DismissRecruit(player);
        ShowMsg("~r~Recruta perdido.");
        return;
    }

    // ── Rastreio em tempo real de mudancas de tarefa ──────────────
    // Logamos APENAS nas transicoes (old→new), sem ruido por frame.
    // TASK_CHANGE e o evento mais importante para depurar:
    //   203 -> 1207  = follow foi aceite (OK)
    //   1207 -> 203  = follow foi cancelado (problema!)
    //   203 -> 264   = BE_IN_GROUP mas nao follow (problema parcial)
    //   cualquer -> X = mudanca inesperada (investigar)
    {
        CTask* pt  = g_recruit->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int    tid = pt ? (int)pt->GetId() : -1;
        if (g_prevRecruitTaskId != -999 && tid != g_prevRecruitTaskId)
            LogTask("TASK_CHANGE: %d -> %d  (%s -> %s)",
                g_prevRecruitTaskId, tid,
                (g_prevRecruitTaskId == 203  ? "STAND_STILL" :
                 g_prevRecruitTaskId == 400  ? "GANG_SPAWN_AI" :
                 g_prevRecruitTaskId == 1207 ? "GANG_FOLLOWER" :
                 g_prevRecruitTaskId == 264  ? "BE_IN_GROUP" :
                 g_prevRecruitTaskId == 200  ? "NONE" : "?"),
                (tid == 203  ? "STAND_STILL" :
                 tid == 400  ? "GANG_SPAWN_AI" :
                 tid == 1207 ? "GANG_FOLLOWER" :
                 tid == 264  ? "BE_IN_GROUP" :
                 tid == 200  ? "NONE" :
                 tid == 709  ? "CAR_DRIVE" : "?"));
        g_prevRecruitTaskId = tid;

        // ── Verificacao pos-TellGroup (3 frames diferida) ────────
        // Confirma se CreateFirstSubTask (deferred) atribuiu 1207.
        // Se ainda for 203: TellGroupToStartFollowingPlayer e no-op
        // (DM nao configurado ou Respects retornou false).
        if (g_postFollowTimer > 0)
        {
            --g_postFollowTimer;
            if (g_postFollowTimer == 0)
                LogTask("POST_FOLLOW_CHECK(3fr): activeTask=%d (%s) — %s",
                    tid,
                    (tid == 1207 ? "GANG_FOLLOWER OK!" :
                     tid == 203  ? "STAND_STILL=congelado! TellGroup foi no-op" :
                     tid == 264  ? "BE_IN_GROUP(sem follow)" : "outro"),
                    (tid == 1207 ? "follow bem sucedido" :
                     "PROBLEMA: recruta nao seguiu — verificar PRE_JOIN no log"));
        }
    }

    // ── Revalidacao periodica do grupo (a cada 2s ~ 120 frames) ──
    if (++g_groupRescanTimer >= GROUP_RESCAN_INTERVAL)
    {
        g_groupRescanTimer = 0;

        int slot = FindRecruitMemberID(player);
        // Detectar dispand nativo: recruta foi retirado do grupo pelo sistema vanilla
        // (jogador apontou arma + botao de dispand) → tratar como Dispensar
        if (slot < 0)
        {
            LogEvent("ProcessOnFoot: RESCAN — recruta nao esta no grupo (dismiss nativo detectado)");
            DismissRecruit(player);
            ShowMsg("~y~Recruta dispensado do grupo.");
            return;
        }

        // Dump do estado no momento do rescan (logar flags para debug)
        CVector rPos = g_recruit->GetPosition();
        CVector pPos = player->GetPosition();
        float dist = Dist2D(rPos, pPos);
        CTask* pRescanTask = g_recruit->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int rescanTaskId = pRescanTask ? (int)pRescanTask->GetId() : -1;
        LogGroup("ProcessOnFoot: RESCAN slot=%d dist=%.1fm pos=(%.1f,%.1f,%.1f) bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d bInVeh=%d aggr=%d initTimer=%d activeTask=%d pedType=%d respect=%.0f",
            slot, dist, rPos.x, rPos.y, rPos.z,
            (int)g_recruit->bNeverLeavesGroup,
            (int)g_recruit->bKeepTasksAfterCleanUp,
            (int)g_recruit->bDoesntListenToPlayerGroupCommands,
            (int)g_recruit->bInVehicle,
            (int)g_aggressive,
            g_initialFollowTimer,
            rescanTaskId,
            (int)g_recruit->m_nPedType,
            CStats::GetStatValue(STAT_RESPECT));

        // Re-emitir sequencia completa (mantem grupo estavel)
        AddRecruitToGroup(player);  // add + never-leave + keep-tasks + sep + follow
    }

    // ── Burst inicial + Modo PASSIVO: re-emitir follow a cada 18 frames ──
    // g_initialFollowTimer > 0: burst 5s apos spawn (garante arranque do follow)
    // !g_aggressive: modo passivo permanente (identico ao CLEO "15@==0: 0850")
    // Assumindo 60fps: 18fr ≈ 300ms (igual ao loop de 300ms do CLEO).
    bool doFollow = (g_initialFollowTimer > 0) || (!g_aggressive);
    if (g_initialFollowTimer > 0)
        --g_initialFollowTimer;

    if (doFollow)
    {
        if (++g_passiveTimer >= 18)
        {
            g_passiveTimer = 0;
            // verbose=true apenas no burst inicial; no modo passivo (g_aggressive=false)
            // o log seria emitido a cada 300ms — suprimir para nao inundar o ficheiro.
            bool burstActive = (g_initialFollowTimer > 0);
            TellGroupFollowWithRespect(player, g_aggressive, burstActive);
            if (burstActive)
                LogTask("TellGroupFollowWithRespect (burst inicial) initTimer=%d", g_initialFollowTimer);
        }
    }
    else
    {
        g_passiveTimer = 0;  // modo agressivo sem burst: deixar IA de combate funcionar
    }

    // ── Dump AI throttled a cada ~2s (120 frames) no modo a-pe ──
    if (++g_logAiFrame >= 120)
    {
        g_logAiFrame = 0;
        CVector rPos = g_recruit->GetPosition();
        CVector pPos = player->GetPosition();
        // Tarefa activa do recruta: TASK_COMPLEX_GANG_FOLLOWER=1207 significa a seguir;
        // TASK_NONE=200 ou -1 significa congelado/sem tarefa de grupo.
        CTask* pActiveTask = g_recruit->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int taskId = pActiveTask ? (int)pActiveTask->GetId() : -1;
        LogAI("ON_FOOT: dist=%.1fm rPos=(%.1f,%.1f,%.1f) initTimer=%d passiveTimer=%d rescanTimer=%d aggr=%d doFollow=%d activeTask=%d pedType=%d respect=%.0f",
            Dist2D(rPos, pPos), rPos.x, rPos.y, rPos.z,
            g_initialFollowTimer, g_passiveTimer, g_groupRescanTimer,
            (int)g_aggressive, (int)doFollow, taskId, (int)g_recruit->m_nPedType,
            CStats::GetStatValue(STAT_RESPECT));
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
            LogEvent("KEY 1 (RECRUIT): spawn iniciado modelo=%d pos=(%.1f,%.1f,%.1f) aggr_padrao=%d respect_atual=%.0f",
                modelIdx,
                player->GetPosition().x, player->GetPosition().y, player->GetPosition().z,
                (int)g_aggressive,
                CStats::GetStatValue(STAT_RESPECT));
            CStreaming::RequestModel(modelIdx, 0);
            // LoadAllRequestedModels(true) = BLOQUEANTE: espera ate o modelo
            // estar completamente carregado antes de criar o ped.
            // Com false (nao bloqueante) o modelo pode nao estar pronto quando
            // AddPed e chamado — o ped fica sem malha/animacoes, congelado.
            CStreaming::LoadAllRequestedModels(true);

            // Calcular posicao de spawn (atras do jogador)
            CVector pPos = player->GetPosition();
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
                LogError("KEY 1 (RECRUIT): CPopulation::AddPed retornou nullptr para modelo=%d!", modelIdx);
                ShowMsg("~r~Falha ao criar recruta!");
                return;
            }

            LogEvent("KEY 1 (RECRUIT): ped criado %p modelo=%d pedType=%d (GSF=8) spawnPos=(%.1f,%.1f,%.1f)",
                static_cast<void*>(ped), modelIdx, (int)ped->m_nPedType,
                spawnPos.x, spawnPos.y, spawnPos.z);

            // Configurar ped como ped de missao (nao recolhido pelo GC)
            ped->SetCharCreatedBy(2);  // 2 = PEDCREATED_MISSION

            // Dar arma
            ped->GiveWeapon(RECRUIT_WEAPON, RECRUIT_AMMO, false);
            LogEvent("KEY 1 (RECRUIT): arma=%d ammo=%d atribuida", (int)RECRUIT_WEAPON, RECRUIT_AMMO);

            g_recruit = ped;
            g_state   = ModState::ON_FOOT;

            // Flags criticas ANTES de gerir o grupo:
            // bDoesntListenToPlayerGroupCommands=0 e essencial — peds criados
            // via CPopulation::AddPed com tipo PED_TYPE_GANG2 (=8, GSF) podem
            // ter este flag=1, fazendo TellGroupToStartFollowingPlayer ignorar o ped.
            g_recruit->bNeverLeavesGroup                  = 1;
            g_recruit->bKeepTasksAfterCleanUp             = 1;
            g_recruit->bDoesntListenToPlayerGroupCommands = 0;

            // Reset de timers para janela limpa apos spawn
            g_groupRescanTimer   = 0;
            g_initialFollowTimer = INITIAL_FOLLOW_FRAMES;
            g_logAiFrame         = 0;
            g_prevRecruitTaskId  = -999;  // forcar log da primeira tarefa vista
            g_postFollowTimer    = 0;
            g_wasWrongDir        = false;

            LogEvent("KEY 1 (RECRUIT): flags pre-grupo — bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d initTimer=%d",
                (int)g_recruit->bNeverLeavesGroup,
                (int)g_recruit->bKeepTasksAfterCleanUp,
                (int)g_recruit->bDoesntListenToPlayerGroupCommands,
                g_initialFollowTimer);

            // Activar boost persistente de respeito ANTES de qualquer operacao de grupo.
            // Cobre: MakeThisPedJoinOurGroup, TellGroupToStartFollowingPlayer,
            // CreateFirstSubTask (deferred), FindMaxNumberOfGroupMembers (periodico).
            ActivateRespectBoost();

            // Adicionar ao grupo do jogador (vai seguir automaticamente)
            AddRecruitToGroup(player);

            ShowMsg("~g~Recruta activo! [2=carro, 4=modo, N=agressivo, 1=dispensar]");
        }
        else
        {
            // ── Dispensar ──
            LogKey("KEY 1 (DISMISS): estado=%s", StateName(g_state));
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
        LogKey("KEY 2 (CAR): estado=%s", StateName(g_state));
        if (!IsRecruitValid())
        {
            LogWarn("KEY 2 (CAR): recruta invalido");
            ShowMsg("~r~Sem recruta activo.");
            return;
        }

        if (g_state == ModState::DRIVING && IsCarValid())
        {
            // ── DRIVING → ON_FOOT: recruta sai do carro ──────────────
            // CLEO: 0633 exit_car → 12@=1 → 0631 → 087F → 0850
            // g_car e preservado para re-entrada posterior via tecla 2.
            LogEvent("KEY 2: DRIVING -> ON_FOOT (recruta sai do carro %p)", static_cast<void*>(g_car));
            CTaskComplexLeaveCar* pTask =
                new CTaskComplexLeaveCar(g_car, 0, 0, true, false);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);
            LogTask("CTaskComplexLeaveCar emitido para carro %p", static_cast<void*>(g_car));

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
                LogEvent("KEY 2: ON_FOOT -> ENTER_CAR carro_guardado=%p", static_cast<void*>(g_car));
                targetCar = g_car;
            }
            else
            {
                // Procurar carro livre mais proximo
                LogEvent("KEY 2: ON_FOOT -> ENTER_CAR procurando carro livre raio=%.0fm...", FIND_CAR_RADIUS);
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

            g_car = targetCar;
            g_state = ModState::ENTER_CAR;
            g_enterCarTimer = ENTER_CAR_TIMEOUT;
            LogTask("CTaskComplexEnterCarAsDriver emitido para carro %p timeout=%d frames",
                static_cast<void*>(targetCar), ENTER_CAR_TIMEOUT);
            LogEvent("KEY 2: estado -> ENTER_CAR (carro=%p)", static_cast<void*>(targetCar));
            ShowMsg("~y~Recruta a entrar no carro...");
            return;
        }
        return;
    }

    // ── 3: Jogador como passageiro / sair do carro ───────────────
    // VK 0x33 = tecla '3' (CLEO key_pressed 51)
    if (KeyJustPressed(VK_PASSENGER))
    {
        LogKey("KEY 3 (PASSENGER): estado=%s", StateName(g_state));
        if (g_state == ModState::DRIVING && IsCarValid())
        {
            // Jogador entra como passageiro
            CTaskComplexEnterCarAsPassenger* pTask =
                new CTaskComplexEnterCarAsPassenger(g_car, 0, false);

            player->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            g_state = ModState::PASSENGER;
            LogEvent("KEY 3: DRIVING -> PASSENGER carro=%p", static_cast<void*>(g_car));
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
            LogEvent("KEY 3: PASSENGER -> DRIVING carro=%p", static_cast<void*>(g_car));
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
        DriveMode prevMode = g_driveMode;
        g_driveMode = static_cast<DriveMode>(nextMode);

        LogKey("KEY 4 (MODE): %s -> %s estado=%s",
            DriveModeName(prevMode), DriveModeName(g_driveMode), StateName(g_state));
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
        LogKey("KEY N (AGGRO): aggr=%d (agora: %s) estado=%s",
            (int)g_aggressive, g_aggressive ? "AGRESSIVO" : "PASSIVO", StateName(g_state));
        if (g_state == ModState::ON_FOOT)
        {
            player->ForceGroupToAlwaysFollow(!g_aggressive);
            LogGroup("ForceGroupToAlwaysFollow(%d) via tecla N", (int)(!g_aggressive));
        }
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
        LogKey("KEY B (DRIVEBY): driveby=%d", (int)g_driveby);
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
    ++g_logFrame;  // contador de frames para o log

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
        // Inicializar log antes do primeiro frame
        LogInit();
        LogEvent("Plugin carregado — grove_recruit_standalone.asi v1.0");
        LogEvent("Teclas: 1=spawn/dismiss 2=carro 3=passageiro 4=modo N=aggro B=driveby");
        LogEvent("Modo inicial: aggr=%d driveMode=%s",
            (int)g_aggressive, DriveModeName(g_driveMode));

        Events::gameProcessEvent += []()
            {
                ProcessFrame();
            };
    }
};

// Instancia global — construtor executado quando o ASI e carregado
static GroveRecruitStandalone g_standalone;