#pragma once
/*
 * grove_recruit_config.h
 *
 * Includes, constantes, enumeracoes e helpers inline partilhados por
 * todos os modulos do grove_recruit_standalone.asi.
 *
 * REGRA: este header NAO deve incluir nenhum header especifico do mod.
 * Apenas includes do SDK/runtime, constantes e tipos simples.
 */

// ───────────────────────────────────────────────────────────────────
// SDK / Game includes
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
#include "CTimer.h"
#include "CFont.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>   // rand, srand
#include <cstring>
#include <ctime>     // time (srand seed)
#include <algorithm>
#include <windows.h>   // GetAsyncKeyState

// ───────────────────────────────────────────────────────────────────
// Modelos e tipo do recruta
// ───────────────────────────────────────────────────────────────────

// Modelos Grove Street Families (FAM1/FAM2/FAM3)
static constexpr int   FAM_MODELS[]    = { 105, 106, 107 };
static constexpr int   FAM_MODEL_COUNT = 3;

// PED_TYPE_GANG2 = 8 = Grove Street Families (correcto).
// NAO usar PED_TYPE_GANG1 = 7 (Ballas/Vagos): causa congelamento e inimizade.
static constexpr ePedType RECRUIT_PED_TYPE = PED_TYPE_GANG2;

// Arma padrao: AK47 = 30
static constexpr eWeaponType RECRUIT_WEAPON = static_cast<eWeaponType>(30);
static constexpr int         RECRUIT_AMMO   = 300;

// Distancia de spawn atras do jogador (metros)
static constexpr float SPAWN_BEHIND_DIST = 2.5f;

// ───────────────────────────────────────────────────────────────────
// Distancias de zona (metros)
// ───────────────────────────────────────────────────────────────────
static constexpr float STOP_ZONE_M   = 6.0f;    // para completamente
static constexpr float SLOW_ZONE_M   = 10.0f;   // abranda

static constexpr float OFFROAD_DIST_M = 28.0f;  // distancia ao no → offroad

// Distancia minima para que WRONG_DIR_RECOVER dispare SetupDriveMode (v2 fix).
// CORRECAO v2: condicao INVERTIDA — SetupDriveMode so dispara quando dist > esta constante.
// ANTERIOR (bug): disparava quando dist < 30m → JoinCarWithRoadSystem em range proximo
//   → re-snap errado → WRONG_DIR prolongado 38+ segundos = modo "chase" off-road.
// FIX: apenas quando longe (dist > 30m). Range proximo usa snap periodico (ROAD_SNAP_INTERVAL).
static constexpr float WRONG_DIR_RECOVERY_DIST_M = 30.0f;

// ───────────────────────────────────────────────────────────────────
// Velocidades (unidades SA ≈ km/h)
// ───────────────────────────────────────────────────────────────────
static constexpr unsigned char SPEED_CIVICO      = 46;   // velocidade padrao CIVICO (era 38)
static constexpr unsigned char SPEED_CIVICO_HIGH = 55;   // velocidade em retas longas
static constexpr unsigned char SPEED_CIVICO_CLOSE = 22;  // cap de velocidade quando dist < CLOSE_RANGE_SWITCH_DIST
                                                          // Previne subida de passeio em curvas proximas ao jogador
static constexpr unsigned char SPEED_SLOW        = 12;
static constexpr unsigned char SPEED_DIRETO      = 60;
static constexpr unsigned char SPEED_MIN         = 8;    // minimo absoluto

