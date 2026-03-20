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
 *   2. Periodic road snap: JoinCarWithRoadSystem a cada ROAD_SNAP_INTERVAL (~1s)
 *      em modos CIVICO para manter alinhamento com os nos de estrada.
 *      Snap ignorado durante WRONG_DIR (SetupDriveMode trata da recuperacao).
 *
 * NOTA: SLOW_ZONE re-snap REMOVIDO (causava INVALID_LINK → beelining).
 *
 * v5.9 CHANGELOG:
 *   - OFFROAD FIX: Aumentado PLAYER_OFFROAD_ON_DIST_M de 22m para 35m e
 *     PLAYER_OFFROAD_OFF_DIST_M de 18m para 15m. Isto aumenta a hysteresis
 *     de 4m para 20m, evitando que o recruta retorne prematuramente a estrada
 *     quando o jogador ainda esta offroad.
 *   - TRAFFIC HANDLING: Adicionado sistema de deteccao de trafego pesado
 *     (DetectNearbyTraffic) que conta carros num raio de 30m. Quando >=8 carros
 *     detectados, aplica boost de +15 kmh a velocidade do recruta (cap 70 kmh)
 *     em mid-range (15-40m) para ajudar a acompanhar o jogador em areas
 *     congestionadas. Boost nao aplica em close-range (<15m) para seguranca.
 *
 * v5.10 CHANGELOG:
 *   - SIDE-BY-SIDE FIX MELHORADO: Aumentado CIVICO_FOLLOW_OFFSET de 15m → 20m
 *     para manter recruta mais distante. CIVICO_CLOSE_ALIGN_DIST aumentado de
 *     20m → 25m para detectar posicionamento lateral mais cedo.
 *   - ALIGNMENT THRESHOLD MAIS ESTRITO: CIVICO_ALIGN_DOT_THRESHOLD aumentado de
 *     0.5 (60°) → 0.7 (45°) — mais rigoroso sobre o que conta como "atras".
 *     CIVICO_CLOSE_RETREAT_OFFSET aumentado de 10m → 15m para recuo mais agressivo
 *     quando desalinhado. Resultado: recruta fica mais distante, detecta lateral
 *     mais cedo, e recua mais quando desalinhado. Reduz confusao e side-by-side.
 *   - CURVE BRAKE LOGGING: Adicionado logging de transicoes ON/OFF para diagnosticar
 *     comportamento em curva. Log mostra deltaH em radianos e graus, fonte (roadLink
 *     vs destVector), speed antes/depois, e distancia. Permite verificar se curve
 *     brake activa correctamente em todos os casos.
 *
 * v5.11 CHANGELOG:
 *   - ALIGNMENT THRESHOLD AINDA MAIS ESTRITO: Log v5.10 mostrou recruta com
 *     alignDot=0.70 exactamente no threshold=0.70, causando activacao inconsistente
 *     do lateral fix. CIVICO_ALIGN_DOT_THRESHOLD aumentado de 0.7 (45°) → 0.75 (41°)
 *     para activar fix mais cedo e consistentemente.
 *   - DETECCAO LATERAL MAIS CEDO: CIVICO_CLOSE_ALIGN_DIST aumentado de 25m → 30m
 *     porque log v5.10 mostrou recruta a chegar a 5-10m antes de activar fix,
 *     resultando em side-by-side muito proximo. Deteccao aos 30m activa fix mais cedo.
 *   - RECUO MAIS AGRESSIVO: CIVICO_CLOSE_RETREAT_OFFSET aumentado de 15m → 18m
 *     para reposicionamento mais decisivo quando recruta esta desalinhado.
 *   - SLOW_ZONE AUMENTADO: SLOW_ZONE_M aumentado de 10m → 12m. Log v5.10 mostrou
 *     STOP_FOREVER a activar repetidamente a 5-8m (muito proximo). Com 12m, recruta
 *     mantem distancia minima mais segura e confortavel.
 *
 * v5.12 CHANGELOG:
 *   - REVERSE CLEARING PER-FRAME EM CIVICO: Analise de gta-reversed confirmou que
 *     SA engine SetTempAction(REVERSE) armazena duracao em m_nTempActionTime mas
 *     NUNCA a verifica para timeout. REVERSE persiste indefinidamente sem
 *     ClearTempAct() explicito. Log v5.11 mostrou REVERSE por 4+ segundos
 *     consecutivos, recruta de 74m a 95m (20m errados). PASSENGER mode ja limpava
 *     desde v5.4 — replicado para CIVICO. Limpa REVERSE(3)/REVERSE_LEFT(13)/
 *     REVERSE_RIGHT(14) per-frame. PRINCIPAL FIX para pathfinding em CIVICO.
 *   - CURVE BRAKE DESTINATION-VECTOR: Road-link heading (GetRoadLinkHeading) NAO
 *     reflecte direccao real em GOTOCOORDS — link actual pode divergir da rota.
 *     Log v5.11 mostrou curveBrake=1 em ~95% das entries com deltaH 1.0-3.0 rad
 *     usando road-link, capping velocidade a 25kmh constantemente. Fix: usar heading
 *     ao DESTINO (destination vector) com thresholds CIVICO-especificos (0.60/0.35
 *     vs 0.35/0.20 PASSENGER). Reflecte curva REAL do percurso ao destino.
 *   - LATERAL FIX SIMPLIFICADO: v5.8-v5.11 forcavam destino atras do recruta
 *     (CIVICO_CLOSE_RETREAT_OFFSET) causando oscilacao e backing-up. v5.12: quando
 *     desalinhado (alignDot < threshold), apenas REDUZIR VELOCIDADE para SPEED_SLOW.
 *     Jogador puxa a frente naturalmente → recruta fica atras sem embicar lateralmente.
 *     Destino normal (atras do jogador por heading) mantido.
 *   - PERIODIC ROAD SNAP RESTAURADO: JoinCarWithRoadSystem cada ROAD_SNAP_INTERVAL
 *     (60 frames, 1s) em CIVICO. Removido em v5.6 (transicao para GOTOCOORDS puro).
 *     gta-reversed mostra que o engine chama JoinCarWithRoadSystemGotoCoors internamente
 *     cada 8 frames para GOTOCOORDS. Periodic snap ajuda autopilot a manter consciencia
 *     de estrada, previne routing errado em interseccoes.
 *
 * v5.13 CHANGELOG:
 *   - LANE HOLD (FIX SIDE-BY-SIDE): Quando jogador parado (<3kmh) e recruta proximo
 *     (<30m), parar recruta na faixa actual (STOP_FOREVER). GOTOCOORDS+AVOID_CARS
 *     causa SA engine a rotear por faixas adjacentes ao tentar chegar ao destino
 *     20m atras. Com STOP_FOREVER, recruta fica onde esta. Retoma quando jogador
 *     anda (>8kmh). Activado em TODOS os modos CIVICO.
 *   - DEST UPDATE SUAVE: Analise PASSENGER vs CIVICO revelou que PASSENGER actualiza
 *     destino ~nunca (waypoint fixo) → autopilot com controlo total → navegacao suave.
 *     CIVICO v5.12 actualizava a cada frame (stale>3m) E resetava m_nTempAction=0 a
 *     cada update → interrompia manobras de desvio (SWERVE etc.) → jitter.
 *     Fix: (a) CIVICO_DEST_STALE_DIST 3m→8m, (b) timer minimo 30 frames (0.5s) entre
 *     updates, (c) NAO resetar m_nTempAction em updates. REVERSE limpo per-frame separado.
 *   - TELEPORT CATCH-UP: Quando recruta >100m (v5.16: era 150m), warpar para 30m atras do jogador.
 *     Usa CEntity::Teleport() (gta-reversed: Remove world, reset physics, re-Add).
 *     Apos: JoinCarWithRoadSystem. Cooldown 5s. Resolve despawn sem velocidades altas.
 *     v5.15: Movido para ANTES dos early-return (offroad/direto/player-offroad/invalid-link).
 *   - PROTECCAO PED ANTI-DESPAWN: SetCharCreatedBy(2)=PED_MISSION + bStreamingDontDelete
 *     no recruta PED (gta-reversed Population.cpp: ManagePed skipa PED_MISSION).
 *     ApplyRecruitEnhancement aplica a TODOS os recrutas (spawned+vanilla).
 *     v5.16: bStreamingDontDelete re-aplicado per-frame em ProcessDriving.
 *   - LOG MELHORADO: CIVICO_DRIVE_1 inclui laneHold, targetCar, teleportCD.
 *     v5.18: Log simplificado para FOLLOWCAR (sem dest/alignDot/aheadHold/curveBrake).
 *   - v5.18 FOLLOWCAR: Log v5.17 mostrou AHEAD_HOLD catastrofico — recruta parado 60m
 *     atras do jogador (alignDot negativo em estradas curvas = falso positivo).
 *     GOTOCOORDS com destino atras do jogador e FUNDAMENTALMENTE incompativel com
 *     road-graph pathfinding: pathfinder rotea por cruzamentos errados.
 *     FIX: Usar FOLLOWCAR_FARAWAY(52) — missao nativa do SA engine que segue o
 *     CAMINHO do carro-alvo pelo road-graph. Comportamento identico a trafego normal.
 *     Referencia gta-reversed: CCarAI::UpdateCarAI processa FOLLOWCAR missions.
 *   - v5.19 FOLLOWCAR SPEED + SLOW_ZONE FIX:
 *     BUG 1: SLOW_ZONE (6-12m) sobrescrevia FOLLOWCAR com GOTOCOORDS(8)+targetCar=null.
 *     Causava oscilacao missao. FIX: SLOW_ZONE apenas reduz cruiseSpeed, nao toca missao.
 *     BUG 2: playerSpeed-based speed zones davam speed_ap=8 quando jogador parado (0+8=8).
 *     Recruta aproximava-se a 8kmh de 30m. FIX: Zonas fixas por distancia (46/62/80/85).
 *     FOLLOWCAR gere speed-matching ao carro-alvo internamente (como trafego normal).
 *
 * v5.21 CHANGELOG:
 *   - STOP_FOR_CARS EM ZONA PROXIMA (Fix calçada + recruta ao lado):
 *     CAUSA: SLOW_ZONE (dist<12m) e STOP_ZONE retornavam sem definir driveStyle.
 *     O driveStyle ficava em AVOID_CARS do frame anterior quando dist era >30m.
 *     AVOID_CARS a curtas distancias faz o recruta desviar pela calcada para chegar
 *     ao destino, acabando ao lado do jogador em vez de atras.
 *     FIX 1: SLOW_ZONE agora define STOP_FOR_CARS em modo CIVICO (AVOID_CARS so >30m).
 *     FIX 2: CIVICO block usa CIVICO_STOP_FOR_CARS_DIST=30m (era CIVICO_CLOSE_ALIGN_DIST=20m)
 *     para threshold de STOP_FOR_CARS. Assim AVOID_CARS so fica activo a >30m (catch-up).
 *     Referencia real-traffic-fix: trafego normal USA sempre STOP_FOR_CARS. AVOID_CARS e
 *     reservado para situacoes especificas (ex: policia a perseguir).
 *   - STEER-ANGLE SPEED REDUCTION (inspirado no real-traffic-fix):
 *     real-traffic-fix usa: fNewSpeed -= cfgTurningSpeedDecrease(15) * absSteerAngle.
 *     Para o recruta (cruiseSpeed 46-85 vs trafego 15), usa reducao proporcional:
 *       speedCap = max(SPEED_CIVICO_TURN, speed * (1.0 - absSteerAngle))
 *     Aplicado APOS heading-delta curve brake. Threshold CIVICO_STEER_BRAKE_MIN=0.15
 *     filtra micro-correccoes de steering em linha recta. Aplicado em todos os blocos
 *     de conducao (FOLLOWCAR, OFFROAD_DIRECT, PLAYER_OFFROAD_DIRECT).
 *     Exemplo: speed=80, steer=0.4 → cap=48kmh. speed=80, steer=0.7 → cap=24→25kmh.
 *   - CURVE BRAKE THRESHOLDS MAIS BAIXOS:
 *     Log v5.19 (compilado v5.19): deltaH=0.784-0.853 nao trigava (threshold era 0.80).
 *     Recruta ia a 75-93kmh em curvas. FIX: ACT 0.80→0.55 rad, DEACT 0.50→0.30 rad.
 *     Captura curvas com deltaH >= 0.55 rad (~32°). Sai do curve brake em < 0.30 rad (~17°).
 *   - OFFROAD QUANDO JOGADOR PARADO (activacao imediata):
 *     Quando jogador para (<3kmh) com playerRoadDist > PLAYER_OFFROAD_ON_DIST_M,
 *     s_playerOffroadOnFrames e definido directamente para PLAYER_OFFROAD_SUSTAIN_FRAMES.
 *     Evita oscilacao do playerRoadDist quando parado em zona borderline (25±1m).
 *   - RTF COMPATIBILIDADE (fix CRITICAL — real-traffic-fix instalado no jogo):
 *     CAUSA: RTF faz "vehicle->m_nCruiseSpeed = cfgCruiseSpeed(15)" ANTES do check
 *     de m_pTargetCar. Mesmo em FOLLOWCAR mode (m_pTargetCar!=null que deveria
 *     skip RTF), o cruiseSpeed ja foi sobreescrito para 15 (cfgCruiseSpeed).
 *     Isso reduz o recruta de 46-85kmh para ~8kmh real a cada render frame.
 *     FIX: g_car->m_nCreatedBy = 2 (MISSION_VEHICLE) no setup E per-frame.
 *     RTF check: "if (vehicle->m_nCreatedBy != MISSION_VEHICLE)" — com =2, skipa.
 *     Side effect positivo: previne CCarCtrl::DeleteOldestCar de remover g_car.
 *     Log confirma: "CAR_DURABILITY_SETUP: ... createdBy=2(MISSION) RTF_BLOCKED=1"
 *   - LOG v5.21: steer=%.2f adicionado ao CIVICO_DRIVE_1 para diagnostico RTF.
 *     CIVICO_DRIVE_2: playerStopped=%d para diagnostico de lane hold.
 */

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
static bool  s_playerOffroadDirect = false; // seguindo jogador fora do grafo
static int   s_playerOffroadOnFrames  = 0;  // frames consecutivos com playerRoadDist acima do ON
static int   s_playerOffroadOffFrames = 0;  // frames consecutivos com playerRoadDist abaixo do OFF
static float s_lastPlayerRoadDist     = 0.0f; // v5.5: dist do jogador ao nó de estrada mais proximo
static int   s_reverseFrames    = 0;       // frames consecutivos em tempAction de marcha-atrás
// v3.4: INVALID_LINK fallback para GOTOCOORDS direto
static bool  s_invalidLinkForceDirect = false; // force direct navigation durante INVALID_LINK storm
static int   s_invalidLinkForceDirectTimer = 0; // timer para retornar ao CIVICO (frames)
static bool  s_passengerWaitingWaypoint = false; // modo passageiro aguarda waypoint do mapa
static bool  s_passengerArrived         = false; // waypoint do passageiro atingido
static bool  s_waypointSoloWaiting      = false; // modo waypoint solo aguarda waypoint
static bool  s_waypointSoloArrived      = false; // waypoint solo atingido
// v5.0: Hysteresis de curveBrake por modo (previne flickering ON→OFF→ON)
// v5.3: s_civicoCurveBrake removido — MC67 tratava curvas nativamente.
// v5.7: s_civicoCurveBrake restaurado — GOTOCOORDS puro nao tem inteligencia
//   de curva do road-graph; sem brake o recruta passa curvas a 70 km/h.
static bool  s_passengerCurveBrake      = false; // curve brake activo no modo PASSENGER
static bool  s_waypointCurveBrake       = false; // curve brake activo no modo WAYPOINT_SOLO
static bool  s_civicoCurveBrake         = false; // v5.20: re-activado — FOLLOWCAR nao limita velocidade em curvas nativamente
// v5.0: Timer de reparacao visual do carro
static int   s_carVisualFixTimer        = 0;
// v5.13: Teleport catch-up cooldown (conta para baixo)
static int   s_teleportCooldownTimer    = 0;

