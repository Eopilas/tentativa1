/*
 * Main.cpp  (grove_recruit_standalone)
 *
 * Ponto de entrada do plugin ASI.  Este ficheiro e intencionalmente slim:
 *   - Definicao dos globals partilhados (declarados extern em grove_recruit_shared.h)
 *   - Utilitarios basicos (IsRecruitValid, IsCarValid, Dist2D, ShowMsg, KeyJustPressed)
 *   - HandleKeys  — interpretacao das teclas 1/2/3/4/N/B + INSERT (menu)
 *   - ProcessFrame — dispatcher de estados + observador vanilla + scan vanilla recruits
 *   - GroveRecruitStandalone — registo dos hooks Events::gameProcessEvent + drawHudEvent
 *
 * Toda a logica modular esta nos outros .cpp:
 *   grove_recruit_log.cpp      — LogInit / LogEvent / LogObsv / GetTaskName / ...
 *   grove_recruit_group.cpp    — AddRecruitToGroup / DismissRecruit / ScanPlayerGroup / ...
 *   grove_recruit_drive.cpp    — SetupDriveMode / ProcessDrivingAI / ProcessMultiRecruitCars / ...
 *   grove_recruit_ai.cpp       — ProcessOnFoot
 *   grove_recruit_observer.cpp — ProcessObserver (vanilla engine observer)
 *   grove_recruit_menu.cpp     — HandleMenuKeys / RenderMenu (menu in-game)
 *
 * TECLAS:
 *   INSERT — Abrir/fechar menu (overlay com todas as opcoes)   [VK 0x2D]
 *   1 — Recrutar (ou dispensar)                               [VK 0x31]
 *   2 — Entrar/sair do carro (TODOS os recrutas)              [VK 0x32]
 *   3 — Jogador como passageiro / sair                        [VK 0x33]
 *   4 — Ciclar modo de conducao                               [VK 0x34]
 *   5 — Forcar refresh do destino por waypoint do mapa        [VK 0x35]
 *   N — Alternar agressividade                                [VK 0x4E]
 *   B — Alternar drive-by (TODOS os recrutas em carros)       [VK 0x42]
 *
 * PASSAGEIROS — COMPORTAMENTO MULTI-RECRUTA:
 *   Quando o jogador entra num carro (sem Key 2 previa):
 *     → Recruta primario (g_recruit) entra como passageiro no carro do jogador (seat 0)
 *     → Recrutas secundarios sem carro proprio entram nos lugares restantes (seats 1,2,...)
 *     → Cada recruta com ridesWithPlayer=true e rastreado por ProcessMultiRecruitCars
 *     → Quando jogador sai, todos os recrutas com ridesWithPlayer recebem LeaveCar
 *   Quando jogador prime Key 3 (DRIVING→PASSENGER no carro do recruta primario):
 *     → Jogador entra como passageiro no g_car do recruta primario (seat 0)
 *     → Recrutas secundarios sem carro proprio entram no mesmo g_car (seats 1,2,...)
 *   Quando jogador prime Key 3 novamente (PASSENGER→DRIVING):
 *     → Jogador sai; recrutas secundarios com ridesWithPlayer recebem LeaveCar
 *
 * MODOS DE CONDUCAO (ciclo via tecla 4 ou menu):
 *   CIVICO_F — MC_ESCORT_REAR_FARAWAY(67) + AVOID_CARS
 *   CIVICO_G — MC_FOLLOWCAR_CLOSE(53)     + AVOID_CARS
 *   CIVICO_H — MC_FOLLOWCAR_FARAWAY(52)   + AVOID_CARS  (melhor combo road-graph)
 *   DIRETO   — MISSION_GOTOCOORDS(8) destino offset atras
 *   PARADO   — MISSION_STOP_FOREVER(11)
 *
 * VANILLA COMPAT:
 *   ScanPlayerGroup() detecta recrutas recrutados pelo metodo vanilla
 *   (tecla Y + respeito) e aplica flags do mod (bNeverLeavesGroup, etc.).
 *   OnPlayerEnterVehicle() activa SIT_IN_LEADER_CAR para todos os membros.
 */

