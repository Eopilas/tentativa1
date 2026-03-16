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

static_assert(CLOSE_RANGE_SWITCH_DIST == 22.0f, "CLOSE_RANGE_SWITCH_DIST expected 22m (close-range speed tuning)");

// ───────────────────────────────────────────────────────────────────
// Variaveis estaticas de tracking (internas a este modulo)
// ───────────────────────────────────────────────────────────────────
static int   s_prevTempAction   = 0;       // detectar mudancas de tempAction
static float s_prevDistLog      = -1.0f;   // ultima distancia amostrada (para tendencia)
static int   s_distTrendTimer   = 0;       // throttle de dist-trend log
static bool  s_inCloseRange     = false;   // rastrear entry/exit de close-range
static bool  s_catchupActive    = false;   // FAR_CATCHUP activo
static int   s_stuckTimer       = 0;       // frames consecutivos com physSpeed < STUCK_SPEED_KMH
static int   s_stuckCooldown    = 0;       // cooldown pos-STUCK_RECOVER
static int   s_headonFrames     = 0;       // frames consecutivos com tempAction=HEADON_COLLISION
static int   s_headonCooldown   = 0;       // cooldown pos-HEADON_PERSISTENT (independente de stuckCooldown)
static eCarMission s_prevCloseRangeMission = (eCarMission)(-1); // debounce log CLOSE_RANGE_FORCE
static bool  s_closeSafeStyle   = false;   // close-range usa STOP_FOR_CARS_IGNORE_LIGHTS