static constexpr float HEADING_PI                     = 3.14159265358979323846f;
static constexpr float HEADING_TWO_PI                 = HEADING_PI * 2.0f;
static constexpr float HEADING_PREFERENCE_MARGIN_RAD  = 0.15f;
// v5.19: APPROACH_SPEED_MARGIN_CLOSE/FAR removidos — FOLLOWCAR gere speed-matching.
static constexpr float MIN_NODE_HEADING_DELTA         = 0.01f;
static constexpr float MAX_CRUISE_SPEED_UCHAR_F       = 255.0f;
static constexpr int   PLAYER_OFFROAD_SUSTAIN_FRAMES  = 15;  // v5.14: 15 frames (0.25s) — era 30 (0.5s)
static constexpr int   DIRECT_EXIT_SNAP_COOLDOWN_FRAMES = 60;
static constexpr unsigned int FORCED_REVERSE_DURATION_MS = 1000u;
static constexpr int   TEMP_ACTION_REVERSE            = 3;
static constexpr int   TEMP_ACTION_WAIT               = 1;
static constexpr int   TEMP_ACTION_SWERVE_LEFT        = 10;
static constexpr int   TEMP_ACTION_SWERVE_RIGHT       = 11;
static constexpr int   TEMP_ACTION_STUCK_TRAFFIC      = 12;
static constexpr int   TEMP_ACTION_REVERSE_LEFT       = 13;
static constexpr int   TEMP_ACTION_REVERSE_RIGHT      = 14;
static constexpr int   TEMP_ACTION_HEADON_COLLISION   = 19;

enum class AlignSource : unsigned char
{
    CURRENT_HEADING = 0,
    CLIPPED_LINK,
    ROAD_NODE_INVALID,
    ROAD_NODE_MISMATCH,
    DESTINATION_VECTOR
};

static AlignSource s_lastAlignSource = AlignSource::CURRENT_HEADING;
static float       s_lastRoadHeading = 0.0f;
static int         s_invalidLinkBurstFrames = 0;

static const char* GetAlignSourceName(AlignSource src)
{
    switch (src)
    {
    case AlignSource::CLIPPED_LINK:           return "CLIPPED_LINK";
    case AlignSource::ROAD_NODE_INVALID:      return "ROAD_NODE_INVALID";
    case AlignSource::ROAD_NODE_MISMATCH:     return "ROAD_NODE_MISMATCH";
    case AlignSource::DESTINATION_VECTOR:     return "DESTINATION_VECTOR";
    default:                                  return "CURRENT_HEADING";
    }
}

static float NormalizeHeadingDelta(float delta)
{
    while (delta >  HEADING_PI) delta -= HEADING_TWO_PI;
    while (delta < -HEADING_PI) delta += HEADING_TWO_PI;
    return delta;
}

static float AbsHeadingDelta(float a, float b)
{
    float delta = NormalizeHeadingDelta(a - b);
    return delta < 0.0f ? -delta : delta;
}

static bool GetDestinationVectorHeading(CVehicle* veh, CVector const& dest, float& outHeading)
{
    if (!veh) return false;
    CVector pos   = veh->GetPosition();
    CVector delta = dest - pos;
    float   dist2 = delta.x * delta.x + delta.y * delta.y;
    if (dist2 <= 0.0001f)
        return false;
    outHeading = std::atan2(delta.x, delta.y);
    return true;
}

// ───────────────────────────────────────────────────────────────────
// v5.1: GetRoadLinkHeading — obter heading da estrada actual via
// ClipTargetOrientationToLink. Usado para deteccao de curvas REAIS
// (a curva da estrada, nao a direcao ao waypoint).
//
// Retorna true se heading valido obtido. Se linkId invalido (off-road),
// retorna false e outHeading nao e alterado.
// ───────────────────────────────────────────────────────────────────
static bool GetRoadLinkHeading(CVehicle* veh, float& outHeading)
{
    if (!veh) return false;
    CAutoPilot& ap = veh->m_autoPilot;
    unsigned linkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;

    // linkId invalido = off-road ou nao snapped ao road-graph
    if ((linkId == 0 && ap.m_nCurrentPathNodeInfo.m_nAreaId == 0) || linkId > MAX_VALID_LINK_ID)
        return false;

    CVector fwd = veh->GetForward();
    float targetH = veh->GetHeading();
    CCarCtrl::ClipTargetOrientationToLink(
        veh,
        ap.m_nCurrentPathNodeInfo,
        ap.m_nCurrentLane,
        &targetH,
        fwd.x,
        fwd.y
    );
    outHeading = targetH;
    return true;
}

// ───────────────────────────────────────────────────────────────────
// v5.4: Reparar dano visual do carro do recruta.
// SEGURO: Apenas escrita directa a membros de dados (sem chamadas a
// metodos do engine como CloseAllDoors/FixDoor/FixPanel que podem
// causar ESP crash por calling convention mismatch).
//
// v5.3: Intervalo reduzido 2s→1s para fechar portas mais rapidamente
// apos colisoes. Usa SetDoorStatus loop (metodo seguro de CDamageManager).
// NOTA: m_nDoorStatus NAO existe no plugin-sdk CDamageManager.
//
// bCanBeDamaged=false (set no CAR_DURABILITY_SETUP) previne NOVOS
// danos. Esta funcao limpa danos residuais que possam existir.
// ───────────────────────────────────────────────────────────────────
static void RepairCarVisualDamage(CVehicle* veh)
{
    if (!veh || veh->m_nVehicleClass != VEHICLE_AUTOMOBILE) return;
    CAutomobile* car = static_cast<CAutomobile*>(veh);

    // Limpar todos os estados de dano via escrita directa (seguro — sem method calls)
    car->m_damageManager.m_nPanelsStatus = 0; // todos os paineis OK
    car->m_damageManager.m_nLightsStatus = 0; // todas as luzes OK

    // Limpar estado de todas as portas via SetDoorStatus (metodo seguro de CDamageManager,
    // diferente dos metodos de CAutomobile que causam ESP crash).
    // NOTA: m_nDoorStatus NAO existe no plugin-sdk CDamageManager — usar SetDoorStatus loop.
    for (int d = 0; d < 6; ++d)
        car->m_damageManager.SetDoorStatus((eDoors)d, DAMSTATE_OK);

    // Garantir que bCanBeDamaged continua false (pode ser reset pelo engine)
    veh->bCanBeDamaged = false;
}

