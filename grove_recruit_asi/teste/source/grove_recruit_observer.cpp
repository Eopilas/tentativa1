/*
 * grove_recruit_observer.cpp
 *
 * Sistema de observacao do motor do jogo (OBSV).
 *
 * ProcessObserver() e chamado a cada OBSERVER_INTERVAL frames (~2s).
 * Loga o estado de NPCs vanilla SEM interferencia do mod, permitindo
 * comparar o comportamento do recruta com o da IA nativa do GTA SA.
 *
 * O que e observado:
 *
 *   1. NearestTrafficCar
 *      O veiculo de trafego (NPC nao-jogador, nao-recruta) mais proximo
 *      do jogador. Loga o estado completo do CAutoPilot:
 *        mission, driveStyle, speed_ap, physSpeed, heading, targetH,
 *        deltaH, speedMult, linkId, areaId, lane, straight, dest, targetCar.
 *      USE: comparar mission/heading/linkId do recruta com o NPC vanilla
 *           para perceber se o recruta esta a usar o road-graph correctamente.
 *
 *   2. NearestGSFPed
 *      O ped GSF (pedType=8) mais proximo do jogador que NAO e o recruta
 *      e NAO esta em veiculo. Loga todos os slots de tarefa do CTaskManager.
 *      USE: perceber quais as tarefas normais de um ped GSF vanilla (1207,
 *           264, 1500, etc.) para comparar com o recruta.
 *
 *   3. PlayerGroupState
 *      Estado do grupo do jogador (todos os membros).
 *      Loga: slot, ped ponteiro, pedType, tasks, distancia ao jogador.
 *      USE: verificar se o recruta esta no grupo e a que distancia.
 *
 *   4. PlayerState
 *      Estado do jogador: a pe (velocidade, tasks) ou em carro (CAutoPilot).
 *      USE: referencia para o comportamento do jogador (o "lider").
 *
 * SEGURANCA: todas as iteracoes verificam ponteiros nulos e limites validos.
 */
#include "grove_recruit_shared.h"

