/*
 * Main.cpp  (grove_recruit_standalone)
 *
 * Ponto de entrada do plugin ASI.  Este ficheiro e intencionalmente slim:
 *   - Definicao dos globals partilhados (declarados extern em grove_recruit_shared.h)
 *   - Utilitarios basicos (IsRecruitValid, IsCarValid, Dist2D, ShowMsg, KeyJustPressed)
 *   - HandleKeys  — interpretacao das teclas 1/2/3/4/N/B
 *   - ProcessFrame — dispatcher de estados + observador vanilla
 *   - GroveRecruitStandalone — registo do hook Events::gameProcessEvent
 *
 * Toda a logica modular esta nos outros .cpp:
 *   grove_recruit_log.cpp      — LogInit / LogEvent / LogObsv / GetTaskName / ...
 *   grove_recruit_group.cpp    — AddRecruitToGroup / DismissRecruit / ...
 *   grove_recruit_drive.cpp    — SetupDriveMode / ProcessDrivingAI / ...
 *   grove_recruit_ai.cpp       — ProcessOnFoot
 *   grove_recruit_observer.cpp — ProcessObserver (vanilla engine observer)
 *
 * TECLAS:
 *   1 — Recrutar (ou dispensar)                       [VK 0x31]
 *   2 — Entrar/sair do carro (dual)                   [VK 0x32]
 *   3 — Jogador como passageiro / sair               [VK 0x33]
 *   4 — Ciclar modo de conducao                       [VK 0x34]
 *   N — Alternar agressividade                        [VK 0x4E]
 *   B — Alternar drive-by (so PASSENGER)             [VK 0x42]
 *
 * MODOS DE CONDUCAO (cyclo via tecla 4):
 *   CIVICO_D (★ padrao) — MISSION_43 road-following vanilla + auto-degrade a curta dist
 *   CIVICO_E            — MISSION_34 segue a distancia
 *   DIRETO              — MISSION_GOTOCOORDS plough-through
 *   PARADO              — MISSION_STOP_FOREVER
 */

#include "grove_recruit_shared.h"
using namespace plugin;  // Events::gameProcessEvent, etc.

// ───────────────────────────────────────────────────────────────────
// Definicoes dos globals (declarados extern em grove_recruit_shared.h)
// ───────────────────────────────────────────────────────────────────
ModState   g_state       = ModState::INACTIVE;
DriveMode  g_driveMode   = DriveMode::CIVICO_D;
bool       g_aggressive  = true;
bool       g_driveby     = false;

CPed*      g_recruit     = nullptr;
CVehicle*  g_car         = nullptr;

int  g_enterCarTimer      = 0;
int  g_offroadTimer       = 0;
int  g_diretoTimer        = 0;
int  g_groupRescanTimer   = 0;
int  g_passiveTimer       = 0;
int  g_initialFollowTimer = 0;
bool g_isOffroad          = false;

int  g_prevRecruitTaskId  = -999;
int  g_postFollowTimer    = 0;
int  g_postFollowRetries  = 0;  // contagem de tentativas FOLLOW_FALLBACK neste ciclo

bool g_wasWrongDir         = false;
bool g_wasInvalidLink      = false;
int  g_missionRecoveryTimer = 0;
bool g_slowZoneRestoring   = false;

float g_savedRespect       = -1.0f;

int  g_logFrame     = 0;
int  g_logAiFrame   = 0;

int  g_civicRoadSnapTimer = 0;
int  g_observerTimer      = 0;

// ───────────────────────────────────────────────────────────────────
// Utilitarios basicos
// ───────────────────────────────────────────────────────────────────
bool IsRecruitValid()
{
    if (!g_recruit) return false;
    if (!CPools::ms_pPedPool->IsObjectValid(g_recruit)) return false;
    if (!g_recruit->IsAlive()) return false;
    return true;
}

bool IsCarValid()
{
    if (!g_car) return false;
    if (!CPools::ms_pVehiclePool->IsObjectValid(g_car)) return false;
    return true;
}

float Dist2D(CVector const& a, CVector const& b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrtf(dx * dx + dy * dy);
}

void ShowMsg(const char* text, unsigned int durationMs)
{
    CMessages::AddMessageJumpQ(text, durationMs, 0, false);
}

bool KeyJustPressed(int vk)
{
    static bool s_prev[256] = {};
    bool curr = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool just = curr && !s_prev[vk];
    s_prev[vk] = curr;
    return just;
}