// ───────────────────────────────────────────────────────────────────
// v5.9: Detectar trafego pesado ao redor do recruta.
// Retorna o numero de carros (que nao sejam do jogador ou do recruta)
// num raio de TRAFFIC_DETECT_RADIUS_M. Usado para activar speed boost
// em areas de trafego intenso.
// ───────────────────────────────────────────────────────────────────
static int DetectNearbyTraffic(CVehicle* recruitVeh, CVehicle* playerVeh)
{
    if (!recruitVeh) return 0;

    CVector recruitPos = recruitVeh->GetPosition();
    float radius = TRAFFIC_DETECT_RADIUS_M;
    float radiusSq = radius * radius;
    int trafficCount = 0;

    // Iterar sobre todos os veiculos proximos usando PoolIterator (range-based for)
    for (CVehicle* veh : *CPools::ms_pVehiclePool)
    {
        if (!veh || veh == recruitVeh || veh == playerVeh) continue;

        // Apenas contar carros (nao motos, barcos, helicopteros, etc.)
        if (veh->m_nVehicleClass != VEHICLE_AUTOMOBILE) continue;

        // Verificar distancia
        CVector vehPos = veh->GetPosition();
        float distSq = (vehPos - recruitPos).MagnitudeSqr();

        if (distSq < radiusSq)
        {
            trafficCount++;
        }
    }

    return trafficCount;
}

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
    s_playerOffroadDirect   = false;
    s_playerOffroadOnFrames = 0;
    s_playerOffroadOffFrames = 0;
    s_lastPlayerRoadDist    = 0.0f;
    s_reverseFrames         = 0;
    s_lastAlignSource       = AlignSource::CURRENT_HEADING;
    s_lastRoadHeading       = 0.0f;
    s_invalidLinkBurstFrames = 0;
    // v5.0
    // v5.3: s_civicoCurveBrake removido; v5.7: restaurado para GOTOCOORDS puro
    // v5.18: mantido para reset mas nao usado (FOLLOWCAR trata curvas nativamente)
    s_passengerCurveBrake   = false;
    s_waypointCurveBrake    = false;
    s_civicoCurveBrake      = false;
    s_carVisualFixTimer     = 0;
    // v5.13
    s_teleportCooldownTimer = 0;
}

// ───────────────────────────────────────────────────────────────────
// DetectOffroad (throttled via g_offroadTimer)
// v4.5: Hysteresis para prevenir oscilacao ON↔OFF
// Usa OFFROAD_ON_DIST_M para ativar, OFFROAD_OFF_DIST_M para desativar
// ───────────────────────────────────────────────────────────────────
bool DetectOffroad(CVehicle* veh, bool currentlyOffroad)
{
    if (!veh) return true;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);

    if (node1.IsEmpty()) return true;

    CPathNode* pNode = ThePaths.GetPathNode(node1);
    if (!pNode) return true;

    CVector nodePos   = pNode->GetNodeCoors();
    CVector vehicPos  = veh->GetPosition();
    float dist        = Dist2D(vehicPos, nodePos);

    // v4.5: Hysteresis logic
    bool shouldActivate   = (!currentlyOffroad && dist > OFFROAD_ON_DIST_M);    // 20m - ativa
    bool shouldDeactivate = (currentlyOffroad  && dist < OFFROAD_OFF_DIST_M);  // 16m - desativa

    if (shouldActivate)
        return true;   // Ativar offroad
    if (shouldDeactivate)
        return false;  // Desativar offroad

    // Manter estado atual se dentro da zona de hysteresis (16m-20m)
    return currentlyOffroad;
}

static float DistToNearestRoadNode(CVehicle* veh)
{
    if (!veh) return 1.0e9f;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);
    if (node1.IsEmpty()) return 1.0e9f;

    CPathNode* pNode = ThePaths.GetPathNode(node1);
    if (!pNode) return 1.0e9f;

    return Dist2D(veh->GetPosition(), pNode->GetNodeCoors());
}