// Reseta todas as variaveis de tracking de drive (chamado por DismissRecruit)
void ResetDriveStatics()
{
    s_prevTempAction        = 0;
    s_prevDistLog           = -1.0f;
    s_distTrendTimer        = 0;
    s_inCloseRange          = false;
    s_catchupActive         = false;
    s_stuckTimer            = 0;
    s_stuckCooldown         = 0;
    s_headonFrames          = 0;
    s_headonCooldown        = 0;
    s_prevCloseRangeMission = (eCarMission)(-1);
    s_closeSafeStyle        = false;
}

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
// AdaptiveSpeed: ajusta velocidade ao angulo da curva e reta.
//
// NOTA: FindSpeedMultiplierWithSpeedFromNodes(m_nStraightLineDistance)
// e inutil para MISSION_43/34/52: o campo m_nStraightLineDistance fica
// fixo em 20 (valor minimo constante) nestas missoes, devolvendo sempre
// mult=1.0 — sem qualquer reducao de velocidade em curvas.
//
// FIX: usa a diferenca de heading entre o veiculo e o targetHeading
// (calculado por ApplyLaneAlignment) como indicador de curvatura:
//   |dH| <= 0.20 rad  → reta longa:  usa SPEED_CIVICO_HIGH (velocidade maxima)
//   0.20 < |dH| <= 1.5 → curva:      mult linear de 1.0 ate (1.0-REDUCTION) sobre base
//   |dH| >  1.5 rad  → WRONG_DIR:   mult=0.3 (minimo — sentido contrario)
//
// CONSERVADOR: MISALIGNED=0.20rad (11.5°) começa a abrandar mais cedo.
//              REDUCTION=0.80 → 20% de velocidade base na curva maxima.
// Em reta (|dH|<=0.20): usa SPEED_CIVICO_HIGH (60) em vez de baseSpeed (46).
// ───────────────────────────────────────────────────────────────────
// distToPlayer: distância recruta->jogador em metros (usada para reduzir boost em close-range).
unsigned char AdaptiveSpeed(CVehicle* veh, float targetHeading, unsigned char baseSpeed, float distToPlayer)
{
    if (!veh) return baseSpeed;

    float vH    = veh->GetHeading();
    float dH    = targetHeading - vH;
    while (dH >  3.14159f) dH -= 6.28318f;
    while (dH < -3.14159f) dH += 6.28318f;
    float absDH = dH < 0.0f ? -dH : dH;

    float mult;
    unsigned char effectiveBase;

    bool closeRange = (distToPlayer < CLOSE_RANGE_SWITCH_DIST);

    if (absDH <= MISALIGNED_THRESHOLD_RAD)
    {
        // Reta: usar SPEED_CIVICO_HIGH como minimo se baseSpeed >= SPEED_CIVICO,
        // mas respeitar um baseSpeed ainda mais alto (ex: SPEED_CATCHUP em FAR_CATCHUP).
        // "boost to HIGH unless already boosted higher (catchup)"
        mult          = 1.0f;
        // Em close-range (< CLOSE_RANGE_SWITCH_DIST=22m),
        // evitar boost para SPEED_CIVICO_HIGH para não entrar
        // rápido demais em cruzamentos/curvas ao aproximar do jogador.
        if (!closeRange && baseSpeed >= SPEED_CIVICO)
            effectiveBase = (baseSpeed > SPEED_CIVICO_HIGH) ? baseSpeed : SPEED_CIVICO_HIGH;
        else
            effectiveBase = baseSpeed;
    }
    else if (absDH <= WRONG_DIR_THRESHOLD_RAD)
    {
        // Curva: abranda progressivamente
        mult          = 1.0f - (absDH - MISALIGNED_THRESHOLD_RAD) /
                        (WRONG_DIR_THRESHOLD_RAD - MISALIGNED_THRESHOLD_RAD) * CURVE_SPEED_REDUCTION;
        effectiveBase = baseSpeed;
    }
    else
    {
        // WRONG_DIR: velocidade minima
        mult          = 0.3f;
        effectiveBase = baseSpeed;
    }

    auto ideal = static_cast<unsigned char>(static_cast<float>(effectiveBase) * mult);
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
// Exclui: lista de carros passada em excludes[], carros com condutor,
// avioes/helicopteros.
//
// ESTRATEGIA DE SELECCAO (dois passes):
//   1.o passe: prefere carros ja alinhados com o road-graph (linkId valido)
//              para minimizar problemas de INVALID_LINK em CIVICO.
//   2.o passe: se nenhum no 1.o, aceita qualquer carro dentro do raio.
// Loga linkId do carro escolhido para diagnostico.
// ───────────────────────────────────────────────────────────────────
CVehicle* FindNearestFreeCar(CVector const& searchPos, CVehicle** excludes, int numExcludes)
{
    CVehicle* bestSnapped  = nullptr;
    float     bestSnappedD = FIND_CAR_RADIUS;

    CVehicle* bestAny      = nullptr;
    float     bestAnyD     = FIND_CAR_RADIUS;

    for (CVehicle* veh : *CPools::ms_pVehiclePool)
    {
        if (!veh)                        continue;
        if (veh->m_pDriver)              continue;
        if (veh->m_nVehicleSubClass > 2) continue;   // excluir avioes/helicopteros

        // Verificar lista de exclusoes
        bool excluded = false;
        for (int xi = 0; xi < numExcludes; ++xi)
            if (veh == excludes[xi]) { excluded = true; break; }
        if (excluded) continue;

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
        LogWarn("FindNearestFreeCar: nenhum carro livre num raio de %.0fm (excludes=%d)", FIND_CAR_RADIUS, numExcludes);
    }

    return best;
}

// Helper: retorna o heading actual do jogador (carro ou a pe)
static inline float GetPlayerHeading(CPlayerPed* player)
{
    return (player->bInVehicle && player->m_pVehicle)
           ? player->m_pVehicle->GetHeading()
           : player->m_fCurrentRotation;
}

// Procura waypoint activo no mapa (blip sprite 41) e devolve coordenadas.
static bool GetMapWaypoint(CVector& outPos)
{
    static constexpr unsigned int RADAR_TRACE_COUNT = 175u; // plugin-sdk SA: tRadarTrace[175]
    const unsigned char waypointSprite = static_cast<unsigned char>(RADAR_SPRITE_WAYPOINT);
    if (!CRadar::ms_RadarTrace) return false;
    for (unsigned int i = 0; i < RADAR_TRACE_COUNT; ++i)
    {
        const tRadarTrace& tr = CRadar::ms_RadarTrace[i];
        if (!tr.m_bInUse) continue;
        if (tr.m_nRadarSprite != waypointSprite) continue;
        outPos = tr.m_vecPos;
        return true;
    }
    return false;
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

    // Limpar estado de CLOSE_BLOCKED ao mudar de modo ou re-inicializar
    // (evita estado obsoleto se o utilizador mudou para outro modo CIVICO).
    if (g_closeBlocked || g_closeBlockedTimer > 0)
    {
        g_closeBlocked      = false;
        g_closeBlockedTimer = 0;
    }
    // Limpar direct-follow de offroad ao mudar de modo (o novo modo recalcula)
    g_wasOffroadDirect   = false;
    g_offroadSustainedFrames = 0;

    CVehicle*   recruitCar = g_car;
    CAutoPilot& ap         = recruitCar->m_autoPilot;
    CVehicle*   playerCar  = player->bInVehicle ? player->m_pVehicle : nullptr;

    switch (mode)
    {
    // ── CIVICO-F: EscortRearFaraway (67), AVOID_CARS ────────────────
    // MC_ESCORT_REAR_FARAWAY: road-graph, escolta atras do jogador.
    // AVOID_CARS: recruta desvia do trafego em vez de parar atras dele.
    // Quando proximo (<CLOSE_RANGE_SWITCH_DIST), ProcessDrivingAI
    // substitui MC_ESCORT_REAR(31) por MC_FOLLOWCAR_FARAWAY(52) para
    // evitar "chase geometrico" (posicionamento exacto-atras off-road).
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
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPost  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPost  = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_F mission=EscortRearFaraway(67) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u areaId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost,
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-G: FollowCarClose (53), AVOID_CARS ───────────────────
    // MC_FOLLOWCAR_CLOSE: segue o mesmo trajecto do jogador de perto.
    // AVOID_CARS: desvia do trafego. Bom para seguimento agressivo proximo.
    // Nota: MC53 pode fazer curvas mais agressivas em close range.
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
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            float    headPre  = recruitCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            float    headPost  = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_G mission=FollowCarClose(53) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, headPre, headPost,
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-H: FollowCarFaraway (52), AVOID_CARS ─────────────────
    // Melhor combinacao: road-graph (MC52) + evitamento de trafego.
    // MC_FOLLOWCAR_FARAWAY segue o trajecto do jogador pelo road-graph.
    // AVOID_CARS tenta contornar o trafego em vez de parar atras dele.
    // Quando proximo (<CLOSE_RANGE_SWITCH_DIST), o motor SA pode transicionar
    // para MC_FOLLOWCAR_CLOSE(53); CLOSE_BLOCKED WAIT gere obstrucoes proximas.
    case DriveMode::CIVICO_H:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_H sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            float    headPre  = recruitCar->GetHeading();
            CCarCtrl::JoinCarWithRoadSystem(recruitCar);
            g_civicRoadSnapTimer = 0;
            g_invalidLinkCounter = 0;
            unsigned linkPost  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            float    headPost  = recruitCar->GetHeading();
            LogDrive("SetupDriveMode: CIVICO_H mission=FollowCarFaraway(52) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, headPre, headPost,
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
        float   playerHeading = GetPlayerHeading(player);
        CVector pFwd(std::sinf(playerHeading), std::cosf(playerHeading), 0.0f);
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
// SetupDriveModeSimple
// Versao leve de SetupDriveMode para recrutas secundarios (nao o primario).
// NAO toca nos globals do primario (g_civicRoadSnapTimer, g_wasWrongDir, etc.).
// Apenas configura o autopilot do carro e chama JoinCarWithRoadSystem.
// Modos suportados: todos os CIVICO (usam AVOID_CARS + road-graph).
// Para DIRETO/PARADO nao e necessario (o primario partilha o mesmo destino).
// ───────────────────────────────────────────────────────────────────
void SetupDriveModeSimple(CPlayerPed* player, CPed* ped, CVehicle* recCar, DriveMode mode)
{
    if (!recCar || !ped || !player) return;

    CAutoPilot& ap        = recCar->m_autoPilot;
    CVehicle*   playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;

    if (IsCivicoMode(mode))
    {
        if (!playerCar)
        {
            // Sem carro do jogador: DIRETO simplificado (GotoCoords offset)
            float   h = player->m_fCurrentRotation;
            CVector pFwd(std::sinf(h), std::cosf(h), 0.0f);
            CVector dest = player->GetPosition() - pFwd * DIRETO_FOLLOW_OFFSET;
            dest.z = player->GetPosition().z;
            ap.m_nCarMission         = MISSION_GOTOCOORDS;
            ap.m_pTargetCar          = nullptr;
            ap.m_vecDestinationCoors = dest;
            ap.m_nCruiseSpeed        = SPEED_CIVICO;
            ap.m_nCarDrivingStyle    = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
            LogDrive("[recr:%p] SetupDriveModeSimple: sem playerCar -> GotoCoords offset", (void*)ped);
            return;
        }

        eCarMission  mission = GetExpectedMission(mode);
        unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        CCarCtrl::JoinCarWithRoadSystem(recCar);
        unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;

        ap.m_nCarMission      = mission;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;

        LogDrive("[recr:%p] SetupDriveModeSimple: modo=%s mission=%d playerCar=%p linkId %u->%u",
            (void*)ped, DriveModeName(mode), (int)mission,
            (void*)playerCar, linkPre, linkPost);
    }
    else if (mode == DriveMode::DIRETO)
    {
        float   h = GetPlayerHeading(player);
        CVector pFwd(std::sinf(h), std::cosf(h), 0.0f);
        CVector dest = player->GetPosition() - pFwd * DIRETO_FOLLOW_OFFSET;
        dest.z = player->GetPosition().z;
        ap.m_nCarMission         = MISSION_GOTOCOORDS;
        ap.m_pTargetCar          = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed        = SPEED_DIRETO;
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
        LogDrive("[recr:%p] SetupDriveModeSimple: DIRETO dest=(%.1f,%.1f,%.1f)",
            (void*)ped, dest.x, dest.y, dest.z);
    }
    else if (mode == DriveMode::PARADO)
    {
        ap.m_nCarMission      = MISSION_STOP_FOREVER;
        ap.m_nCruiseSpeed     = 0;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
    }
}

// ───────────────────────────────────────────────────────────────────
// ProcessMultiRecruitCars
// AI simplificado para recrutas secundarios (nao o primario g_recruit).
// Chamado por ProcessFrame a cada frame.
//
// Dois modos por recruta em g_allRecruits:
//   ridesWithPlayer=true : recruta e passageiro (sem AI de conducao proprio).
//     Apenas monitora se ainda esta num veiculo; limpa flag quando sai.
//   ridesWithPlayer=false + car!=nullptr : recruta e condutor do seu proprio carro.
//     - Aguarda entrada (enterTimer)
//     - Snap periodico ao road-graph
//     - Zonas STOP/SLOW, STOP_FOREVER recovery, saude, velocidade
//     - Logs detalhados: mission, style, tempAction, linkId, stuck, zonas
// ───────────────────────────────────────────────────────────────────
void ProcessMultiRecruitCars(CPlayerPed* player)
{
    if (!player) return;

    CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;

    for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
    {
        TrackedRecruit& tr = g_allRecruits[i];
        if (!tr.ped)        continue;
        if (tr.ped == g_recruit) continue;  // primario tem AI proprio
        if (!CPools::ms_pPedPool->IsObjectValid(tr.ped)) { tr = TrackedRecruit{}; continue; }
        if (!tr.ped->IsAlive())
        {
            tr.car = nullptr; tr.enterTimer = 0;
            tr.ridesWithPlayer = false; tr.stuckTimer = 0;
            continue;
        }

        // ── Modo PASSAGEIRO (ridesWithPlayer) ─────────────────────────
        // Auto-detectar: recruta sem car mas dentro de um veiculo = provavelmente no carro do jogador
        if (!tr.ridesWithPlayer && !tr.car && tr.ped->bInVehicle)
        {
            // Detectar automaticamente se recruta entrou no carro do jogador via vanilla SIT_IN_LEADER_CAR
            CVehicle* ridingVeh = tr.ped->m_pVehicle;
            if (ridingVeh && playerCar && ridingVeh == playerCar)
            {
                tr.ridesWithPlayer = true;
                LogMulti("[recr:%d] MULTI_RIDING_AUTODETECT: ped=%p entrou no carro do jogador=%p (SIT_IN_LEADER_CAR)",
                    i, (void*)tr.ped, (void*)ridingVeh);
            }
        }

        if (tr.ridesWithPlayer)
        {
            // Verificar se ainda esta num veiculo
            if (!tr.ped->bInVehicle)
            {
                LogMulti("[recr:%d] MULTI_RIDING_EXIT: ped=%p saiu do veiculo — ridesWithPlayer cleared",
                    i, (void*)tr.ped);
                tr.ridesWithPlayer = false;
                tr.enterTimer = 0;
                tr.stuckTimer = 0;
            }
            // Passageiro: sem AI de conducao propria
            continue;
        }

        if (!tr.car) continue;  // sem carro atribuido e nao e passageiro

        // Validar carro
        if (!CPools::ms_pVehiclePool->IsObjectValid(tr.car))
        {
            LogMulti("[recr:%d] MULTI_CAR_DESTROYED: carro=%p destruido — car cleared",
                i, (void*)tr.car);
            tr.car = nullptr; tr.enterTimer = 0; tr.stuckTimer = 0;
            continue;
        }

        if (!tr.ped->bInVehicle)
        {
            // ── Aguardar entrada no carro ──
            if (--tr.enterTimer <= 0)
            {
                LogWarn("[recr:%d] MULTI_ENTER_TIMEOUT: ped=%p timeout entrada carro=%p — car cleared",
                    i, (void*)tr.ped, (void*)tr.car);
                tr.car = nullptr; tr.enterTimer = 0;
            }
            continue;
        }

        // ── Recruta esta no carro ──────────────────────────────────
        CVehicle*   recCar = tr.car;
        CAutoPilot& ap     = recCar->m_autoPilot;

        // Distancia ao jogador
        CVector rPos = recCar->GetPosition();
        CVector pPos = player->GetPosition();
        float   dist = Dist2D(rPos, pPos);
        float   physSpeed = recCar->m_vecMoveSpeed.Magnitude() * 180.0f;

        // ── Zonas STOP/SLOW: log de transicao ─────────────────────
        bool nowStopZone = (dist < STOP_ZONE_M);
        bool nowSlowZone = (dist < SLOW_ZONE_M) && !nowStopZone;
        if (nowStopZone != tr.inStopZone)
        {
            tr.inStopZone = nowStopZone;
            LogMulti("[recr:%d] MULTI_STOP_ZONE_%s: dist=%.1fm physSpeed=%.0fkmh",
                i, nowStopZone ? "ENTER" : "EXIT", dist, physSpeed);
        }
        if (nowSlowZone != tr.inSlowZone)
        {
            tr.inSlowZone = nowSlowZone;
            LogMulti("[recr:%d] MULTI_SLOW_ZONE_%s: dist=%.1fm physSpeed=%.0fkmh",
                i, nowSlowZone ? "ENTER" : "EXIT", dist, physSpeed);
        }

        if (dist < STOP_ZONE_M)
        {
            ap.m_nCruiseSpeed = 0;
            ap.m_nCarMission  = MISSION_STOP_FOREVER;
            tr.stuckTimer = 0;
            continue;
        }
        if (dist < SLOW_ZONE_M)
        {
            ap.m_nCruiseSpeed = SPEED_SLOW;
            tr.stuckTimer = 0;
            continue;
        }

        // ── TempAction change log ──────────────────────────────────
        {
            int curTA = (int)ap.m_nTempAction;
            if (tr.prevTempAction == -1) tr.prevTempAction = curTA;  // inicializar
            if (curTA != tr.prevTempAction)
            {
                LogMulti("[recr:%d] MULTI_TEMPACTION_CHANGE: %d(%s) -> %d(%s) dist=%.1fm physSpeed=%.0fkmh",
                    i,
                    tr.prevTempAction, GetTempActionName(tr.prevTempAction),
                    curTA,             GetTempActionName(curTA),
                    dist, physSpeed);
                tr.prevTempAction = curTA;
            }
        }

        // ── STOP_FOREVER inesperado: recovery ─────────────────────
        {
            int curMission = (int)ap.m_nCarMission;
            if (tr.prevMission == -1) tr.prevMission = curMission;  // inicializar
            if (ap.m_nCarMission == MISSION_STOP_FOREVER)
            {
                LogMulti("[recr:%d] MULTI_STOP_RECOVERY: STOP_FOREVER fora de zona — re-snap e modo=%s",
                    i, DriveModeName(g_driveMode));
                SetupDriveModeSimple(player, tr.ped, recCar, g_driveMode);
                tr.prevMission = (int)ap.m_nCarMission;
            }
            else if (curMission != tr.prevMission)
            {
                LogMulti("[recr:%d] MULTI_MISSION_CHANGE: %d(%s) -> %d(%s) dist=%.1fm",
                    i,
                    tr.prevMission, GetCarMissionName(tr.prevMission),
                    curMission,     GetCarMissionName(curMission),
                    dist);
                tr.prevMission = curMission;
            }
        }

        // ── Stuck detection ────────────────────────────────────────
        if (physSpeed < STUCK_SPEED_KMH)
        {
            if (++tr.stuckTimer >= STUCK_DETECT_FRAMES)
            {
                tr.stuckTimer = 0;
                unsigned linkBefore = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                CCarCtrl::JoinCarWithRoadSystem(recCar);
                unsigned linkAfter  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                LogMulti("[recr:%d] MULTI_STUCK_RECOVER: physSpeed=%.1fkmh dist=%.1fm "
                         "mission=%d(%s) tempAction=%d(%s) linkId %u->%u",
                    i, physSpeed, dist,
                    (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
                    (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
                    linkBefore, linkAfter);
                SetupDriveModeSimple(player, tr.ped, recCar, g_driveMode);
            }
        }
        else
        {
            tr.stuckTimer = 0;
        }

        // ── Snap periodico ao road-graph ───────────────────────────
        if (++tr.snapTimer >= MULTI_RECRUIT_SNAP_INTERVAL)
        {
            tr.snapTimer = 0;
            unsigned linkBefore = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            CCarCtrl::JoinCarWithRoadSystem(recCar);
            unsigned linkAfter  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;

            // Re-aplicar missao CIVICO apos snap (snap pode resetar missao)
            SetupDriveModeSimple(player, tr.ped, recCar, g_driveMode);
            tr.prevMission = (int)ap.m_nCarMission;  // sync apos snap

            // ── Status dump completo no snap ───────────────────────
            LogMulti("[recr:%d] MULTI_STATUS: ped=%p car=%p dist=%.1fm speed=%d(%.0fkmh) "
                     "mission=%d(%s) style=%d(%s) tempAction=%d(%s) "
                     "linkId %u(%s)->%u(%s) stuck=%d stopZone=%d slowZone=%d",
                i, (void*)tr.ped, (void*)recCar, dist,
                (int)ap.m_nCruiseSpeed, physSpeed,
                (int)ap.m_nCarMission,      GetCarMissionName((int)ap.m_nCarMission),
                (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
                (int)ap.m_nTempAction,      GetTempActionName((int)ap.m_nTempAction),
                linkBefore, (linkBefore <= MAX_VALID_LINK_ID ? "OK" : "INVALID"),
                linkAfter,  (linkAfter  <= MAX_VALID_LINK_ID ? "OK" : "INVALID"),
                tr.stuckTimer,
                (int)tr.inStopZone, (int)tr.inSlowZone);
        }

        // ── Restauracao de saude do carro ──────────────────────────
        if (++tr.healthTimer >= MULTI_RECRUIT_HEALTH_INTERVAL)
        {
            tr.healthTimer = 0;
            if (recCar->m_fHealth < RECRUIT_CAR_HEALTH_MIN)
            {
                LogMulti("[recr:%d] MULTI_CAR_HEALTH_RESTORE: %.0f -> %.0f",
                    i, recCar->m_fHealth, RECRUIT_CAR_HEALTH_INITIAL);
                recCar->m_fHealth = RECRUIT_CAR_HEALTH_INITIAL;
            }
        }

        // ── Velocidade base: CIVICO normal ─────────────────────────
        if (ap.m_nCruiseSpeed < SPEED_MIN)
            ap.m_nCruiseSpeed = SPEED_CIVICO;

        // Garantir targetCar correcto se playerCar mudou
        if (playerCar && IsCivicoMode(g_driveMode) && ap.m_pTargetCar != playerCar)
        {
            LogMulti("[recr:%d] MULTI_TARGET_CAR_UPDATE: %p -> %p",
                i, (void*)ap.m_pTargetCar, (void*)playerCar);
            ap.m_pTargetCar = playerCar;
        }
    }
}

void ProcessDrivingAI(CPlayerPed* player)
{
    if (!IsCarValid()) return;

    CVehicle*   veh       = g_car;
    CAutoPilot& ap        = veh->m_autoPilot;
    CVector     vPos      = veh->GetPosition();
    CVector     playerPos = player->GetPosition();
    float       dist      = Dist2D(vPos, playerPos);

    // ── Modo PASSAGEIRO: jogador está no mesmo carro do recruta ───────────
    // Quando o jogador prime KEY 3 (DRIVING→PASSENGER) e entra no carro do
    // recruta como passageiro, dist ≈ 0 → as zonas STOP/SLOW disparariam
    // imediatamente e poriam o recruta em STOP_FOREVER.
    // FIX: detectar esta situação e usar navegação GOTOCOORDS à frente em
    // vez de seguir o jogador (que está no mesmo carro).
    {
        bool playerInThisCar = player->bInVehicle && player->m_pVehicle == veh;
        if (playerInThisCar)
        {
            // Actualizar destino: waypoint do mapa (prioridade) ou fallback
            // 60m à frente no heading actual do carro quando não há waypoint.
            if (g_diretoTimer <= 0 || ap.m_nCarMission != MISSION_GOTOCOORDS)
            {
                CVector dest{};
                bool hasWaypoint = GetMapWaypoint(dest);
                if (!hasWaypoint)
                {
                    float   h   = veh->GetHeading();
                    CVector fwd(std::sinf(h), std::cosf(h), 0.0f);
                    dest = vPos + fwd * 60.0f;
                    dest.z = vPos.z;
                }
                ap.m_nCarMission         = MISSION_GOTOCOORDS;
                ap.m_pTargetCar          = nullptr;
                ap.m_vecDestinationCoors = dest;
                g_diretoTimer = DIRETO_UPDATE_INTERVAL;
                LogDrive("PASSENGER_NAV: source=%s dest=(%.1f,%.1f,%.1f) speed=%d",
                    hasWaypoint ? "MAP_WAYPOINT" : "AHEAD_FALLBACK",
                    dest.x, dest.y, dest.z, (int)SPEED_CIVICO);
            }
            else
            {
                --g_diretoTimer;
            }
            ap.m_nCruiseSpeed     = SPEED_CIVICO;
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;

            // Stuck recovery activa em modo passageiro
            if (s_stuckCooldown > 0) --s_stuckCooldown;
            {
                float physSpeedP = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
                if (physSpeedP < STUCK_SPEED_KMH)
                {
                    if (++s_stuckTimer >= STUCK_DETECT_FRAMES)
                    {
                        s_stuckTimer    = 0;
                        s_stuckCooldown = STUCK_RECOVER_COOLDOWN;
                        CCarCtrl::JoinCarWithRoadSystem(veh);
                        g_diretoTimer = 0;   // forcar update de destino no proximo frame
                        LogDrive("PASSENGER_STUCK_RECOVER: physSpeed=%.1fkmh -> JoinRoadSystem + dest reset",
                            physSpeedP);
                    }
                }
                else
                {
                    s_stuckTimer = 0;
                }
            }

            // Log periodico do estado de conducao em modo passageiro
            if (++g_logAiFrame >= LOG_AI_INTERVAL)
            {
                g_logAiFrame = 0;
                float physSpeedP = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
                LogAI("PASSENGER_DRIVING: speed_ap=%d physSpeed=%.0fkmh mission=%d(%s) "
                      "style=%d(%s) tempAction=%d(%s) heading=%.3f dest=(%.1f,%.1f,%.1f)",
                    (int)ap.m_nCruiseSpeed, physSpeedP,
                    (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
                    (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
                    (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
                    veh->GetHeading(),
                    ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z);
            }
            return;
        }
    }

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
            // Ao sair de offroad em CIVICO: snap ao road-graph para realinhar
            if (!g_isOffroad && IsCivicoMode(g_driveMode))
            {
                CCarCtrl::JoinCarWithRoadSystem(veh);
                g_civicRoadSnapTimer = 0;
                LogDrive("OFFROAD_EXIT_SNAP: JoinCarWithRoadSystem ao regressar a estrada (CIVICO)");
            }
            if (g_isOffroad)
            {
                g_offroadSustainedFrames = 0; // resetar ao entrar em offroad (re-acumula)
            }
        }
        // Actualizar contador de frames consecutivos em offroad
        // (usado por OFFROAD_DIRECT_FOLLOW para o cenario do canal)
        if (g_isOffroad)
            g_offroadSustainedFrames += OFFROAD_CHECK_INTERVAL;
        else
            g_offroadSustainedFrames = 0;
    }
    else
    {
        --g_offroadTimer;
    }

    // ── Offroad + CIVICO: direct-follow sustentado (canal/zona sem estrada) ──
    // Quando offroad por >= OFFROAD_DIRECT_FOLLOW_FRAMES: road-graph nao existe
    // nessa zona (ex: canal) → recruta ficaria em WRONG_DIR infinito tentando
    // voltar a uma estrada que nao esta la. Fix: mudar para GOTOCOORDS directo
    // (como DIRETO mas a velocidade CIVICO+AVOID_CARS) enquanto offroad sustentado.
    // Ao regressar a estrada: OFFROAD_EXIT_SNAP + road-follow CIVICO retoma.
    if (IsCivicoMode(g_driveMode) && g_isOffroad
        && g_offroadSustainedFrames >= OFFROAD_DIRECT_FOLLOW_FRAMES)
    {
        bool entering = !g_wasOffroadDirect;
        g_wasOffroadDirect = true;

        if (entering)
        {
            LogDrive("OFFROAD_DIRECT_START: offroad sustentado %d frames >= %d — direct-follow activado "
                     "(modo=%s)", g_offroadSustainedFrames, OFFROAD_DIRECT_FOLLOW_FRAMES,
                     DriveModeName(g_driveMode));
        }

        // Destino = DIRETO_FOLLOW_OFFSET metros atras do jogador (como DIRETO)
        float   playerHeading = GetPlayerHeading(player);
        CVector pFwd(std::sinf(playerHeading), std::cosf(playerHeading), 0.0f);
        CVector dest = playerPos - pFwd * DIRETO_FOLLOW_OFFSET;
        dest.z = playerPos.z;

        ap.m_nCarMission         = MISSION_GOTOCOORDS;
        ap.m_pTargetCar          = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed        = SPEED_CIVICO;
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS; // desvia de obstaculos

        // Health restoration ainda actua neste modo (ver bloco abaixo)
        if (IsCarValid() && g_car->m_fHealth < RECRUIT_CAR_HEALTH_MIN)
        {
            g_car->m_fHealth = RECRUIT_CAR_HEALTH_INITIAL;
        }
        return; // skip road-graph processing enquanto offroad
    }
    else if (g_wasOffroadDirect)
    {
        // Acabamos de sair do modo direct-follow: road-follow CIVICO retoma
        g_wasOffroadDirect = false;
        g_civicRoadSnapTimer = 0;
        LogDrive("OFFROAD_DIRECT_END: de volta a estrada — road-follow CIVICO retomado");
        // OFFROAD_EXIT_SNAP ja chamou JoinCarWithRoadSystem na transicao de offroad
    }
    if (g_isOffroad && IsCivicoMode(g_driveMode))
    {
        // Offroad ainda nao atingiu threshold: resetar snap para evitar snap errado
        g_civicRoadSnapTimer = 0;
    }

    // ── Modo DIRETO: actualizar destino com offset atras do jogador ─
    // CORRECAO v2: STOP_FOR_CARS_IGNORE_LIGHTS (v1 usava PLOUGH_THROUGH).
    // Destino = DIRETO_FOLLOW_OFFSET metros atras do jogador por heading.
    if (g_driveMode == DriveMode::DIRETO)
    {
        if (g_diretoTimer <= 0)
        {
            float   playerHeading = GetPlayerHeading(player);
            CVector pFwd(std::sinf(playerHeading), std::cosf(playerHeading), 0.0f);
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
    // A partir daqui: modos CIVICO (CIVICO_F/G/H) em estrada
    // ═══════════════════════════════════════════════════════════════

    // Em close-range CIVICO (< CLOSE_RANGE_SWITCH_DIST), usar
    // STOP_FOR_CARS_IGNORE_LIGHTS para reduzir manobras agressivas sem
    // prender o recruta em semáforos. Fora desta zona volta ao estilo base.
    {
        bool closeSafeStyle = IsCivicoMode(g_driveMode) && dist < CLOSE_RANGE_SWITCH_DIST;
        ap.m_nCarDrivingStyle = closeSafeStyle
            ? DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS
            : GetExpectedDriveStyle(g_driveMode);
        if (closeSafeStyle != s_closeSafeStyle)
        {
            s_closeSafeStyle = closeSafeStyle;
            LogDrive("CLOSE_STYLE_%s: dist=%.1fm style=%d(%s) modo=%s",
                closeSafeStyle ? "SAFE_ON" : "SAFE_OFF",
                dist,
                (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
                DriveModeName(g_driveMode));
        }
    }

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
            CVehicle* currentPlayerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            eCarDrivingStyle dstyle = GetExpectedDriveStyle(g_driveMode);
            LogDrive("MISSION_RECOVERY: STOP_FOREVER(11) fora das zonas — restaurar mission=%d "
                     "targetCar=%s modo=%s",
                (int)expectedMission,
                currentPlayerCar ? "valido" : "nullptr(pe)",
                DriveModeName(g_driveMode));
            ap.m_nCarMission      = expectedMission;
            if (currentPlayerCar) ap.m_pTargetCar = currentPlayerCar;
            ap.m_nCarDrivingStyle = dstyle;
            g_missionRecoveryTimer = 30;
        }
    }

    // Speed adaptativa + alinhamento de faixa.
    float targetHeading = ApplyLaneAlignment(veh);
    float physSpeed     = veh->m_vecMoveSpeed.Magnitude() * 180.0f;

    // ── Base speed: SLOW se link invalido, CLOSE se perto, FAR_CATCHUP se longe ─
    unsigned char baseSpd;
    if (g_wasInvalidLink)
        baseSpd = SPEED_SLOW;
    else if (dist < CLOSE_RANGE_SWITCH_DIST)
        baseSpd = SPEED_CIVICO;
    else if (dist > FAR_CATCHUP_DIST_M && !g_isOffroad && !g_wasWrongDir)
        baseSpd = SPEED_CATCHUP;   // catch-up quando longe + em estrada + sentido correcto
    else
        baseSpd = SPEED_CIVICO;
    ap.m_nCruiseSpeed = AdaptiveSpeed(veh, targetHeading, baseSpd, dist);

    // ── Prevenir MC52→MC53 (close-range chase): forcar StraightLineDistance baixo ──
    // SA engine (CCarAI::UpdateAutoPilot): MC52 transiciona para MC53 quando
    //   dist² ≤ m_nStraightLineDistance² (default=20 → transicao a 20m).
    // MC53 ignora road-graph em close range → sobe passeio, bate em postes.
    // FIX: forcar m_nStraightLineDistance = CLOSE_RANGE_STRAIGHT_LINE_DIST (=5) cada frame.
    //   → SA engine so transiciona MC52→MC53 quando dist < 5m (dentro da STOP_ZONE).
    //   → MC52 (road-graph) permanece activo para todo o range de seguimento normal.
    // NOTA: JoinCarWithRoadSystem repoe o valor; por isso forcamos CADA FRAME.
    if (IsCivicoMode(g_driveMode))
        ap.m_nStraightLineDistance = CLOSE_RANGE_STRAIGHT_LINE_DIST;

    // ── FAR_CATCHUP log (transicao on/off) ─────────────────────────
    {
        bool nowCatchup = (baseSpd == SPEED_CATCHUP);
        if (nowCatchup != s_catchupActive)
        {
            s_catchupActive = nowCatchup;
            LogDrive("FAR_CATCHUP_%s: dist=%.1fm FAR_CATCHUP_DIST=%.0fm speed=%d modo=%s",
                nowCatchup ? "ON" : "OFF",
                dist, FAR_CATCHUP_DIST_M, (int)ap.m_nCruiseSpeed,
                DriveModeName(g_driveMode));
        }
    }

    // ── TempAction speed penalty + persistent HEADON recovery ─────────
    // Principio: simular "AVOID_CARS + SLOW_DOWN" simultaneamente.
    // eCarDrivingStyle so tem 5 valores (0-4) — nao e possivel combinar
    // AVOID_CARS(2) e SLOW_DOWN_FOR_CARS(1) num unico valor (1|2=3=PLOUGH_THROUGH!).
    // SOLUCAO: usar AVOID_CARS como base (faz swerve) + nos proprios reduzimos
    // a velocidade quando o autopilot detecta colisao/obstrucao (tempAction != NONE).
    //
    // Props/postes: CCarCtrl::SlowCarDownForObject (0x426220) ja abranda o carro
    // para objectos estaticos na frente, mas nao faz swerve.
    // STUCK_RECOVER e HEADON_PERSISTENT tratam encravamentos em props.
    //
    // Tabela de penalizacoes de velocidade por tempAction:
    //   HEADON_COLLISION(19): 50% — bateu de frente (carro, prop, muro)
    //   STUCK_TRAFFIC(12):    40% — encravado no transito
    //   SWERVE_LEFT(10)/RIGHT(11): 75% — a fazer desvio activo → abrandar durante manobra
    //   REVERSE(3)/REV_LEFT(13)/REV_RIGHT(14): 0% (velocidade ja controlada pelo autopilot)
    {
        int tempAction = (int)ap.m_nTempAction;

        // Persistent HEADON detection: recruta encravado contra prop/muro/carro imovivel
        if (tempAction == 19 /* HEADON_COLLISION */)
        {
            ++s_headonFrames;
            if (s_headonCooldown > 0) --s_headonCooldown;
            if (s_headonFrames >= HEADON_PERSISTENT_FRAMES && s_headonCooldown <= 0)
            {
                // Recovery agressiva: re-snap ao road-graph para escapar do prop
                s_headonFrames   = 0;
                s_headonCooldown = HEADON_RECOVER_COOLDOWN;
                s_stuckTimer     = 0;  // reset stuck também para evitar double-recovery imediata
                CCarCtrl::JoinCarWithRoadSystem(veh);
                g_civicRoadSnapTimer = 0;
                LogDrive("HEADON_PERSISTENT: HEADON_COLLISION por >=%d frames -> JoinCarWithRoadSystem "
                         "(prop/muro/carro imovivel) physSpeed=%.1fkmh dist=%.1fm modo=%s",
                    HEADON_PERSISTENT_FRAMES, physSpeed, dist, DriveModeName(g_driveMode));
            }
            // Reduzir velocidade 50% para dar tempo ao autopilot de manobrar
            unsigned char penalized = static_cast<unsigned char>(
                static_cast<float>(ap.m_nCruiseSpeed) * HEADON_SPEED_FACTOR);
            ap.m_nCruiseSpeed = std::max(penalized, SPEED_MIN);
        }
        else if (tempAction == 12 /* STUCK_TRAFFIC */)
        {
            s_headonFrames = 0;
            unsigned char penalized = static_cast<unsigned char>(
                static_cast<float>(ap.m_nCruiseSpeed) * STUCK_TRAFFIC_SPEED_FACTOR);
            ap.m_nCruiseSpeed = std::max(penalized, SPEED_MIN);
        }
        else if (tempAction == 10 /* SWERVE_LEFT */ || tempAction == 11 /* SWERVE_RIGHT */)
        {
            s_headonFrames = 0;
            // Reduzir 25% durante swerve: simula o efeito de SLOW_DOWN_FOR_CARS
            // enquanto o AVOID_CARS faz o swerve — combinacao AVOID+SLOW efectiva.
            unsigned char penalized = static_cast<unsigned char>(
                static_cast<float>(ap.m_nCruiseSpeed) * SWERVE_SPEED_FACTOR);
            ap.m_nCruiseSpeed = std::max(penalized, SPEED_MIN);
        }
        else
        {
            s_headonFrames = 0;
        }
    }

    // ── TempAction change log ──────────────────────────────────────
    {
        int curTA = (int)ap.m_nTempAction;
        if (curTA != s_prevTempAction)
        {
            LogDrive("TEMPACTION_CHANGE: %d(%s) -> %d(%s) dist=%.1fm physSpeed=%.0fkmh modo=%s",
                s_prevTempAction, GetTempActionName(s_prevTempAction),
                curTA,            GetTempActionName(curTA),
                dist, physSpeed,
                DriveModeName(g_driveMode));
            s_prevTempAction = curTA;
        }
    }

    // ── CLOSE_RANGE entry/exit log ─────────────────────────────────
    {
        bool nowClose = (dist < CLOSE_RANGE_SWITCH_DIST);
        if (nowClose != s_inCloseRange)
        {
            s_inCloseRange = nowClose;
            LogDrive("CLOSE_RANGE_%s: dist=%.1fm (threshold=%.0fm) modo=%s mission=%d physSpeed=%.0fkmh",
                nowClose ? "ENTER" : "EXIT",
                dist, CLOSE_RANGE_SWITCH_DIST,
                DriveModeName(g_driveMode),
                (int)ap.m_nCarMission, physSpeed);
        }
    }

    // ── Stuck/collision detection + recovery ───────────────────────
    // Quando recruta fica encravado contra parede/prop/carro imovivel:
    //   physSpeed < STUCK_SPEED_KMH por STUCK_DETECT_FRAMES → forcar re-snap
    // Cooldown evita recuperacoes em loop. Nao activar na STOP/SLOW zone.
    if (s_stuckCooldown > 0) --s_stuckCooldown;
    if (dist > SLOW_ZONE_M && s_stuckCooldown <= 0)
    {
        if (physSpeed < STUCK_SPEED_KMH)
        {
            if (++s_stuckTimer >= STUCK_DETECT_FRAMES)
            {
                s_stuckTimer    = 0;
                s_stuckCooldown = STUCK_RECOVER_COOLDOWN;
                CCarCtrl::JoinCarWithRoadSystem(veh);
                g_civicRoadSnapTimer = 0;
                LogDrive("STUCK_RECOVER: physSpeed=%.1fkmh por >=%d frames -> JoinCarWithRoadSystem "
                         "dist=%.1fm mode=%s mission=%d tempAction=%d(%s)",
                    physSpeed, STUCK_DETECT_FRAMES,
                    dist, DriveModeName(g_driveMode),
                    (int)ap.m_nCarMission,
                    (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction));
            }
        }
        else
        {
            s_stuckTimer = 0;
        }
    }
    else
    {
        s_stuckTimer = 0;
    }

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

    // ── CIVICO: close-blocked WAIT (todos os modos CIVICO) ───────────
    // Problema: com dist < CLOSE_RANGE_SWITCH_DIST o motor SA pode entrar
    // em "chase mode" (MC_FOLLOWCAR_CLOSE). Se o jogador esta parado no
    // transito com um carro entre eles, o recruta tenta forcar caminho
    // → sobe o passeio ou vai na contramao.
    // Fix: quando ambos ficam parados >= 1.5s nesta zona, comutar para
    // STOP_FOREVER (esperar). Retoma quando o jogador voltar a andar
    // (>= CLOSE_BLOCKED_RESUME_KMH) OU quando a distancia limpar a zona.
    if (IsCivicoMode(g_driveMode))
    {
        float recruitSpeed = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
        float playerSpeed  = playerCar ? playerCar->m_vecMoveSpeed.Magnitude() * 180.0f : 0.0f;
        bool  inCloseZone  = dist < CLOSE_RANGE_SWITCH_DIST;

        if (!g_closeBlocked)
        {
            // Contar frames consecutivos em que ambos estao parados na zona proxima
            if (inCloseZone && playerCar
                && recruitSpeed < CLOSE_BLOCKED_MIN_KMH
                && playerSpeed  < CLOSE_BLOCKED_MIN_KMH)
            {
                if (++g_closeBlockedTimer >= CLOSE_BLOCKED_FRAMES)
                {
                    g_closeBlocked = true;
                    LogDrive("CLOSE_BLOCKED_START: dist=%.1fm recruit=%.0fkmh player=%.0fkmh "
                             "-> STOP_FOREVER (aguardar desobstrucao de transito)",
                        dist, recruitSpeed, playerSpeed);
                }
            }
            else
            {
                g_closeBlockedTimer = 0;
            }
        }
        else
        {
            // Em modo de espera: retomar quando jogador se mover ou afastar
            bool canResume = !inCloseZone || playerSpeed >= CLOSE_BLOCKED_RESUME_KMH;
            if (canResume)
            {
                g_closeBlocked      = false;
                g_closeBlockedTimer = 0;
                CCarCtrl::JoinCarWithRoadSystem(veh);
                LogDrive("CLOSE_BLOCKED_END: dist=%.1fm playerSpeed=%.0fkmh -> CIVICO retomado",
                    dist, playerSpeed);
                // cair para o processamento CIVICO normal neste mesmo frame
            }
            else
            {
                // Ainda bloqueado: manter parado, nao processar mais nada
                ap.m_nCarMission  = MISSION_STOP_FOREVER;
                ap.m_nCruiseSpeed = 0;
                return;
            }
        }
    }

    // ── CLOSE_RANGE_SMOOTH: todos os modos CIVICO perto → FollowCarFaraway ──
    // SA engine: MC52 transiciona automaticamente para MC53 (chase off-road) quando
    //   dist ≤ m_nStraightLineDistance. Embora tenhamos forcado m_nStraightLineDistance=5
    //   acima, o SA engine pode ter ja transitado para MC53 NESTE FRAME antes do nosso
    //   codigo correr. Igualmente, MC67→MC31 (ESCORT_REAR_FARAWAY→ESCORT_REAR) ocorre
    //   quando perto. AMBAS as transicoes causam "chase mode" off-road.
    // FIX v3 (TODOS OS MODOS): detectar qualquer missao != MC52 em close range e forcar
    //   MC52 de volta. Combinado com m_nStraightLineDistance=5 acima, na proxima frame
    //   o SA engine nao transitara mais para MC53 (dist > 5m fora da STOP_ZONE).
    if (playerCar && IsCivicoMode(g_driveMode)
        && dist < CLOSE_RANGE_SWITCH_DIST && dist >= SLOW_ZONE_M)
    {
        if (ap.m_nCarMission != MC_FOLLOWCAR_FARAWAY)
        {
            eCarMission cur = ap.m_nCarMission;
            // Log apenas na transicao (debounce via s_prevCloseRangeMission)
            if (cur != s_prevCloseRangeMission)
            {
                s_prevCloseRangeMission = cur;
                LogDrive("CLOSE_RANGE_FORCE_MC52: dist=%.1fm %d(%s)->MC52(road-graph) modo=%s",
                    dist, (int)cur, GetCarMissionName((int)cur), DriveModeName(g_driveMode));
            }
            ap.m_nCarMission = MC_FOLLOWCAR_FARAWAY;
            ap.m_pTargetCar  = playerCar;
        }
        else
        {
            s_prevCloseRangeMission = (eCarMission)(-1);  // reset debounce quando MC52 OK
        }
    }
    else
    {
        s_prevCloseRangeMission = (eCarMission)(-1);
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
                // FIX v3 (dist <= 30m, SOFT): trocar missao para MC_FOLLOWCAR_FARAWAY(52)
                //   road-graph — nao abandona a estrada ao perto (MC_FOLLOWCAR_CLOSE era beeline).
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
                        // Soft recovery: mudar para FollowCarFaraway (road-graph, nao beeline)
                        // FIX v2: MC_FOLLOWCAR_FARAWAY(52) em vez de MC_FOLLOWCAR_CLOSE(53).
                        // MC53 beeline abandona road-graph mesmo em estrada normal → subida de
                        // passeio reportada pelo jogador. MC52 usa road-graph → fica em faixa.
                        ap.m_nCarMission = MC_FOLLOWCAR_FARAWAY;
                        ap.m_pTargetCar  = playerCar;
                        LogDrive("WRONG_DIR_RECOVER_CLOSE: soft — mission->FollowCarFaraway(52) "
                                 "dist=%.1fm <= %.0fm (sem JoinRoad, road-graph activo)",
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

    // ── Dump AI throttled a cada LOG_AI_INTERVAL frames (~1s) ──────
    if (++g_logAiFrame >= LOG_AI_INTERVAL)
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
        int   tempAction = (int)ap.m_nTempAction;
        char taskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
            BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
        }
        LogAI("DRIVING_1: dist=%.1fm speed_ap=%d physSpeed=%.0fkmh mission=%d(%s) driveStyle=%d(%s) "
              "tempAction=%d(%s) offroad=%d stuck=%d/%d modo=%s heading=%.3f targetH=%.3f deltaH=%.3f(%s) speedMult=%.2f",
            dist, (int)ap.m_nCruiseSpeed, physSpeed,
            (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
            (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
            tempAction, GetTempActionName(tempAction),
            (int)g_isOffroad,
            s_stuckTimer, STUCK_DETECT_FRAMES,
            DriveModeName(g_driveMode),
            vehHeading, targetHeading, deltaH,
            (absDeltaH > WRONG_DIR_THRESHOLD_RAD)  ? "WRONG_DIR!" :
            (absDeltaH > MISALIGNED_THRESHOLD_RAD) ? "desalinhado" : "OK",
            speedMult);
        LogAI("DRIVING_2: straight=%d lane=%d linkId=%u(%s) areaId=%u "
              "dest=(%.1f,%.1f,%.1f) targetCar=%p snapTimer=%d catchup=%d tasks=%s",
            (int)ap.m_nStraightLineDistance,
            (int)ap.m_nCurrentLane,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
            ((unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId <= MAX_VALID_LINK_ID) ? "OK" : "INVALID",
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId,
            ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z,
            (void*)ap.m_pTargetCar,
            g_civicRoadSnapTimer,
            (int)s_catchupActive,
            taskBuf);

        // ── Dist trend: ver se recruta se esta a aproximar ou afastar ──
        // APROXIMAR: delta < -DIST_TREND_STABLE_M | AFASTAR: delta > +DIST_TREND_STABLE_M
        if (s_prevDistLog >= 0.0f)
        {
            float delta = dist - s_prevDistLog;
            const char* trend = (delta < -DIST_TREND_STABLE_M) ? "APROXIMAR" :
                                (delta >  DIST_TREND_STABLE_M) ? "AFASTAR" : "ESTAVEL";
            LogAI("DIST_TREND: dist=%.1fm prev=%.1fm delta=%.1fm -> %s",
                dist, s_prevDistLog, delta, trend);
        }
        s_prevDistLog = dist;
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

            // ── Durabilidade do carro (replica CLEO 0852+0224) ──────
            // 0224: set_car_health 1750 — vida acima do maximo vanilla (1000)
            // bTakeLessDamage: carro recebe ~50% menos dano por impacto
            // Fumaca vanilla aparece em <= 256 de vida (comportamento SA nao alterado)
            g_car->m_fHealth      = RECRUIT_CAR_HEALTH_INITIAL;
            g_car->bTakeLessDamage = true;
            g_carHealthTimer       = 0;
            LogEvent("CAR_DURABILITY_SETUP: health=%.0f bTakeLessDamage=1 (CLEO 0852+0224 replicado)",
                RECRUIT_CAR_HEALTH_INITIAL);

            SetupDriveMode(player, g_driveMode);
            ShowMsg("~g~RECRUTA A CONDUZIR [4=modo, 3=passageiro, 2=sair]");
        }
        return;
    }

    if (--g_enterCarTimer <= 0)
    {
        int limit = g_enterCarAsPassenger ? ENTER_CAR_PASSENGER_TIMEOUT : ENTER_CAR_DRIVER_TIMEOUT;
        LogWarn("ProcessEnterCar: TIMEOUT apos %d frames (%.0fs) — recruta nao conseguiu entrar. Voltando a ON_FOOT.",
            limit, limit / 60.0f);
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

    // ── Restauracao periodica de saude do carro do recruta ────────────
    // Replica CLEO 0224 (set_car_health 1750) de forma continua:
    // se a saude caiu abaixo do threshold, restaurar ao valor inicial.
    // Intervalo de 5s para nao tornar o carro completamente invulneravel —
    // permite fumaca vanilla (< 256) em impactos fortes, mas recupera depois.
    if (IsCarValid())
    {
        if (++g_carHealthTimer >= RECRUIT_CAR_HEALTH_RESTORE_INTERVAL)
        {
            g_carHealthTimer = 0;
            if (g_car->m_fHealth < RECRUIT_CAR_HEALTH_MIN)
            {
                LogDrive("CAR_HEALTH_RESTORE: %.0f -> %.0f (abaixo do limiar %.0f)",
                    g_car->m_fHealth, RECRUIT_CAR_HEALTH_INITIAL, RECRUIT_CAR_HEALTH_MIN);
                g_car->m_fHealth = RECRUIT_CAR_HEALTH_INITIAL;
            }
        }
    }
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
