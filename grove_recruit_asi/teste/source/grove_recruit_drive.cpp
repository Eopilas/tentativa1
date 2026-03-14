/*
 * grove_recruit_drive.cpp
 *
 * IA de conducao do recruta:
 *   - DetectOffroad, AdaptiveSpeed, ApplyLaneAlignment
 *   - FindNearestFreeCar
 *   - SetupDriveMode (configura CAutoPilot no momento de mudar de modo)
 *   - ProcessDrivingAI (per-frame: zonas STOP/SLOW, offroad, recovery,
 *       CIVICO auto-degrade para MISSION_34 a curta distancia,
 *       periodic JoinCarWithRoadSystem)
 *   - ProcessEnterCar, ProcessDriving, ProcessPassenger
 *
 * MELHORIAS DE CONDUCAO CIVICO:
 *   1. CIVICO_D auto-degrade: quando dist < MEDIUM_DIST_M (25m), CIVICO_D usa
 *      MISSION_34 (FollowCarFaraway) em vez de MISSION_43 (EscortRearFaraway).
 *      MISSION_34 tem menos comportamento "chase" ao virar bruscamente perto do
 *      recruta. Restaura MISSION_43 quando dist >= MEDIUM_DIST_M.
 *
 *   2. SLOW_ZONE re-snap: JoinCarWithRoadSystem ao sair da SLOW_ZONE (dist>10m)
 *      para re-alinhar imediatamente com a estrada apos a pausa forcada.
 *      Tambem reset g_civicRoadSnapTimer para evitar snap duplo.
 *
 *   3. Periodic road snap: JoinCarWithRoadSystem a cada ROAD_SNAP_INTERVAL (5s)
 *      em modos CIVICO para manter alinhamento com os nos de estrada.
 *      Reduz desvios de faixa acumulados durante a conducao continuada.
 */
#include "grove_recruit_shared.h"

// ───────────────────────────────────────────────────────────────────
// DetectOffroad (throttled via g_offroadTimer)
// Devolve true se o veiculo esta mais de OFFROAD_DIST_M do no mais proximo.
// ───────────────────────────────────────────────────────────────────
bool DetectOffroad(CVehicle* veh)
{
    if (!veh) return true;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);

    if (node1.IsEmpty()) return true;

    CPathNode* pNode = ThePaths.GetPathNode(node1);
    if (!pNode) return true;

    CVector nodePos   = pNode->GetNodeCoors();
    CVector vehicPos  = veh->GetPosition();

    return Dist2D(vehicPos, nodePos) > OFFROAD_DIST_M;
}

// ───────────────────────────────────────────────────────────────────
// AdaptiveSpeed: ajusta velocidade ao angulo da curva.
// CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(straightLine) →
//   mult 1.0 em reta, < 1.0 em curva.
// ───────────────────────────────────────────────────────────────────
unsigned char AdaptiveSpeed(CVehicle* veh, unsigned char baseSpeed)
{
    if (!veh) return baseSpeed;
    CAutoPilot& ap = veh->m_autoPilot;
    float mult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);
    mult = std::max(0.0f, std::min(1.0f, mult));
    auto ideal = static_cast<unsigned char>(static_cast<float>(baseSpeed) * mult);
    return std::max(ideal, SPEED_MIN);
}

// ───────────────────────────────────────────────────────────────────
// ApplyLaneAlignment: calcula targetHeading clipado ao road-graph.
// Devolve heading clipado ou heading actual se sem link valido.
// (Valor devolvido usado apenas para logging — steering nao alterado.)
// ───────────────────────────────────────────────────────────────────
float ApplyLaneAlignment(CVehicle* veh)
{
    if (!veh) return 0.0f;
    CAutoPilot& ap = veh->m_autoPilot;

    float currentHeading = veh->GetHeading();

    if (ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId == 0 &&
        ap.m_nCurrentPathNodeInfo.m_nAreaId == 0)
        return currentHeading;

    CVector fwd = veh->GetForward();
    float targetHeading = currentHeading;

    CCarCtrl::ClipTargetOrientationToLink(
        veh,
        ap.m_nCurrentPathNodeInfo,
        ap.m_nCurrentLane,
        &targetHeading,
        fwd.x,
        fwd.y
    );
    return targetHeading;
}