#include "grove_recruit_shared.h"
using namespace plugin;  // Events::gameProcessEvent, etc.

// ───────────────────────────────────────────────────────────────────
// Definicoes dos globals (declarados extern em grove_recruit_shared.h)
// ───────────────────────────────────────────────────────────────────
ModState   g_state       = ModState::INACTIVE;
DriveMode  g_driveMode   = DriveMode::CIVICO_F;
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

bool  g_enterCarAsPassenger  = false;
bool  g_playerWasInVehicle   = false;

bool  g_wasWrongDir         = false;
bool g_wasInvalidLink      = false;
int  g_missionRecoveryTimer = 0;
bool g_slowZoneRestoring   = false;

bool  g_joinedViaAddFollower = false;

int  g_logFrame     = 0;
int  g_logAiFrame   = 0;

int  g_civicRoadSnapTimer = 0;
int  g_observerTimer      = 0;
int  g_invalidLinkCounter = 0;
int  g_scanGroupTimer     = 0;
int  g_closeBlockedTimer  = 0;   // frames consecutivos perto+parado (todos os modos CIVICO)
bool g_closeBlocked       = false; // recruta em modo de espera por obstrucao proxima
int  g_offroadSustainedFrames = 0;  // frames consecutivos em offroad (direct-follow canal)
bool g_wasOffroadDirect       = false; // transicao de direct-follow offroad
int  g_carHealthTimer     = 0;   // timer de restauracao de saude do carro do recruta

// Multi-recruit tracking
TrackedRecruit g_allRecruits[MAX_TRACKED_RECRUITS] = {};
int            g_numAllRecruits = 0;

