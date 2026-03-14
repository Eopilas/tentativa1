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

// CIVICO_D: abaixo desta distancia usa MISSION_34 (FollowCarFaraway)
// em vez de MISSION_43 (EscortRearFaraway) para reduzir o efeito "chase"
// ao virar rapidamente perto do recruta.
static constexpr float MEDIUM_DIST_M = 25.0f;

static constexpr float OFFROAD_DIST_M = 28.0f;  // distancia ao no → offroad

// ───────────────────────────────────────────────────────────────────
// Velocidades (unidades SA ≈ km/h)
// ───────────────────────────────────────────────────────────────────
static constexpr unsigned char SPEED_CIVICO = 38;
static constexpr unsigned char SPEED_SLOW   = 12;
static constexpr unsigned char SPEED_DIRETO = 60;
static constexpr unsigned char SPEED_MIN    = 8;  // minimo absoluto (evita paragem em curva)

// ───────────────────────────────────────────────────────────────────
// Intervalos de temporizador (frames @ 60 fps)
// ───────────────────────────────────────────────────────────────────
static constexpr int OFFROAD_CHECK_INTERVAL = 30;   // 0.5s
static constexpr int DIRETO_UPDATE_INTERVAL = 60;   // 1.0s
static constexpr int ENTER_CAR_TIMEOUT      = 360;  // 6.0s
static constexpr int GROUP_RESCAN_INTERVAL  = 120;  // 2.0s
static constexpr int INITIAL_FOLLOW_FRAMES  = 300;  // 5.0s

// Intervalo de re-snap periodico ao road-graph para modos CIVICO.
// JoinCarWithRoadSystem e re-chamado a cada N frames para manter o
// veiculo alinhado com os nos de estrada e reduzir desvios de faixa.
static constexpr int ROAD_SNAP_INTERVAL     = 300;  // 5.0s

// Intervalo do sistema de observacao vanilla (diagnostico de motor do jogo)
static constexpr int OBSERVER_INTERVAL      = 120;  // 2.0s

// ───────────────────────────────────────────────────────────────────
// Outros limites
// ───────────────────────────────────────────────────────────────────
static constexpr float    FIND_CAR_RADIUS    = 50.0f;
static constexpr unsigned MAX_VALID_LINK_ID  = 50000u;
static constexpr float    RESPECT_TEST_BOOST = 1000.0f;

// ───────────────────────────────────────────────────────────────────
// Virtual Keys (espelham codigos CLEO: 49/50/51/52/78/66)
// ───────────────────────────────────────────────────────────────────
static constexpr int VK_RECRUIT   = 0x31;  // 1
static constexpr int VK_CAR       = 0x32;  // 2
static constexpr int VK_PASSENGER = 0x33;  // 3
static constexpr int VK_MODE      = 0x34;  // 4
static constexpr int VK_AGGRO     = 0x4E;  // N
static constexpr int VK_DRIVEBY   = 0x42;  // B

// ───────────────────────────────────────────────────────────────────
// Enumeracoes de estado
// ───────────────────────────────────────────────────────────────────
enum class ModState : int
{
    INACTIVE  = 0,
    ON_FOOT   = 1,
    ENTER_CAR = 2,
    DRIVING   = 3,
    PASSENGER = 4,
};

enum class DriveMode : int
{
    CIVICO_D = 0,   // MISSION_43 (EscortRearFaraway) — road-following vanilla
    CIVICO_E = 1,   // MISSION_34 (FollowCarFaraway)  — segue a distancia
    DIRETO   = 2,   // MISSION_GOTOCOORDS (8)          — vai directo ao jogador
    PARADO   = 3,   // MISSION_STOP_FOREVER (11)        — para
    COUNT    = 4,
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
        default:                  return "UNKNOWN";
    }
}

inline const char* DriveModeName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_D: return "CIVICO_D(MISSION_43)";
        case DriveMode::CIVICO_E: return "CIVICO_E(MISSION_34)";
        case DriveMode::DIRETO:   return "DIRETO(MISSION_8)";
        case DriveMode::PARADO:   return "PARADO(MISSION_11)";
        default:                  return "UNKNOWN";
    }
}

// Verdadeiro se o modo usa o road-graph (CIVICO_D ou CIVICO_E).
inline bool IsCivicoMode(DriveMode m)
{
    return m == DriveMode::CIVICO_D || m == DriveMode::CIVICO_E;
}

// Missao AutoPilot base para um dado modo CIVICO.
// CIVICO_D → MISSION_43 (EscortRearFaraway)
// CIVICO_E → MISSION_34 (FollowCarFaraway)
// NOTA: em CIVICO_D ProcessDrivingAI pode baixar para MISSION_34
//       quando a distancia e menor que MEDIUM_DIST_M.
inline eCarMission GetExpectedMission(DriveMode m)
{
    if (m == DriveMode::CIVICO_D) return MISSION_43;
    if (m == DriveMode::CIVICO_E) return MISSION_34;
    return MISSION_STOP_FOREVER;   // sentinela para DIRETO/PARADO
}