// ───────────────────────────────────────────────────────────────────
// FindNearestFreeCar
// Procura o carro desocupado mais proximo para o recruta entrar.
// Exclui: carro do jogador, carros com condutor, avioes/helicopteros.
//
// ESTRATEGIA DE SELECCAO (dois passes):
//   1.o passe: prefere carros ja alinhados com o road-graph (linkId valido)
//              para minimizar problemas de INVALID_LINK em CIVICO.
//   2.o passe: se nenhum no 1.o, aceita qualquer carro dentro do raio.
// Loga linkId do carro escolhido para diagnostico.
// ───────────────────────────────────────────────────────────────────
CVehicle* FindNearestFreeCar(CVector const& searchPos, CVehicle* excludePlayerCar)
{
    CVehicle* bestSnapped  = nullptr;
    float     bestSnappedD = FIND_CAR_RADIUS;

    CVehicle* bestAny      = nullptr;
    float     bestAnyD     = FIND_CAR_RADIUS;

    for (CVehicle* veh : *CPools::ms_pVehiclePool)
    {
        if (!veh)                        continue;
        if (veh == excludePlayerCar)     continue;
        if (veh->m_pDriver)              continue;
        if (veh->m_nVehicleSubClass > 2) continue;   // excluir avioes/helicopteros

        float d = Dist2D(veh->GetPosition(), searchPos);

        // Passe "qualquer"
        if (d < bestAnyD)
        {
            bestAnyD = d;
            bestAny  = veh;
        }

        // Passe "snapped": linkId valido → carro ja no road-graph
        unsigned linkId = (unsigned)veh->m_autoPilot.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        if (linkId <= MAX_VALID_LINK_ID && d < bestSnappedD)
        {
            bestSnappedD = d;
            bestSnapped  = veh;
        }
    }

    // Preferir carro snapped; fallback para qualquer carro disponivel
    CVehicle* best     = bestSnapped ? bestSnapped : bestAny;
    float     bestDist = bestSnapped ? bestSnappedD : bestAnyD;

    if (best)
    {
        unsigned linkId = (unsigned)best->m_autoPilot.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        LogEvent("FindNearestFreeCar: veh=%p dist=%.1fm linkId=%u(%s) pos=(%.1f,%.1f,%.1f) %s",
            static_cast<void*>(best), bestDist,
            linkId, (linkId <= MAX_VALID_LINK_ID) ? "OK" : "INVALID",
            best->GetPosition().x, best->GetPosition().y, best->GetPosition().z,
            bestSnapped ? "(road-snapped preferido)" : "(sem carro snapped — qualquer aceite)");
    }
    else
    {
        LogWarn("FindNearestFreeCar: nenhum carro livre num raio de %.0fm", FIND_CAR_RADIUS);
    }

    return best;
}

