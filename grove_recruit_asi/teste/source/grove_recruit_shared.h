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
extern int  g_postFollowRetries;  // tentativas FOLLOW_FALLBACK neste ciclo

// Rastreio conducao
extern bool g_wasWrongDir;
extern bool g_wasInvalidLink;
extern int  g_missionRecoveryTimer;
extern bool g_slowZoneRestoring;

// Boost de respeito persistente (-1.0f = inactivo; >=0 = boost activo)
// extern float g_savedRespect;  // REMOVIDO — Respects() usa m_acquaintance.m_nRespect, nao STAT_RESPECT

// Flag: recruta foi adicionado via AddFollower (fallback) em vez de MakeThisPedJoinOurGroup.
extern bool g_joinedViaAddFollower;

// Flag: recruta entrou no carro como passageiro (auto-entrada ou manual).
// Quando true, ProcessEnterCar transita para RIDING em vez de DRIVING.
extern bool g_enterCarAsPassenger;

// Rastreio: jogador estava em veiculo no frame anterior.
// Usado para detectar a transicao on-foot→veiculo e emitir auto-entrada.
extern bool g_playerWasInVehicle;

// Contadores de frame partilhados
extern int g_logFrame;      // incrementado em ProcessFrame (Main.cpp); usado em LogWrite
extern int g_logAiFrame;    // throttle dos dumps AI (~2s); usado em ProcessOnFoot/DrivingAI

// Timer de re-snap periodico ao road-graph (modos CIVICO, reset por DismissRecruit)
// Valor negativo = pausa de snap (storm de INVALID_LINK detectado); conta em direcao a 0.
extern int g_civicRoadSnapTimer;

// Timer do sistema de observacao vanilla (reset por DismissRecruit)
extern int g_observerTimer;

// Contador de links invalidos consecutivos — protege contra INVALID_LINK storm.
// Resetado quando link fica valido ou em DismissRecruit.
extern int g_invalidLinkCounter;

// Timer de re-scan para recrutas vanilla (reset por DismissRecruit)
extern int g_scanGroupTimer;

// CIVICO_H/I close-blocked WAIT: detectar recruta bloqueado proximo ao jogador parado
extern int  g_closeBlockedTimer;  // frames consecutivos perto+parado para activar espera
extern bool g_closeBlocked;       // true = modo WAIT activo (STOP_FOREVER ate desbloquear)

// Offroad direct-follow (canal/zona sem estrada)
extern int  g_offroadSustainedFrames; // frames consecutivos em offroad (incrementado por frame)
extern bool g_wasOffroadDirect;       // flag de transicao: era direct-follow no frame anterior

// Durabilidade do carro do recruta
extern int g_carHealthTimer;  // conta frames para restauracao periodica de saude do carro

// ───────────────────────────────────────────────────────────────────
// Multi-recruit tracking
// Rastreia todos os peds no grupo do jogador (spawned + vanilla).
// ───────────────────────────────────────────────────────────────────
struct TrackedRecruit
{
    CPed* ped       = nullptr;  // ped rastreado (nullptr = slot vazio)
    bool  isVanilla = false;    // true = detectado via scan de grupo vanilla
    bool  flagsSet  = false;    // true = mod flags (bNeverLeaves, etc.) ja aplicados
};

extern TrackedRecruit g_allRecruits[MAX_TRACKED_RECRUITS];  // todos os recrutas
extern int            g_numAllRecruits;                     // numero de slots ocupados

// ───────────────────────────────────────────────────────────────────
// Menu state
// ───────────────────────────────────────────────────────────────────
extern bool g_menuOpen;   // true = menu visivel
extern int  g_menuSel;    // item seleccionado (0-based)

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
//   Tipos (ePedGroupDefaultTaskAllocatorType enum, gta-reversed):
//     0 = FOLLOW_ANY_MEANS   (CTaskComplexFollowLeaderAnyMeans — nao usado na pratica; tem DebugBreak)
//     1 = FOLLOW_LIMITED     (CTaskComplexFollowLeaderInFormation para lider, WanderGang para membros)
//     2 = STAND_STILL        (CTaskSimpleStandStill para todos os membros)
//     3 = CHAT
//     4 = SIT_IN_LEADER_CAR  (tenta entrar no carro do lider; no-op se lider nao tem carro)
//     5 = RANDOM             (wander aleatorio; e o que TellGroupToStartFollowingPlayer usa)
//   NOTA: quando chamado, AllocateDefaultTasks(nullptr) e invocado internamente,
//   atribuindo a tarefa padrao a TODOS os membros. Apenas afecta m_DefaultPedTaskPairs
//   (tarefa de fallback), nao o slot[3]=TASK_PRIMARY_PRIMARY.
//
inline void GroupIntelSetDefaultTaskAllocatorType(void* pIntel, int type)
{
    if (!pIntel) return;
    typedef void (__thiscall* Fn)(void*, int);
    static const Fn fn = reinterpret_cast<Fn>(0x5FBB70);
    fn(pIntel, type);
}

