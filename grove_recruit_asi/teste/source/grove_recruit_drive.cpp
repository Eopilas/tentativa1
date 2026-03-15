/*
 * grove_recruit_drive.cpp
 *
 * IA de conducao do recruta:
 *   - DetectOffroad, AdaptiveSpeed, ApplyLaneAlignment
 *   - FindNearestFreeCar
 *   - SetupDriveMode (configura CAutoPilot no momento de mudar de modo)
 *   - ProcessDrivingAI (per-frame: zonas STOP/SLOW, offroad, recovery,
 *       periodic JoinCarWithRoadSystem)
 *   - ProcessEnterCar, ProcessDriving, ProcessPassenger
 *
 * MELHORIAS DE CONDUCAO CIVICO:
 *   1. DriveStyle STOP_FOR_CARS_IGNORE_LIGHTS (=4): para para obstaculos
 *      mas ignora semaforos — evita que o recruta fique preso em semaforos
 *      vermelhos enquanto o jogador continua a circular.
 *      NOTA: o road-following e controlado pela missao (MISSION_43/34 usam
 *      o road-graph do CCarCtrl), NAO pelo driveStyle.
 *      O trafego vanilla usa MISSION_CRUISE(1) com driveStyle 0 ou 4 —
 *      sao missoess fundamentalmente diferentes das nossas.
 *
 *   2. Periodic road snap: JoinCarWithRoadSystem a cada ROAD_SNAP_INTERVAL (3s)
 *      em modos CIVICO para manter alinhamento com os nos de estrada.
 *      Snap ignorado durante WRONG_DIR (SetupDriveMode trata da recuperacao).
 *
 * NOTA: SLOW_ZONE re-snap REMOVIDO (causava INVALID_LINK → beelining).
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
//
// NOTA: FindSpeedMultiplierWithSpeedFromNodes(m_nStraightLineDistance)
// e inutil para MISSION_43/34: o campo m_nStraightLineDistance fica
// fixo em 20 (valor minimo constante) nestas missoes, devolvendo sempre
// mult=1.0 — sem qualquer reducao de velocidade em curvas.
//
// FIX: usa a diferenca de heading entre o veiculo e o targetHeading
// (calculado por ApplyLaneAlignment) como indicador de curvatura:
//   |dH| <= 0.3 rad  → em reta:    mult=1.0 (velocidade cheia)
//   0.3 < |dH| <= 1.5 → em curva:  mult linear de 1.0 ate 0.5 (abranda)
//   |dH| >  1.5 rad  → WRONG_DIR:  mult=0.3 (minimo — sentido contrario)
// ───────────────────────────────────────────────────────────────────
unsigned char AdaptiveSpeed(CVehicle* veh, float targetHeading, unsigned char baseSpeed)
{
    if (!veh) return baseSpeed;

    float vH    = veh->GetHeading();
    float dH    = targetHeading - vH;
    while (dH >  3.14159f) dH -= 6.28318f;
    while (dH < -3.14159f) dH += 6.28318f;
    float absDH = dH < 0.0f ? -dH : dH;

    float mult;
    if (absDH <= MISALIGNED_THRESHOLD_RAD)
        mult = 1.0f;
    else if (absDH <= WRONG_DIR_THRESHOLD_RAD)
        mult = 1.0f - (absDH - MISALIGNED_THRESHOLD_RAD) /
               (WRONG_DIR_THRESHOLD_RAD - MISALIGNED_THRESHOLD_RAD) * CURVE_SPEED_REDUCTION;
    else
        mult = 0.3f;

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
    // ── CIVICO-D: EscortRearFaraway (67), STOP_IGNORE_LIGHTS ────────
    // MC_ESCORT_REAR_FARAWAY: road-graph, escolta atras do jogador.
    // STOP_FOR_CARS_IGNORE_LIGHTS: para obstaculos, ignora semaforos.
    // Nota: Quando proximo (<CLOSE_RANGE_SWITCH_DIST), ProcessDrivingAI
    // substitui MC_ESCORT_REAR(31) por MC_FOLLOWCAR_CLOSE(53) para evitar
    // comportamento de "chase geometrico" (posicionamento exacto-atras).
    case DriveMode::CIVICO_D:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_D sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MC_ESCORT_REAR_FARAWAY;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        {
            unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre = recruitCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            LogDrive("SetupDriveMode: CIVICO_D mission=EscortRearFaraway(67) speed=%d "
                     "driveStyle=STOP_IGNORE_LIGHTS playerCar=%p "
                     "linkId %u->%u areaId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, recruitCar->GetHeading(),
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-E: FollowCarFaraway (52), STOP_IGNORE_LIGHTS ─────────
    // MC_FOLLOWCAR_FARAWAY: road-graph, segue carro a distancia.
    // Transiciona para MC_FOLLOWCAR_CLOSE(53) quando proximo.
    case DriveMode::CIVICO_E:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_E sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        {
            unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre = recruitCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            LogDrive("SetupDriveMode: CIVICO_E mission=FollowCarFaraway(52) speed=%d "
                     "driveStyle=STOP_IGNORE_LIGHTS playerCar=%p "
                     "linkId %u->%u areaId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, recruitCar->GetHeading(),
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-F: EscortRearFaraway (67), AVOID_CARS ────────────────
    // Identico a CIVICO_D mas com DRIVINGSTYLE_AVOID_CARS(2):
    // recruta tenta desviar do trafego em vez de parar atras dele.
    // A mesma logica de CLOSE_RANGE_SWITCH_DIST aplica-se em ProcessDrivingAI.
    case DriveMode::CIVICO_F:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_F sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MC_ESCORT_REAR_FARAWAY;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            LogDrive("SetupDriveMode: CIVICO_F mission=EscortRearFaraway(67) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p linkId %u->%u (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost,
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-G: FollowCarClose (53), AVOID_CARS ───────────────────
    // MC_FOLLOWCAR_CLOSE: segue o mesmo trajecto do jogador de perto.
    // AVOID_CARS: desvia do trafego. Melhor para seguimento agressivo proximo.
    case DriveMode::CIVICO_G:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_G sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MC_FOLLOWCAR_CLOSE;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            LogDrive("SetupDriveMode: CIVICO_G mission=FollowCarClose(53) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p linkId %u->%u (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost,
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── DIRETO: GotoCoords com offset atras do jogador ──────────────
    // CORRECAO v2: STOP_FOR_CARS_IGNORE_LIGHTS em vez de PLOUGH_THROUGH.
    //   Bug anterior: PLOUGH_THROUGH fazia recruta bater no carro do jogador
    //   ou passar por cima — o carro ignorava todos os obstaculos incluindo
    //   o proprio jogador. Fix: para atras do carro do jogador.
    // Destino = DIRETO_FOLLOW_OFFSET metros atras do jogador (por heading)
    //   para evitar que o recruta tente chegar exactamente ao jogador.
    case DriveMode::DIRETO:
    {
        float   pH;
        if (player->bInVehicle && player->m_pVehicle)
            pH = player->m_pVehicle->GetHeading();
        else
            pH = player->m_fCurrentRotation;
        CVector pFwd(std::sinf(pH), std::cosf(pH), 0.0f);
        CVector dest = player->GetPosition() - pFwd * DIRETO_FOLLOW_OFFSET;
        dest.z = player->GetPosition().z;

        ap.m_nCarMission         = MISSION_GOTOCOORDS;
        ap.m_pTargetCar          = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed        = SPEED_DIRETO;
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        LogDrive("SetupDriveMode: DIRETO mission=GotoCoords(8) speed=%d "
                 "driveStyle=STOP_IGNORE_LIGHTS dest=(%.1f,%.1f,%.1f) offset=%.0fm",
            (int)ap.m_nCruiseSpeed, dest.x, dest.y, dest.z, DIRETO_FOLLOW_OFFSET);
        break;
    }

    // ── PARADO: StopForever ─────────────────────────────────────────
    case DriveMode::PARADO:
    {
        ap.m_nCarMission      = MISSION_STOP_FOREVER;
        ap.m_pTargetCar       = nullptr;
        ap.m_nCruiseSpeed     = 0;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        LogDrive("SetupDriveMode: PARADO mission=StopForever(11) speed=0");
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
// periodic road snap, dump AI throttled.
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
                    LogDrive("SLOW_ZONE: dist=%.1fm missao_atual=%d -> mission=%d restaurada "
                             "speed=%d targetCar=%s modo=%s",
                        dist, (int)ap.m_nCarMission, (int)expectedM,
                        (int)SPEED_SLOW,
                        pCar ? "valido" : "nullptr(pe)",
                        DriveModeName(g_driveMode));
                    g_slowZoneRestoring = true;
                }
                ap.m_nCarMission = expectedM;
                if (pCar) ap.m_pTargetCar = pCar;
            }
        }
        return;
    }

    // Saiu da SLOW_ZONE (dist >= SLOW_ZONE_M): road-follow retomado.
    // NOTA: o re-snap via JoinCarWithRoadSystem foi REMOVIDO daqui porque
    // produzia linkId invalido (0xFFFFFE1E) quando o carro estava numa posicao
    // critica (interseccao, passeio, etc.) → activava o guard INVALID_LINK →
    // recruta beelining no passeio por ate ~28 segundos.
    // O snap periodico (ROAD_SNAP_INTERVAL=180fr=3s) e suficiente para re-alinhar.
    if (g_slowZoneRestoring)
    {
        g_slowZoneRestoring = false;
        LogDrive("SLOW_ZONE: saiu (dist=%.1fm) — road-follow retomado (snap periodico em %d frames)",
            dist, ROAD_SNAP_INTERVAL - g_civicRoadSnapTimer);
    }

    // ── Verificacao de offroad (throttled) ───────────────────────
    if (g_offroadTimer <= 0)
    {
        bool wasOffroad = g_isOffroad;
        g_isOffroad     = DetectOffroad(veh);
        g_offroadTimer  = OFFROAD_CHECK_INTERVAL;
        if (g_isOffroad != wasOffroad)
        {
            LogDrive("Offroad: %s -> %s (modo=%s)",
                wasOffroad ? "SIM" : "NAO",
                g_isOffroad  ? "SIM" : "NAO",
                DriveModeName(g_driveMode));
            // Ao entrar em offroad em modo CIVICO: snap imediato ao road-graph
            // para tentar re-alinhar com a estrada (sem beelining).
            if (g_isOffroad && IsCivicoMode(g_driveMode))
            {
                CCarCtrl::JoinCarWithRoadSystem(veh);
                g_civicRoadSnapTimer = 0;
                LogDrive("OFFROAD_ENTER_SNAP: JoinCarWithRoadSystem ao detectar offroad (CIVICO — sem beelining)");
            }
        }
    }
    else
    {
        --g_offroadTimer;
    }

    // ── Offroad + CIVICO: impedir snap periodico extra, sem beelining ─
    // Em CIVICO o recruta deve seguir pela estrada mesmo quando offroad.
    // GOTOCOORDS+PLOUGH_THROUGH (beelining) e apenas para DIRETO.
    // Mantemos g_civicRoadSnapTimer=0 para forcar re-snap ao sair de offroad.
    if (g_isOffroad && IsCivicoMode(g_driveMode))
    {
        g_civicRoadSnapTimer = 0;
    }

    // ── Modo DIRETO: actualizar destino com offset atras do jogador ─
    // CORRECAO v2: STOP_FOR_CARS_IGNORE_LIGHTS (v1 usava PLOUGH_THROUGH).
    // Destino = DIRETO_FOLLOW_OFFSET metros atras do jogador por heading.
    if (g_driveMode == DriveMode::DIRETO)
    {
        if (g_diretoTimer <= 0)
        {
            float pH = player->bInVehicle && player->m_pVehicle
                       ? player->m_pVehicle->GetHeading()
                       : player->m_fCurrentRotation;
            CVector pFwd(std::sinf(pH), std::cosf(pH), 0.0f);
            CVector dest = playerPos - pFwd * DIRETO_FOLLOW_OFFSET;
            dest.z = playerPos.z;
            ap.m_vecDestinationCoors = dest;
            g_diretoTimer = DIRETO_UPDATE_INTERVAL;
        }
        else
        {
            --g_diretoTimer;
        }
        ap.m_nCruiseSpeed     = SPEED_DIRETO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        ap.m_nCarMission      = MISSION_GOTOCOORDS;  // re-impor por frame
        return;
    }

    // ── Modo PARADO: nada a fazer per-frame ──────────────────────
    if (g_driveMode == DriveMode::PARADO)
        return;

    // ═══════════════════════════════════════════════════════════════
    // A partir daqui: modos CIVICO (CIVICO_D/E/F/G) em estrada
    // ═══════════════════════════════════════════════════════════════

    // Garantir driveStyle correcto por modo:
    //   CIVICO_D/E: STOP_FOR_CARS_IGNORE_LIGHTS(4)
    //   CIVICO_F/G: AVOID_CARS(2)
    if (g_driveMode == DriveMode::CIVICO_F || g_driveMode == DriveMode::CIVICO_G)
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
    else
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;

    // ── Guard: link ID invalido ──────────────────────────────────
    // JoinCarWithRoadSystem pode produzir linkId=0xFFFFFE1E (visto em log).
    // Com link invalido, ClipTargetOrientationToLink devolve lixo → WRONG_DIR.
    // ANTERIOR: fallback para GOTOCOORDS+PLOUGH_THROUGH (beelining) — REMOVIDO.
    //   Causa: recruta sobe passeios e conduz na faixa errada durante ~28s.
    // FIX: ao detectar link invalido, tentar re-snap imediato UMA VEZ.
    //   Se re-snap corrigir: continua CIVICO normalmente.
    //   Se re-snap falhar: manter missao CIVICO com velocidade reduzida
    //     (sem beelining); snap periodico vai tentar corrigir nos proximos 5s.
    {
        unsigned linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        if (linkId > MAX_VALID_LINK_ID)
        {
            if (!g_wasInvalidLink)
            {
                g_wasInvalidLink = true;
                ++g_invalidLinkCounter;
                LogDrive("INVALID_LINK: linkId=%u (invalido! MAX=%u) consecutivos=%d — %s",
                    linkId, MAX_VALID_LINK_ID, g_invalidLinkCounter,
                    g_invalidLinkCounter > 5
                        ? "STORM detectado — pausando snap 120 frames"
                        : "re-snap imediato (sem beelining)");

                if (g_invalidLinkCounter > 5)
                {
                    LogWarn("INVALID_LINK_STORM: %d links invalidos consecutivos — pausando snap 120 frames",
                        g_invalidLinkCounter);
                    g_civicRoadSnapTimer = -120;  // valor negativo = pausa de snap
                    g_invalidLinkCounter = 0;
                }
                else
                {
                    CCarCtrl::JoinCarWithRoadSystem(veh);
                    g_civicRoadSnapTimer = 0;
                    linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                    if (linkId <= MAX_VALID_LINK_ID)
                    {
                        LogDrive("INVALID_LINK: re-snap corrigiu -> linkId=%u, CIVICO restaurado", linkId);
                        g_wasInvalidLink = false;
                        g_invalidLinkCounter = 0;
                        // fall through to normal CIVICO processing
                    }
                    else
                    {
                        LogDrive("INVALID_LINK: re-snap ainda invalido -> linkId=%u, "
                                 "CIVICO reduzido (snap periodico vai corrigir)", linkId);
                    }
                }
            }
            if (g_wasInvalidLink)
            {
                // Ainda invalido: reduzir velocidade mas manter CIVICO (sem beelining).
                // O snap periodico (abaixo) vai chamar JoinCarWithRoadSystem e corrigir.
                ap.m_nCruiseSpeed = SPEED_SLOW;
                // Nao usar return aqui: deixar o snap periodico correr neste frame.
            }
        }
        else if (g_wasInvalidLink)
        {
            LogDrive("INVALID_LINK: linkId=%u — link valido restaurado, retomando CIVICO e re-snap road-graph",
                linkId);
            g_wasInvalidLink = false;
            g_invalidLinkCounter = 0;
            CCarCtrl::JoinCarWithRoadSystem(veh);
            g_civicRoadSnapTimer = 0;
        }
    }

    // ── Recuperacao de MISSION_STOP_FOREVER inesperado ───────────
    // APENAS MISSION_STOP_FOREVER(11) e recuperado.
    // Estados intermédios (31/52/53/67) sao normais (road-SM interno).
    if (g_missionRecoveryTimer > 0) --g_missionRecoveryTimer;
    {
        eCarMission expectedMission = GetExpectedMission(g_driveMode);
        if (ap.m_nCarMission == MISSION_STOP_FOREVER && g_missionRecoveryTimer <= 0)
        {
            CVehicle* playerCar2 = player->bInVehicle ? player->m_pVehicle : nullptr;
            eCarDrivingStyle dstyle = (g_driveMode == DriveMode::CIVICO_F || g_driveMode == DriveMode::CIVICO_G)
                                      ? DRIVINGSTYLE_AVOID_CARS
                                      : DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
            LogDrive("MISSION_RECOVERY: STOP_FOREVER(11) fora das zonas — restaurar mission=%d "
                     "targetCar=%s modo=%s",
                (int)expectedMission,
                playerCar2 ? "valido" : "nullptr(pe)",
                DriveModeName(g_driveMode));
            ap.m_nCarMission      = expectedMission;
            if (playerCar2) ap.m_pTargetCar = playerCar2;
            ap.m_nCarDrivingStyle = dstyle;
            g_missionRecoveryTimer = 30;
        }
    }

    // Speed adaptativa + alinhamento de faixa (para logging).
    float targetHeading = ApplyLaneAlignment(veh);
    unsigned char baseSpd = g_wasInvalidLink ? SPEED_SLOW : SPEED_CIVICO;
    ap.m_nCruiseSpeed = AdaptiveSpeed(veh, targetHeading, baseSpd);

    // ── Re-sincronizar target car se jogador mudou de veiculo ──────
    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
    if (playerCar && ap.m_pTargetCar != playerCar)
    {
        ap.m_pTargetCar = playerCar;
        CCarCtrl::JoinCarWithRoadSystem(veh);
        g_civicRoadSnapTimer = 0;
        LogDrive("ProcessDrivingAI: target_car atualizado -> %p + JoinRoadSystem",
            static_cast<void*>(playerCar));
    }

    // ── CLOSE_RANGE_SMOOTH: CIVICO_D/F, perto → FollowCarClose ────
    // Quando dist < CLOSE_RANGE_SWITCH_DIST e o motor SA transicionou para
    // MC_ESCORT_REAR(31): substituir por MC_FOLLOWCAR_CLOSE(53).
    // MC_ESCORT_REAR tenta posicionar-se geometricamente-exacto atras do
    // jogador, podendo sair da estrada ("chase mode") a curta distancia.
    // MC_FOLLOWCAR_CLOSE segue o mesmo trajecto do jogador sem forcar posicao.
    if ((g_driveMode == DriveMode::CIVICO_D || g_driveMode == DriveMode::CIVICO_F)
        && dist < CLOSE_RANGE_SWITCH_DIST && dist >= SLOW_ZONE_M
        && playerCar)
    {
        if (ap.m_nCarMission == MC_ESCORT_REAR)
        {
            ap.m_nCarMission = MC_FOLLOWCAR_CLOSE;
            ap.m_pTargetCar  = playerCar;
            // nao logar todos os frames — e continuo enquanto proximo
        }
    }

    // ── Periodic road snap: JoinCarWithRoadSystem a cada ~1.5s ────
    // Mais frequente (90fr) que antes (180fr) para seguir curvas.
    // Apenas em modos CIVICO, em estrada, fora de WRONG_DIR.
    // g_civicRoadSnapTimer < 0: pausa por INVALID_LINK_STORM.
    if (IsCivicoMode(g_driveMode) && !g_isOffroad && !g_wasWrongDir)
    {
        if (g_civicRoadSnapTimer < 0)
        {
            ++g_civicRoadSnapTimer;  // pausa: contar em direcao a 0
        }
        else if (++g_civicRoadSnapTimer >= ROAD_SNAP_INTERVAL)
        {
            g_civicRoadSnapTimer = 0;
            unsigned linkBefore = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(veh);
            unsigned linkAfter = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            LogDrive("PERIODIC_ROAD_SNAP: dist=%.1fm linkId %u->%u (re-snap a cada %.1fs)",
                dist, linkBefore, linkAfter, ROAD_SNAP_INTERVAL / 60.0f);
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
            {
                LogDrive("WRONG_DIR_START: heading=%.3f targetH=%.3f deltaH=%.3f physSpeed=%.0fkmh "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d straightLine=%d",
                    vH, tH, dH, physSpeedWD,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane,
                    (int)ap.m_nStraightLineDistance);

                // WRONG_DIR_RECOVER: apenas re-snap via SetupDriveMode quando LONGE.
                // Bug anterior: JoinCarWithRoadSystem a <30m snappava para no errado
                // → WRONG_DIR por 38+ segundos ("chase mode").
                // FIX v2 (dist > 30m): SetupDriveMode + JoinCarWithRoadSystem OK.
                // FIX v3 (dist <= 30m, SOFT): trocar missao para MC_FOLLOWCAR_CLOSE(53)
                //   que segue o mesmo trajecto sem posicao geometrica forçada.
                //   Nao chama JoinCarWithRoadSystem (evita o re-snap errado ao perto).
                if (IsCivicoMode(g_driveMode) && player->bInVehicle)
                {
                    if (dist > WRONG_DIR_RECOVERY_DIST_M)
                    {
                        SetupDriveMode(player, g_driveMode);
                        LogDrive("WRONG_DIR_RECOVER_FAR: SetupDriveMode (dist=%.1fm > %.0fm)",
                            dist, WRONG_DIR_RECOVERY_DIST_M);
                    }
                    else if (playerCar)
                    {
                        // Soft recovery: mudar para FollowCarClose sem JoinRoadSystem
                        ap.m_nCarMission = MC_FOLLOWCAR_CLOSE;
                        ap.m_pTargetCar  = playerCar;
                        LogDrive("WRONG_DIR_RECOVER_CLOSE: soft — mission->FollowCarClose(53) "
                                 "dist=%.1fm <= %.0fm (sem JoinRoad para evitar re-snap errado)",
                            dist, WRONG_DIR_RECOVERY_DIST_M);
                    }
                }
            }
            else
            {
                LogDrive("WRONG_DIR_END:   heading=%.3f targetH=%.3f deltaH=%.3f physSpeed=%.0fkmh "
                         "modo=%s mission=%d linkId=%u areaId=%u lane=%d",
                    vH, tH, dH, physSpeedWD,
                    DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
                    (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
                    (int)ap.m_nCurrentLane);
            }
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
        // speedMult reflecte o multiplicador real de AdaptiveSpeed (heading-diff)
        float speedMult;
        if (absDeltaH <= MISALIGNED_THRESHOLD_RAD)
            speedMult = 1.0f;
        else if (absDeltaH <= WRONG_DIR_THRESHOLD_RAD)
            speedMult = 1.0f - (absDeltaH - MISALIGNED_THRESHOLD_RAD) /
                        (WRONG_DIR_THRESHOLD_RAD - MISALIGNED_THRESHOLD_RAD) * CURVE_SPEED_REDUCTION;
        else
            speedMult = 0.3f;
        float physSpeed = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
        int   tempAction = (int)ap.m_nTempAction;
        char taskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
            BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
        }
        LogAI("DRIVING_1: dist=%.1fm speed_ap=%d physSpeed=%.0fkmh mission=%d(%s) driveStyle=%d(%s) "
              "tempAction=%d(%s) offroad=%d modo=%s heading=%.3f targetH=%.3f deltaH=%.3f(%s) speedMult=%.2f",
            dist, (int)ap.m_nCruiseSpeed, physSpeed,
            (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
            (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
            tempAction, GetTempActionName(tempAction),
            (int)g_isOffroad,
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
        if (g_enterCarAsPassenger)
        {
            // Recruta entrou como passageiro no carro do jogador
            g_car   = g_recruit->m_pVehicle;
            g_state = ModState::RIDING;
            LogEvent("ProcessEnterCar: recruta entrou como PASSAGEIRO no carro %p -> estado RIDING",
                static_cast<void*>(g_car));
            ShowMsg("~g~RECRUTA A BORDO [2=sair do carro, 1=dispensar]");
        }
        else
        {
            g_car   = g_recruit->m_pVehicle;
            g_state = ModState::DRIVING;
            LogEvent("ProcessEnterCar: recruta entrou como CONDUTOR no carro %p -> estado DRIVING modo=%s",
                static_cast<void*>(g_car), DriveModeName(g_driveMode));
            SetupDriveMode(player, g_driveMode);
            ShowMsg("~g~RECRUTA A CONDUZIR [4=modo, 3=passageiro, 2=sair]");
        }
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
// ProcessPassenger — per-frame quando JOGADOR e passageiro (recruta conduz)
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

// ───────────────────────────────────────────────────────────────────
// ProcessRiding — per-frame quando recruta e passageiro no carro do jogador
// ───────────────────────────────────────────────────────────────────
void ProcessRiding(CPlayerPed* player)
{
    if (!IsRecruitValid())
    {
        LogError("ProcessRiding: recruta invalido/morto — dismiss");
        DismissRecruit(player);
        ShowMsg("~r~Recruta perdido.");
        return;
    }

    // Recruta saiu do carro por qualquer razao → voltar a pe
    if (!g_recruit->bInVehicle)
    {
        LogEvent("ProcessRiding: recruta saiu do carro -> ON_FOOT");
        g_enterCarAsPassenger = false;
        g_playerWasInVehicle  = false;
        g_car = nullptr;
        g_passiveTimer = 0;
        AddRecruitToGroup(player);
        g_state = ModState::ON_FOOT;
        ShowMsg("~y~Recruta saiu do carro — a seguir a pe.");
        return;
    }

    // Jogador saiu do carro: emitir LeaveCar ao recruta
    if (!player->bInVehicle)
    {
        if (IsCarValid() && g_recruit->m_pVehicle == g_car)
        {
            CTaskComplexLeaveCar* pTask = new CTaskComplexLeaveCar(g_car, 0, 0, true, false);
            g_recruit->m_pIntelligence->m_TaskMgr.SetTask(
                pTask, TASK_PRIMARY_PRIMARY, true);
            LogEvent("ProcessRiding: jogador saiu -> CTaskComplexLeaveCar emitida ao recruta");
        }
        g_enterCarAsPassenger = false;
        g_playerWasInVehicle  = false;
        g_car = nullptr;
        g_passiveTimer = 0;
        AddRecruitToGroup(player);
        g_state = ModState::ON_FOOT;
        ShowMsg("~y~Recruta a sair do carro — a seguir a pe.");
        return;
    }

    // Actualizar g_car se recruta mudou de veiculo (seguranca)
    if (g_recruit->m_pVehicle && g_recruit->m_pVehicle != g_car)
    {
        LogEvent("ProcessRiding: recruta mudou de carro %p -> %p",
            static_cast<void*>(g_car), static_cast<void*>(g_recruit->m_pVehicle));
        g_car = g_recruit->m_pVehicle;
    }

    // Log throttled a cada 2s
    if (++g_logAiFrame >= 120)
    {
        g_logAiFrame = 0;
        float spd = player->m_pVehicle
                    ? player->m_pVehicle->m_vecMoveSpeed.Magnitude() * 180.0f : 0.0f;
        CVector rPos = g_recruit->GetPosition();
        LogAI("RIDING: recruta_a_bordo carro=%p playerSpeed=%.0fkmh pos=(%.1f,%.1f,%.1f)",
            static_cast<void*>(g_car), spd, rPos.x, rPos.y, rPos.z);
    }
}