// Menu state
bool g_menuOpen = false;
int  g_menuSel  = 0;

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
    // ── INSERT: abrir/fechar menu ────────────────────────────────
    if (KeyJustPressed(VK_MENU_OPEN))
    {
        g_menuOpen = !g_menuOpen;
        g_menuSel  = 0;
        LogMenu("KEY INSERT (MENU): %s", g_menuOpen ? "ABERTO" : "FECHADO");
        return;
    }

    // Se menu aberto, delegar input ao menu (HandleMenuKeys)
    if (g_menuOpen)
    {
        HandleMenuKeys(player);
        return;
    }
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
            g_invalidLinkCounter = 0;
            g_scanGroupTimer     = 0;
            g_closeBlockedTimer  = 0;
            g_closeBlocked       = false;
            g_offroadSustainedFrames = 0;
            g_wasOffroadDirect   = false;
            g_carHealthTimer     = 0;

            LogEvent("KEY 1 (RECRUIT): flags pre-grupo — bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d initTimer=%d",
                (int)g_recruit->bNeverLeavesGroup,
                (int)g_recruit->bKeepTasksAfterCleanUp,
                (int)g_recruit->bDoesntListenToPlayerGroupCommands,
                g_initialFollowTimer);

            // AddRecruitToGroup define m_acquaintance.m_nRespect (ver ACQUAINTANCE_FIX no log)
            AddRecruitToGroup(player);
            ShowMsg("~g~Recruta activo! [INSERT=menu, 1=dispen, 2=carro, 4=modo]");
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

            // ── Multi-recruit: tambem mandar recrutas secundarios sair dos carros ──
            int nLeft = 0;
            for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
            {
                TrackedRecruit& tr = g_allRecruits[i];
                if (!tr.ped || tr.ped == g_recruit) continue;

                // Condutor com carro proprio
                if (tr.car && tr.ped->bInVehicle &&
                    CPools::ms_pVehiclePool->IsObjectValid(tr.car))
                {
                    CTaskComplexLeaveCar* pLeave = new CTaskComplexLeaveCar(tr.car, 0, 0, true, false);
                    tr.ped->m_pIntelligence->m_TaskMgr.SetTask(pLeave, TASK_PRIMARY_PRIMARY, true);
                    LogTask("[recr:%d] CTaskComplexLeaveCar (condutor) ped=%p carro=%p", i, (void*)tr.ped, (void*)tr.car);
                    tr.car = nullptr; tr.enterTimer = 0; tr.driveby = false; tr.stuckTimer = 0;
                    ++nLeft;
                }
                // Passageiro no carro do jogador/recruta
                else if (tr.ridesWithPlayer && tr.ped->bInVehicle && tr.ped->m_pVehicle &&
                         CPools::ms_pVehiclePool->IsObjectValid(tr.ped->m_pVehicle))
                {
                    CTaskComplexLeaveCar* pLeave =
                        new CTaskComplexLeaveCar(tr.ped->m_pVehicle, 0, 0, true, false);
                    tr.ped->m_pIntelligence->m_TaskMgr.SetTask(pLeave, TASK_PRIMARY_PRIMARY, true);
                    LogTask("[recr:%d] CTaskComplexLeaveCar (passageiro) ped=%p veh=%p", i, (void*)tr.ped, (void*)tr.ped->m_pVehicle);
                    tr.ridesWithPlayer = false; tr.enterTimer = 0; tr.stuckTimer = 0;
                    ++nLeft;
                }
            }
            if (nLeft > 0)
                LogEvent("KEY 2: DRIVING->ON_FOOT multi — %d recrutas secundarios a sair dos carros/passageiros", nLeft);

            ShowMsg("~y~Recruta(s) a sair do(s) carro(s). [2=retomar]");
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
                CVehicle* excl[] = { playerCar };
                targetCar = FindNearestFreeCar(searchPos, excl, playerCar ? 1 : 0);
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
            g_enterCarTimer = ENTER_CAR_DRIVER_TIMEOUT;
            g_civicRoadSnapTimer = 0;
            LogTask("CTaskComplexEnterCarAsDriver emitido para carro %p timeout=%d frames (%.0fs)",
                static_cast<void*>(targetCar), ENTER_CAR_DRIVER_TIMEOUT,
                ENTER_CAR_DRIVER_TIMEOUT / 60.0f);
            LogEvent("KEY 2: estado -> ENTER_CAR (carro=%p)", static_cast<void*>(targetCar));

            // ── Multi-recruit: enviar recrutas secundarios para os seus proprios carros ──
            AssignCarsToAllRecruits(player);

            ShowMsg("~y~Recruta(s) a entrar em carros... [3=passageiro, 4=modo]");
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
            // Jogador entra no carro do recruta como passageiro (seat 0)
            CTaskComplexEnterCarAsPassenger* pTask =
                new CTaskComplexEnterCarAsPassenger(g_car, 0, false);
            player->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);
            g_state = ModState::PASSENGER;
            LogEvent("KEY 3: DRIVING -> PASSENGER carro=%p", static_cast<void*>(g_car));

            // ── Recrutas secundarios sem carro: entrar no mesmo carro ──
            // Seats restantes: g_car->m_nMaxPassengers - m_nNumPassengers - 1 (jogador)
            {
                int seatsLeft = (int)g_car->m_nMaxPassengers
                              - (int)g_car->m_nNumPassengers
                              - 1;  // reservar um para o jogador
                int seat = 1;       // seat 0 = jogador
                int nSec = 0;
                for (int si = 0; si < MAX_TRACKED_RECRUITS && seatsLeft > 0; ++si)
                {
                    TrackedRecruit& tr = g_allRecruits[si];
                    if (!tr.ped || tr.ped == g_recruit)                             continue;
                    if (!CPools::ms_pPedPool->IsObjectValid(tr.ped))               continue;
                    if (!tr.ped->IsAlive())                                         continue;
                    if (tr.ped->bInVehicle)                                         continue;
                    if (tr.car || tr.ridesWithPlayer)                               continue;

                    CTaskComplexEnterCarAsPassenger* pSecTask =
                        new CTaskComplexEnterCarAsPassenger(g_car, seat, false);
                    tr.ped->m_pIntelligence->m_TaskMgr.SetTask(pSecTask, TASK_PRIMARY_PRIMARY, true);

                    tr.ridesWithPlayer = true;
                    tr.enterTimer      = ENTER_CAR_PASSENGER_TIMEOUT;
                    tr.stuckTimer      = 0;
                    --seatsLeft;
                    ++nSec;

                    LogRecruit("[recr:%d] KEY3_PASSENGER_SECONDARY: ped=%p -> g_car=%p seat=%d",
                        si, (void*)tr.ped, (void*)g_car, seat);
                    ++seat;
                }
                LogEvent("KEY 3: PASSENGER_SECONDARY: %d recrutas secundarios enviados para g_car=%p",
                    nSec, static_cast<void*>(g_car));
            }

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

            // ── Recrutas secundarios que estavam neste carro como passageiros: sair ──
            {
                int nExit = 0;
                for (int si = 0; si < MAX_TRACKED_RECRUITS; ++si)
                {
                    TrackedRecruit& tr = g_allRecruits[si];
                    if (!tr.ped || tr.ped == g_recruit) continue;
                    if (!tr.ridesWithPlayer)             continue;

                    tr.ridesWithPlayer = false;
                    tr.enterTimer      = 0;
                    tr.stuckTimer      = 0;

                    if (tr.ped->bInVehicle && tr.ped->m_pVehicle &&
                        CPools::ms_pVehiclePool->IsObjectValid(tr.ped->m_pVehicle))
                    {
                        CTaskComplexLeaveCar* pLeave =
                            new CTaskComplexLeaveCar(tr.ped->m_pVehicle, 0, 0, true, false);
                        tr.ped->m_pIntelligence->m_TaskMgr.SetTask(pLeave, TASK_PRIMARY_PRIMARY, true);
                        LogRecruit("[recr:%d] KEY3_EXIT_SECONDARY: ped=%p LeaveCar emitido (veh=%p)",
                            si, (void*)tr.ped, (void*)tr.ped->m_pVehicle);
                    }
                    else
                    {
                        LogRecruit("[recr:%d] KEY3_EXIT_SECONDARY: ped=%p ja a pe",
                            si, (void*)tr.ped);
                    }
                    ++nExit;
                }
                if (nExit > 0)
                    LogEvent("KEY 3: EXIT_SECONDARY: %d recrutas secundarios saem do carro", nExit);
            }

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
        // So aplicar SetupDriveMode se recruta esta efectivamente a conduzir
        if (g_state == ModState::DRIVING || g_state == ModState::PASSENGER)
            SetupDriveMode(player, g_driveMode);

        static const char* const MODE_NAMES[] = {
            "~g~CIVICO-F: EscortRear+AvoidCars    [4=prox]",
            "~g~CIVICO-G: FollowClose+AvoidCars   [4=prox]",
            "~g~CIVICO-H: FollowCar+AvoidCars     [4=prox]",
            "~b~DIRETO:   GotoCoords offset       [4=prox]",
            "~r~PARADO:   StopForever             [4=prox]",
        };
        ShowMsg(MODE_NAMES[static_cast<int>(g_driveMode)]);
        return;
    }

    // ── 5: Waypoint mode — PASSENGER refresh / DRIVING→WAYPOINT_SOLO ─
    if (KeyJustPressed(VK_WAYPOINT))
    {
        if (g_state == ModState::PASSENGER && IsCarValid())
        {
            // ProcessDrivingAI usa g_diretoTimer para atualizar o destino.
            // Forcar 0 aqui dispara refresh imediato no mesmo ciclo.
            g_diretoTimer = 0;
            LogKey("KEY 5 (WAYPOINT): refresh imediato solicitado (estado=%s car=%p)",
                StateName(g_state), static_cast<void*>(g_car));
            ShowMsg("~g~Waypoint refresh solicitado.");
        }
        else if (g_state == ModState::DRIVING && IsCarValid())
        {
            // v4.4: DRIVING → WAYPOINT_SOLO — recruta conduz sozinho ao waypoint
            g_state = ModState::WAYPOINT_SOLO;
            g_diretoTimer = 0;  // forcar refresh imediato do destino
            LogEvent("KEY 5: DRIVING -> WAYPOINT_SOLO (recruta conduz sozinho ao waypoint)");
            ShowMsg("~g~Recruta conduz sozinho ao waypoint. [2=voltar ao modo seguimento]");
        }
        else if (g_state == ModState::WAYPOINT_SOLO && IsCarValid())
        {
            // WAYPOINT_SOLO → DRIVING — voltar ao modo seguimento
            g_state = ModState::DRIVING;
            LogEvent("KEY 5: WAYPOINT_SOLO -> DRIVING (voltar ao modo seguimento)");
            ShowMsg("~g~Recruta voltou ao modo seguimento.");
        }
        else
        {
            LogKey("KEY 5 (WAYPOINT): ignorado (estado=%s)", StateName(g_state));
            ShowMsg("~y~Use [5] em modo passageiro ou conducao.");
        }
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
    if (KeyJustPressed(VK_DRIVEBY) && g_state != ModState::INACTIVE)
    {
        // Drive-by do primario: so disponivel em PASSENGER (jogador esta no carro do recruta)
        if (g_state == ModState::PASSENGER)
        {
            g_driveby = !g_driveby;
            LogKey("KEY B (DRIVEBY primario): driveby=%d", (int)g_driveby);
        }

        // Drive-by de todos os recrutas secundarios em conducao: toggle global
        int numToggled = 0;
        for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
        {
            TrackedRecruit& tr = g_allRecruits[i];
            if (!tr.ped || tr.ped == g_recruit) continue;
            if (!tr.car) continue;
            if (!tr.ped->bInVehicle) continue;
            tr.driveby = !tr.driveby;
            LogKey("KEY B (DRIVEBY recr:%d): driveby=%d ped=%p", i, (int)tr.driveby, (void*)tr.ped);
            ++numToggled;
        }

        bool anyActive = g_driveby;
        for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
            if (g_allRecruits[i].ped && g_allRecruits[i].car && g_allRecruits[i].driveby)
                anyActive = true;

        LogKey("KEY B (DRIVEBY): primario=%d secundarios_toggled=%d anyActive=%d",
            (int)g_driveby, numToggled, (int)anyActive);
        ShowMsg(anyActive ? "~r~Drive-by ACTIVO" : "~y~Drive-by INACTIVO");
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

    // Scan periodico do grupo para recrutas vanilla/novos membros
    if (++g_scanGroupTimer >= SCAN_GROUP_INTERVAL)
    {
        g_scanGroupTimer = 0;
        ScanPlayerGroup(player);
    }

    // AI simplificado para recrutas secundarios em conducao propria (multi-car)
    ProcessMultiRecruitCars(player);

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
    case ModState::WAYPOINT_SOLO:  // v4.4: waypoint solo usa mesma logica que DRIVING
        ProcessDriving(player);
        break;
    case ModState::PASSENGER:
        ProcessPassenger(player);
        break;
    case ModState::RIDING:
        ProcessRiding(player);
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
        srand((unsigned int)time(NULL));
        LogInit();
        LogEvent("Plugin carregado — grove_recruit_standalone.asi v4.6 (CIVICO_GOTOCOORDS: sem road-graph, curve brake, speed-match; WAYPOINT_SOLO: recruta ao waypoint independente)");
        LogEvent("Teclas: 1=spawn/dismiss 2=carros(todos) 3=passageiro 4=modo N=aggro B=driveby(todos) INSERT=menu");
        LogEvent("Modo inicial: aggr=%d driveMode=%s",
            (int)g_aggressive, DriveModeName(g_driveMode));
        LogEvent("Modulos: log + group + drive + ai + observer + menu");
        LogEvent("Multi-recruit: scan vanilla activo (a cada %d frames = %.1fs); multi-car activo",
            SCAN_GROUP_INTERVAL, SCAN_GROUP_INTERVAL / 60.0f);

        Events::gameProcessEvent += []()
        {
            ProcessFrame();
        };

        // Render hook: desenhar menu HUD durante frame de jogo activo
        Events::drawHudEvent += []()
        {
            CPlayerPed* p = CWorld::Players[0].m_pPed;
            if (p && g_menuOpen)
                RenderMenu(p);
        };
    }
};

static GroveRecruitStandalone g_standalone;