// ───────────────────────────────────────────────────────────────────
// SetupDriveMode
// Configura o CAutoPilot do carro do recruta para o modo escolhido.
// Chamado uma vez ao mudar de modo (KEY 4) ou ao transitar para DRIVING.
//
// IMPORTANTE: JoinCarWithRoadSystem e o passo critico que snap ao
// road-graph. Sem este passo o NPC pode navegar fora da estrada.
// Equivalente ao opcode CLEO 06E1 que chama esta funcao internamente.
// ───────────────────────────────────────────────────────────────────
void SetupDriveMode(CPlayerPed* player, DriveMode mode)
{
    if (!IsCarValid() || !IsRecruitValid()) return;

    CVehicle*   recruitCar = g_car;
    CAutoPilot& ap         = recruitCar->m_autoPilot;
    CVehicle*   playerCar  = player->bInVehicle ? player->m_pVehicle : nullptr;

    switch (mode)
    {
    // ── CIVICO-D: EscortRearFaraway ─────────────────────────────
    // Usa road-graph identico ao NPC de escolta vanilla.
    // m_pTargetCar aponta para o carro do jogador.
    // NOTA: ProcessDrivingAI pode baixar para MISSION_34 a curta distancia.
    case DriveMode::CIVICO_D:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_D sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MISSION_43;  // EscortRearFaraway (=67)
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            float    playerH  = playerCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;   // reset snap timer apos JoinRoad no SetupDriveMode
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPost = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_D mission=43 speed=%d driveStyle=AVOID_CARS playerCar=%p "
                     "JOIN_ROAD: linkId %u->%u areaId %u->%u heading_pre=%.3f heading_post=%.3f lane=%d "
                     "playerHeading=%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost, (int)ap.m_nCurrentLane,
                playerH,
                (linkPre == linkPost ? "ATENCAO: linkId nao mudou apos JoinRoad!" : "JoinRoad OK snap"));
        }
        break;
    }

    // ── CIVICO-E: FollowCarFaraway ──────────────────────────────
    case DriveMode::CIVICO_E:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_E sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MISSION_34;  // FollowCarFaraway (=52)
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            float    playerH  = playerCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPost = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_E mission=34 speed=%d driveStyle=AVOID_CARS playerCar=%p "
                     "JOIN_ROAD: linkId %u->%u areaId %u->%u heading_pre=%.3f heading_post=%.3f lane=%d "
                     "playerHeading=%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost, (int)ap.m_nCurrentLane,
                playerH,
                (linkPre == linkPost ? "ATENCAO: linkId nao mudou apos JoinRoad!" : "JoinRoad OK snap"));
        }
        break;
    }

    // ── DIRETO: GotoCoords ─────────────────────────────────────
    case DriveMode::DIRETO:
    {
        CVector dest          = player->GetPosition();
        ap.m_nCarMission      = MISSION_GOTOCOORDS;  // =8
        ap.m_pTargetCar       = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed     = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        LogDrive("SetupDriveMode: DIRETO mission=8 speed=%d dest=(%.1f,%.1f,%.1f)",
            (int)ap.m_nCruiseSpeed, dest.x, dest.y, dest.z);
        break;
    }

    // ── PARADO: StopForever ────────────────────────────────────
    case DriveMode::PARADO:
    {
        ap.m_nCarMission      = MISSION_STOP_FOREVER;  // =11
        ap.m_pTargetCar       = nullptr;
        ap.m_nCruiseSpeed     = 0;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        LogDrive("SetupDriveMode: PARADO mission=11 speed=0");
        break;
    }

    default:
        LogWarn("SetupDriveMode: modo desconhecido %d", (int)mode);
        break;
    }
}