// ───────────────────────────────────────────────────────────────────
// Intervalos de temporizador (frames @ 60 fps)
// ───────────────────────────────────────────────────────────────────
static constexpr int OFFROAD_CHECK_INTERVAL      = 30;   // 0.5s
static constexpr int DIRETO_UPDATE_INTERVAL      = 60;   // 1.0s
// Timeout para entrar em carro: separado por caso de uso.
// Como passageiro (entrar no carro do jogador): animacao de andar + abrir porta + sentar.
// O carro pode estar ate ~20m afastado — a pe = ~13s + animacao = 25s realista.
static constexpr int ENTER_CAR_PASSENGER_TIMEOUT = 1500; // 25.0s @ 60fps
// Como condutor (recruta vai buscar proprio carro): pode estar a ate 50m (FIND_CAR_RADIUS).
// A pe @ ~1.5 m/s = ~33s + animacao = 40s; dar margem generosa.
static constexpr int ENTER_CAR_DRIVER_TIMEOUT    = 1800; // 30.0s @ 60fps
static constexpr int GROUP_RESCAN_INTERVAL       = 120;  // 2.0s
static constexpr int INITIAL_FOLLOW_FRAMES       = 300;  // 5.0s
static constexpr int SCAN_GROUP_INTERVAL         = 180;  // 3.0s — scan para recrutas vanilla

// Intervalo de re-snap periodico ao road-graph para modos CIVICO.
// JoinCarWithRoadSystem e re-chamado a cada N frames para manter o
// veiculo alinhado com os nos de estrada e reduzir desvios de faixa.
// 180 frames (3s): mais frequente que 300 (5s) para corrigir desvios
// antes de acumular. O snap e ignorado durante WRONG_DIR (ver ProcessDrivingAI).
static constexpr int ROAD_SNAP_INTERVAL     = 90;   // 1.5s (era 180=3s; mais frequente para nao errar curvas)

// ── CIVICO_H/I close-blocked WAIT ────────────────────────────────
// Quando o recruta esta perto (< CLOSE_RANGE_SWITCH_DIST) E ambos —
// recruta E jogador — estao parados durante CLOSE_BLOCKED_FRAMES
// frames consecutivos (sinal de obstrucao no transito), o recruta
// comuta para STOP_FOREVER em vez de subir o passeio ou ir na
// contramao. Retoma o CIVICO normal quando o jogador voltar a andar.
static constexpr int   CLOSE_BLOCKED_FRAMES      = 90;  // 1.5s @ 60fps: frames consecutivos parados p/ activar
static constexpr float CLOSE_BLOCKED_MIN_KMH     = 3.0f;  // velocidade < 3 km/h = "parado"
static constexpr float CLOSE_BLOCKED_RESUME_KMH  = 8.0f;  // velocidade minima do jogador para retomar CIVICO

// ── Offroad direct-follow (canal/zona sem estrada) ─────────────────
// Em zonas sem estrada (ex: canal), o road-graph n existe → o recruta
// trava em WRONG_DIR tentando voltar a estrada que n esta la.
// Fix: apos OFFROAD_DIRECT_FOLLOW_FRAMES frames consecutivos em offroad,
// mudar para GOTOCOORDS directo (como DIRETO mas a velocidade CIVICO+AVOID_CARS).
// Retoma road-follow CIVICO quando o g_isOffroad limpar.
static constexpr int OFFROAD_DIRECT_FOLLOW_FRAMES = 90;  // 1.5s @ 60fps: offroad sustentado p/ activar beeline

// ── Durabilidade do carro do recruta ──────────────────────────────
// Replica comportamento do mod CLEO: carro arranca com vida alta (1750),
// e bTakeLessDamage para reduzir dano por impacto. Fumaca vanilla continua
// a aparecer quando a vida cai abaixo ~256. Restauracao periodica de saude
// evita destruicao por dano acumulado (o carro resiste mais sem parecer God-mode).
static constexpr float RECRUIT_CAR_HEALTH_INITIAL  = 1750.0f; // vida inicial (CLEO 0224)
static constexpr float RECRUIT_CAR_HEALTH_MIN      = 700.0f;  // threshold de restauracao
static constexpr int   RECRUIT_CAR_HEALTH_RESTORE_INTERVAL = 300; // 5.0s @ 60fps

// Intervalo do sistema de observacao vanilla (diagnostico de motor do jogo)
static constexpr int OBSERVER_INTERVAL      = 120;  // 2.0s

// ───────────────────────────────────────────────────────────────────
// Outros limites
// ───────────────────────────────────────────────────────────────────
static constexpr float    FIND_CAR_RADIUS    = 50.0f;
static constexpr unsigned MAX_VALID_LINK_ID  = 50000u;