// ───────────────────────────────────────────────────────────────────
// LogAutoPilot helper — loga estado CAutoPilot de um veiculo arbitrario
// ───────────────────────────────────────────────────────────────────
static void LogAutoPilotState(const char* label, CVehicle* veh, CVector const& playerPos)
{
    if (!veh) return;

    CAutoPilot& ap = veh->m_autoPilot;
    CVector vPos   = veh->GetPosition();
    float dist     = Dist2D(vPos, playerPos);
    float physSpeed = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
    float vH        = veh->GetHeading();

    // targetHeading via ClipTargetOrientationToLink (road-graph)
    // Nota: apenas chamado com linkId valido — com linkId invalido o resultado
    // seria lixo e causaria falsos WRONG_DIR no log.
    float targetH   = vH;
    unsigned linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
    if (linkId <= MAX_VALID_LINK_ID &&
        (ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId != 0 ||
         ap.m_nCurrentPathNodeInfo.m_nAreaId != 0))
    {
        CVector fwd = veh->GetForward();
        CCarCtrl::ClipTargetOrientationToLink(
            veh, ap.m_nCurrentPathNodeInfo, ap.m_nCurrentLane,
            &targetH, fwd.x, fwd.y);
    }

    float dH = targetH - vH;
    while (dH >  3.14159f) dH -= 6.28318f;
    while (dH < -3.14159f) dH += 6.28318f;
    float absDH   = dH < 0.0f ? -dH : dH;
    // Se linkId invalido, nao reportar WRONG_DIR (targetH = heading real, deltaH = 0)
    const char* dirLabel = (linkId > MAX_VALID_LINK_ID)       ? "linkId_invalido" :
                           (absDH > WRONG_DIR_THRESHOLD_RAD)  ? "WRONG_DIR!"      :
                           (absDH > MISALIGNED_THRESHOLD_RAD) ? "desalinhado"     : "OK";
    float speedMult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);

    LogObsv("%s: veh=%p dist=%.1fm mission=%d(%s) driveStyle=%d(%s) tempAction=%d(%s) "
            "speed_ap=%d physSpeed=%.0fkmh heading=%.3f targetH=%.3f "
            "deltaH=%.3f(%s) speedMult=%.2f straight=%d lane=%d "
            "linkId=%u(%s) areaId=%u dest=(%.1f,%.1f,%.1f) targetCar=%p",
        label,
        static_cast<void*>(veh), dist,
        (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
        (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
        (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
        (int)ap.m_nCruiseSpeed, physSpeed,
        vH, targetH, dH,
        dirLabel,
        speedMult,
        (int)ap.m_nStraightLineDistance, (int)ap.m_nCurrentLane,
        linkId, (linkId <= MAX_VALID_LINK_ID) ? "OK" : "INVALID",
        (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
        ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z,
        (void*)ap.m_pTargetCar);
}

// ───────────────────────────────────────────────────────────────────
// LogPedTasks helper — loga tasks de um ped arbitrario
// ───────────────────────────────────────────────────────────────────
static void LogPedTasks(const char* label, CPed* ped, CVector const& playerPos)
{
    if (!ped) return;

    CVector pPos  = ped->GetPosition();
    float   dist  = Dist2D(pPos, playerPos);
    char    taskBuf[384] = {};
    {
        CTaskManager& tm = ped->m_pIntelligence->m_TaskMgr;
        int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
        BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
    }
    CTask* activeT = ped->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
    int    activeId = activeT ? (int)activeT->GetId() : -1;
    CTask* primaryT = ped->m_pIntelligence->m_TaskMgr.GetActiveTask();
    int    primaryId = primaryT ? (int)primaryT->GetId() : -1;

    LogObsv("%s: ped=%p pedType=%d dist=%.1fm pos=(%.1f,%.1f,%.1f) "
            "bInVeh=%d activeTask=%d(%s) primaryTask=%d(%s) tasks=%s",
        label,
        static_cast<void*>(ped), (int)ped->m_nPedType,
        dist, pPos.x, pPos.y, pPos.z,
        (int)ped->bInVehicle,
        activeId, GetTaskName(activeId),
        primaryId, GetTaskName(primaryId),
        taskBuf);
}

// ───────────────────────────────────────────────────────────────────
// ProcessObserver — observacao vanilla (throttled via g_observerTimer)
// ───────────────────────────────────────────────────────────────────
void ProcessObserver(CPlayerPed* player)
{
    if (!player) return;

    if (++g_observerTimer < OBSERVER_INTERVAL) return;
    g_observerTimer = 0;

    CVector playerPos = player->GetPosition();
    CVehicle* playerVeh = player->bInVehicle ? player->m_pVehicle : nullptr;

    LogObsv("=== OBSERVER_TICK frame=%d state=%s mode=%s ===",
        g_logFrame, StateName(g_state), DriveModeName(g_driveMode));

    // ─── 0. [WORLD] log a cada 300 frames (~5s) ──────────────────
    // Estado global do motor: timer, passo de frame, etc.
    if (g_logFrame % 300 == 0)
    {
        LogWorld("CTimer: ms_nTimeInMilliseconds=%u ms_fTimeStep=%.4f",
            CTimer::ms_nTimeInMilliseconds, CTimer::ms_fTimeStep);
    }

    // ─── 1. NearestTrafficCar ────────────────────────────────────
    // Procura o veiculo de trafego NPC mais proximo do jogador.
    // Exclui: carro do jogador, carro do recruta, carros sem condutor.
    {
        CVehicle* nearestCar  = nullptr;
        float     nearestDist = 80.0f;  // raio maximo de busca

        for (CVehicle* veh : *CPools::ms_pVehiclePool)
        {
            if (!veh)                       continue;
            if (veh == playerVeh)           continue;
            if (veh == g_car)               continue;
            if (!veh->m_pDriver)            continue;
            if (veh->m_pDriver == player)   continue;
            // Apenas veiculos terrestres (subClass 0=car, 1=bike, 2=bicycle)
            if (veh->m_nVehicleSubClass > 2) continue;

            // Excluir condutor que e o recruta
            if (g_recruit && veh->m_pDriver == g_recruit) continue;

            float d = Dist2D(veh->GetPosition(), playerPos);
            if (d < nearestDist)
            {
                nearestDist = d;
                nearestCar  = veh;
            }
        }

        if (nearestCar)
        {
            LogAutoPilotState("NearestTrafficCar", nearestCar, playerPos);
        }
        else
        {
            LogObsv("NearestTrafficCar: nenhum NPC de trafego num raio de 80m");
        }
    }

    // ─── 2. NearestGSFPed ────────────────────────────────────────
    // Ped GSF (pedType=8) a pe mais proximo do jogador (excluindo recruta).
    {
        CPed*  nearestGSF  = nullptr;
        float  nearestDist = 80.0f;

        for (CPed* ped : *CPools::ms_pPedPool)
        {
            if (!ped)                       continue;
            if (ped == player)              continue;
            if (ped == g_recruit)           continue;
            if (ped->bInVehicle)            continue;
            if ((int)ped->m_nPedType != 8)  continue;

            float d = Dist2D(ped->GetPosition(), playerPos);
            if (d < nearestDist)
            {
                nearestDist = d;
                nearestGSF  = ped;
            }
        }

        if (nearestGSF)
        {
            LogPedTasks("NearestGSFPed", nearestGSF, playerPos);
        }
        else
        {
            LogObsv("NearestGSFPed: nenhum ped GSF a pe num raio de 80m");
        }
    }

    // ─── 3. PlayerGroupState ─────────────────────────────────────
    // Todos os membros do grupo do jogador (slots 0..6).
    {
        unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
        if (groupIdx < 8u)
        {
            CPedGroupMembership& membership =
                CPedGroups::ms_groups[groupIdx].m_groupMembership;
            int memberCount = membership.CountMembersExcludingLeader();

            LogObsv("PlayerGroup: groupIdx=%u membros=%d FindMaxGroupMembers=%d respect=%.0f",
                groupIdx, memberCount, FindMaxGroupMembers(),
                CStats::GetStatValue(STAT_RESPECT));

            for (int i = 0; i < 7; ++i)
            {
                CPed* member = membership.m_apMembers[i];
                if (!member) continue;
                LogPedTasks("PlayerGroup.member", member, playerPos);
            }
        }
        else
        {
            LogObsv("PlayerGroup: groupIdx=%u invalido", groupIdx);
        }
    }

    // ─── 4. PlayerState ──────────────────────────────────────────
    // Estado do jogador (a pe ou em carro).
    {
        if (playerVeh)
        {
            LogAutoPilotState("PlayerCar", playerVeh, playerPos);
        }
        else
        {
            char playerTaskBuf[384] = {};
            {
                CTaskManager& tm = player->m_pIntelligence->m_TaskMgr;
                int w = BuildPrimaryTaskBuf(playerTaskBuf, (int)sizeof(playerTaskBuf), tm);
                BuildSecondaryTaskBuf(playerTaskBuf, (int)sizeof(playerTaskBuf), w, tm);
            }
            float physSpeed = player->m_vecMoveSpeed.Magnitude() * 180.0f;
            LogObsv("PlayerOnFoot: physSpeed=%.0fkmh tasks=%s",
                physSpeed, playerTaskBuf);
        }
    }

    // ─── 5. RecruitState (referencia cruzada) ────────────────────
    // Se o recruta existir, loga o seu estado para comparacao directa.
    if (IsRecruitValid())
    {
        if (g_state == ModState::DRIVING && IsCarValid())
        {
            LogAutoPilotState("RecruitCar(OBSV)", g_car, playerPos);
        }
        else if (g_state == ModState::ON_FOOT)
        {
            LogPedTasks("RecruitPed(OBSV)", g_recruit, playerPos);
        }
    }

    LogObsv("=== OBSERVER_END ===");
}