// ───────────────────────────────────────────────────────────────────
// ProcessDrivingAI
// Per-frame: zonas STOP/SLOW, offroad, recovery, speed adaptativa,
// auto-degrade CIVICO_D, periodic road snap, dump AI throttled.
// ───────────────────────────────────────────────────────────────────
void ProcessDrivingAI(CPlayerPed* player)
{
    if (!IsCarValid()) return;

    CVehicle*   veh       = g_car;
    CAutoPilot& ap        = veh->m_autoPilot;
    CVector     vPos      = veh->GetPosition();
    CVector     playerPos = player->GetPosition();
    float       dist      = Dist2D(vPos, playerPos);

    // ── ZONA STOP: recruta completamente parado ──────────────────
    if (dist < STOP_ZONE_M)
    {
        ap.m_nCruiseSpeed = 0;
        ap.m_nCarMission  = MISSION_STOP_FOREVER;
        return;
    }

    // ── ZONA SLOW: recruta abranda + restaura missao CIVICO ──────
    // A STOP zone pode ter sobrescrito mission=STOP_FOREVER(11).
    // A SLOW zone restaura a missao CIVICO para que o road-follow
    // retome assim que o carro sair da zona (dist > SLOW_ZONE_M).
    if (dist < SLOW_ZONE_M)
    {
        ap.m_nCruiseSpeed = SPEED_SLOW;
        if (IsCivicoMode(g_driveMode))
        {
            eCarMission expectedM = GetExpectedMission(g_driveMode);
            if (ap.m_nCarMission != expectedM)
            {
                CVehicle* pCar = player->bInVehicle ? player->m_pVehicle : nullptr;
                if (!g_slowZoneRestoring)
                {
                    LogDrive("SLOW_ZONE: dist=%.1fm missao_atual=%d->%s restaurada "
                             "speed=%d targetCar=%s (road-follow retomara ao sair da zona)",
                        dist, (int)ap.m_nCarMission,
                        (expectedM == MISSION_43) ? "MISSION_43(EscortRear)" : "MISSION_34(FollowFaraway)",
                        (int)SPEED_SLOW,
                        pCar ? "valido" : "nullptr(pe)");
                    g_slowZoneRestoring = true;
                }
                ap.m_nCarMission = expectedM;
                if (pCar) ap.m_pTargetCar = pCar;
            }
        }
        return;
    }

    // Saiu da SLOW_ZONE (dist >= SLOW_ZONE_M): re-snap ao road-graph
    // imediatamente para re-alinhar apos a pausa forcada.
    if (g_slowZoneRestoring)
    {
        g_slowZoneRestoring = false;
        if (IsCivicoMode(g_driveMode) && !g_isOffroad)
        {
            unsigned linkBefore = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(veh);
            unsigned linkAfter = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            g_civicRoadSnapTimer = 0;  // reset snap timer — acabamos de fazer snap
            LogDrive("SLOW_ZONE: saiu (dist=%.1fm) — re-snap road-graph linkId %u->%u (road-follow retomado)",
                dist, linkBefore, linkAfter);
        }
        else
        {
            LogDrive("SLOW_ZONE: saiu (dist=%.1fm) — road-follow normal retomado", dist);
        }
    }

    // ── Verificacao de offroad (throttled) ───────────────────────
    if (g_offroadTimer <= 0)
    {
        bool wasOffroad = g_isOffroad;
        g_isOffroad     = DetectOffroad(veh);
        g_offroadTimer  = OFFROAD_CHECK_INTERVAL;
        if (g_isOffroad != wasOffroad)
            LogDrive("Offroad: %s -> %s (modo=%s)",
                wasOffroad ? "SIM" : "NAO",
                g_isOffroad  ? "SIM" : "NAO",
                DriveModeName(g_driveMode));
    }
    else
    {
        --g_offroadTimer;
    }

    // ── Offroad + CIVICO: comutar para DIRETO temporario ─────────
    if (g_isOffroad && IsCivicoMode(g_driveMode))
    {
        ap.m_nCruiseSpeed     = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        ap.m_nCarMission      = MISSION_GOTOCOORDS;
        ap.m_vecDestinationCoors = playerPos;
        g_civicRoadSnapTimer  = 0;  // reset snap: proximo snap sera depois de voltar a estrada
        return;
    }

    // ── Modo DIRETO: actualizar destino periodicamente ───────────
    if (g_driveMode == DriveMode::DIRETO)
    {
        if (g_diretoTimer <= 0)
        {
            ap.m_vecDestinationCoors = playerPos;
            g_diretoTimer = DIRETO_UPDATE_INTERVAL;
        }
        else
        {
            --g_diretoTimer;
        }
        ap.m_nCruiseSpeed     = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
        // Re-impor GOTOCOORDS por frame: o jogo pode sobrescrever para STOP_FOREVER
        ap.m_nCarMission = MISSION_GOTOCOORDS;
        return;
    }

    // ── Modo PARADO: nada a fazer per-frame ──────────────────────
    if (g_driveMode == DriveMode::PARADO)
        return;

    // ═══════════════════════════════════════════════════════════════
    // A partir daqui: modos CIVICO em estrada (CIVICO_D ou CIVICO_E)
    // ═══════════════════════════════════════════════════════════════

    // ── Guard: link ID invalido ──────────────────────────────────
    // JoinCarWithRoadSystem pode produzir linkId=0xFFFFFE1E (visto em log).
    // Com link invalido, ClipTargetOrientationToLink devolve lixo → WRONG_DIR.
    {
        unsigned linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        if (linkId > MAX_VALID_LINK_ID)
        {
            if (!g_wasInvalidLink)
            {
                LogDrive("INVALID_LINK: linkId=%u (invalido! MAX=%u) "
                         "— fallback DIRETO temporario para evitar WRONG_DIR",
                    linkId, MAX_VALID_LINK_ID);
                g_wasInvalidLink = true;
            }
            ap.m_nCruiseSpeed     = SPEED_DIRETO;
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_PLOUGH_THROUGH;
            ap.m_nCarMission      = MISSION_GOTOCOORDS;
            ap.m_vecDestinationCoors = playerPos;
            return;
        }
        if (g_wasInvalidLink)
        {
            LogDrive("INVALID_LINK: linkId=%u — link valido restaurado, retomando CIVICO e re-snap road-graph",
                linkId);
            g_wasInvalidLink = false;
            CCarCtrl::JoinCarWithRoadSystem(veh);
            g_civicRoadSnapTimer = 0;
        }
    }

    // ── Recuperacao de MISSION_STOP_FOREVER inesperado ───────────
    // APENAS MISSION_STOP_FOREVER(11) e recuperado.
    // Estados intermédios 31/53 sao normais (road-SM interno) — NAO sobrescrever.
    if (g_missionRecoveryTimer > 0) --g_missionRecoveryTimer;
    {
        eCarMission expectedMission = GetExpectedMission(g_driveMode);
        if (ap.m_nCarMission == MISSION_STOP_FOREVER && g_missionRecoveryTimer <= 0)
        {
            CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            LogDrive("MISSION_RECOVERY: STOP_FOREVER(11) detectado fora das zonas "
                     "— restaurar %s targetCar=%s (modo=%s)",
                (expectedMission == MISSION_43) ? "MISSION_43(EscortRear)" : "MISSION_34(FollowFaraway)",
                playerCar ? "valido" : "nullptr(pe)",
                DriveModeName(g_driveMode));
            ap.m_nCarMission      = expectedMission;
            if (playerCar) ap.m_pTargetCar = playerCar;
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
            g_missionRecoveryTimer = 30;
        }
    }

    // Speed adaptativa + alinhamento de faixa (para logging)
    ap.m_nCruiseSpeed = AdaptiveSpeed(veh, SPEED_CIVICO);
    float targetHeading = ApplyLaneAlignment(veh);

    // Re-sincronizar target car se jogador mudou de veiculo
    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
    if (playerCar && ap.m_pTargetCar != playerCar)
    {
        ap.m_pTargetCar = playerCar;
        CCarCtrl::JoinCarWithRoadSystem(veh);
        g_civicRoadSnapTimer = 0;
        LogDrive("ProcessDrivingAI: target_car atualizado para %p e JoinRoadSystem re-emitido",
            static_cast<void*>(playerCar));
    }

    // ── CIVICO_D auto-degrade: MISSION_43 → MISSION_34 a curta distancia ──
    // MISSION_43 (EscortRearFaraway) tem comportamento "chase" ao virar
    // bruscamente a curta distancia: copia o heading do jogador frame a frame.
    // MISSION_34 (FollowCarFaraway) e menos agressivo nessa situacao.
    // Abaixo de MEDIUM_DIST_M (25m): baixar para MISSION_34.
    // Acima de MEDIUM_DIST_M:        restaurar MISSION_43.
    // NOTA: MISSION_RECOVERY ja tratou STOP_FOREVER acima; aqui apenas
    // alternamos entre MISSION_43 e MISSION_34 (ambas validas para CIVICO).
    if (g_driveMode == DriveMode::CIVICO_D && playerCar)
    {
        eCarMission idealMission = (dist < MEDIUM_DIST_M) ? MISSION_34 : MISSION_43;
        if (ap.m_nCarMission != idealMission)
        {
            // So altera se a missao actual e uma das duas CIVICO validas
            // (nao interfere com estados intermédios 31/53 do road-SM)
            if (ap.m_nCarMission == MISSION_43 || ap.m_nCarMission == MISSION_34)
            {
                LogDrive("CIVICO_D: dist=%.1fm auto-%s %s -> %s (MEDIUM_DIST=%.0fm)",
                    dist,
                    (dist < MEDIUM_DIST_M) ? "degrade(perto:menos_chase)" : "restore(longe:road_follow)",
                    (ap.m_nCarMission == MISSION_43) ? "MISSION_43" : "MISSION_34",
                    (idealMission    == MISSION_43) ? "MISSION_43" : "MISSION_34",
                    MEDIUM_DIST_M);
                ap.m_nCarMission = idealMission;
                ap.m_pTargetCar  = playerCar;
            }
        }
    }

    // ── Periodic road snap: JoinCarWithRoadSystem a cada ~5s ─────
    // Re-snap periodico para manter o veiculo alinhado com os nos
    // de estrada e reduzir desvios de faixa acumulados.
    // Apenas em modos CIVICO e em estrada (nao offroad).
    if (IsCivicoMode(g_driveMode) && !g_isOffroad)
    {
        if (++g_civicRoadSnapTimer >= ROAD_SNAP_INTERVAL)
        {
            g_civicRoadSnapTimer = 0;
            unsigned linkBefore = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(veh);
            unsigned linkAfter = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            LogDrive("PERIODIC_ROAD_SNAP: dist=%.1fm linkId %u->%u (re-snap ao road-graph a cada %ds)",
                dist, linkBefore, linkAfter, ROAD_SNAP_INTERVAL / 60);
        }
    }
    else
    {
        // Reset snap timer quando saimos de CIVICO ou estamos offroad
        g_civicRoadSnapTimer = 0;
    }

    // ── Deteccao de WRONG_DIR por transicao (nao throttled) ──────
    {
        float vH    = veh->GetHeading();
        float tH    = targetHeading;
        float dH    = tH - vH;
        while (dH >  3.14159f) dH -= 6.28318f;
        while (dH < -3.14159f) dH += 6.28318f;
        float absDH = dH < 0.0f ? -dH : dH;
        bool isWrong = (absDH > WRONG_DIR_THRESHOLD_RAD);
        if (isWrong != g_wasWrongDir)
        {
            float physSpeedWD = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
            if (isWrong)
                LogDrive("WRONG_DIR_START: heading=%.3f targetH=%.3f deltaH=%.3f physSpeed=%.0fkmh "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d straightLine=%d",
                    vH, tH, dH, physSpeedWD,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane,
                    (int)ap.m_nStraightLineDistance);
            else
                LogDrive("WRONG_DIR_END:   heading=%.3f targetH=%.3f deltaH=%.3f physSpeed=%.0fkmh "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d",
                    vH, tH, dH, physSpeedWD,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane);
            g_wasWrongDir = isWrong;
        }
    }

    // ── Dump AI throttled a cada ~2s (120 frames) ──────────────
    if (++g_logAiFrame >= 120)
    {
        g_logAiFrame = 0;
        float vehHeading = veh->GetHeading();
        float deltaH     = targetHeading - vehHeading;
        while (deltaH >  3.14159f) deltaH -= 6.28318f;
        while (deltaH < -3.14159f) deltaH += 6.28318f;
        float absDeltaH = deltaH < 0.0f ? -deltaH : deltaH;
        float speedMult = CCarCtrl::FindSpeedMultiplierWithSpeedFromNodes(ap.m_nStraightLineDistance);
        float physSpeed = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
        char taskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
            BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
        }
        LogAI("DRIVING_1: dist=%.1fm speed_ap=%d physSpeed=%.0fkmh mission=%d driveStyle=%d "
              "offroad=%d modo=%s heading=%.3f targetH=%.3f deltaH=%.3f(%s) speedMult=%.2f",
            dist, (int)ap.m_nCruiseSpeed, physSpeed,
            (int)ap.m_nCarMission, (int)ap.m_nCarDrivingStyle, (int)g_isOffroad,
            DriveModeName(g_driveMode),
            vehHeading, targetHeading, deltaH,
            (absDeltaH > WRONG_DIR_THRESHOLD_RAD)  ? "WRONG_DIR!" :
            (absDeltaH > MISALIGNED_THRESHOLD_RAD) ? "desalinhado" : "OK",
            speedMult);
        LogAI("DRIVING_2: straight=%d lane=%d linkId=%u areaId=%u "
              "dest=(%.1f,%.1f,%.1f) targetCar=%p car=%p snapTimer=%d tasks=%s",
            (int)ap.m_nStraightLineDistance,
            (int)ap.m_nCurrentLane,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
            ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z,
            (void*)ap.m_pTargetCar,
            static_cast<void*>(veh),
            g_civicRoadSnapTimer,
            taskBuf);
    }
}