// Maximo de tentativas FOLLOW_FALLBACK (POST_FOLLOW_CHECK) por ciclo.
// Apos este limite a re-armacao do timer para — o RESCAN periodico (120fr)
// e a rajada inicial tratam do follow. Evita loop infinito de SetAllocatorType.
static constexpr int MAX_FOLLOW_FALLBACK_RETRIES = 5;

// Limiares de desvio de heading para diagnostico de direccao:
//   > WRONG_DIR_THRESHOLD_RAD: recruta em sentido contrario (WRONG_DIR!)
//   > MISALIGNED_THRESHOLD_RAD: recruta desalinhado mas nao invertido
static constexpr float WRONG_DIR_THRESHOLD_RAD  = 1.5f;
static constexpr float MISALIGNED_THRESHOLD_RAD = 0.3f;

// Reducao de velocidade maxima em curvas (AdaptiveSpeed, modo CIVICO).
// O multiplicador e interpolado linearmente de 1.0 ate (1.0 - CURVE_SPEED_REDUCTION)
// quando o desalinhamento de heading vai de MISALIGNED_THRESHOLD_RAD ate
// WRONG_DIR_THRESHOLD_RAD. Ex: 0.5 → velocidade 50% na curva maxima pre-WRONG_DIR.
static constexpr float CURVE_SPEED_REDUCTION = 0.6f;

// Distancia proxima (metros) abaixo da qual CIVICO_D/F substitui
// MC_ESCORT_REAR(31) por MC_FOLLOWCAR_CLOSE(53) em ProcessDrivingAI.
// ESCORT_REAR tenta posicionar-se geometricamente-exacto atras do jogador,
// o que pode causar "chase mode" off-road quando proximo. FOLLOWCAR_CLOSE
// segue a mesma rota do jogador sem tentar atingir uma posicao especifica.
static constexpr float CLOSE_RANGE_SWITCH_DIST = 22.0f;

// ───────────────────────────────────────────────────────────────────
// Aliases de missao eCarMission (nomes gta-reversed para clareza)
// Plugin-sdk usa nomes hexadecimais (MISSION_43=67, MISSION_34=52, etc.)
// que nao correspondem ao valor inteiro. Aliases abaixo usam nomes
// gta-reversed/CCarCtrl que descrevem o comportamento correctamente.
//
//   MC_ESCORT_REAR_FARAWAY (67) — escolta atras, longe; usa road-graph;
//     transiciona para MC_ESCORT_REAR(31) quando proximo. Usado em CIVICO_D/F.
//   MC_FOLLOWCAR_FARAWAY   (52) — segue carro, longe; road-graph.
//     transiciona para MC_FOLLOWCAR_CLOSE(53) quando proximo. Usado em CIVICO_E.
//   MC_FOLLOWCAR_CLOSE     (53) — segue carro, proximo; road-graph.
//     modo inicial para CIVICO_G (seguimento proximo directo).
//   MC_APPROACHPLAYER_FARAWAY (43) — aproximacao ao jogador, road-graph.
// ───────────────────────────────────────────────────────────────────
static constexpr eCarMission MC_ESCORT_REAR_FARAWAY   = MISSION_43;           // int=67
static constexpr eCarMission MC_FOLLOWCAR_FARAWAY     = MISSION_34;           // int=52
static constexpr eCarMission MC_FOLLOWCAR_CLOSE       = MISSION_35;           // int=53
static constexpr eCarMission MC_APPROACHPLAYER_FARAWAY = MISSION_POLICE_BIKE; // int=43
static constexpr eCarMission MC_ESCORT_REAR           = MISSION_1F;           // int=31

// Validacao em tempo de compilacao: garantir que os nomes hexadecimais do plugin-sdk
// correspondem aos valores inteiros esperados (gta-reversed/SASCM.ini).
static_assert((int)MC_ESCORT_REAR_FARAWAY == 67, "MC_ESCORT_REAR_FARAWAY deve ser 67");
static_assert((int)MC_FOLLOWCAR_FARAWAY   == 52, "MC_FOLLOWCAR_FARAWAY deve ser 52");
static_assert((int)MC_FOLLOWCAR_CLOSE     == 53, "MC_FOLLOWCAR_CLOSE deve ser 53");
static_assert((int)MC_ESCORT_REAR         == 31, "MC_ESCORT_REAR deve ser 31");

