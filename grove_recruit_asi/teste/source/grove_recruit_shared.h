#pragma once
/*
 * grove_recruit_shared.h
 *
 * Header mestre partilhado por todos os modulos .cpp do mod.
 * Inclui em cascata: grove_recruit_config.h + grove_recruit_log.h.
 *
 * Contem:
 *   - Declaracoes extern de todos os globals (definidos em Main.cpp)
 *   - Wrappers inline para funcoes internas do GTA SA (0xADDRESS)
 *   - Forward declarations de todas as funcoes do mod
 *
 * REGRA: cada .cpp deve incluir APENAS este header (em vez de incluir
 * grove_recruit_config.h ou grove_recruit_log.h directamente).
 */
#include "grove_recruit_log.h"

// ───────────────────────────────────────────────────────────────────
// Estado global (definido em Main.cpp)
// ───────────────────────────────────────────────────────────────────
extern ModState   g_state;
extern DriveMode  g_driveMode;
extern bool       g_aggressive;
extern bool       g_driveby;

extern CPed*      g_recruit;
extern CVehicle*  g_car;

// Timers
extern int  g_enterCarTimer;
extern int  g_offroadTimer;
extern int  g_diretoTimer;
extern int  g_groupRescanTimer;
extern int  g_passiveTimer;
extern int  g_initialFollowTimer;
extern bool g_isOffroad;

// Rastreio on-foot
extern int  g_prevRecruitTaskId;
extern int  g_postFollowTimer;

// Rastreio conducao
extern bool g_wasWrongDir;
extern bool g_wasInvalidLink;
extern int  g_missionRecoveryTimer;
extern bool g_slowZoneRestoring;

// Boost de respeito persistente (-1.0f = inactivo; >=0 = boost activo)
extern float g_savedRespect;

// Contadores de frame partilhados
extern int g_logFrame;      // incrementado em ProcessFrame (Main.cpp); usado em LogWrite
extern int g_logAiFrame;    // throttle dos dumps AI (~2s); usado em ProcessOnFoot/DrivingAI

// Timer de re-snap periodico ao road-graph (modos CIVICO, reset por DismissRecruit)
extern int g_civicRoadSnapTimer;

// Timer do sistema de observacao vanilla (reset por DismissRecruit)
extern int g_observerTimer;

// ───────────────────────────────────────────────────────────────────
// Wrappers inline de funcoes internas GTA SA
// ───────────────────────────────────────────────────────────────────

//
// FindMaxNumberOfGroupMembers (0x559A50)
//   Le STAT_RESPECT e devolve o numero maximo de membros permitidos
//   no grupo do jogador (0..7).  Chamada apenas para diagnostico.
//
inline int FindMaxGroupMembers()
{
    typedef int (__cdecl* Fn)();
    static const Fn fn = reinterpret_cast<Fn>(0x559A50);
    return fn();
}

//
// CPedGroupIntelligence* is an inline member of CPedGroup at offset +0x30.
// ATENCAO: NAO escrever nesta posicao directamente (crash — e um ponteiro vtable).
// Usar apenas para chamar metodos via cast de ponteiro.
//
inline void* GetGroupIntelligence(unsigned int groupIdx)
{
    if (groupIdx >= 8u) return nullptr;
    return reinterpret_cast<char*>(&CPedGroups::ms_groups[groupIdx]) + 0x30;
}

//
// CPedGroupIntelligence::SetDefaultTaskAllocatorType (0x5FBB70)
//   Tipos:  0=None  1=GangFollower  2=Formation  3=Cover  4=FollowLeaderAnyMeans
//   Tipo 4 (FollowLeaderAnyMeans) bypassa o check FindMaxGroupMembers e garante
//   que o recruta segue o jogador mesmo sem respeito suficiente via story progress.
//   Usado como FALLBACK quando TellGroupToStartFollowingPlayer nao atribui task 1207.
//
inline void GroupIntelSetDefaultTaskAllocatorType(void* pIntel, int type)
{
    if (!pIntel) return;
    typedef void (__thiscall* Fn)(void*, int);
    static const Fn fn = reinterpret_cast<Fn>(0x5FBB70);
    fn(pIntel, type);
}

// ───────────────────────────────────────────────────────────────────
// Forward declarations — Main.cpp
// ───────────────────────────────────────────────────────────────────
bool  IsRecruitValid();
bool  IsCarValid();
float Dist2D(CVector const& a, CVector const& b);
void  ShowMsg(const char* text, unsigned int durationMs = 2500);
bool  KeyJustPressed(int vk);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_group.cpp
// ───────────────────────────────────────────────────────────────────
int   FindRecruitMemberID(CPlayerPed* player);
void  ActivateRespectBoost();
void  DeactivateRespectBoost();
void  TellGroupFollowWithRespect(CPlayerPed* player, bool aggressive, bool verbose = true);
void  AddRecruitToGroup(CPlayerPed* player);
void  RemoveRecruitFromGroup(CPlayerPed* player);
void  DismissRecruit(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_drive.cpp
// ───────────────────────────────────────────────────────────────────
bool          DetectOffroad(CVehicle* veh);
unsigned char AdaptiveSpeed(CVehicle* veh, unsigned char baseSpeed);
float         ApplyLaneAlignment(CVehicle* veh);
CVehicle*     FindNearestFreeCar(CVector const& searchPos, CVehicle* excludePlayerCar);
void          SetupDriveMode(CPlayerPed* player, DriveMode mode);
void          ProcessDrivingAI(CPlayerPed* player);
void          ProcessEnterCar(CPlayerPed* player);
void          ProcessDriving(CPlayerPed* player);
void          ProcessPassenger(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_ai.cpp
// ───────────────────────────────────────────────────────────────────
void ProcessOnFoot(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_observer.cpp
// ───────────────────────────────────────────────────────────────────
void ProcessObserver(CPlayerPed* player);