static bool GetNearestRoadHeading(CVehicle* veh, float currentHeading, float& outHeading)
{
    if (!veh) return false;

    CNodeAddress node1, node2;
    CCarCtrl::FindNodesThisCarIsNearestTo(veh, node1, node2);
    if (node1.IsEmpty() || node2.IsEmpty()) return false;

    CPathNode* pNode1 = ThePaths.GetPathNode(node1);
    CPathNode* pNode2 = ThePaths.GetPathNode(node2);
    if (!pNode1 || !pNode2) return false;

    CVector pos1 = pNode1->GetNodeCoors();
    CVector pos2 = pNode2->GetNodeCoors();
    float dx = pos2.x - pos1.x;
    float dy = pos2.y - pos1.y;
    if (std::fabs(dx) < MIN_NODE_HEADING_DELTA && std::fabs(dy) < MIN_NODE_HEADING_DELTA) return false;

    float headingForward = std::atan2(dx, dy);
    float headingBack    = headingForward + HEADING_PI;
    if (headingBack > HEADING_PI)
        headingBack -= HEADING_TWO_PI;
    outHeading = AbsHeadingDelta(headingForward, currentHeading) <= AbsHeadingDelta(headingBack, currentHeading)
        ? headingForward
        : headingBack;
    return true;
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
// distToPlayer: distance recruit->player in meters (used to prevent boost in close-range).
unsigned char AdaptiveSpeed(CVehicle* veh, float targetHeading, unsigned char baseSpeed, float distToPlayer)
{
    if (!veh) return baseSpeed;

    float vH    = veh->GetHeading();
    float dH    = NormalizeHeadingDelta(targetHeading - vH);
    float absDH = dH < 0.0f ? -dH : dH;

    float mult;
    unsigned char effectiveBase;

    // Faixa de aproximacao mais larga que o close-range puro: entre 22m e
    // APPROACH_SLOW_DIST_M o recruta ainda nao esta "colado", mas ja esta
    // perto o suficiente para nao receber boost de reta e mergulhar demais
    // em intersecoes ou na traseira do carro do jogador.
    bool closeRange = (distToPlayer < APPROACH_SLOW_DIST_M);

    if (absDH <= MISALIGNED_THRESHOLD_RAD)
    {
        // Reta: usar SPEED_CIVICO_HIGH como minimo se baseSpeed >= SPEED_CIVICO,
        // mas respeitar um baseSpeed ainda mais alto (ex: SPEED_CATCHUP em FAR_CATCHUP).
        // "boost to HIGH unless already boosted higher (catchup)"
        mult          = 1.0f;
        // In close-range (< CLOSE_RANGE_SWITCH_DIST=22m, defined in grove_recruit_config.h), avoid boosting to
        // SPEED_CIVICO_HIGH so the recruit does not dive into intersections/turns
        // too fast while approaching the player.
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
    float roadHeading    = currentHeading;
    bool  hasRoadHeading = GetNearestRoadHeading(veh, currentHeading, roadHeading);
    unsigned currentLinkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
    s_lastRoadHeading = roadHeading;
    s_lastAlignSource = AlignSource::CURRENT_HEADING;

    if (ap.m_nCarMission == MISSION_GOTOCOORDS && ap.m_pTargetCar == nullptr)
    {
        float destHeading = currentHeading;
        if (GetDestinationVectorHeading(veh, ap.m_vecDestinationCoors, destHeading))
        {
            s_lastAlignSource = AlignSource::DESTINATION_VECTOR;
            return destHeading;
        }
    }

    if ((currentLinkId == 0 && ap.m_nCurrentPathNodeInfo.m_nAreaId == 0) || currentLinkId > MAX_VALID_LINK_ID)
    {
        if (hasRoadHeading)
            s_lastAlignSource = AlignSource::ROAD_NODE_INVALID;
        return hasRoadHeading ? roadHeading : currentHeading;
    }

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
    s_lastAlignSource = AlignSource::CLIPPED_LINK;

    if (hasRoadHeading)
    {
        float clipVsCurrent = AbsHeadingDelta(targetHeading, currentHeading);
        // CORRECAO INTERSECCOES (v4.3): Confiar no link-based heading do ClipTargetOrientationToLink.
        // O SA autopilot usa o link para pathfinding — sobrescrever com node-based heading
        // causa viragens erradas em interseccoes (recruta vira quando devia ir em frente).
        // EVIDENCIA: WRONG_DIR events nos logs tinham sempre align=ROAD_NODE_MISMATCH,
        // indicando que o fallback para node-based heading era a escolha errada.
        // APENAS usar heading actual como fallback se o link estiver completamente invertido
        // (> 162 graus = quase oposto), o que indica um bug critico no road-graph.
        if (clipVsCurrent > HEADING_PI * 0.9f)  // > 162 graus (quase oposto)
        {
            // Link heading completamente errado (muito raro) — usar heading actual
            // e aguardar proximo snap para corrigir.
            targetHeading = currentHeading;
            s_lastAlignSource = AlignSource::CURRENT_HEADING;
        }
        // NOTA: roadHeading (node-based) NAO e usado como fallback.
        // O link-based heading (de ClipTargetOrientationToLink) e a fonte correcta
        // porque reflecte a rota escolhida pelo autopilot SA no road-graph.
    }
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
void SetupDriveMode(CPlayerPed* player, DriveMode mode, bool skipSnap)
{
    if (!IsCarValid() || !IsRecruitValid()) return;

    // Limpar estado de CLOSE_BLOCKED ao mudar de modo ou re-inicializar
    // (evita estado obsoleto se o utilizador mudou para outro modo CIVICO).
    if (g_closeBlocked)
    {
        g_closeBlocked = false;
    }
    // Limpar direct-follow de offroad ao mudar de modo (o novo modo recalcula)
    g_wasOffroadDirect   = false;
    g_offroadSustainedFrames = 0;
    s_playerOffroadOnFrames  = 0;
    s_playerOffroadOffFrames = 0;
    s_lastPlayerRoadDist     = 0.0f;

    CVehicle*   recruitCar = g_car;
    CAutoPilot& ap         = recruitCar->m_autoPilot;
    CVehicle*   playerCar  = player->bInVehicle ? player->m_pVehicle : nullptr;

    switch (mode)
    {
    // ── CIVICO-F: FOLLOWCAR_FARAWAY, AVOID_CARS ────────────────────
    // v5.18: FOLLOWCAR com m_pTargetCar=playerCar.
    // SA engine segue o carro do jogador pelo road-graph — como trafego normal.
    // ProcessDrivingAI mantém missão e targetCar per-frame.
    case DriveMode::CIVICO_F:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_F sem carro jogador -> fallback DIRETO");
            SetupDriveMode(player, DriveMode::DIRETO);
            return;
        }
        // v5.18: FOLLOWCAR_FARAWAY inicial
        ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;
        ap.m_pTargetCar       = playerCar;
        ap.m_nCruiseSpeed     = SPEED_CIVICO;
        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;
        {
            unsigned linkPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
            unsigned areaPre  = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
            float    headPre  = recruitCar->GetHeading();
            g_invalidLinkCounter = 0;
            unsigned linkPost  = linkPre;
            unsigned areaPost  = areaPre;
            float    headPost  = headPre;
            if (!skipSnap)
            {
                CCarCtrl::JoinCarWithRoadSystem(recruitCar);
                g_civicRoadSnapTimer = 0;
                linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                areaPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nAreaId;
                headPost = recruitCar->GetHeading();
            }
            LogDrive("SetupDriveMode: CIVICO_F mission=FOLLOWCAR_FARAWAY(52) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u areaId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, areaPre, areaPost,
                headPre, headPost,
                skipSnap ? "skipSnap" :
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-G: FOLLOWCAR_FARAWAY, AVOID_CARS ────────────────────
    // v5.18: Mesmo que CIVICO_F — FOLLOWCAR com m_pTargetCar=playerCar.
    case DriveMode::CIVICO_G:
    {
        if (!playerCar)
        {
            LogDrive("SetupDriveMode: CIVICO_G sem carro jogador -> fallback DIRETO");
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
            g_invalidLinkCounter = 0;
            unsigned linkPost  = linkPre;
            float    headPost  = headPre;
            if (!skipSnap)
            {
                CCarCtrl::JoinCarWithRoadSystem(recruitCar);
                g_civicRoadSnapTimer = 0;
                linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                headPost = recruitCar->GetHeading();
            }
            LogDrive("SetupDriveMode: CIVICO_G mission=FOLLOWCAR_FARAWAY(52) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, headPre, headPost,
                skipSnap ? "skipSnap" :
                (linkPre == linkPost ? "ATENCAO:linkId nao mudou" : "JoinRoad OK"));
        }
        break;
    }

    // ── CIVICO-H: FOLLOWCAR_FARAWAY, AVOID_CARS ────────────────────
    // v5.18: Mesmo que CIVICO_F — FOLLOWCAR com m_pTargetCar=playerCar.
    // CLOSE_BLOCKED_WAIT continua activo em ProcessDrivingAI.
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
            g_invalidLinkCounter = 0;
            unsigned linkPost  = linkPre;
            float    headPost  = headPre;
            if (!skipSnap)
            {
                CCarCtrl::JoinCarWithRoadSystem(recruitCar);
                g_civicRoadSnapTimer = 0;
                linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
                headPost = recruitCar->GetHeading();
            }
            LogDrive("SetupDriveMode: CIVICO_H mission=FOLLOWCAR_FARAWAY(52) speed=%d "
                     "driveStyle=AVOID_CARS playerCar=%p "
                     "linkId %u->%u heading %.3f->%.3f (%s)",
                (int)ap.m_nCruiseSpeed, static_cast<void*>(playerCar),
                linkPre, linkPost, headPre, headPost,
                skipSnap ? "skipSnap" :
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

        // v5.18: FOLLOWCAR_FARAWAY — segue o carro do jogador pelo road-graph.
        // SA engine trata: faixa, cruzamentos, paragem, trafego — como NPC normal.
        // v5.6-v5.17 usavam GOTOCOORDS com destino 20m atras, que causava:
        //   routing errado em cruzamentos, posicao lateral, recruta parado.
        unsigned linkPre = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
        CCarCtrl::JoinCarWithRoadSystem(recCar);
        unsigned linkPost = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;

        ap.m_nCarMission         = MC_FOLLOWCAR_FARAWAY;
        ap.m_pTargetCar          = playerCar;
        ap.m_nCruiseSpeed        = SPEED_CIVICO;
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS;

        LogDrive("[recr:%p] SetupDriveModeSimple: modo=%s FOLLOWCAR_FARAWAY(52) "
                 "targetCar=%p linkId %u->%u",
            (void*)ped, DriveModeName(mode),
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

        // v5.18: FOLLOWCAR precisa de m_pTargetCar=playerCar.
        // v5.6 nullificava m_pTargetCar para GOTOCOORDS puro.
        // v5.18: actualizar m_pTargetCar e missao para FOLLOWCAR em multi-recrutas.
        if (IsCivicoMode(g_driveMode))
        {
            CVehicle* playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;
            if (playerCar)
            {
                if (ap.m_pTargetCar != playerCar)
                {
                    LogMulti("[recr:%d] MULTI_TARGET_UPDATE: m_pTargetCar=%p -> %p (FOLLOWCAR)",
                        i, (void*)ap.m_pTargetCar, (void*)playerCar);
                    ap.m_pTargetCar = playerCar;
                }
                // Garantir missao FOLLOWCAR para multi-recrutas
                if (ap.m_nCarMission != MC_FOLLOWCAR_FARAWAY &&
                    ap.m_nCarMission != MC_FOLLOWCAR_CLOSE)
                {
                    ap.m_nCarMission = MC_FOLLOWCAR_FARAWAY;
                }
            }
            else
            {
                // Jogador saiu do carro — parar multi-recruta
                ap.m_nCarMission = MISSION_STOP_FOREVER;
                ap.m_nCruiseSpeed = 0;
                ap.m_pTargetCar = nullptr;
            }
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
    CVehicle*   playerCar = player->bInVehicle ? player->m_pVehicle : nullptr;

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
            CVector waypoint{};
            bool hasWaypoint = GetMapWaypoint(waypoint);
            if (!hasWaypoint)
            {
                g_diretoTimer = 0;
                s_passengerArrived = false;
                ap.m_nCarMission = MISSION_STOP_FOREVER;
                ap.m_nCruiseSpeed = 0;
                ap.m_pTargetCar = nullptr;
                if (!s_passengerWaitingWaypoint)
                {
                    s_passengerWaitingWaypoint = true;
                    LogDrive("PASSENGER_WAIT: sem waypoint do mapa -> STOP_FOREVER ate novo destino");
                }
                return;
            }

            s_passengerWaitingWaypoint = false;
            float distToWaypoint = Dist2D(vPos, waypoint);
            if (distToWaypoint <= PASSENGER_ARRIVE_DIST_M)
            {
                g_diretoTimer = 0;
                ap.m_nCarMission = MISSION_STOP_FOREVER;
                ap.m_nCruiseSpeed = 0;
                ap.m_pTargetCar = nullptr;
                if (!s_passengerArrived)
                {
                    s_passengerArrived = true;
                    LogDrive("PASSENGER_ARRIVED: distToWaypoint=%.1fm <= %.1fm -> STOP_FOREVER",
                        distToWaypoint, PASSENGER_ARRIVE_DIST_M);
                }
                return;
            }

            s_passengerArrived = false;

            // Actualizar destino: waypoint do mapa.
            if (g_diretoTimer <= 0 || ap.m_nCarMission != MISSION_GOTOCOORDS ||
                Dist2D(ap.m_vecDestinationCoors, waypoint) > 1.0f)
            {
                ap.m_nCarMission         = MISSION_GOTOCOORDS;
                ap.m_pTargetCar          = nullptr;
                ap.m_vecDestinationCoors = waypoint;
                // v5.4: Limpar REVERSE ao definir novo destino para prevenir
                // reverso persistente. Log v5.3 mostrou tempAction=3(REVERSE)
                // durante sessoes inteiras de PASSENGER mode.
                ap.m_nTempAction         = 0;
                g_diretoTimer = DIRETO_UPDATE_INTERVAL;
                LogDrive("PASSENGER_NAV: source=MAP_WAYPOINT dest=(%.1f,%.1f,%.1f) distToWaypoint=%.1fm maxSpeed=%d turnSpeed=%d",
                    waypoint.x, waypoint.y, waypoint.z, distToWaypoint,
                    (int)SPEED_PASSENGER, (int)SPEED_PASSENGER_TURN);
            }
            else
            {
                --g_diretoTimer;
            }
            // v5.1: CURVE BRAKE — detectar curvas REAIS da estrada.
            // Usa heading do road-link (ClipTargetOrientationToLink) em vez da
            // direcao ao waypoint. A estrada nao esta necessariamente alinhada
            // com o waypoint — usar direcao ao waypoint causava curve brake
            // permanente em estradas rectas.
            // Fallback: se off-road (linkId invalido), usa direcao ao destino.
            float currentHeading = veh->GetHeading();
            float roadLinkH      = currentHeading;
            bool  hasRoadLink    = GetRoadLinkHeading(veh, roadLinkH);
            bool  hasCurveBrake  = false;
            float deltaHeading   = 0.0f;

            if (hasRoadLink)
            {
                // On-road: deltaH = diferenca entre heading actual e heading da estrada
                deltaHeading = AbsHeadingDelta(roadLinkH, currentHeading);
            }
            else
            {
                // Off-road: fallback — direcao ao destino
                float destH = currentHeading;
                GetDestinationVectorHeading(veh, ap.m_vecDestinationCoors, destH);
                deltaHeading = AbsHeadingDelta(destH, currentHeading);
            }

            // Hysteresis: activar a 0.35 rad, desactivar a 0.20 rad
            if (s_passengerCurveBrake)
            {
                if (deltaHeading < CURVE_BRAKE_DEACT_RAD) s_passengerCurveBrake = false;
            }
            else
            {
                if (deltaHeading > CURVE_BRAKE_ACT_RAD) s_passengerCurveBrake = true;
            }
            hasCurveBrake = s_passengerCurveBrake;
            float targetHeading = hasRoadLink ? roadLinkH : currentHeading;

            if (hasCurveBrake)
            {
                ap.m_nCruiseSpeed = SPEED_PASSENGER_TURN;
            }
            else
            {
                // Alinhado com estrada: velocidade maxima
                ap.m_nCruiseSpeed = SPEED_PASSENGER;
            }

            ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;

            // v5.4: Limpar REVERSE persistente per-frame em PASSENGER mode.
            // Log v5.3 mostrou tempAction=3(REVERSE) durante sessoes inteiras.
            // O SA engine re-aplica REVERSE se o destino estiver atras do carro
            // (angulo > 90°), mas em PASSENGER mode queremos que o recruta
            // contorne — GOTOCOORDS com AVOID_CARS deveria fazer U-turn, nao reverso.
            if (ap.m_nTempAction == 3) // 3 = REVERSE
            {
                ap.m_nTempAction = 0;
            }
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
                LogAI("PASSENGER_DRIVING: speed_ap=%d physSpeed=%.0fkmh curveBrake=%d deltaH=%.3f "
                      "distToWaypoint=%.1fm mission=%d(%s) style=%d(%s) tempAction=%d(%s) "
                      "heading=%.3f targetH=%.3f dest=(%.1f,%.1f,%.1f)",
                    (int)ap.m_nCruiseSpeed, physSpeedP, hasCurveBrake ? 1 : 0, deltaHeading,
                    distToWaypoint,
                    (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
                    (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
                    (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
                    currentHeading, targetHeading,
                    ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z);
            }
            return;
        }
    }

    // ── Modo WAYPOINT_SOLO: recruta conduz sozinho ao waypoint (v4.4) ───
    // Jogador NAO está no carro — recruta conduz para waypoint independentemente.
    // Usa mesma logica de navegacao que PASSENGER mas sem restricao de jogador.
    if (g_state == ModState::WAYPOINT_SOLO)
    {
        CVector waypoint{};
        bool hasWaypoint = GetMapWaypoint(waypoint);
        if (!hasWaypoint)
        {
            g_diretoTimer = 0;
            s_waypointSoloArrived = false;
            ap.m_nCarMission = MISSION_STOP_FOREVER;
            ap.m_nCruiseSpeed = 0;
            ap.m_pTargetCar = nullptr;
            if (!s_waypointSoloWaiting)
            {
                s_waypointSoloWaiting = true;
                LogDrive("WAYPOINT_SOLO_WAIT: sem waypoint do mapa -> STOP_FOREVER ate marcar destino");
            }
            return;
        }

        s_waypointSoloWaiting = false;
        float distToWaypoint = Dist2D(vPos, waypoint);
        if (distToWaypoint <= PASSENGER_ARRIVE_DIST_M)
        {
            g_diretoTimer = 0;
            ap.m_nCarMission = MISSION_STOP_FOREVER;
            ap.m_nCruiseSpeed = 0;
            ap.m_pTargetCar = nullptr;
            if (!s_waypointSoloArrived)
            {
                s_waypointSoloArrived = true;
                LogDrive("WAYPOINT_SOLO_ARRIVED: distToWaypoint=%.1fm <= %.1fm -> STOP_FOREVER",
                    distToWaypoint, PASSENGER_ARRIVE_DIST_M);
            }
            return;
        }

        s_waypointSoloArrived = false;

        // Actualizar destino: waypoint do mapa.
        if (g_diretoTimer <= 0 || ap.m_nCarMission != MISSION_GOTOCOORDS ||
            Dist2D(ap.m_vecDestinationCoors, waypoint) > 1.0f)
        {
            LogDrive("WAYPOINT_SOLO_NAV: source=MAP_WAYPOINT dest=(%.1f,%.1f,%.1f) distToWaypoint=%.1fm maxSpeed=%d turnSpeed=%d",
                waypoint.x, waypoint.y, waypoint.z, distToWaypoint,
                (int)SPEED_PASSENGER, (int)SPEED_PASSENGER_TURN);
            ap.m_nCarMission         = MISSION_GOTOCOORDS;
            ap.m_pTargetCar          = nullptr;
            ap.m_vecDestinationCoors = waypoint;
            g_diretoTimer = DIRETO_UPDATE_INTERVAL;
        }
        else
        {
            --g_diretoTimer;
        }

        // v5.1: CURVE BRAKE — road-link-based (mesma logica que PASSENGER)
        float currentHeading = veh->GetHeading();
        float roadLinkH      = currentHeading;
        bool  hasRoadLink    = GetRoadLinkHeading(veh, roadLinkH);
        bool  hasCurveBrake  = false;
        float deltaHeading   = 0.0f;

        if (hasRoadLink)
        {
            deltaHeading = AbsHeadingDelta(roadLinkH, currentHeading);
        }
        else
        {
            float destH = currentHeading;
            GetDestinationVectorHeading(veh, ap.m_vecDestinationCoors, destH);
            deltaHeading = AbsHeadingDelta(destH, currentHeading);
        }

        // Hysteresis
        if (s_waypointCurveBrake)
        {
            if (deltaHeading < CURVE_BRAKE_DEACT_RAD) s_waypointCurveBrake = false;
        }
        else
        {
            if (deltaHeading > CURVE_BRAKE_ACT_RAD) s_waypointCurveBrake = true;
        }
        hasCurveBrake = s_waypointCurveBrake;
        float targetHeading = hasRoadLink ? roadLinkH : currentHeading;

        if (hasCurveBrake)
        {
            ap.m_nCruiseSpeed = SPEED_PASSENGER_TURN;
        }
        else
        {
            // Alinhado com estrada: velocidade maxima
            ap.m_nCruiseSpeed = SPEED_PASSENGER;
        }

        ap.m_nCarDrivingStyle = DRIVINGSTYLE_AVOID_CARS;

        // Stuck recovery activa
        if (s_stuckCooldown > 0) --s_stuckCooldown;
        {
            float physSpeedW = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
            if (physSpeedW < STUCK_SPEED_KMH)
            {
                if (++s_stuckTimer >= STUCK_DETECT_FRAMES)
                {
                    s_stuckTimer    = 0;
                    s_stuckCooldown = STUCK_RECOVER_COOLDOWN;
                    CCarCtrl::JoinCarWithRoadSystem(veh);
                    g_diretoTimer = 0;
                    LogDrive("WAYPOINT_SOLO_STUCK_RECOVER: physSpeed=%.1fkmh -> JoinRoadSystem + dest reset",
                        physSpeedW);
                }
            }
            else
            {
                s_stuckTimer = 0;
            }
        }

        // Log periodico
        if (++g_logAiFrame >= LOG_AI_INTERVAL)
        {
            g_logAiFrame = 0;
            float physSpeedW = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
            float distToWaypointLog = Dist2D(vPos, ap.m_vecDestinationCoors);
            LogAI("WAYPOINT_SOLO_DRIVING: speed_ap=%d physSpeed=%.0fkmh curveBrake=%d deltaH=%.3f "
                  "distToWaypoint=%.1fm mission=%d(%s) style=%d(%s) tempAction=%d(%s) "
                  "heading=%.3f targetH=%.3f dest=(%.1f,%.1f,%.1f)",
                (int)ap.m_nCruiseSpeed, physSpeedW, hasCurveBrake ? 1 : 0, deltaHeading,
                distToWaypointLog,
                (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
                (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
                (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
                currentHeading, targetHeading,
                ap.m_vecDestinationCoors.x, ap.m_vecDestinationCoors.y, ap.m_vecDestinationCoors.z);
        }
        return;
    }

    // ── ZONA STOP: recruta completamente parado ──────────────────
    if (dist < STOP_ZONE_M)
    {
        ap.m_nCruiseSpeed    = 0;
        ap.m_nCarMission     = MISSION_STOP_FOREVER;
        // v5.21: STOP_FOR_CARS na zona de paragem total para consistencia de estilo.
        // Quando o recruta retomar movimento (dist aumenta), inicia ja com STOP_FOR_CARS
        // em vez de AVOID_CARS do frame anterior, evitando primeiro frame a desviar.
        if (IsCivicoMode(g_driveMode))
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;
        return;
    }

    // ── ZONA SLOW: recruta abranda ──────────────────────────────
    // v5.19: Para FOLLOWCAR, NAO sobrescrever missao com GOTOCOORDS.
    // v5.18 bug: SLOW_ZONE restaurava GOTOCOORDS(8) que destruia FOLLOWCAR(52),
    //   causando oscilacao GOTOCOORDS↔FOLLOWCAR a cada frame que entrava/saia da zona.
    //   Log v5.18 mostrava CIVICO_FOLLOWCAR_RESTORE: mission 9(?)->52 repetido.
    // FIX: Apenas reduzir cruiseSpeed. FOLLOWCAR desacelera nativamente quando
    //   proximo do carro-alvo (como trafego normal). Nao precisa de GOTOCOORDS.
    if (dist < SLOW_ZONE_M)
    {
        ap.m_nCruiseSpeed = SPEED_SLOW;
        // v5.19: Restaurar FOLLOWCAR se STOP_ZONE tinha posto STOP_FOREVER.
        // Nao tocar na missao se ja e FOLLOWCAR (52/53).
        if (IsCivicoMode(g_driveMode) && playerCar)
        {
            // v5.21: RTF-inspired — STOP_FOR_CARS em zona proxima (dist < 12m).
            // Real-traffic-fix: trafego normal usa STOP_FOR_CARS (nao AVOID_CARS).
            // AVOID_CARS a <12m faz o recruta desviar pela calcada para chegar ao
            // destino, resultando em paragem ao lado do jogador em vez de atras.
            // STOP_FOR_CARS mantem o recruta na faixa correcta como trafego normal.
            ap.m_nCarDrivingStyle = DRIVINGSTYLE_STOP_FOR_CARS;

            if (ap.m_nCarMission == MISSION_STOP_FOREVER)
            {
                if (!g_slowZoneRestoring)
                {
                    LogDrive("SLOW_ZONE: dist=%.1fm missao_atual=%d(STOP_FOREVER) -> FOLLOWCAR restaurada "
                             "speed=%d modo=%s",
                        dist, (int)ap.m_nCarMission,
                        (int)SPEED_SLOW,
                        DriveModeName(g_driveMode));
                    g_slowZoneRestoring = true;
                }
                ap.m_nCarMission  = MC_FOLLOWCAR_FARAWAY;
                ap.m_pTargetCar   = playerCar;
            }
        }
        return;
    }

    // Saiu da SLOW_ZONE (dist >= SLOW_ZONE_M): navegacao FOLLOWCAR retomada.
    if (g_slowZoneRestoring)
    {
        g_slowZoneRestoring = false;
        LogDrive("SLOW_ZONE: saiu (dist=%.1fm) — navegacao FOLLOWCAR retomada", dist);
    }

    // ── TELEPORT CATCH-UP (todos os modos) ──────────────────────
    // v5.15: Movido para ANTES dos early-return de offroad/direto/player-offroad.
    // Anteriormente (v5.13-v5.14) o teleport so corria dentro do bloco CIVICO normal,
    // nunca alcancado quando recruta estava em offroad-direct, player-offroad-direct,
    // DIRETO, ou invalid-link-fallback. Recruta podia ficar preso nesses modos
    // e desaparecer pelo streaming engine sem nunca ser teleportado.
    if (s_teleportCooldownTimer > 0) --s_teleportCooldownTimer;
    if (dist > TELEPORT_CATCHUP_DIST && s_teleportCooldownTimer <= 0)
    {
        float   tpHeading = GetPlayerHeading(player);
        CVector tpFwd(std::sinf(tpHeading), std::cosf(tpHeading), 0.0f);
        CVector warpDest = playerPos - tpFwd * TELEPORT_CATCHUP_BEHIND;
        warpDest.z = playerPos.z;

        // CEntity::Teleport (virtual, override CAutomobile/CBike/CBoat):
        //   CWorld::Remove, SetPosition, ResetMoveSpeed+TurnSpeed+Suspension, CWorld::Add.
        veh->Teleport(warpDest, false);
        veh->SetHeading(tpHeading);
        CCarCtrl::JoinCarWithRoadSystem(veh);
        g_civicRoadSnapTimer    = 0;
        s_teleportCooldownTimer = TELEPORT_CATCHUP_COOLDOWN;

        // v5.18: Restaurar missao apos teleport — FOLLOWCAR para carro do jogador
        if (playerCar)
        {
            ap.m_nCarMission         = MC_FOLLOWCAR_FARAWAY;
            ap.m_pTargetCar          = playerCar;
            ap.m_nCruiseSpeed        = SPEED_CIVICO;
            ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS;
            ap.m_nTempAction         = 0;
        }
        else
        {
            ap.m_nCarMission         = MISSION_STOP_FOREVER;
            ap.m_nCruiseSpeed        = 0;
            ap.m_nTempAction         = 0;
        }

        // Limpar flags de offroad para que o recruta retome normalmente
        if (g_wasOffroadDirect)     g_wasOffroadDirect = false;
        if (s_playerOffroadDirect)  s_playerOffroadDirect = false;
        s_playerOffroadOnFrames  = 0;
        s_playerOffroadOffFrames = 0;
        if (s_invalidLinkForceDirect) {
            s_invalidLinkForceDirect = false;
            s_invalidLinkBurstFrames = 0;
            g_wasInvalidLink = false;
            g_invalidLinkCounter = 0;
        }

        LogDrive("TELEPORT_CATCHUP: dist=%.1fm>%.0fm -> warp (%.1f,%.1f,%.1f) "
                 "heading=%.3f cooldown=%d — flags offroad/invalidLink limpos",
            dist, TELEPORT_CATCHUP_DIST,
            warpDest.x, warpDest.y, warpDest.z,
            tpHeading, TELEPORT_CATCHUP_COOLDOWN);
        return; // skip restante processamento neste frame (acabou de ser teleportado)
    }

    // ── Verificacao de offroad (throttled) ───────────────────────
    if (g_offroadTimer <= 0)
    {
        bool wasOffroad = g_isOffroad;
        g_isOffroad     = DetectOffroad(veh, g_isOffroad);  // v4.5: passa estado atual para hysteresis
        g_offroadTimer  = OFFROAD_CHECK_INTERVAL;
        if (g_isOffroad != wasOffroad)
        {
            LogDrive("Offroad: %s -> %s (modo=%s)",
                wasOffroad ? "SIM" : "NAO",
                g_isOffroad  ? "SIM" : "NAO",
                DriveModeName(g_driveMode));
            // Ao sair de offroad em CIVICO: snap ao road-graph para realinhar.
            // v5.6: nao snap se o JOGADOR ainda estiver offroad (s_playerOffroadDirect) —
            // JoinCarWithRoadSystem nesse caso faz o recruta desviar para a estrada
            // mais proxima em vez de continuar a seguir o jogador no offroad.
            if (!g_isOffroad && IsCivicoMode(g_driveMode) && !g_wasOffroadDirect && !s_playerOffroadDirect)
            {
                CCarCtrl::JoinCarWithRoadSystem(veh);
                g_civicRoadSnapTimer = 0;
                LogDrive("OFFROAD_EXIT_SNAP: JoinCarWithRoadSystem ao regressar a estrada (CIVICO)");
            }
            else if (!g_isOffroad && IsCivicoMode(g_driveMode) && s_playerOffroadDirect)
            {
                LogDrive("OFFROAD_EXIT_SNAP: SKIP (playerOffroad=1 — recruta segue jogador sem snap)");
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
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS; // desvia de obstaculos em offroad

        // v5.21: Steer-angle speed reduction (RTF-inspired) — previne excesso de
        // velocidade em curvas durante offroad direct follow.
        {
            unsigned char offroadSpd = SPEED_CIVICO;
            float absSteerOff = std::abs(veh->m_fSteerAngle);
            if (absSteerOff > CIVICO_STEER_BRAKE_MIN)
            {
                float cap = (float)offroadSpd * (1.0f - absSteerOff);
                if (cap < (float)SPEED_CIVICO_TURN) cap = (float)SPEED_CIVICO_TURN;
                offroadSpd = static_cast<unsigned char>(static_cast<unsigned int>(cap));
            }
            ap.m_nCruiseSpeed = offroadSpd;
        }

        // Health restoration ainda actua neste modo (ver bloco abaixo)
        if (IsCarValid() && g_car->m_fHealth < RECRUIT_CAR_HEALTH_MIN)
        {
            g_car->m_fHealth = RECRUIT_CAR_HEALTH_INITIAL;
        }
        return; // skip road-graph processing enquanto offroad
    }
    else if (g_wasOffroadDirect && !s_playerOffroadDirect)
    {
        // Acabamos de sair do modo direct-follow: road-follow CIVICO retoma.
        // v5.15: Guard !s_playerOffroadDirect — se o jogador ainda esta offroad,
        // NAO restaurar CIVICO (o bloco s_playerOffroadDirect abaixo trata o follow).
        g_wasOffroadDirect = false;
        g_civicRoadSnapTimer = -DIRECT_EXIT_SNAP_COOLDOWN_FRAMES;
        SetupDriveMode(player, g_driveMode, true);
        LogDrive("OFFROAD_DIRECT_END: de volta a estrada — road-follow CIVICO restaurado (cooldown=%d, skipSnap)",
            DIRECT_EXIT_SNAP_COOLDOWN_FRAMES);
    }
    if (g_isOffroad && IsCivicoMode(g_driveMode))
    {
        // Offroad ainda nao atingiu threshold: resetar snap para evitar snap errado
        g_civicRoadSnapTimer = 0;
    }

    // ── v3.4: INVALID_LINK fallback para GOTOCOORDS direto ───────────────
    // Se INVALID_LINK burst >20 frames: desistir do road-graph temporariamente
    // e usar navegacao direta por 5s para escapar da area problematica.
    // Usa SPEED_CIVICO + AVOID_CARS (nao SPEED_DIRETO) para manter elegancia.
    if (s_invalidLinkForceDirect && s_invalidLinkForceDirectTimer > 0)
    {
        --s_invalidLinkForceDirectTimer;

        // Destino = DIRETO_FOLLOW_OFFSET metros atras do jogador
        float   playerHeading = GetPlayerHeading(player);
        CVector pFwd(std::sinf(playerHeading), std::cosf(playerHeading), 0.0f);
        CVector dest = playerPos - pFwd * DIRETO_FOLLOW_OFFSET;
        dest.z = playerPos.z;

        ap.m_nCarMission         = MISSION_GOTOCOORDS;
        ap.m_pTargetCar          = nullptr;
        ap.m_vecDestinationCoors = dest;
        ap.m_nCruiseSpeed        = SPEED_CIVICO; // conservador: 46 kmh, nao SPEED_DIRETO (60)
        ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS; // evita paredes/props

        // Health restoration ainda actua
        if (IsCarValid() && g_car->m_fHealth < RECRUIT_CAR_HEALTH_MIN)
        {
            g_car->m_fHealth = RECRUIT_CAR_HEALTH_INITIAL;
        }
        return; // skip road-graph processing durante fallback
    }
    else if (s_invalidLinkForceDirect && s_invalidLinkForceDirectTimer == 0)
    {
        // Timer expirou: retornar ao modo CIVICO normal
        s_invalidLinkForceDirect = false;
        s_invalidLinkBurstFrames = 0; // reset burst counter
        g_wasInvalidLink = false;
        g_invalidLinkCounter = 0;
        g_civicRoadSnapTimer = 0;
        SetupDriveMode(player, g_driveMode);
        LogDrive("INVALID_LINK_FALLBACK_DIRECT_END: 5s expirados — retomando modo %s com road-graph",
                 DriveModeName(g_driveMode));
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

    // Jogador fora do grafo (longe de um nó): mudar temporariamente para GOTOCOORDS
    // directo até ao jogador, evitando que o recruta fique preso no road-graph.
    // Hysteresis: ativa aos 25m, desativa aos 10m (previne oscilação rápida).
    if (playerCar)
    {
        float playerRoadDist = DistToNearestRoadNode(playerCar);
        s_lastPlayerRoadDist = playerRoadDist;  // guardar para logging

        // v5.21: Activacao imediata quando jogador parado em offroad.
        // Quando jogador para (<3kmh) com playerRoadDist > ON_DIST, o playerRoadDist
        // pode oscilar ±1-2m em redor do threshold a cada frame (recalculo de nos).
        // Isso reseta s_playerOffroadOnFrames constantemente, impedindo activacao.
        // FIX: jogador parado EM offroad → forcar imediatamente o contador ao limite.
        float playerSpeedKmhOff = playerCar->m_vecMoveSpeed.Magnitude() * 180.0f;
        bool  playerStoppedOffroad = (playerSpeedKmhOff < CLOSE_BLOCKED_MIN_KMH
                                      && playerRoadDist > PLAYER_OFFROAD_ON_DIST_M
                                      && !s_playerOffroadDirect);

        if (playerRoadDist > PLAYER_OFFROAD_ON_DIST_M)
        {
            if (playerStoppedOffroad)
            {
                // Imediato — sem esperar 15 frames quando parado em offroad
                s_playerOffroadOnFrames = PLAYER_OFFROAD_SUSTAIN_FRAMES;
            }
            else
            {
                ++s_playerOffroadOnFrames;
            }
            s_playerOffroadOffFrames = 0;
        }
        else if (playerRoadDist < PLAYER_OFFROAD_OFF_DIST_M)
        {
            ++s_playerOffroadOffFrames;
            s_playerOffroadOnFrames = 0;
        }
        else
        {
            s_playerOffroadOnFrames = 0;
            s_playerOffroadOffFrames = 0;
        }

        bool shouldActivate = (!s_playerOffroadDirect && s_playerOffroadOnFrames >= PLAYER_OFFROAD_SUSTAIN_FRAMES);
        bool shouldDeactivate = (s_playerOffroadDirect && s_playerOffroadOffFrames >= PLAYER_OFFROAD_SUSTAIN_FRAMES);

        // Apenas modos CIVICO precisam deste fallback: DIRETO ja usa GOTOCOORDS,
        // PARADO deve continuar parado, e PASSAGEIRO nao conduz.
        if (shouldActivate && IsCivicoMode(g_driveMode))
        {
            s_playerOffroadDirect = true;
            s_playerOffroadOnFrames = 0;
            s_playerOffroadOffFrames = 0;
            LogDrive("PLAYER_OFFROAD_DIRECT_START: playerRoadDist=%.1fm>%.0f sustain=%d frames%s -> GOTOCOORDS direto ao jogador",
                playerRoadDist, PLAYER_OFFROAD_ON_DIST_M, PLAYER_OFFROAD_SUSTAIN_FRAMES,
                playerStoppedOffroad ? "(IMEDIATO-parado)" : "");
        }
        else if (shouldDeactivate)
        {
            s_playerOffroadDirect = false;
            s_playerOffroadOnFrames = 0;
            s_playerOffroadOffFrames = 0;
            g_civicRoadSnapTimer = -DIRECT_EXIT_SNAP_COOLDOWN_FRAMES;
            // Reaplicar o modo actual sem re-snap imediato: evita snap errado ao sair
            // de zonas limite/interseccoes onde o player esteve momentaneamente fora do grafo.
            SetupDriveMode(player, g_driveMode, true);
            LogDrive("PLAYER_OFFROAD_DIRECT_END: playerRoadDist=%.1fm<%.0f sustain=%d frames ok -> restaurar CIVICO (skipSnap=1 linkId=%u)",
                playerRoadDist, PLAYER_OFFROAD_OFF_DIST_M, PLAYER_OFFROAD_SUSTAIN_FRAMES,
                (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId);
            // continuar processamento normal no mesmo frame
        }

        if (s_playerOffroadDirect)
        {
            ap.m_nCarMission         = MISSION_GOTOCOORDS;
            ap.m_pTargetCar          = nullptr;
            ap.m_vecDestinationCoors = playerPos;
            ap.m_nCarDrivingStyle    = DRIVINGSTYLE_AVOID_CARS;

            // v5.21: Steer-angle speed reduction (RTF-inspired) — reduz velocidade
            // em curvas durante perseguicao directa ao jogador offroad.
            {
                unsigned char playerOffSpd = SPEED_CIVICO;
                float absSteerPOff = std::abs(veh->m_fSteerAngle);
                if (absSteerPOff > CIVICO_STEER_BRAKE_MIN)
                {
                    float cap = (float)playerOffSpd * (1.0f - absSteerPOff);
                    if (cap < (float)SPEED_CIVICO_TURN) cap = (float)SPEED_CIVICO_TURN;
                    playerOffSpd = static_cast<unsigned char>(static_cast<unsigned int>(cap));
                }
                ap.m_nCruiseSpeed = playerOffSpd;
            }
            return;
        }
    }

    // ── Modo PARADO: nada a fazer per-frame ──────────────────────
    if (g_driveMode == DriveMode::PARADO)
        return;

    // ═══════════════════════════════════════════════════════════════
    // A partir daqui: modos CIVICO (CIVICO_F/G/H)
    //
    // v5.18: FOLLOWCAR_FARAWAY — segue o carro do jogador pelo road-graph.
    //   Historico (para referencia):
    //     v5.3-v5.5: MC67 (ESCORT_REAR_FARAWAY) — posicao lateral + crash.
    //     v5.6-v5.17: GOTOCOORDS PURO com destino 20m atras do jogador —
    //       routing errado em cruzamentos (pathfinder para coordenada atras),
    //       recruta parado de lado, viragens erradas, AHEAD_HOLD catastrofico.
    //   v5.18: Log v5.17 mostrou AHEAD_HOLD parando recruta 60m atras (alignDot
    //     negativo em estradas curvas = falso positivo). GOTOCOORDS com destino
    //     atras do jogador e FUNDAMENTALMENTE incompativel com road-graph pathfinding.
    //   Solucao: FOLLOWCAR_FARAWAY (52) — missao nativa do SA engine.
    //     - Segue o CAMINHO do carro-alvo (nao uma coordenada fixa)
    //     - Em cruzamentos, segue o mesmo caminho que o jogador tomou
    //     - Fica na faixa atras do jogador (como trafego normal)
    //     - Para naturalmente quando o jogador para
    //     - SA engine trata curvas, trafego, faixas nativamente
    //   v5.6 abandonou MC53/MC52/MC67 por: (1) ESCORT lateral, (2) oscilacao
    //     ESCORT↔GOTOCOORDS → crash. v5.18 usa FOLLOWCAR (nao ESCORT)
    //     consistentemente (sem oscilacao) + null-check per-frame.
    // ═══════════════════════════════════════════════════════════════

    float physSpeedC = veh->m_vecMoveSpeed.Magnitude() * 180.0f;

    // Determinar se estamos on-road (linkId valido)
    unsigned civicoLinkId = (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId;
    bool onRoad = !((civicoLinkId == 0 && ap.m_nCurrentPathNodeInfo.m_nAreaId == 0) || civicoLinkId > MAX_VALID_LINK_ID);

    // ── v5.20: CURVE BRAKE para CIVICO FOLLOWCAR ──────────────────────
    // v5.18 removeu curve brake acreditando que FOLLOWCAR limitava velocidade
    // em curvas nativamente. Log v5.19 mostrou physSpeed=100kmh em curvas apertadas
    // (heading=1.2 rad), recruta cortava calcadas e parava ao lado do jogador.
    // FIX: Re-aplicar curve brake usando road-link heading (mesmo que PASSENGER mode
    // que funciona perfeitamente). Referencia gta-reversed: road-link heading indica
    // a direcao da estrada no ponto actual — deltaH alto = curva real.
    // Thresholds CIVICO sao mais altos que PASSENGER (0.80/0.50 vs 0.35/0.20)
    // porque FOLLOWCAR ja tem alguma inteligencia de curva, so precisa de um cap.
    float currentHeading = veh->GetHeading();
    float civicoRoadLinkH = currentHeading;
    bool  civicoHasRoadLink = GetRoadLinkHeading(veh, civicoRoadLinkH);
    float civicoDeltaHeading = 0.0f;

    if (civicoHasRoadLink)
    {
        civicoDeltaHeading = AbsHeadingDelta(civicoRoadLinkH, currentHeading);
    }

    // Hysteresis com limite superior (v5.16 logica preservada):
    // - Activar quando deltaH > CIVICO_CURVE_BRAKE_ACT_RAD (0.80 rad ~46°)
    // - Desactivar quando deltaH < CIVICO_CURVE_BRAKE_DEACT_RAD (0.50 rad ~29°)
    // - NAO activar quando deltaH > CIVICO_CURVE_BRAKE_MAX_RAD (1.80 rad) = catch-up/reposicionamento
    if (s_civicoCurveBrake)
    {
        if (civicoDeltaHeading < CIVICO_CURVE_BRAKE_DEACT_RAD)
            s_civicoCurveBrake = false;
    }
    else
    {
        if (civicoDeltaHeading > CIVICO_CURVE_BRAKE_ACT_RAD &&
            civicoDeltaHeading < CIVICO_CURVE_BRAKE_MAX_RAD)
            s_civicoCurveBrake = true;
    }
    bool hasCivicoCurveBrake = s_civicoCurveBrake;

    // Velocidade base: zonas por distancia
    // v5.19: FOLLOWCAR gere a velocidade em relacao ao carro-alvo internamente
    //   (como o trafego normal faz). m_nCruiseSpeed e apenas um CAP/alvo para
    //   aceleracao — FOLLOWCAR pode ir mais rapido quando o alvo vai rapido.
    //   v5.18 bug: playerSpeed-based zones davam speed_ap=8 quando jogador parado
    //   (0+8=8) — recruta aproximava-se a 8kmh de 30m de distancia.
    //   FIX: Usar zonas fixas por distancia. FOLLOWCAR trata speed-matching.
    // v5.20: Adicionado zona 12-20m com velocidade reduzida para evitar overshoot
    //   em curvas de aproximacao. Curve brake aplica reducao adicional.
    unsigned char speed = SPEED_CIVICO;
    float playerSpeed = 0.0f;
    if (playerCar)
    {
        playerSpeed = playerCar->m_vecMoveSpeed.Magnitude() * 180.0f;
        if (dist > 80.0f)
        {
            // >80m: catch-up muito agressivo
            speed = SPEED_CATCHUP_VERY_FAR;
        }
        else if (dist > 60.0f)
        {
            // 60-80m: catch-up longe
            speed = SPEED_CATCHUP_FAR;
        }
        else if (dist > FAR_CATCHUP_ON_DIST_M)
        {
            // 40-60m: catch-up base
            speed = SPEED_CATCHUP;
        }
        else if (dist > CIVICO_CLOSE_ALIGN_DIST)
        {
            // 20-40m: velocidade normal de seguimento.
            speed = SPEED_CIVICO;
        }
        else
        {
            // <20m: velocidade reduzida para aproximacao — evita overshoot em curvas.
            // v5.20: A esta distancia o recruta ja esta proximo; velocidade alta
            // causa corte de curvas pela calcada e paragem ao lado do jogador.
            // SPEED_SLOW (25) e suficiente — FOLLOWCAR desacelera atras do alvo.
            speed = SPEED_SLOW;
        }
    }

    // v5.20: Curve brake override — reduzir para SPEED_CIVICO_TURN (25) em curvas
    // independentemente da zona de distancia. Prioridade sobre speed zones.
    if (hasCivicoCurveBrake)
    {
        speed = std::min(speed, SPEED_CIVICO_TURN);
    }

    // v5.21: Steer-angle speed reduction (inspirado no real-traffic-fix).
    // real-traffic-fix: fNewSpeed -= cfgTurningSpeedDecrease(15) * absSteerAngle.
    // Para o recruta (cruiseSpeed 46-85 vs trafego normal 15), usa reducao proporcional:
    //   speedCap = max(SPEED_CIVICO_TURN, speed * (1.0 - absSteerAngle))
    // Threshold CIVICO_STEER_BRAKE_MIN=0.15 filtra micro-correccoes em linha recta.
    // Aplicado APOS heading-delta curve brake para adicionar reducao extra em curvas reais.
    // m_fSteerAngle e o angulo actual do volante do autopilot (-1.0 a +1.0).
    {
        float absSteer = std::abs(veh->m_fSteerAngle);
        if (absSteer > CIVICO_STEER_BRAKE_MIN)
        {
            float steerCap = (float)speed * (1.0f - absSteer);
            if (steerCap < (float)SPEED_CIVICO_TURN) steerCap = (float)SPEED_CIVICO_TURN;
            unsigned char steerCapUC = static_cast<unsigned char>(static_cast<unsigned int>(steerCap));
            speed = std::min(speed, steerCapUC);
        }
    }

    // v5.21: DriveStyle adaptativo por distancia — threshold aumentado para 30m.
    // v5.20: threshold era 20m (CIVICO_CLOSE_ALIGN_DIST). Log v5.19 mostrou que
    //   o recruta usava AVOID_CARS a 10-15m (SLOW_ZONE retornava sem definir driveStyle).
    //   AVOID_CARS a distancias proximas causa corte de calcadas em curvas.
    // v5.21: FIX duplo — SLOW_ZONE agora define STOP_FOR_CARS (dist<12m) + este
    //   bloco usa CIVICO_STOP_FOR_CARS_DIST=30m para cobrir 12-30m tambem.
    //   AVOID_CARS so activo a >30m (catch-up puro — sem risco de calcadas).
    // Referencia real-traffic-fix: trafego normal usa STOP_FOR_CARS em todos os ranges.
    eCarDrivingStyle civicoStyle = (dist < CIVICO_STOP_FOR_CARS_DIST)
        ? DRIVINGSTYLE_STOP_FOR_CARS
        : DRIVINGSTYLE_AVOID_CARS;

    // v5.9: Traffic-aware speed boost. Detectar trafego pesado e aumentar velocidade
    // para ajudar o recruta a acompanhar o jogador em areas congestionadas.
    if (dist >= 15.0f && dist < FAR_CATCHUP_ON_DIST_M)
    {
        int trafficCount = DetectNearbyTraffic(veh, playerCar);
        if (trafficCount >= TRAFFIC_HEAVY_THRESHOLD)
        {
            float boostedSpeed = (float)speed + TRAFFIC_SPEED_BOOST;
            float capped = std::min(boostedSpeed, (float)SPEED_PASSENGER);
            unsigned char oldSpeed = speed;
            speed = static_cast<unsigned char>(capped);

            static int s_trafficBoostLogCooldown = 0;
            if (s_trafficBoostLogCooldown <= 0)
            {
                LogDrive("TRAFFIC_BOOST: %d carros detectados >= %d threshold, "
                         "speed %d->%d (+%.0f boost) dist=%.1fm",
                    trafficCount, TRAFFIC_HEAVY_THRESHOLD,
                    (int)oldSpeed, (int)speed, TRAFFIC_SPEED_BOOST, dist);
                s_trafficBoostLogCooldown = 120;
            }
            else
            {
                --s_trafficBoostLogCooldown;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // v5.18: FOLLOWCAR_FARAWAY — manter missao e m_pTargetCar per-frame
    // ═══════════════════════════════════════════════════════════════

    // ── Seguranca: verificar playerCar per-frame ──────────────────────
    // v5.6 crash era por m_pTargetCar ficar null apos jogador sair do carro.
    // v5.18: verificar per-frame e fall back para STOP_FOREVER se null.
    static bool s_wasNoPlayerCar = false;
    if (!playerCar)
    {
        ap.m_nCarMission  = MISSION_STOP_FOREVER;
        ap.m_nCruiseSpeed = 0;
        ap.m_pTargetCar   = nullptr;
        if (!s_wasNoPlayerCar)
        {
            LogDrive("CIVICO_NO_PLAYERCAR: jogador saiu do carro -> STOP_FOREVER");
            s_wasNoPlayerCar = true;
        }
    }
    else
    {
        if (s_wasNoPlayerCar)
        {
            LogDrive("CIVICO_PLAYERCAR_RESTORED: jogador entrou no carro %p -> FOLLOWCAR retomado",
                (void*)playerCar);
            s_wasNoPlayerCar = false;
        }

        // ── LANE HOLD — parar na faixa quando jogador parado ─────────────
        // v5.13: Quando jogador parado (<3kmh) e recruta proximo (<20m),
        // STOP_FOREVER para manter posicao na faixa actual.
        // v5.18: Mantido como seguranca adicional — FOLLOWCAR ja para naturalmente
        // atras do jogador, mas LANE_HOLD garante que o recruta nao tenta
        // ultrapassar em situacoes de baixa velocidade.
        bool playerStopped = (playerCar->m_vecMoveSpeed.Magnitude() * 180.0f < CLOSE_BLOCKED_MIN_KMH);
        bool inLaneHold = false;

        if (playerStopped && dist < CIVICO_CLOSE_ALIGN_DIST && IsCivicoMode(g_driveMode))
        {
            inLaneHold = true;

            if (!g_closeBlocked)
            {
                g_closeBlocked = true;
                LogDrive("CIVICO_LANE_HOLD_ON: jogador parado (%.1fkmh<%.1f) dist=%.1fm "
                         "-> STOP_FOREVER (manter posicao na faixa)",
                    playerCar->m_vecMoveSpeed.Magnitude() * 180.0f,
                    CLOSE_BLOCKED_MIN_KMH, dist);
            }
        }
        else if (g_closeBlocked)
        {
            float playerSpeedNow = playerCar->m_vecMoveSpeed.Magnitude() * 180.0f;
            if (playerSpeedNow > CLOSE_BLOCKED_RESUME_KMH || dist >= CIVICO_CLOSE_ALIGN_DIST)
            {
                g_closeBlocked = false;
                LogDrive("CIVICO_LANE_HOLD_OFF: jogador a andar (%.1fkmh>%.1f) dist=%.1fm "
                         "-> FOLLOWCAR retomado",
                    playerSpeedNow, CLOSE_BLOCKED_RESUME_KMH, dist);
            }
            else
            {
                inLaneHold = true;
            }
        }

        // ── APLICAR: STOP_FOREVER ou FOLLOWCAR ───────────────────────────
        if (inLaneHold)
        {
            ap.m_nCarMission  = MISSION_STOP_FOREVER;
            ap.m_nCruiseSpeed = 0;
        }
        else
        {
            // v5.18: Manter FOLLOWCAR_FARAWAY com targetCar = carro do jogador.
            // Actualizar per-frame para garantir que m_pTargetCar e valido.
            // Se a missao foi alterada (stuck recovery, teleport, etc.), restaurar.
            // FOLLOWCAR_FARAWAY (52) transiciona automaticamente para FOLLOWCAR_CLOSE (53)
            // quando proximo — ambos sao validos.
            bool missionOk = (ap.m_nCarMission == MC_FOLLOWCAR_FARAWAY ||
                              ap.m_nCarMission == MC_FOLLOWCAR_CLOSE);

            if (!missionOk || ap.m_pTargetCar != playerCar)
            {
                eCarMission oldMission = ap.m_nCarMission;
                ap.m_nCarMission      = MC_FOLLOWCAR_FARAWAY;
                ap.m_pTargetCar       = playerCar;
                ap.m_nCarDrivingStyle = civicoStyle;

                if (oldMission != MC_FOLLOWCAR_FARAWAY && oldMission != MC_FOLLOWCAR_CLOSE)
                {
                    LogDrive("CIVICO_FOLLOWCAR_RESTORE: mission %d(%s)->%d(FOLLOWCAR_FARAWAY) "
                             "targetCar=%p dist=%.1fm",
                        (int)oldMission, GetCarMissionName((int)oldMission),
                        (int)MC_FOLLOWCAR_FARAWAY, (void*)playerCar, dist);
                }
            }
            ap.m_nCruiseSpeed     = speed;
            ap.m_nCarDrivingStyle = civicoStyle;
        }

        // v5.12: Limpar REVERSE per-frame (SA engine nunca faz timeout).
        if (ap.m_nTempAction == TEMP_ACTION_REVERSE ||
            ap.m_nTempAction == TEMP_ACTION_REVERSE_LEFT ||
            ap.m_nTempAction == TEMP_ACTION_REVERSE_RIGHT)
        {
            ap.m_nTempAction = 0;
        }

        // v5.15: Teleport catch-up movido para inicio de ProcessDrivingAI (antes de early returns).
    }

    // v5.20: currentHeading ja definido acima (curve brake block).

    // Stuck recovery
    if (s_stuckCooldown > 0) --s_stuckCooldown;
    {
        if (physSpeedC < STUCK_SPEED_KMH && dist > SLOW_ZONE_M)
        {
            if (++s_stuckTimer >= STUCK_DETECT_FRAMES)
            {
                s_stuckTimer    = 0;
                s_stuckCooldown = STUCK_RECOVER_COOLDOWN;
                CCarCtrl::JoinCarWithRoadSystem(veh);
                LogDrive("CIVICO_STUCK_RECOVER: physSpeed=%.1fkmh dist=%.1fm -> JoinRoadSystem",
                    physSpeedC, dist);
            }
        }
        else
        {
            s_stuckTimer = 0;
        }
    }

    // v5.12: Periodic road snap para CIVICO.
    // v5.18: Mantido — JoinCarWithRoadSystem ajuda FOLLOWCAR a manter
    // consciencia de estrada, especialmente apos teleport ou stuck recovery.
    if (g_civicRoadSnapTimer >= 0 && ++g_civicRoadSnapTimer >= ROAD_SNAP_INTERVAL)
    {
        g_civicRoadSnapTimer = 0;
        if (s_stuckCooldown <= 0 && !g_isOffroad)
        {
            CCarCtrl::JoinCarWithRoadSystem(veh);
        }
    }
    else if (g_civicRoadSnapTimer < 0)
    {
        ++g_civicRoadSnapTimer;
    }

    // Log periodico
    if (++g_logAiFrame >= LOG_AI_INTERVAL)
    {
        g_logAiFrame = 0;
        float physSpeedLog = veh->m_vecMoveSpeed.Magnitude() * 180.0f;
        float playerSpeedLog = playerCar ? playerCar->m_vecMoveSpeed.Magnitude() * 180.0f : 0.0f;
        char taskBuf[384] = {};
        {
            CTaskManager& tm = g_recruit->m_pIntelligence->m_TaskMgr;
            int w = BuildPrimaryTaskBuf(taskBuf, (int)sizeof(taskBuf), tm);
            BuildSecondaryTaskBuf(taskBuf, (int)sizeof(taskBuf), w, tm);
        }
        // v5.21: Log actualizado — steer= adicionado (RTF diagnostico de curva por angulo de volante).
        // stopFC= mostra qual driveStyle foi escolhido (0=AVOID_CARS, 1=STOP_FOR_CARS).
        float logSteer = veh->m_fSteerAngle;
        int   logStopFC = (ap.m_nCarDrivingStyle == DRIVINGSTYLE_STOP_FOR_CARS) ? 1 : 0;
        bool  logPlayerStopped = playerCar && (playerCar->m_vecMoveSpeed.Magnitude() * 180.0f < CLOSE_BLOCKED_MIN_KMH);
        LogAI("CIVICO_DRIVE_1: speed_ap=%d physSpeed=%.0fkmh playerSpeed=%.0fkmh "
              "dist=%.1fm mission=%d(%s) style=%d(%s) tempAction=%d(%s) "
              "heading=%.3f curveBrake=%d deltaH=%.3f steer=%.2f stopFC=%d aggr=%d modo=%s onRoad=%d "
              "offroad=%d playerOffroad=%d playerRoadDist=%.1fm "
              "laneHold=%d playerStopped=%d targetCar=%s teleportCD=%d",
            (int)ap.m_nCruiseSpeed, physSpeedLog, playerSpeedLog,
            dist,
            (int)ap.m_nCarMission, GetCarMissionName((int)ap.m_nCarMission),
            (int)ap.m_nCarDrivingStyle, GetDriveStyleName((int)ap.m_nCarDrivingStyle),
            (int)ap.m_nTempAction, GetTempActionName((int)ap.m_nTempAction),
            currentHeading, (int)hasCivicoCurveBrake, civicoDeltaHeading,
            logSteer, logStopFC,
            (int)g_aggressive, DriveModeName(g_driveMode), (int)onRoad,
            (int)g_isOffroad, (int)s_playerOffroadDirect, s_lastPlayerRoadDist,
            (int)g_closeBlocked, (int)logPlayerStopped,
            (ap.m_pTargetCar == playerCar) ? "PLAYER" : (ap.m_pTargetCar ? "OTHER" : "NULL"),
            s_teleportCooldownTimer);
        // v5.18: DRIVE_2 mantido para diagnostico de stuck/offroad/tasks
        LogAI("CIVICO_DRIVE_2: offroad=%d offroadSust=%d stuck=%d/%d stuckCD=%d headon=%d/%d headonCD=%d "
              "linkId=%u playerOffroadDirect=%d wasOffroadDirect=%d tasks=%s",
            (int)g_isOffroad, g_offroadSustainedFrames,
            s_stuckTimer, STUCK_DETECT_FRAMES, s_stuckCooldown,
            s_headonFrames, HEADON_PERSISTENT_FRAMES, s_headonCooldown,
            (unsigned)ap.m_nCurrentPathNodeInfo.m_nCarPathLinkId,
            (int)s_playerOffroadDirect, (int)g_wasOffroadDirect,
            taskBuf);

        // Dist trend: ver se recruta se esta a aproximar ou afastar
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
            // v5.0: bCanBeDamaged=false previne dano visual (portas abertas, paineis deformados)
            // Fumaca vanilla aparece em <= 256 de vida (comportamento SA nao alterado)
            g_car->m_fHealth       = RECRUIT_CAR_HEALTH_INITIAL;
            g_car->bTakeLessDamage = true;
            g_car->bCanBeDamaged   = false;
            // v5.4: Prevenir despawn do carro do recruta pelo streaming engine.
            // Sem bStreamingDontDelete, o SA engine pode remover o carro quando o
            // jogador se afasta (ex: offroad, curvas largas). Com esta flag,
            // o carro so e removido por codigo do mod (DismissRecruit).
            g_car->bStreamingDontDelete = true;
            // v5.21: Marcar como MISSION_VEHICLE para que real-traffic-fix (e outros
            // mods de trafego) nao interfiram com o nosso AI per-frame.
            // CAUSA DO BUG: real-traffic-fix define m_nCruiseSpeed=15 (cfgCruiseSpeed)
            //   ANTES do check de m_pTargetCar — mesmo em FOLLOWCAR mode, o cruiseSpeed
            //   fica 15 (overriding os nossos 46-85kmh) por um frame inteiro por render.
            //   A SA engine processa o autopilot com o valor errado (15 = ~8kmh reais).
            //   Ref RTF codigo: vehicle->m_nCruiseSpeed = cfgCruiseSpeed; // linha antes do check
            // FIX: m_nCreatedBy=MISSION_VEHICLE(2) => RTF skipa completamente via este
            //   check UNICO que controla todo o bloco: "if (vehicle->m_nCreatedBy !=
            //   eVehicleCreatedBy::MISSION_VEHICLE)". Com MISSION_VEHICLE, sao bloqueados:
            //   (a) cruiseSpeed override (15 kmh), e
            //   (b) SwitchVehicleToRealPhysics — que convertiria o carro de autopilot
            //       para fisica real, destruindo FOLLOWCAR e toda a logica de missao.
            // m_nCreatedBy e unsigned char (plugin-sdk CVehicle.h:237), logo cast necessario.
            // eVehicleCreatedBy::MISSION_VEHICLE = 2 (CVehicle.h:69).
            // SIDE EFFECTS POSITIVOS: previne remocao por CCarCtrl::DeleteOldestCar.
            g_car->m_nCreatedBy = static_cast<unsigned char>(eVehicleCreatedBy::MISSION_VEHICLE);
            // v5.13: Proteger o PED recruta de despawn pelo streaming engine.
            // SetCharCreatedBy(2) ja feito no spawn (Main.cpp:206) e em
            // ApplyRecruitEnhancement. bStreamingDontDelete no ped previne
            // remocao do RW object (graficos) mesmo quando muito longe.
            // gta-reversed Population.cpp: ManagePed() skipa PED_MISSION.
            g_recruit->bStreamingDontDelete = true;
            g_carHealthTimer       = 0;
            s_carVisualFixTimer    = 0;
            // v5.3: Reparar dano visual imediato ao entrar — limpa portas abertas
            // e paineis deformados do carro antes de comecar a conduzir.
            RepairCarVisualDamage(g_car);
            LogEvent("CAR_DURABILITY_SETUP: health=%.0f bTakeLessDamage=1 bCanBeDamaged=0 "
                "bStreamingDontDelete=1(car+ped) createdBy=2(MISSION) visualRepair=immediate "
                "RTF_BLOCKED=1",
                RECRUIT_CAR_HEALTH_INITIAL);

            SetupDriveMode(player, g_driveMode);

            // v5.4: Limpar tempAction apos SetupDriveMode para prevenir reverso
            // automatico na entrada. O SA engine pode definir tempAction=REVERSE
            // ao calcular a rota inicial (ex: carro virado para lado oposto do
            // alvo). Limpando aqui, o autopilot recalcula a rota no proximo frame
            // sem o bias de reverso, partindo para a frente.
            // NOTA: m_nTimeTempAction NAO existe no plugin-sdk CAutoPilot.
            // Limpar m_nTempAction=0 e suficiente — SA engine reseta o timer internamente.
            g_car->m_autoPilot.m_nTempAction  = 0;
            LogEvent("ENTRY_CLEAR_REVERSE: tempAction limpo apos SetupDriveMode");

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

        // v5.3: Reparacao visual periodica — portas abertas, paineis deformados, etc.
        // Intervalo de 1s (CAR_VISUAL_FIX_INTERVAL=60) para fechar portas rapidamente
        // apos colisoes. Tambem re-aplica bCanBeDamaged=false.
        if (++s_carVisualFixTimer >= CAR_VISUAL_FIX_INTERVAL)
        {
            s_carVisualFixTimer = 0;
            RepairCarVisualDamage(g_car);
        }

        // v5.16: Anti-despawn enforcement per-frame.
        // O streaming engine pode limpar bStreamingDontDelete em certas condicoes
        // (ex: muita memoria usada, muito longe do jogador). Re-aplicar a cada
        // frame para GARANTIR que o carro e ped do recruta nao desaparecem.
        // v5.21: Tambem re-aplicar m_nCreatedBy=2 (MISSION_VEHICLE) per-frame.
        // Razao: nao ha evidencia de que SA reset este campo, mas o custo e zero
        // e garante que real-traffic-fix NUNCA processa o nosso carro mesmo apos
        // uma mudanca interna de veiculo (g_car update em ProcessDriving).
        // Custo: 3 writes de 1 byte por frame — negligivel.
        g_car->bStreamingDontDelete = true;
        // v5.21 per-frame: bloqueia RTF (cruiseSpeed override + SwitchVehicleToRealPhysics).
        g_car->m_nCreatedBy         = static_cast<unsigned char>(eVehicleCreatedBy::MISSION_VEHICLE);
        g_recruit->bStreamingDontDelete = true;
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
