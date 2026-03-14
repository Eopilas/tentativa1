/*
 * grove_recruit_group.cpp
 *
 * Gestao do grupo do jogador: join/leave, respect boost,
 * TellGroupFollowWithRespect, AddRecruitToGroup, RemoveRecruitFromGroup,
 * DismissRecruit.
 */
#include "grove_recruit_shared.h"

// ───────────────────────────────────────────────────────────────────
// FindRecruitMemberID
// Devolve o slot do recruta no grupo do jogador, ou -1 se nao encontrado.
// m_apMembers[0..6] = seguidores, m_apMembers[7] = lider
// ───────────────────────────────────────────────────────────────────
int FindRecruitMemberID(CPlayerPed* player)
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
// Boost PERSISTENTE de respeito
//
// POR QUE boost PERSISTENTE (nao temporario por call):
//   TellGroupToStartFollowingPlayer cria CTaskComplexGangFollower
//   cujo CreateFirstSubTask e chamado NO FRAME SEGUINTE pelo task manager
//   — DEPOIS do boost temporario ja ter sido restaurado.
//   FindMaxNumberOfGroupMembers le STAT_RESPECT em frames periodicos
//   para validar slots de grupo — se restaurado, ejecta o recruta.
//
//   SOLUCAO: boost activo durante TODA a sessao (spawn → dismiss).
//     ActivateRespectBoost()   — chamada UMA VEZ no spawn
//     DeactivateRespectBoost() — chamada UMA VEZ no dismiss
// ───────────────────────────────────────────────────────────────────
void ActivateRespectBoost()
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
        g_savedRespect = -1.0f;
        LogEvent("RESPECT_BOOST: desnecessario (respect=%.0f >= %.0f)", current, RESPECT_TEST_BOOST);
    }
}

void DeactivateRespectBoost()
{
    if (g_savedRespect >= 0.0f)
    {
        CStats::SetStatValue(STAT_RESPECT, g_savedRespect);
        LogEvent("RESPECT_BOOST: DESACTIVADO -> %.0f restaurado", g_savedRespect);
        g_savedRespect = -1.0f;
    }
}

// ───────────────────────────────────────────────────────────────────
// TellGroupFollowWithRespect
// Wrapper sobre TellGroupToStartFollowingPlayer com logging.
// O boost persistente ja esta activo desde o spawn — sem boost/restore aqui.
// ───────────────────────────────────────────────────────────────────
void TellGroupFollowWithRespect(CPlayerPed* player, bool aggressive, bool verbose)
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
    // activa do recruta para confirmar se CreateFirstSubTask atribuiu
    // TASK_COMPLEX_GANG_FOLLOWER(1207) ou ficou em TASK_SIMPLE_STAND_STILL(203).
    if (g_postFollowTimer <= 0)
    {
        g_postFollowTimer   = 3;
        g_postFollowRetries = 0;  // nova tentativa: reset contador de fallbacks
    }
}