// ───────────────────────────────────────────────────────────────────
// ProcessEnterCar — aguardar animacao de entrada e transitar para DRIVING
// ───────────────────────────────────────────────────────────────────
void ProcessEnterCar(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        LogError("ProcessEnterCar: recruta invalido/morto durante ENTER_CAR — resetar para INACTIVE");
        g_recruit = nullptr; g_car = nullptr;
        g_state = ModState::INACTIVE;
        return;
    }

    if (g_recruit->bInVehicle && g_recruit->m_pVehicle)
    {
        g_car   = g_recruit->m_pVehicle;
        g_state = ModState::DRIVING;
        LogEvent("ProcessEnterCar: recruta entrou no carro %p -> estado DRIVING, modo=%s",
            static_cast<void*>(g_car), DriveModeName(g_driveMode));
        SetupDriveMode(player, g_driveMode);
        ShowMsg("~g~RECRUTA A CONDUZIR [4=modo, 3=passageiro, 2=sair]");
        return;
    }

    if (--g_enterCarTimer <= 0)
    {
        LogWarn("ProcessEnterCar: TIMEOUT apos %d frames — recruta nao conseguiu entrar. Voltando a ON_FOOT.",
            ENTER_CAR_TIMEOUT);
        ShowMsg("~r~Recruta nao conseguiu entrar no carro.");
        g_passiveTimer = 0;
        AddRecruitToGroup(player);
        g_state = ModState::ON_FOOT;
    }
}

