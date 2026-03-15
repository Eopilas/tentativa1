/*
 * grove_recruit_ai.cpp
 *
 * IA de seguimento a pe (ProcessOnFoot).
 *
 * Funcionalidades:
 *   - Rastreio em tempo real de mudancas de tarefa (TASK_CHANGE)
 *   - POST_FOLLOW_CHECK: verifica tarefa 3 frames apos TellGroupFollow
 *     e, se nao for 1207/1500, tenta FOLLOW_FALLBACK via
 *     GroupIntelSetDefaultTaskAllocatorType(4)
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

        if (g_prevRecruitTaskId != -999 && tid != g_prevRecruitTaskId)
        {
            LogTask("TASK_CHANGE: %d -> %d  (%s -> %s)",
                g_prevRecruitTaskId, tid,
                GetTaskName(g_prevRecruitTaskId),
                GetTaskName(tid));

            // Quando GANG_SPAWN_AI termina, limpar slot[2]=EVENT_NONTEMP e re-emitir follow.
            // GANG_SPAWN_COMPLEX(1219) ou GANG_SPAWN_AI(400) pode persistir no slot[2]
            // mesmo depois de bKeepTasksAfterCleanUp=1 (flag que impede limpeza auto).
            // Com slot[2] ocupado, GetSimplestActiveTask devolve essa tarefa em vez de
            // GANG_FOLLOWER(1207) do slot[3] → recruta fica STAND_STILL para sempre.
            // ClearTaskEventResponse (0x681BD0) limpa slot[1] e slot[2], permitindo
            // que a re-emissao de follow ocupe slot[3] normalmente.
            if (g_prevRecruitTaskId == 400 /* GANG_SPAWN_AI */ && tid != 400)
            {
                LogTask("GANG_SPAWN_AI_END: spawn concluido (tid=%d %s) — limpando slots[1-2] e re-emitindo follow",
                    tid, GetTaskName(tid));
                // Limpar quaisquer tarefas de spawn residuais em EVENT_NONTEMP (slot[2])
                ClearTaskEventResponse(&g_recruit->m_pIntelligence->m_TaskMgr);
                g_postFollowTimer   = 0;  // cancela check pendente
                g_postFollowRetries = 0;  // reset contador
                TellGroupFollowWithRespect(player, g_aggressive, true);
            }
        }
        g_prevRecruitTaskId = tid;

        // ── POST_FOLLOW_CHECK (3 frames diferida) ─────────────────
        // Confirma se CreateFirstSubTask (deferred) atribuiu 1207.
        // Se nao for 1207 nem 1500, tenta fallback via
        // GroupIntelSetDefaultTaskAllocatorType(4) = FollowLeaderAnyMeans +
        // GroupIntelComputeDefaultTasks para aplicar imediatamente.
        // Limita tentativas a MAX_FOLLOW_FALLBACK_RETRIES para evitar loop.
        if (g_postFollowTimer > 0)
        {
            --g_postFollowTimer;
            if (g_postFollowTimer == 0)
            {
                bool followOk = (tid == 1207 || tid == 1500);
                // Estados transitórios de spawn — nao e erro, apenas aguardar
                bool transient = (tid == 902 || tid == 400);

                LogTask("POST_FOLLOW_CHECK(%d/%d): activeTask=%d (%s) — %s",
                    g_postFollowRetries + 1, MAX_FOLLOW_FALLBACK_RETRIES,
                    tid,
                    GetTaskName(tid),
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
                        // Fallback: SetDefaultTaskAllocatorType(4) + ComputeDefaultTasks
                        unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
                        void* pIntel = GetGroupIntelligence(groupIdx);
                        if (pIntel)
                        {
                            LogTask("FOLLOW_FALLBACK(%d/%d): SetAllocatorType(4)+ComputeDefault "
                                    "groupIdx=%u pIntel=%p (FollowLeaderAnyMeans)",
                                g_postFollowRetries, MAX_FOLLOW_FALLBACK_RETRIES,
                                groupIdx, pIntel);
                            GroupIntelSetDefaultTaskAllocatorType(pIntel, 4);
                            GroupIntelComputeDefaultTasks(pIntel, g_recruit);
                            // Re-armar check em mais 3 frames para confirmar
                            g_postFollowTimer = 3;
                        }
                        else
                        {
                            LogWarn("FOLLOW_FALLBACK: GetGroupIntelligence(%u) retornou nullptr", groupIdx);
                        }
                    }
                    else
                    {
                        LogWarn("FOLLOW_FALLBACK: limite %d tentativas atingido (tid=%d %s) "
                                "— aguardando RESCAN/burst",
                            MAX_FOLLOW_FALLBACK_RETRIES, tid, GetTaskName(tid));
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
    // g_initialFollowTimer > 0: burst 5s apos spawn
    // !g_aggressive: modo passivo permanente
    bool doFollow = (g_initialFollowTimer > 0) || (!g_aggressive);
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
