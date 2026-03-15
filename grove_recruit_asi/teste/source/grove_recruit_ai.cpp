/*
 * grove_recruit_ai.cpp
 *
 * IA de seguimento a pe (ProcessOnFoot).
 *
 * Funcionalidades:
 *   - Rastreio em tempo real de mudancas de tarefa (TASK_CHANGE)
 *   - GANG_SPAWN_ANIM_END: apos TASK_SIMPLE_ANIM(400) de spawn terminar,
 *     limpa slot[2], garante BE_IN_GROUP(243) em slot[3] e re-emite follow
 *   - POST_FOLLOW_CHECK: verifica tarefa 3 frames apos TellGroupFollow
 *     e, se nao estiver em estado de follow, tenta FOLLOW_FALLBACK via
 *     EnsureBeInGroup + TellGroupFollowWithRespect
 *   - Revalidacao periodica do grupo (GROUP_RESCAN_INTERVAL)
 *   - Burst inicial de follow (5s apos spawn)
 *   - Modo passivo (g_aggressive=false): re-emite follow a cada 300ms
 *   - Dump AI throttled (~2s) com todos os slots de tarefa
 */
#include "grove_recruit_shared.h"

// ───────────────────────────────────────────────────────────────────
// ProcessOnFoot
// ───────────────────────────────────────────────────────────────────
void ProcessOnFoot(CPlayerPed* player)
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
    //   203 -> 1207  = follow bem aceite (OK)
    //   1207 -> 203  = follow foi cancelado (problema!)
    //   203 -> 264   = BE_IN_GROUP mas nao follow (problema parcial)
    //   902 / 400    = estados transitórios de spawn (normais)
    {
        CTask* pt  = g_recruit->m_pIntelligence->m_TaskMgr.GetSimplestActiveTask();
        int    tid = pt ? (int)pt->GetId() : -1;
        CTask* at  = g_recruit->m_pIntelligence->m_TaskMgr.GetActiveTask();
        int    atid = at ? (int)at->GetId() : -1;

        if (g_prevRecruitTaskId != -999 && tid != g_prevRecruitTaskId)
        {
            LogTask("TASK_CHANGE: %d -> %d  (%s -> %s)",
                g_prevRecruitTaskId, tid,
                GetTaskName(g_prevRecruitTaskId),
                GetTaskName(tid));

            // Quando TASK_SIMPLE_ANIM(400) de spawn termina, limpar slot[2]=EVENT_NONTEMP
            // e re-emitir follow.
            // TASK_COMPLEX_GANG_JOIN_RESPOND(1219) pode persistir no slot[2] mesmo depois
            // de bKeepTasksAfterCleanUp=1 (flag que impede limpeza auto).
            // ClearTaskEventResponse (0x681BD0) limpa slot[1] e slot[2].
            if (g_prevRecruitTaskId == 400 /* TASK_SIMPLE_ANIM (spawn anim) */ && tid != 400)
            {
                LogTask("GANG_SPAWN_ANIM_END: spawn concluido (tid=%d %s) — limpando slots[1-2] e re-emitindo follow",
                    tid, GetTaskName(tid));
                // Limpar tarefas de spawn residuais em EVENT_NONTEMP (slot[2])
                ClearTaskEventResponse(&g_recruit->m_pIntelligence->m_TaskMgr);
                // Garantir BE_IN_GROUP em slot[3] (pode ter sido removido durante o spawn)
                unsigned int gIdxSpawn = player->m_pPlayerData->m_nPlayerGroup;
                if (EnsureBeInGroup(g_recruit, gIdxSpawn))
                    LogTask("GANG_SPAWN_ANIM_END: BE_IN_GROUP(243) re-atribuido a slot[3]");
                g_postFollowTimer   = 0;  // cancela check pendente
                g_postFollowRetries = 0;  // reset contador
                TellGroupFollowWithRespect(player, g_aggressive, true);
            }
        }
        g_prevRecruitTaskId = tid;

        // ── POST_FOLLOW_CHECK (3 frames diferida) ─────────────────
        // Confirma se a tarefa de follow foi aceite 3 frames apos TellGroupFollow.
        // followOk: tarefas validas de follow em curso.
        //   1207 = TASK_COMPLEX_GANG_FOLLOWER    (follow vanilla de gang)
        //   1500 = TASK_GROUP_FOLLOW_LEADER_ANY_MEANS
        //   913  = TASK_COMPLEX_FOLLOW_LEADER_IN_FORMATION (formacao vanilla)
        //   243  = TASK_COMPLEX_BE_IN_GROUP  (wrapper de grupo; sub-tarefa pode ser qualquer coisa)
        // transient: estados normais durante o spawn — aguardar sem contar como falha.
        //   400  = TASK_SIMPLE_ANIM         (animacao de spawn em curso)
        //   900  = TASK_SIMPLE_GO_TO_POINT  (navegacao 1-frame para o follow)
        //   902  = TASK_SIMPLE_ACHIEVE_HEADING (orientacao 1-frame)
        //   1219 = TASK_COMPLEX_GANG_JOIN_RESPOND (wrapper de spawn no slot[2])
        if (g_postFollowTimer > 0)
        {
            --g_postFollowTimer;
            if (g_postFollowTimer == 0)
            {
                // Verificar AMBAS simplest (folha) E active (slot[3]=PRIMARY).
                // BE_IN_GROUP(243) em slot[3] e OK: sub-tarefa pode ser GO_TO_POINT/ACHIEVE_HEADING.
                bool followOk = (tid  == 1207 || tid  == 1500 || tid  == 913 || tid  == 243) ||
                                (atid == 1207 || atid == 1500 || atid == 913 || atid == 243);
                // Estados transitórios de spawn — nao e erro, apenas aguardar.
                // 1219=TASK_COMPLEX_GANG_JOIN_RESPOND: wrapper de spawn activo no slot[2].
                bool transient = (tid == 902 || tid == 400 || tid == 900) || (atid == 1219 || atid == 400);

                LogTask("POST_FOLLOW_CHECK(%d/%d): activeTask=%d (%s) primaryTask=%d (%s) — %s",
                    g_postFollowRetries + 1, MAX_FOLLOW_FALLBACK_RETRIES,
                    tid, GetTaskName(tid),
                    atid, GetTaskName(atid),
                    followOk   ? "follow OK" :
                    transient  ? "estado_transitorio(spawn) — aguardando" :
                                 "follow nao confirmado");

                if (followOk)
                {
                    // Follow OK: reset retries
                    g_postFollowRetries = 0;
                }
                else if (!transient)
                {
                    if (g_postFollowRetries < MAX_FOLLOW_FALLBACK_RETRIES)
                    {
                        ++g_postFollowRetries;
                        // Fallback: garantir BE_IN_GROUP em slot[3] + re-emitir GATHER event
                        // via TellGroupFollowWithRespect (que chama TellGroupToStartFollowingPlayer
                        // com arg0=aggressive=true, disparando CEventPlayerCommandToGroup GATHER →
                        // ComputeResponseGather → SeekEntity em m_PedTaskPairs[recruit]).
                        // BE_IN_GROUP em slot[3] consome SeekEntity via GetTaskMain(recruit).
                        unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
                        bool beInFixed = EnsureBeInGroup(g_recruit, groupIdx);
                        LogTask("FOLLOW_FALLBACK(%d/%d): EnsureBeInGroup=%s + TellGroupFollow "
                                "activeTask=%d(%s) primaryTask=%d(%s)",
                            g_postFollowRetries, MAX_FOLLOW_FALLBACK_RETRIES,
                            beInFixed ? "atribuido" : "ja_presente",
                            tid, GetTaskName(tid), atid, GetTaskName(atid));
                        TellGroupFollowWithRespect(player, g_aggressive, false);
                        // Re-armar check em mais 3 frames para confirmar
                        g_postFollowTimer = 3;
                    }
                    else
                    {
                        LogWarn("FOLLOW_FALLBACK: limite %d tentativas atingido (activeTask=%d %s primaryTask=%d %s) "
                                "— aguardando RESCAN/burst",
                            MAX_FOLLOW_FALLBACK_RETRIES, tid, GetTaskName(tid), atid, GetTaskName(atid));
                        g_postFollowRetries = 0;
                        // Nao re-armar: RESCAN (120fr) e burst tratam do follow
                    }
                }
                // se transient: nao incrementar retries; nao re-armar (follow chegara sozinho)
            }
        }
    }

    // ── Revalidacao periodica do grupo (a cada 2s ~ 120 frames) ──
    if (++g_groupRescanTimer >= GROUP_RESCAN_INTERVAL)
    {
        g_groupRescanTimer = 0;

        int slot = FindRecruitMemberID(player);
        if (slot < 0)
        {
            LogEvent("ProcessOnFoot: RESCAN — recruta nao esta no grupo (dismiss nativo detectado)");
            DismissRecruit(player);
            ShowMsg("~y~Recruta dispensado do grupo.");
            return;
        }

        CVector rPos  = g_recruit->GetPosition();
        CVector pPos  = player->GetPosition();
        float   dist  = Dist2D(rPos, pPos);
        char rescanTaskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(rescanTaskBuf, (int)sizeof(rescanTaskBuf), tm);
            BuildSecondaryTaskBuf(rescanTaskBuf, (int)sizeof(rescanTaskBuf), w, tm);
        }
        LogGroup("ProcessOnFoot: RESCAN slot=%d dist=%.1fm pos=(%.1f,%.1f,%.1f) "
                 "bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d bInVeh=%d "
                 "aggr=%d initTimer=%d pedType=%d respect=%.0f tasks=%s",
            slot, dist, rPos.x, rPos.y, rPos.z,
            (int)g_recruit->bNeverLeavesGroup,
            (int)g_recruit->bKeepTasksAfterCleanUp,
            (int)g_recruit->bDoesntListenToPlayerGroupCommands,
            (int)g_recruit->bInVehicle,
            (int)g_aggressive,
            g_initialFollowTimer,
            (int)g_recruit->m_nPedType,
            CStats::GetStatValue(STAT_RESPECT),
            rescanTaskBuf);

        AddRecruitToGroup(player);
    }

    // ── Burst inicial + Modo PASSIVO: re-emitir follow a cada 18 frames ──
    // Nao emitir follow se jogador esta num veiculo: interfere com a entrada
    // automatica como passageiro (task sobrepoe-se a EnterCarAsPassenger).
    bool doFollow = ((g_initialFollowTimer > 0) || (!g_aggressive))
                    && !player->bInVehicle;
    if (g_initialFollowTimer > 0)
        --g_initialFollowTimer;

    if (doFollow)
    {
        if (++g_passiveTimer >= 18)
        {
            g_passiveTimer = 0;
            bool burstActive = (g_initialFollowTimer > 0);
            TellGroupFollowWithRespect(player, g_aggressive, burstActive);
            if (burstActive)
                LogTask("TellGroupFollowWithRespect (burst inicial) initTimer=%d", g_initialFollowTimer);
        }
    }
    else
    {
        g_passiveTimer = 0;
    }

    // ── AUTO-ENTER como passageiro quando jogador entra num veiculo ──
    // Comportamento vanilla: recrutas de gang entram automaticamente no carro
    // do jogador como passageiros quando este entra num veiculo.
    // Condicoes: jogador acabou de entrar num veiculo (transicao), o veiculo
    // tem lugares de passageiro livres, e o recruta esta proximo (<60m).
    // Restrito a carros(subClass=0) e motas(subClass=1); exclui avioes,
    // helicopteros, barcos, etc. onde passageiros de gang nao fazem sentido.
    {
        bool prevInVehicle      = g_playerWasInVehicle;
        bool playerNowInVehicle = player->bInVehicle;
        bool justEnteredVehicle = playerNowInVehicle && !prevInVehicle;
        bool justExitedVehicle  = !playerNowInVehicle && prevInVehicle;
        g_playerWasInVehicle    = playerNowInVehicle;

        if (justEnteredVehicle)
        {
            CVehicle* playerVeh = player->m_pVehicle;

            // Activar SIT_IN_LEADER_CAR para TODOS os membros do grupo:
            // membros vanilla entram automaticamente no carro do jogador.
            OnPlayerEnterVehicle(player);

            // So carros (0) e motas (1) suportam passageiros de gang
            bool vehSupported = (playerVeh->m_nVehicleSubClass <= 1);
            // Verificar se ha lugares livres
            bool hasSeat = vehSupported
                           && (playerVeh->m_nMaxPassengers > 0)
                           && (playerVeh->m_nNumPassengers < playerVeh->m_nMaxPassengers);

            if (hasSeat)
            {
                float distToVeh = Dist2D(g_recruit->GetPosition(), playerVeh->GetPosition());
                if (distToVeh <= RECRUIT_AUTO_ENTER_DIST)
                {
                    CTaskComplexEnterCarAsPassenger* pTask =
                        new CTaskComplexEnterCarAsPassenger(playerVeh, 0, false);
                    g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                        pTask, TASK_PRIMARY_PRIMARY, true);
                    g_car               = playerVeh;
                    g_enterCarAsPassenger = true;
                    g_enterCarTimer     = ENTER_CAR_TIMEOUT;
                    g_state             = ModState::ENTER_CAR;
                    LogEvent("AUTO_ENTER_PASSENGER: veh=%p dist=%.1fm -> ENTER_CAR "
                             "[SIT_IN_CAR activado para outros membros]",
                        static_cast<void*>(playerVeh), distToVeh);
                    ShowMsg("~g~Recruta a entrar no carro...");
                }
                else
                {
                    LogEvent("AUTO_ENTER_PASSENGER: veh=%p dist=%.1fm > %.0fm (recruta longe)",
                        static_cast<void*>(playerVeh), distToVeh, RECRUIT_AUTO_ENTER_DIST);
                }
            }
            else
            {
                LogEvent("AUTO_ENTER_PASSENGER: veh=%p subClass=%u sem lugar para passageiro",
                    static_cast<void*>(playerVeh),
                    playerVeh ? (unsigned)playerVeh->m_nVehicleSubClass : 99u);
            }
        }

        // Detectar saida do veiculo → restaurar FOLLOW_LIMITED
        if (justExitedVehicle)
        {
            OnPlayerExitVehicle(player);
            LogEvent("AUTO_EXIT_VEHICLE: jogador saiu do veiculo — FOLLOW_LIMITED restaurado");
        }
    }

    // ── Dump AI throttled a cada ~2s (120 frames) ──────────────
    if (++g_logAiFrame >= 120)
    {
        g_logAiFrame = 0;
        CVector rPos = g_recruit->GetPosition();
        CVector pPos = player->GetPosition();
        float playerPhysSpeed = player->m_vecMoveSpeed.Magnitude() * 180.0f;
        char taskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
            BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
        }
        LogAI("ON_FOOT_1: dist=%.1fm rPos=(%.1f,%.1f,%.1f) initTimer=%d passiveTimer=%d rescanTimer=%d",
            Dist2D(rPos, pPos), rPos.x, rPos.y, rPos.z,
            g_initialFollowTimer, g_passiveTimer, g_groupRescanTimer);
        LogAI("ON_FOOT_2: aggr=%d doFollow=%d pedType=%d respect=%.0f playerSpeed=%.0fkmh tasks=%s",
            (int)g_aggressive, (int)doFollow, (int)g_recruit->m_nPedType,
            CStats::GetStatValue(STAT_RESPECT), playerPhysSpeed, taskBuf);
    }
}