//
// CPedGroupIntelligence::ComputeDefaultTasks (0x5F88D0)
//   Actualiza a tarefa padrao (m_DefaultPedTaskPairs) do ped especificado:
//     FlushTasks(m_DefaultPedTaskPairs, ped)
//     m_DefaultTaskAllocator->AllocateDefaultTasks(pedGroup, ped)
//   IMPORTANTE: quando ped != nullptr, a funcao processa APENAS o slot desse ped
//   (o loop faz skip se tp.Ped != ped). Quando ped = player (lider), apenas a tarefa
//   padrao do LIDER e actualizada — nao a do recruta!
//   Para actualizar o recruta: passar nullptr (afecta todos os membros).
//   Esta funcao NAO atribui TASK_COMPLEX_BE_IN_GROUP (243) a TASK_PRIMARY_PRIMARY;
//   apenas afecta o fallback em m_DefaultPedTaskPairs.
//
inline void GroupIntelComputeDefaultTasks(void* pIntel, CPed* ped)
{
    if (!pIntel) return;
    typedef void (__thiscall* Fn)(void*, CPed*);
    static const Fn fn = reinterpret_cast<Fn>(0x5F88D0);
    fn(pIntel, ped);
}

// CTaskManager::ClearTaskEventResponse (0x681BD0)
// Limpa as tarefas de event-response do task manager:
//   slot[1]=TASK_PRIMARY_EVENT_RESPONSE_TEMP
//   slot[2]=TASK_PRIMARY_EVENT_RESPONSE_NONTEMP
// Usado para limpar TASK_COMPLEX_GANG_JOIN_RESPOND(1219) que persiste no slot[2] apos
// TASK_SIMPLE_ANIM(400) terminar (bKeepTasksAfterCleanUp=1 impede limpeza automatica).
// Sem esta chamada, GANG_JOIN_RESPOND em slot[2] e devolvido por GetSimplestActiveTask
// antes de TASK_COMPLEX_BE_IN_GROUP em slot[3], ocultando o estado de follow real.
inline void ClearTaskEventResponse(CTaskManager* tm)
{
    if (!tm) return;
    typedef void (__thiscall* Fn)(CTaskManager*);
    static const Fn fn = reinterpret_cast<Fn>(0x681BD0);
    fn(tm);
}

//
// EnsureBeInGroup — Garante que TASK_COMPLEX_BE_IN_GROUP(243) esta em
//   TASK_PRIMARY_PRIMARY (slot[3]) do recruta.
//
// Mecanismo de follow (gta-reversed):
//   - TellGroupToStartFollowingPlayer(true) dispara evento GATHER que coloca
//     CTaskComplexSeekEntity em m_PedTaskPairs[recruit] da inteligencia do grupo.
//   - TASK_COMPLEX_BE_IN_GROUP chama GetTaskMain(recruit) a cada frame, obtem
//     SeekEntity e executa-o como sub-tarefa → recruta caminha para o jogador.
//   - Sem BE_IN_GROUP em slot[3], GetTaskMain nunca e chamado para o recruta,
//     eventos GATHER sao ignorados e o recruta fica em STAND_STILL para sempre.
//
// MakeThisPedJoinOurGroup deveria atribuir BE_IN_GROUP a slot[3], mas falha em
// condicoes especificas (ped spawn recente, tarefas residuais, etc.).
// Esta funcao detecta e corrige a omissao de forma cirurgica.
//
// Tamanho de CTaskComplexBeInGroup: 0x28 bytes (VALIDATE_SIZE em gta-reversed).
// Constructor: 0x632E50 (arg0=groupId int, arg1=isLeader bool).
//
inline bool EnsureBeInGroup(CPed* recruit, unsigned int groupId)
{
    if (!recruit) return false;
    CTask* slot3 = recruit->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    if (slot3 && (int)slot3->GetId() == 243 /* TASK_COMPLEX_BE_IN_GROUP */)
        return false; // ja presente

    void* mem = operator new(0x28);
    if (!mem) return false;
    typedef void (__thiscall* CtorFn)(void*, int, bool);
    static const CtorFn ctor = reinterpret_cast<CtorFn>(0x632E50);
    ctor(mem, (int)groupId, false /* isLeader=false: recruta e membro */);
    recruit->m_pIntelligence->m_TaskMgr.SetTask(
        static_cast<CTask*>(mem), TASK_PRIMARY_PRIMARY, false);
    return true;
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
void  TellGroupFollowWithRespect(CPlayerPed* player, bool aggressive, bool verbose = true);
void  AddRecruitToGroup(CPlayerPed* player);
void  RemoveRecruitFromGroup(CPlayerPed* player);
void  DismissRecruit(CPlayerPed* player);
void  ApplyRecruitEnhancement(CPed* ped, bool isVanilla);
void  ScanPlayerGroup(CPlayerPed* player);
void  OnPlayerEnterVehicle(CPlayerPed* player);
void  OnPlayerExitVehicle(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_drive.cpp
// ───────────────────────────────────────────────────────────────────
bool          DetectOffroad(CVehicle* veh);
unsigned char AdaptiveSpeed(CVehicle* veh, float targetHeading, unsigned char baseSpeed);
float         ApplyLaneAlignment(CVehicle* veh);
CVehicle*     FindNearestFreeCar(CVector const& searchPos, CVehicle* excludePlayerCar);
void          SetupDriveMode(CPlayerPed* player, DriveMode mode);
void          ProcessDrivingAI(CPlayerPed* player);
void          ProcessEnterCar(CPlayerPed* player);
void          ProcessDriving(CPlayerPed* player);
void          ProcessPassenger(CPlayerPed* player);
void          ProcessRiding(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_ai.cpp
// ───────────────────────────────────────────────────────────────────
void ProcessOnFoot(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_observer.cpp
// ───────────────────────────────────────────────────────────────────
void ProcessObserver(CPlayerPed* player);

// ───────────────────────────────────────────────────────────────────
// Forward declarations — grove_recruit_menu.cpp
// ───────────────────────────────────────────────────────────────────
void HandleMenuKeys(CPlayerPed* player);
void RenderMenu(CPlayerPed* player);