// ───────────────────────────────────────────────────────────────────
// Distancias e offsets (metros)
// ───────────────────────────────────────────────────────────────────

// Distancia maxima (recruta-jogador) para auto-entrada no carro como passageiro.
// Se o recruta estiver mais longe que isto quando o jogador entrar no carro,
// nao emitir CTaskComplexEnterCarAsPassenger (o recruta nao conseguiria chegar).
static constexpr float RECRUIT_AUTO_ENTER_DIST = 60.0f;

// Offset de destino para modo DIRETO (metros atras do jogador).
// Evita que o recruta colida com o carro do jogador: o destino e calculado
// X metros atras da posicao/heading do jogador em vez de exactamente em cima.
static constexpr float DIRETO_FOLLOW_OFFSET = 10.0f;

// ───────────────────────────────────────────────────────────────────
// Multi-recruit
// ───────────────────────────────────────────────────────────────────
// Maximo de recrutas rastreados pelo mod (inclui o primario + recrutas vanilla).
// GTA SA permite ate 7 seguidores no grupo do jogador.
static constexpr int MAX_TRACKED_RECRUITS = 7;

// Tipos de allocator de tarefa padrao do grupo (ePedGroupDefaultTaskAllocatorType):
//   1 = FOLLOW_LIMITED  — follow em formacao (padrao a pe)
//   4 = SIT_IN_LEADER_CAR — todos tentam entrar no carro do lider
//   5 = RANDOM          — wander aleatorio
static constexpr int ALLOCATOR_FOLLOW          = 1;
static constexpr int ALLOCATOR_SIT_IN_CAR      = 4;

// ───────────────────────────────────────────────────────────────────
// Virtual Keys — acoes existentes + menu
// ───────────────────────────────────────────────────────────────────
static constexpr int VK_RECRUIT    = 0x31;  // 1
static constexpr int VK_CAR        = 0x32;  // 2
static constexpr int VK_PASSENGER  = 0x33;  // 3
static constexpr int VK_MODE       = 0x34;  // 4
static constexpr int VK_AGGRO      = 0x4E;  // N
static constexpr int VK_DRIVEBY    = 0x42;  // B

// Menu
static constexpr int VK_MENU_OPEN  = 0x2D;  // INSERT
static constexpr int VK_MENU_UP    = 0x26;  // UP arrow
static constexpr int VK_MENU_DOWN  = 0x28;  // DOWN arrow
static constexpr int VK_MENU_LEFT  = 0x25;  // LEFT arrow
static constexpr int VK_MENU_RIGHT = 0x27;  // RIGHT arrow
static constexpr int VK_MENU_BACK  = 0x1B;  // ESC

// ───────────────────────────────────────────────────────────────────
// Enumeracoes de estado
// ───────────────────────────────────────────────────────────────────
enum class ModState : int
{
    INACTIVE  = 0,
    ON_FOOT   = 1,
    ENTER_CAR = 2,
    DRIVING   = 3,
    PASSENGER = 4,   // jogador e passageiro no carro do recruta (tecla 3)
    RIDING    = 5,   // recruta e passageiro no carro do jogador (auto ou tecla 2 manual)
};

enum class DriveMode : int
{
    CIVICO_D = 0,   // MC_ESCORT_REAR_FARAWAY(67) road-following, STOP_IGNORE_LIGHTS
    CIVICO_E = 1,   // MC_FOLLOWCAR_FARAWAY(52)   road-following, STOP_IGNORE_LIGHTS
    CIVICO_F = 2,   // MC_ESCORT_REAR_FARAWAY(67) road-following, AVOID_CARS
    CIVICO_G = 3,   // MC_FOLLOWCAR_CLOSE(53)     seguimento proximo, AVOID_CARS
    CIVICO_H = 4,   // MC_FOLLOWCAR_FARAWAY(52)   road-following, AVOID_CARS  (E+G combo)
    CIVICO_I = 5,   // MC_ESCORT_REAR_FARAWAY(67) road-following, SLOW_DOWN_FOR_CARS
    DIRETO   = 6,   // MISSION_GOTOCOORDS(8)      vai directo, offset atras do jogador
    PARADO   = 7,   // MISSION_STOP_FOREVER(11)   para
    COUNT    = 8,
};