// ───────────────────────────────────────────────────────────────────
// AddRecruitToGroup
// Sequencia completa de entrada no grupo + follow.
// Equivalente ao bloco CLEO (0631 + 087F + 0961 + 06F0 + 0850).
// ───────────────────────────────────────────────────────────────────
void AddRecruitToGroup(CPlayerPed* player)
{
    if (!player || !g_recruit) return;

    // ── Passo 1: Flags ANTES de entrar no grupo ──────────────────
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
            // PRE_JOIN: diagnostico — o ped ja esta noutro grupo?
            {
                int existGi = -1, existSi = -1;
                for (int gi = 0; gi < 8 && existGi < 0; ++gi)
                    for (int si = 0; si < 7; ++si)
                        if (CPedGroups::ms_groups[gi].m_groupMembership.m_apMembers[si] == g_recruit)
                        { existGi = gi; existSi = si; break; }

                int maxMem = FindMaxGroupMembers();
                float resp  = CStats::GetStatValue(STAT_RESPECT);
                LogGroup("PRE_JOIN: ped_em_grupo=%d(slot=%d) FindMaxGroupMembers=%d respect=%.0f playerGrp=%u "
                         "(%s)",
                    existGi, existSi, maxMem, resp, groupIdx,
                    existGi >= 0 ? "ATENCAO: ped JA tem grupo — MakeThisPedJoinOurGroup FALHARA!" :
                                   "ped sem grupo (OK para MakeThisPedJoinOurGroup)");
            }

            player->MakeThisPedJoinOurGroup(g_recruit);
            int slotAfter = FindRecruitMemberID(player);
            if (slotAfter < 0)
            {
                // Backup direto via AddFollower
                CPedGroups::ms_groups[groupIdx].m_groupMembership.AddFollower(g_recruit);
                slotAfter = FindRecruitMemberID(player);
                if (slotAfter < 0)
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup E AddFollower falharam — recruta fora do grupo!");
                else
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup falhou (pedType=%d, GSF=8); AddFollower (backup) -> slot=%d.",
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
        // ATENCAO: usar m_groupMembership.m_fMaxSeparation (offset +0x2C em CPedGroup).
        // NAO usar m_fSeparationRange (+0x30): e interpretado como ponteiro para
        // CPedGroupIntelligence — escrever 100.0f=0x42C80000 causa crash imediato.
        CPedGroups::ms_groups[groupIdx].m_groupMembership.m_fMaxSeparation = 100.0f;

        int memberCount = CPedGroups::ms_groups[groupIdx].m_groupMembership.CountMembersExcludingLeader();
        LogGroup("AddRecruitToGroup: grupo=%u membros=%d (excl. lider)", groupIdx, memberCount);
    }
    else
    {
        LogWarn("AddRecruitToGroup: groupIdx=%u invalido (>=8)!", groupIdx);
    }

    LogGroup("AddRecruitToGroup: flags ped=%p bNeverLeaves=%d bKeepTasks=%d bDoesntListen=%d bInVeh=%d",
        static_cast<void*>(g_recruit),
        (int)g_recruit->bNeverLeavesGroup,
        (int)g_recruit->bKeepTasksAfterCleanUp,
        (int)g_recruit->bDoesntListenToPlayerGroupCommands,
        (int)g_recruit->bInVehicle);

    // ── Passo 4a: ForceGroupToAlwaysFollow (0961 equivalente) ────
    // CRITICO: sem esta chamada o grupo nao recebe a flag de seguimento
    // continuo e o engine atribui GANG_SPAWN_AI (tarefa de spawn) em vez
    // de GANG_FOLLOWER (1207). O recruta fica parado apos GANG_SPAWN_AI.
    // ForceGroupToAlwaysFollow(true) faz a CPedGroupIntelligence emitir
    // continuamente GANG_FOLLOWER sempre que a tarefa anterior terminar.
    player->ForceGroupToAlwaysFollow(true);
    LogGroup("AddRecruitToGroup: ForceGroupToAlwaysFollow(true) activado (0961)");

    // ── Passo 4b: Emitir tarefa de seguimento ──
    TellGroupFollowWithRespect(player, g_aggressive);
}

// ───────────────────────────────────────────────────────────────────
// RemoveRecruitFromGroup
// ───────────────────────────────────────────────────────────────────
void RemoveRecruitFromGroup(CPlayerPed* player)
{
    if (!player) return;
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
// DismissRecruit
// Dispensar recruta: remover do grupo, limpar estado, tornar wander NPC.
// ───────────────────────────────────────────────────────────────────
void DismissRecruit(CPlayerPed* player)
{
    LogEvent("DismissRecruit: estado_anterior=%s ped=%p carro=%p",
        StateName(g_state),
        static_cast<void*>(g_recruit),
        static_cast<void*>(g_car));

    DeactivateRespectBoost();

    if (player && g_recruit)
    {
        RemoveRecruitFromGroup(player);
        if (IsRecruitValid())
            g_recruit->SetCharCreatedBy(1);  // 1 = PEDCREATED_RANDOM
    }

    g_recruit = nullptr;
    g_car     = nullptr;
    g_state   = ModState::INACTIVE;
    g_driveMode    = DriveMode::CIVICO_D;
    g_aggressive   = true;
    g_driveby      = false;
    g_isOffroad    = false;
    g_passiveTimer        = 0;
    g_groupRescanTimer    = 0;
    g_initialFollowTimer  = 0;
    g_prevRecruitTaskId   = -999;
    g_postFollowTimer     = 0;
    g_postFollowRetries   = 0;
    g_wasWrongDir         = false;
    g_wasInvalidLink      = false;
    g_missionRecoveryTimer = 0;
    g_slowZoneRestoring   = false;
    g_civicRoadSnapTimer  = 0;
    LogEvent("DismissRecruit: estado resetado para INACTIVE");
}