// ───────────────────────────────────────────────────────────────────
// HandleKeys — interpretacao de teclas de controlo
// ───────────────────────────────────────────────────────────────────
static void HandleKeys(CPlayerPed* player)
{
    // ── 1: Recrutar / Dispensar ──────────────────────────────────
    if (KeyJustPressed(VK_RECRUIT))
    {
        if (g_state == ModState::INACTIVE)
        {
            // ── Spawn ──
            int modelIdx = FAM_MODELS[rand() % FAM_MODEL_COUNT];
            LogEvent("KEY 1 (RECRUIT): spawn iniciado modelo=%d pos=(%.1f,%.1f,%.1f) aggr_padrao=%d respect_atual=%.0f",
                modelIdx,
                player->GetPosition().x, player->GetPosition().y, player->GetPosition().z,
                (int)g_aggressive,
                CStats::GetStatValue(STAT_RESPECT));
            CStreaming::RequestModel(modelIdx, 0);
            CStreaming::LoadAllRequestedModels(true);

            CVector pPos   = player->GetPosition();
            float   heading = player->m_fCurrentRotation;
            CVector spawnPos;
            spawnPos.x = pPos.x - std::sinf(heading) * SPAWN_BEHIND_DIST;
            spawnPos.y = pPos.y - std::cosf(heading) * SPAWN_BEHIND_DIST;
            spawnPos.z = pPos.z;

            CPed* ped = CPopulation::AddPed(RECRUIT_PED_TYPE,
                static_cast<unsigned int>(modelIdx),
                spawnPos, false);
            if (!ped)
            {
                LogError("KEY 1 (RECRUIT): CPopulation::AddPed retornou nullptr para modelo=%d!", modelIdx);
                ShowMsg("~r~Falha ao criar recruta!");
                return;
            }

            LogEvent("KEY 1 (RECRUIT): ped criado %p modelo=%d pedType=%d (GSF=8) spawnPos=(%.1f,%.1f,%.1f)",
                static_cast<void*>(ped), modelIdx, (int)ped->m_nPedType,
                spawnPos.x, spawnPos.y, spawnPos.z);

            ped->SetCharCreatedBy(2);  // PEDCREATED_MISSION
            ped->GiveWeapon(RECRUIT_WEAPON, RECRUIT_AMMO, false);
            LogEvent("KEY 1 (RECRUIT): arma=%d ammo=%d atribuida", (int)RECRUIT_WEAPON, RECRUIT_AMMO);

            g_recruit = ped;
            g_state   = ModState::ON_FOOT;

            g_recruit->bNeverLeavesGroup                  = 1;
            g_recruit->bKeepTasksAfterCleanUp             = 1;
            g_recruit->bDoesntListenToPlayerGroupCommands = 0;

            // Reset timers para sessao limpa
            g_groupRescanTimer   = 0;
            g_initialFollowTimer = INITIAL_FOLLOW_FRAMES;
            g_logAiFrame         = 0;
            g_prevRecruitTaskId  = -999;
            g_postFollowTimer    = 0;
            g_postFollowRetries  = 0;
            g_wasWrongDir        = false;
            g_wasInvalidLink     = false;
            g_missionRecoveryTimer = 0;
            g_slowZoneRestoring  = false;
            g_civicRoadSnapTimer = 0;
            g_observerTimer      = 0;

            LogEvent("KEY 1 (RECRUIT): flags pre-grupo — bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d initTimer=%d",
                (int)g_recruit->bNeverLeavesGroup,
                (int)g_recruit->bKeepTasksAfterCleanUp,
                (int)g_recruit->bDoesntListenToPlayerGroupCommands,
                g_initialFollowTimer);

            ActivateRespectBoost();
            AddRecruitToGroup(player);
            ShowMsg("~g~Recruta activo! [2=carro, 4=modo, N=agressivo, 1=dispensar]");
        }
        else
        {
            LogKey("KEY 1 (DISMISS): estado=%s", StateName(g_state));
            DismissRecruit(player);
            ShowMsg("~y~Recruta dispensado.");
        }
        return;
    }

    // ── 2: Entrar/sair do carro (dual) ───────────────────────────
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
            // DRIVING → ON_FOOT
            LogEvent("KEY 2: DRIVING -> ON_FOOT (recruta sai do carro %p)", static_cast<void*>(g_car));
            CTaskComplexLeaveCar* pTask =
                new CTaskComplexLeaveCar(g_car, 0, 0, true, false);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);
            LogTask("CTaskComplexLeaveCar emitido para carro %p", static_cast<void*>(g_car));
            // g_car PRESERVADO para re-entrada via tecla 2
            g_passiveTimer = 0;
            AddRecruitToGroup(player);
            g_state = ModState::ON_FOOT;
            ShowMsg("~y~Recruta a sair do carro. [2=retomar carro]");
            return;
        }

        if (g_state == ModState::ON_FOOT)
        {
            // ON_FOOT → ENTER_CAR
            CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            CVehicle* targetCar = nullptr;

            if (IsCarValid() && !g_car->m_pDriver)
            {
                LogEvent("KEY 2: ON_FOOT -> ENTER_CAR carro_guardado=%p", static_cast<void*>(g_car));
                targetCar = g_car;
            }
            else
            {
                LogEvent("KEY 2: ON_FOOT -> ENTER_CAR procurando carro livre raio=%.0fm...", FIND_CAR_RADIUS);
                CVector searchPos = g_recruit->GetPosition();
                targetCar = FindNearestFreeCar(searchPos, playerCar);
                if (!targetCar)
                {
                    ShowMsg("~r~Nenhum carro disponivel perto.");
                    return;
                }
            }

            RemoveRecruitFromGroup(player);

            CTaskComplexEnterCarAsDriver* pTask =
                new CTaskComplexEnterCarAsDriver(targetCar);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);

            g_car           = targetCar;
            g_state         = ModState::ENTER_CAR;
            g_enterCarTimer = ENTER_CAR_TIMEOUT;
            g_civicRoadSnapTimer = 0;
            LogTask("CTaskComplexEnterCarAsDriver emitido para carro %p timeout=%d frames",
                static_cast<void*>(targetCar), ENTER_CAR_TIMEOUT);
            LogEvent("KEY 2: estado -> ENTER_CAR (carro=%p)", static_cast<void*>(targetCar));
            ShowMsg("~y~Recruta a entrar no carro...");
            return;
        }
        return;
    }

    // ── 3: Jogador como passageiro / sair ─────────────────────────
    if (KeyJustPressed(VK_PASSENGER))
    {
        LogKey("KEY 3 (PASSENGER): estado=%s", StateName(g_state));
        if (g_state == ModState::DRIVING && IsCarValid())
        {
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

    // ── 4: Ciclar modo de conducao ────────────────────────────────
    if (KeyJustPressed(VK_MODE) && g_state != ModState::INACTIVE)
    {
        int nextMode   = (static_cast<int>(g_driveMode) + 1) %
                         static_cast<int>(DriveMode::COUNT);
        DriveMode prev = g_driveMode;
        g_driveMode    = static_cast<DriveMode>(nextMode);

        LogKey("KEY 4 (MODE): %s -> %s estado=%s",
            DriveModeName(prev), DriveModeName(g_driveMode), StateName(g_state));
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

    // ── N: Alternar agressividade ────────────────────────────────
    if (KeyJustPressed(VK_AGGRO) && g_state != ModState::INACTIVE)
    {
        g_aggressive   = !g_aggressive;
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

    // ── B: Alternar drive-by ────────────────────────────────────
    if (KeyJustPressed(VK_DRIVEBY) && g_state == ModState::PASSENGER)
    {
        g_driveby = !g_driveby;
        LogKey("KEY B (DRIVEBY): driveby=%d", (int)g_driveby);
        ShowMsg(g_driveby ? "~r~Drive-by ACTIVO" : "~y~Drive-by INACTIVO");
        return;
    }
}

// ───────────────────────────────────────────────────────────────────
// ProcessFrame — dispatcher de estados + observador vanilla
// ───────────────────────────────────────────────────────────────────
static void ProcessFrame()
{
    ++g_logFrame;

    CPlayerPed* player = CWorld::Players[0].m_pPed;
    if (!player) return;

    HandleKeys(player);

    // Observador vanilla: loga estado do motor do jogo (throttled ~2s)
    // Activo sempre (mesmo INACTIVE) para capturar comportamento do NPC
    // de trafego e peds GSF sem o mod interferir — referencia de diagnostico.
    ProcessObserver(player);

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
// Plugin entry point
// ───────────────────────────────────────────────────────────────────
class GroveRecruitStandalone
{
public:
    GroveRecruitStandalone()
    {
        srand((unsigned int)time(NULL));  // seed para modelos aleatorios variados entre sessoes
        LogInit();
        LogEvent("Plugin carregado — grove_recruit_standalone.asi v2.0 (multi-modulo)");
        LogEvent("Teclas: 1=spawn/dismiss 2=carro 3=passageiro 4=modo N=aggro B=driveby");
        LogEvent("Modo inicial: aggr=%d driveMode=%s",
            (int)g_aggressive, DriveModeName(g_driveMode));
        LogEvent("Modulos: log + group + drive + ai + observer");

        Events::gameProcessEvent += []()
        {
            ProcessFrame();
        };
    }
};

static GroveRecruitStandalone g_standalone;