// ───────────────────────────────────────────────────────────────────
// Helpers inline (apenas logica de tipo, sem acesso a estado global)
// ───────────────────────────────────────────────────────────────────
inline const char* StateName(ModState s)
{
    switch (s) {
        case ModState::INACTIVE:  return "INACTIVE";
        case ModState::ON_FOOT:   return "ON_FOOT";
        case ModState::ENTER_CAR: return "ENTER_CAR";
        case ModState::DRIVING:   return "DRIVING";
        case ModState::PASSENGER: return "PASSENGER";
        case ModState::RIDING:    return "RIDING";
        default:                  return "UNKNOWN";
    }
}

inline const char* DriveModeName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_D: return "CIVICO_D(MC67+STOP)";
        case DriveMode::CIVICO_E: return "CIVICO_E(MC52+STOP)";
        case DriveMode::CIVICO_F: return "CIVICO_F(MC67+AVOID)";
        case DriveMode::CIVICO_G: return "CIVICO_G(MC53+AVOID)";
        case DriveMode::CIVICO_H: return "CIVICO_H(MC52+AVOID)";
        case DriveMode::CIVICO_I: return "CIVICO_I(MC67+SLOW)";
        case DriveMode::DIRETO:   return "DIRETO(GOTOCOORDS)";
        case DriveMode::PARADO:   return "PARADO(STOP_FOREVER)";
        default:                  return "UNKNOWN";
    }
}

// Nome curto para UI do menu
inline const char* DriveModeShortName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_D: return "CIVICO-D";
        case DriveMode::CIVICO_E: return "CIVICO-E";
        case DriveMode::CIVICO_F: return "CIVICO-F";
        case DriveMode::CIVICO_G: return "CIVICO-G";
        case DriveMode::CIVICO_H: return "CIVICO-H";
        case DriveMode::CIVICO_I: return "CIVICO-I";
        case DriveMode::DIRETO:   return "DIRETO";
        case DriveMode::PARADO:   return "PARADO";
        default:                  return "???";
    }
}

// Verdadeiro se o modo usa o road-graph (CIVICO_D..I).
inline bool IsCivicoMode(DriveMode m)
{
    return m == DriveMode::CIVICO_D || m == DriveMode::CIVICO_E ||
           m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G ||
           m == DriveMode::CIVICO_H || m == DriveMode::CIVICO_I;
}

// Missao AutoPilot base para um dado modo CIVICO.
// CIVICO_D/F/I → MC_ESCORT_REAR_FARAWAY (67)
// CIVICO_E/H   → MC_FOLLOWCAR_FARAWAY   (52)
// CIVICO_G     → MC_FOLLOWCAR_CLOSE     (53)
inline eCarMission GetExpectedMission(DriveMode m)
{
    if (m == DriveMode::CIVICO_D || m == DriveMode::CIVICO_F ||
        m == DriveMode::CIVICO_I) return MC_ESCORT_REAR_FARAWAY;
    if (m == DriveMode::CIVICO_E || m == DriveMode::CIVICO_H) return MC_FOLLOWCAR_FARAWAY;
    if (m == DriveMode::CIVICO_G) return MC_FOLLOWCAR_CLOSE;
    return MISSION_STOP_FOREVER;   // sentinela para DIRETO/PARADO
}

// DriveStyle para um dado modo CIVICO (para MISSION_RECOVERY e ProcessDrivingAI).
inline eCarDrivingStyle GetExpectedDriveStyle(DriveMode m)
{
    if (m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G ||
        m == DriveMode::CIVICO_H) return DRIVINGSTYLE_AVOID_CARS;
    if (m == DriveMode::CIVICO_I) return DRIVINGSTYLE_SLOW_DOWN_FOR_CARS;
    return DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
}
