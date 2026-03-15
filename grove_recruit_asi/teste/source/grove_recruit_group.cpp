/*
 * grove_recruit_group.cpp
 *
 * Gestao do grupo do jogador: join/leave,
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
// TellGroupFollowWithRespect
// Wrapper sobre TellGroupToStartFollowingPlayer com logging.
// ───────────────────────────────────────────────────────────────────
void TellGroupFollowWithRespect(CPlayerPed* player, bool aggressive, bool verbose)
{
    if (!player) return;

    if (verbose)
    {
        float respect = CStats::GetStatValue(STAT_RESPECT);
        LogTask("TellGroupToStartFollowingPlayer: respect=%.0f aggr=%d",
            respect,
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

    // ── Passo 1b: Definir respeito pelo jogador no m_acquaintance ──
    // CPedIntelligence::Respects(playerPed) verifica:
    //   m_pPed->m_acquaintance.m_nRespect & (1 << playerPed->m_nPedType)
    // Para PED_TYPE_PLAYER1=0 isso e equivalente a checar o bit 0.
    // Sem este bit, TellGroupToStartFollowingPlayer/CreateFirstSubTask
    // falha silenciosamente e o recruta fica em STAND_STILL.
    // NOTA: STAT_RESPECT nao tem qualquer efeito nesta verificacao.
    g_recruit->m_acquaintance.m_nRespect |= (1u << PED_TYPE_PLAYER1);
    LogEvent("ACQUAINTANCE_FIX: m_nRespect bit PED_TYPE_PLAYER1 definido "
             "(ped=%p nRespect=0x%X)",
        static_cast<void*>(g_recruit),
        g_recruit->m_acquaintance.m_nRespect);

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
                         "(%s) — boost de respect para 1000 sera aplicado antes de MakeThisPedJoinOurGroup",
                    existGi, existSi, maxMem, resp, groupIdx,
                    existGi >= 0 ? "ATENCAO: ped JA tem grupo — removendo antes de MakeThisPedJoinOurGroup" :
                                   "ped sem grupo (OK para MakeThisPedJoinOurGroup)");

                // Remove do grupo existente para que MakeThisPedJoinOurGroup possa aceitar o ped.
                // GSF NPCs (pedType=8) sao automaticamente colocados em grupos de gang internos;
                // sem esta remocao previa, MakeThisPedJoinOurGroup falha silenciosamente.
                if (existGi >= 0)
                {
                    CPedGroups::ms_groups[existGi].m_groupMembership.RemoveMember(existSi);
                    LogGroup("PRE_JOIN: ped removido do grupo=%d slot=%d (liberado para MakeThisPedJoinOurGroup)",
                        existGi, existSi);
                }
            }

            // MakeThisPedJoinOurGroup sem boost de STAT_RESPECT.
            // O mod cria o ped programaticamente — o check de respect e para
            // recrutamento vanilla (botao Y). Se o join falhar com respect=0,
            // o fallback AddFollower+EnsureBeInGroup garante o follow.
            player->MakeThisPedJoinOurGroup(g_recruit);

            int slotAfter = FindRecruitMemberID(player);
            if (slotAfter < 0)
            {
                // Backup direto via AddFollower
                CPedGroups::ms_groups[groupIdx].m_groupMembership.AddFollower(g_recruit);
                slotAfter = FindRecruitMemberID(player);
                if (slotAfter < 0)
                {
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup E AddFollower falharam — recruta fora do grupo!");
                }
                else
                {
                    // AddFollower adicionou o ped ao m_apMembers[] mas NAO regista no
                    // CPedGroupIntelligence. g_joinedViaAddFollower=true marca para
                    // re-tentativa no RESCAN seguinte (quando FindMaxGroupMembers > 0).
                    g_joinedViaAddFollower = true;
                    LogWarn("AddRecruitToGroup: MakeThisPedJoinOurGroup falhou (pedType=%d, GSF=8); AddFollower (backup) -> slot=%d."
                            " Re-tentativa de join no proximo RESCAN quando FindMaxGroupMembers > 0.",
                        (int)g_recruit->m_nPedType, slotAfter);
                    // Configura DM manualmente: AddFollower nao chama SetPedDecisionMakerTypeInGroup,
                    // o que deixa m_nDecisionMakerTypeInGroup=UNKNOWN e faz CreateFirstSubTask
                    // (chamado por TellGroupToStartFollowingPlayer) ser um no-op.
                    g_recruit->m_pIntelligence->SetPedDecisionMakerTypeInGroup(
                        eDecisionMakerType::PED_GROUPMEMBER);
                }
            }
            else
            {
                g_joinedViaAddFollower = false;  // join correcto via MakeThisPedJoinOurGroup
                // CORRECAO: MakeThisPedJoinOurGroup nao atribui TASK_COMPLEX_BE_IN_GROUP(243)
                // a TASK_PRIMARY_PRIMARY em condicoes de spawn. Sem BE_IN_GROUP em slot[3],
                // CPedGroupIntelligence::GetTaskMain(recruit) nunca e chamado, eventos GATHER
                // (de TellGroupToStartFollowingPlayer) nao sao consumidos e o recruta fica
                // em STAND_STILL para sempre. EnsureBeInGroup corrige a omissao.
                bool beInFixed = EnsureBeInGroup(g_recruit, groupIdx);
                LogGroup("AddRecruitToGroup: MakeThisPedJoinOurGroup OK -> slot=%d pedType=%d%s",
                    slotAfter, (int)g_recruit->m_nPedType,
                    beInFixed ? " + BE_IN_GROUP(243) atribuido manualmente a slot[3]" : " (BE_IN_GROUP ja presente)");
            }
        }
        else if (g_joinedViaAddFollower && FindMaxGroupMembers() > 0)
        {
            // O ped esta no grupo via AddFollower (fallback), mas MakeThisPedJoinOurGroup
            // falhou anteriormente (FindMaxGroupMembers=0 no spawn). Agora que
            // FindMaxGroupMembers > 0, re-tentar o join correcto para registar o ped
            // no CPedGroupIntelligence e permitir que TellGroupFollowWithRespect funcione.
            int maxMem = FindMaxGroupMembers();
            LogGroup("AddRecruitToGroup: re-tentativa MakeThisPedJoinOurGroup "
                     "(AddFollower fallback anterior, FindMaxGroupMembers=%d, slot=%d) — boost respect=1000",
                maxMem, slotBefore);

            // CORRECAO CRITICA: limpar slots[1-2] e bKeepTasksAfterCleanUp=0 antes do re-join.
            //
            // 1. ClearTaskEventResponse: limpa slot[1]=EVENT_TEMP e slot[2]=EVENT_NONTEMP.
            //    TASK_COMPLEX_GANG_JOIN_RESPOND(1219) no slot[2] pode interferir com o
            //    processo de join interno de MakeThisPedJoinOurGroup.
            //
            // 2. bKeepTasksAfterCleanUp=0: permite que CleanupAfterEnteringGroup corra
            //    normalmente no interior de MakeThisPedJoinOurGroup (quando e 1, e no-op).
            ClearTaskEventResponse(&g_recruit->m_pIntelligence->m_TaskMgr);
            g_recruit->bKeepTasksAfterCleanUp = 0;
            LogGroup("AddRecruitToGroup: re-join pre-fix — ClearTaskEventResponse + bKeepTasksAfterCleanUp=0");

            CPedGroups::ms_groups[groupIdx].m_groupMembership.RemoveMember(slotBefore);
            player->MakeThisPedJoinOurGroup(g_recruit);
            int slotRetry = FindRecruitMemberID(player);
            if (slotRetry >= 0)
            {
                g_joinedViaAddFollower = false;
                bool beInFixed = EnsureBeInGroup(g_recruit, groupIdx);
                g_recruit->bKeepTasksAfterCleanUp = 1;
                LogGroup("AddRecruitToGroup: re-join OK slot=%d%s + bKeepTasksAfterCleanUp=1", slotRetry,
                         beInFixed ? " + BE_IN_GROUP(243) a slot[3]" : " (BE_IN_GROUP ja presente)");
            }
            else
            {
                // Re-join falhou sem boost — usar AddFollower + EnsureBeInGroup como fallback.
                g_recruit->bKeepTasksAfterCleanUp = 1;
                CPedGroups::ms_groups[groupIdx].m_groupMembership.AddFollower(g_recruit);
                LogWarn("AddRecruitToGroup: re-join falhou — bKeepTasks restaurado, AddFollower re-aplicado (proximo RESCAN vai re-tentar)");
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

    // ── Passo 4a: ForceGroupToAlwaysFollow REMOVIDO ──────────────
    // HISTORICO: foi usado para forcar re-emissao continua de GANG_FOLLOWER,
    // mas causa dois problemas:
    //   1. O engine envolve TASK_SIMPLE_ANIM(400) em TASK_COMPLEX_GANG_JOIN_RESPOND(1219)
    //      e coloca-o em slot[2]=EVENT_NONTEMP. GetSimplestActiveTask devolve slot[2]
    //      antes de slot[3] → STAND_STILL.
    //   2. Interfere com o mecanismo nativo de recrutamento (botao Y / vanilla
    //      recruit), impedindo o jogador de recrutar outros membros GSF enquanto
    //      o recruta ASI esta activo.
    // FIX: GANG_SPAWN_ANIM_END (grove_recruit_ai.cpp) chama ClearTaskEventResponse
    // para limpar slot[1]/[2] antes de re-emitir follow. O burst inicial (300 frames)
    // e o RESCAN periodico (120 frames) garantem re-emissao continua de follow.
    // player->ForceGroupToAlwaysFollow(true);  // REMOVED — ver comentario acima

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
    g_joinedViaAddFollower = false;
    g_invalidLinkCounter  = 0;
    g_observerTimer       = 0;
    g_enterCarAsPassenger = false;
    g_playerWasInVehicle  = false;
    g_scanGroupTimer      = 0;
    // Limpar tabela de recrutas rastreados (vanilla e spawned)
    for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
        g_allRecruits[i] = TrackedRecruit{};
    g_numAllRecruits = 0;
    LogEvent("DismissRecruit: estado resetado para INACTIVE");
}

// ───────────────────────────────────────────────────────────────────
// ApplyRecruitEnhancement
// Aplica flags do mod a um ped (seja spawned ou vanilla).
// Seguro chamar multiplas vezes (idempotente).
// ───────────────────────────────────────────────────────────────────
void ApplyRecruitEnhancement(CPed* ped, bool isVanilla)
{
    if (!ped) return;

    ped->bNeverLeavesGroup                  = 1;
    ped->bKeepTasksAfterCleanUp             = 1;
    ped->bDoesntListenToPlayerGroupCommands = 0;

    // Garantir que o ped respeita o jogador (necessario para TellGroupFollowWithRespect)
    ped->m_acquaintance.m_nRespect |= (1u << PED_TYPE_PLAYER1);

    // Dar arma se nao tiver nenhuma (recruta vanilla pode estar desarmado)
    if (isVanilla)
    {
        CWeapon& currentWeapon = ped->m_aWeapons[ped->m_nActiveWeaponSlot];
        if ((int)currentWeapon.m_eWeaponType <= 0 ||
            currentWeapon.m_nAmmoInClip <= 0)
        {
            ped->GiveWeapon(RECRUIT_WEAPON, RECRUIT_AMMO, false);
            LogRecruit("ApplyRecruitEnhancement: arma atribuida a vanilla recruit ped=%p weapon=%d ammo=%d",
                static_cast<void*>(ped), (int)RECRUIT_WEAPON, RECRUIT_AMMO);
        }
    }

    LogRecruit("ApplyRecruitEnhancement: ped=%p tipo=%s bNeverLeaves=1 bKeepTasks=1 respect_bit=1",
        static_cast<void*>(ped), isVanilla ? "VANILLA" : "SPAWNED");
}

// ───────────────────────────────────────────────────────────────────
// ScanPlayerGroup
// Scana o grupo do jogador a cada SCAN_GROUP_INTERVAL frames.
// Detecta membros recrutados pelo metodo vanilla (tecla Y + respeito)
// que ainda nao estao na tabela g_allRecruits, e aplica-lhes as flags
// do mod para comportamento correcto (bNeverLeavesGroup, etc.).
//
// ESTRATEGIA:
//   1. Iterar m_apMembers[0..6] do grupo do jogador.
//   2. Para cada membro valido: verificar se ja esta em g_allRecruits.
//   3. Se novo: criar slot, marcar isVanilla=true, chamar ApplyRecruitEnhancement.
//   4. Se o mod nao tem recruta primario (g_state=INACTIVE) e este e o
//      unico membro: promove-o a recruta primario (g_recruit) e muda
//      para ON_FOOT para que o mod o acompanhe correctamente.
//   5. Limpar slots de recrutas mortos ou que sairam do grupo.
// ───────────────────────────────────────────────────────────────────
void ScanPlayerGroup(CPlayerPed* player)
{
    if (!player) return;

    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx >= 8u) return;

    CPedGroupMembership& membership =
        CPedGroups::ms_groups[groupIdx].m_groupMembership;

    int newCount = 0;
    int totalInGroup = 0;

    // ── Passo 1: Remover slots invalidos (mortos ou fora do grupo) ──
    for (int i = 0; i < g_numAllRecruits; ++i)
    {
        TrackedRecruit& slot = g_allRecruits[i];
        if (!slot.ped) continue;

        // Verificar se ainda esta vivo e no grupo
        bool stillInGroup = false;
        if (CPools::ms_pPedPool->IsObjectValid(slot.ped) && slot.ped->IsAlive())
        {
            for (int m = 0; m < 7; ++m)
            {
                if (membership.m_apMembers[m] == slot.ped)
                {
                    stillInGroup = true;
                    break;
                }
            }
        }

        if (!stillInGroup)
        {
            LogRecruit("ScanPlayerGroup: slot[%d] ped=%p saiu/morreu — a limpar",
                i, static_cast<void*>(slot.ped));
            // Se era o recruta primario, marcar como perdido
            if (slot.ped == g_recruit && g_state != ModState::INACTIVE)
            {
                LogWarn("ScanPlayerGroup: recruta primario saiu do grupo — pode precisar de re-recrutar");
            }
            slot = TrackedRecruit{};
        }
    }

    // Compactar: remover slots vazios no meio do array
    {
        int write = 0;
        for (int read = 0; read < g_numAllRecruits; ++read)
        {
            if (g_allRecruits[read].ped)
                g_allRecruits[write++] = g_allRecruits[read];
        }
        while (write < g_numAllRecruits)
            g_allRecruits[write++] = TrackedRecruit{};
        // g_numAllRecruits atualizado abaixo
    }

    // ── Passo 2: Detectar novos membros vanilla ──────────────────
    for (int m = 0; m < 7; ++m)
    {
        CPed* member = membership.m_apMembers[m];
        if (!member) continue;
        if (member == (CPed*)player) continue;
        if (!CPools::ms_pPedPool->IsObjectValid(member)) continue;
        if (!member->IsAlive()) continue;

        ++totalInGroup;

        // Verificar se ja esta rastreado
        bool found = false;
        for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
        {
            if (g_allRecruits[i].ped == member)
            {
                found = true;
                // Aplicar flags se ainda nao foi feito (pode ter entrado antes do mod carregar)
                if (!g_allRecruits[i].flagsSet)
                {
                    ApplyRecruitEnhancement(member, g_allRecruits[i].isVanilla);
                    g_allRecruits[i].flagsSet = true;
                }
                break;
            }
        }

        if (!found)
        {
            // Novo membro — verificar se e vanilla (nao e o g_recruit atual)
            bool isVanilla = (member != g_recruit);

            // Adicionar ao tracking
            bool added = false;
            for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
            {
                if (!g_allRecruits[i].ped)
                {
                    g_allRecruits[i].ped       = member;
                    g_allRecruits[i].isVanilla = isVanilla;
                    g_allRecruits[i].flagsSet  = false;
                    added = true;
                    ++newCount;
                    LogRecruit("ScanPlayerGroup: NOVO membro slot[%d] ped=%p pedType=%d %s",
                        i, static_cast<void*>(member), (int)member->m_nPedType,
                        isVanilla ? "(VANILLA)" : "(spawned)");
                    break;
                }
            }

            if (added)
            {
                ApplyRecruitEnhancement(member, isVanilla);
                // Encontrar o slot recem criado e marcar flagsSet
                for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
                    if (g_allRecruits[i].ped == member)
                        g_allRecruits[i].flagsSet = true;
            }
            else
            {
                LogWarn("ScanPlayerGroup: tabela cheia (MAX=%d), ped=%p nao adicionado",
                    MAX_TRACKED_RECRUITS, static_cast<void*>(member));
            }
        }
    }

    // Contar slots preenchidos
    int count = 0;
    for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
        if (g_allRecruits[i].ped) ++count;
    g_numAllRecruits = count;

    // ── Passo 3: Promover vanilla recruit a primario se mod inactivo ─
    // Se o mod nao tem recruta activo mas o jogador tem membros no grupo
    // (recrutados via vanilla), promover o primeiro membro a primario.
    if (g_state == ModState::INACTIVE && g_numAllRecruits > 0)
    {
        // Procurar um vanilla recruit valido
        for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
        {
            TrackedRecruit& slot = g_allRecruits[i];
            if (!slot.ped || !slot.isVanilla) continue;
            if (!CPools::ms_pPedPool->IsObjectValid(slot.ped)) continue;
            if (!slot.ped->IsAlive()) continue;

            // Promover a primario
            g_recruit = slot.ped;
            g_state   = ModState::ON_FOOT;
            g_recruit->SetCharCreatedBy(2); // PEDCREATED_MISSION (previne despawn)
            // Re-armar timers para nova sessao
            g_groupRescanTimer   = 0;
            g_initialFollowTimer = INITIAL_FOLLOW_FRAMES;
            g_logAiFrame         = 0;
            g_prevRecruitTaskId  = -999;
            g_postFollowTimer    = 0;
            g_postFollowRetries  = 0;
            LogRecruit("ScanPlayerGroup: vanilla recruit promovido a primario ped=%p — mod ON_FOOT",
                static_cast<void*>(g_recruit));
            ShowMsg("~g~Recruta vanilla detectado! [INSERT=menu]");
            break;
        }
    }

    if (newCount > 0)
    {
        LogRecruit("ScanPlayerGroup: %d novos membros detectados, total_rastreados=%d total_grupo=%d",
            newCount, g_numAllRecruits, totalInGroup);
    }
}

// ───────────────────────────────────────────────────────────────────
// OnPlayerEnterVehicle
// Chamado quando o jogador entra num veiculo.
// Define o allocator do grupo para SIT_IN_LEADER_CAR (4) de forma que
// TODOS os membros do grupo (recrutas vanilla + spawned ainda a pe)
// tentem entrar no carro do jogador automaticamente.
// O engine GTA SA trata do comportamento de entrada.
// ───────────────────────────────────────────────────────────────────
void OnPlayerEnterVehicle(CPlayerPed* player)
{
    if (!player) return;

    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx >= 8u) return;

    void* pIntel = GetGroupIntelligence(groupIdx);
    if (!pIntel) return;

    int memberCount = CPedGroups::ms_groups[groupIdx].m_groupMembership.CountMembersExcludingLeader();
    if (memberCount <= 0) return;

    GroupIntelSetDefaultTaskAllocatorType(pIntel, ALLOCATOR_SIT_IN_CAR);
    LogRecruit("OnPlayerEnterVehicle: SIT_IN_LEADER_CAR(4) activado — membros=%d tentarao entrar no carro",
        memberCount);
}

// ───────────────────────────────────────────────────────────────────
// OnPlayerExitVehicle
// Chamado quando o jogador sai de um veiculo.
// Restaura o allocator para FOLLOW_LIMITED (1) — formacao normal a pe.
// ───────────────────────────────────────────────────────────────────
void OnPlayerExitVehicle(CPlayerPed* player)
{
    if (!player) return;

    unsigned int groupIdx = player->m_pPlayerData->m_nPlayerGroup;
    if (groupIdx >= 8u) return;

    void* pIntel = GetGroupIntelligence(groupIdx);
    if (!pIntel) return;

    int memberCount = CPedGroups::ms_groups[groupIdx].m_groupMembership.CountMembersExcludingLeader();

    GroupIntelSetDefaultTaskAllocatorType(pIntel, ALLOCATOR_FOLLOW);
    LogRecruit("OnPlayerExitVehicle: FOLLOW_LIMITED(1) restaurado — membros=%d retomam formacao",
        memberCount);
}