// ───────────────────────────────────────────────────────────────────
// ProcessDriving — per-frame quando recruta esta a conduzir
// ───────────────────────────────────────────────────────────────────
void ProcessDriving(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        LogError("ProcessDriving: recruta invalido/morto — dismiss");
        DismissRecruit(player);
        ShowMsg("~r~Recruta perdido.");
        return;
    }

    if (!g_recruit->bInVehicle)
    {
        if (!IsCarValid())
        {
            LogEvent("ProcessDriving: recruta saiu do carro e carro foi destruido/removido — g_car=null");
            g_car = nullptr;
        }
        else
        {
            LogEvent("ProcessDriving: recruta saiu do carro %p (preservado para re-entrada)",
                static_cast<void*>(g_car));
        }
        g_passiveTimer = 0;
        AddRecruitToGroup(player);
        g_state = ModState::ON_FOOT;
        ShowMsg("~y~Recruta saiu do carro — a seguir a pe. [2=retomar]");
        return;
    }

    if (g_recruit->m_pVehicle && g_recruit->m_pVehicle != g_car)
    {
        LogEvent("ProcessDriving: recruta mudou de carro %p -> %p",
            static_cast<void*>(g_car), static_cast<void*>(g_recruit->m_pVehicle));
        g_car = g_recruit->m_pVehicle;
    }

    ProcessDrivingAI(player);
}

// ───────────────────────────────────────────────────────────────────
// ProcessPassenger — per-frame quando jogador e passageiro
// ───────────────────────────────────────────────────────────────────
void ProcessPassenger(CPlayerPed* player)
{
    if (!IsRecruitValid() || !IsCarValid())
    {
        DismissRecruit(player);
        ShowMsg("~r~Recruta ou carro perdido.");
        return;
    }
    ProcessDrivingAI(player);
}
