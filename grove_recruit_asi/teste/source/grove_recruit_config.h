#pragma once
/*
 * grove_recruit_config.h
 *
 * Includes, constantes, enumeracoes e helpers inline partilhados por
 * todos os modulos do grove_recruit_standalone.asi.
 *
 * REGRA: este header NAO deve incluir nenhum header especifico do mod.
 * Apenas includes do SDK/runtime, constantes e tipos simples.
 */

// ───────────────────────────────────────────────────────────────────
// SDK / Game includes
// ───────────────────────────────────────────────────────────────────
#include "plugin.h"

#include "CWorld.h"
#include "CPools.h"
#include "CPed.h"
#include "CPlayerPed.h"
#include "CVehicle.h"
#include "CAutoPilot.h"
#include "eCarMission.h"
#include "ePedType.h"
#include "CCarCtrl.h"
#include "CPathFind.h"
#include "CNodeAddress.h"
#include "CPopulation.h"
#include "CStreaming.h"
#include "CMessages.h"
#include "CPedGroup.h"
#include "CPedGroupMembership.h"
#include "CPedGroups.h"
#include "CPedIntelligence.h"
#include "CTaskManager.h"
#include "CTaskComplexEnterCarAsDriver.h"
#include "CTaskComplexEnterCarAsPassenger.h"
#include "CTaskComplexLeaveCar.h"
#include "CStats.h"
#include "eStats.h"
#include "eWeaponType.h"
#include "eTaskType.h"
#include "CTimer.h"
#include "CFont.h"
#include "CRadar.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>   // rand, srand
#include <cstring>
#include <ctime>     // time (srand seed)
#include <algorithm>
#include <windows.h>   // GetAsyncKeyState

// Versao exibida no log/menu ao carregar o plugin.
#define PLUGIN_VERSION "4.8"

// ───────────────────────────────────────────────────────────────────
// Modelos e tipo do recruta
// ───────────────────────────────────────────────────────────────────

// Modelos Grove Street Families (FAM1/FAM2/FAM3)
static constexpr int   FAM_MODELS[]    = { 105, 106, 107 };
static constexpr int   FAM_MODEL_COUNT = 3;

// PED_TYPE_GANG2 = 8 = Grove Street Families (correcto).
// NAO usar PED_TYPE_GANG1 = 7 (Ballas/Vagos): causa congelamento e inimizade.
static constexpr ePedType RECRUIT_PED_TYPE = PED_TYPE_GANG2;

// Arma padrao: AK47 = 30
static constexpr eWeaponType RECRUIT_WEAPON = static_cast<eWeaponType>(30);
static constexpr int         RECRUIT_AMMO   = 300;

// Distancia de spawn atras do jogador (metros)
static constexpr float SPAWN_BEHIND_DIST = 2.5f;

// ───────────────────────────────────────────────────────────────────
// Distancias de zona (metros)
// ───────────────────────────────────────────────────────────────────
static constexpr float STOP_ZONE_M   = 6.0f;    // para completamente
static constexpr float SLOW_ZONE_M   = 10.0f;   // abranda

// v4.3: Reduzido 28m→18m para melhor deteccao de offroad em areas pequenas
// (estacionamentos, becos, caminhos laterais). Reduz confusao quando o recruta
// esta numa area offroad legitima mas o sistema pensa que ainda esta "on road".
// v4.5: Adicionado hysteresis para prevenir oscilacao ON↔OFF quando proximo do threshold
static constexpr float OFFROAD_ON_DIST_M  = 20.0f;  // ativa offroad (era OFFROAD_DIST_M=18m)
static constexpr float OFFROAD_OFF_DIST_M = 16.0f;  // desativa offroad (hysteresis 4m gap)
// Jogador fora do grafo: só considerar "fora" quando estiver bem longe de um nó
// para não disparar em casos triviais (ex: subir um passeio).
// Hysteresis: ativa GOTOCOORDS aos 42m, retorna a CIVICO aos 35m (previne oscilação).
// v4.3: Thresholds reduzidos (30→22m, 25→18m) para deteccao mais rapida
// de player offroad em areas pequenas. Hysteresis mantida (4m gap) para prevenir oscilacao.
static constexpr float PLAYER_OFFROAD_ON_DIST_M  = 22.0f;  // ativa GOTOCOORDS direto (era 30m)
static constexpr float PLAYER_OFFROAD_OFF_DIST_M = 18.0f;  // retorna a CIVICO (era 25m)

// Distancia minima para que WRONG_DIR_RECOVER dispare SetupDriveMode (v2 fix).
// CORRECAO v2: condicao INVERTIDA — SetupDriveMode so dispara quando dist > esta constante.
// ANTERIOR (bug): disparava quando dist < 30m → JoinCarWithRoadSystem em range proximo
//   → re-snap errado → WRONG_DIR prolongado 38+ segundos = modo "chase" off-road.
// FIX: apenas quando longe (dist > 30m). Range proximo usa snap periodico (ROAD_SNAP_INTERVAL).
static constexpr float WRONG_DIR_RECOVERY_DIST_M = 30.0f;

// ───────────────────────────────────────────────────────────────────
// Velocidades (unidades SA ≈ km/h)
// ───────────────────────────────────────────────────────────────────
static constexpr unsigned char SPEED_CIVICO       = 46;   // velocidade padrao CIVICO
static constexpr unsigned char SPEED_CIVICO_HIGH  = 60;   // velocidade em retas longas
static constexpr unsigned char SPEED_CATCHUP      = 55;   // velocidade catch-up base (dist 40-60m) - era 62
static constexpr unsigned char SPEED_CATCHUP_FAR  = 70;   // velocidade catch-up longe (dist 60-80m) - era 80
static constexpr unsigned char SPEED_CATCHUP_VERY_FAR = 85; // velocidade catch-up muito longe (dist >80m) - era 100
// v4.3: CORRECAO: Velocidades de catchup reduzidas (62→55, 80→70, 100→85) para
// prevenir aceleracao excessiva e colisoes traseiras. O sistema de aproximacao
// (player speed matching) funciona melhor com catchup speeds mais conservadores.

// v4.4: PASSENGER mode speeds — modo passageiro pode ir mais rapido mantendo seguranca
// v4.6: Aumentado 65→70 kmh mantendo curve brake (deltaH > 0.35 → 20 kmh) para curvas perfeitas
static constexpr unsigned char SPEED_PASSENGER       = 70;   // velocidade maxima modo passageiro (era 65 em v4.4, 46 em v4.3)
static constexpr unsigned char SPEED_PASSENGER_TURN  = 20;   // velocidade em curvas apertadas (curve brake)
static constexpr float PASSENGER_ARRIVE_DIST_M = 12.0f;      // parar ao chegar ao waypoint (passageiro/waypoint solo)
// SPEED_CIVICO_CLOSE REMOVIDO: o cap de 22 km/h tornava o recruta
// demasiado lento em retas proximas. O controlo de velocidade em curvas
// e feito por AdaptiveSpeed (CURVE_SPEED_REDUCTION=0.80), e a prevencao
// de calçada/contramao e feita pelo STOP_FOR_CARS_IGNORE_LIGHTS dinamico
// (activado quando dist < CLOSE_RANGE_SWITCH_DIST) + CLOSE_BLOCKED WAIT.
static constexpr unsigned char SPEED_SLOW         = 12;
static constexpr unsigned char SPEED_DIRETO       = 60;
static constexpr unsigned char SPEED_MIN          = 8;    // minimo absoluto

// Distancia acima da qual activar SPEED_CATCHUP em retas para recuperar distancia perdida.
// Abaixo deste valor usa SPEED_CIVICO_HIGH normal. Log: FAR_CATCHUP_ON/OFF.
// Hysteresis: ativa aos 40m, desativa aos 35m (previne oscilação).
static constexpr float FAR_CATCHUP_ON_DIST_M  = 40.0f;  // ativa catchup
static constexpr float FAR_CATCHUP_OFF_DIST_M = 35.0f;  // desativa catchup
// Faixa de aproximacao mais larga que o close-range puro: dentro deste range o
// recruta deixa de receber boost de reta e usa margem de aproximacao mais curta.
// Ajuda a reduzir batidas traseiras quando o jogador trava/entra em intersecoes.
// v4.3: Aumentado 35m→45m para dar mais tempo de desaceleracao ao aproximar.
static constexpr float APPROACH_SLOW_DIST_M = 45.0f;  // era 35.0f

// ───────────────────────────────────────────────────────────────────
// Intervalos de temporizador (frames @ 60 fps)
// ───────────────────────────────────────────────────────────────────
static constexpr int OFFROAD_CHECK_INTERVAL      = 30;   // 0.5s
static constexpr int DIRETO_UPDATE_INTERVAL      = 60;   // 1.0s
// Timeout para entrar em carro: separado por caso de uso.
// Como passageiro (entrar no carro do jogador): animacao de andar + abrir porta + sentar.
// O carro pode estar ate ~20m afastado — a pe = ~13s + animacao = 25s realista.
static constexpr int ENTER_CAR_PASSENGER_TIMEOUT = 1500; // 25.0s @ 60fps
// Como condutor (recruta vai buscar proprio carro): pode estar a ate 50m (FIND_CAR_RADIUS).
// A pe @ ~1.5 m/s = ~33s + animacao = 40s; dar margem generosa.
static constexpr int ENTER_CAR_DRIVER_TIMEOUT    = 1800; // 30.0s @ 60fps
static constexpr int GROUP_RESCAN_INTERVAL       = 120;  // 2.0s
static constexpr int INITIAL_FOLLOW_FRAMES       = 300;  // 5.0s
static constexpr int SCAN_GROUP_INTERVAL         = 180;  // 3.0s — scan para recrutas vanilla

// Intervalo de re-snap periodico ao road-graph para modos CIVICO.
// JoinCarWithRoadSystem e re-chamado a cada N frames para manter o
// veiculo alinhado com os nos de estrada e reduzir desvios de faixa.
// 60 frames (1s): mais frequente para corrigir escolhas ruins de link/path em
// intersecoes antes de o erro se prolongar por muito tempo.
// O snap e ignorado durante WRONG_DIR (ver ProcessDrivingAI).
static constexpr int ROAD_SNAP_INTERVAL     = 60;   // 1.0s (era 90=1.5s)

// Intervalo de dump AI throttled e diagnostico de distancia (frames @ 60fps).
// LOG_AI_INTERVAL=60: dump a cada 1s (era 120=2s); mais granular para ajustes finos.
// DIST_TREND_INTERVAL: registar delta de distancia para ver se recruta se aproxima ou afasta.
static constexpr int LOG_AI_INTERVAL        = 60;   // 1.0s — dump AI e dist-trend
static constexpr int DIST_TREND_INTERVAL    = 60;   // 1.0s — amostragem de distancia para tendencia

// ── CIVICO_H close-blocked WAIT ──────────────────────────────────
// Quando o recruta esta perto (< CLOSE_RANGE_SWITCH_DIST) E ambos —
// recruta E jogador — estao parados durante CLOSE_BLOCKED_FRAMES
// frames consecutivos (sinal de obstrucao no transito), o recruta
// comuta para STOP_FOREVER em vez de subir o passeio ou ir na
// contramao. Retoma o CIVICO normal quando o jogador voltar a andar.
// (Apenas CIVICO_H — os outros modos nao usam chase-close.)
static constexpr int   CLOSE_BLOCKED_FRAMES      = 90;  // 1.5s @ 60fps: frames consecutivos parados p/ activar
static constexpr float CLOSE_BLOCKED_MIN_KMH     = 3.0f;  // velocidade < 3 km/h = "parado"
static constexpr float CLOSE_BLOCKED_RESUME_KMH  = 8.0f;  // velocidade minima do jogador para retomar CIVICO

// ── Offroad direct-follow (canal/zona sem estrada) ─────────────────
// Em zonas sem estrada (ex: canal), o road-graph n existe → o recruta
// trava em WRONG_DIR tentando voltar a estrada que n esta la.
// Fix: apos OFFROAD_DIRECT_FOLLOW_FRAMES frames consecutivos em offroad,
// mudar para GOTOCOORDS directo (como DIRETO mas a velocidade CIVICO+AVOID_CARS).
// Retoma road-follow CIVICO quando o g_isOffroad limpar.
static constexpr int OFFROAD_DIRECT_FOLLOW_FRAMES = 90;  // 1.5s @ 60fps: offroad sustentado p/ activar beeline

// ── Durabilidade do carro do recruta ──────────────────────────────
// Replica comportamento do mod CLEO: carro arranca com vida alta (1750),
// e bTakeLessDamage para reduzir dano por impacto. Fumaca vanilla continua
// a aparecer quando a vida cai abaixo ~256. Restauracao periodica de saude
// evita destruicao por dano acumulado (o carro resiste mais sem parecer God-mode).
static constexpr float RECRUIT_CAR_HEALTH_INITIAL  = 1750.0f; // vida inicial (CLEO 0224)
static constexpr float RECRUIT_CAR_HEALTH_MIN      = 700.0f;  // threshold de restauracao
static constexpr int   RECRUIT_CAR_HEALTH_RESTORE_INTERVAL = 300; // 5.0s @ 60fps

// Intervalo do sistema de observacao vanilla (diagnostico de motor do jogo)
static constexpr int OBSERVER_INTERVAL      = 120;  // 2.0s

// ── Stuck / collision recovery ──────────────────────────────────────
// Quando o recruta fica encravado contra parede/prop/carro imovivel:
//   physSpeed < STUCK_SPEED_KMH por >= STUCK_DETECT_FRAMES → STUCK_RECOVER
// Recovery: JoinCarWithRoadSystem + log. Cooldown evita recuperacoes em loop.
// Em CIVICO_G (MC53 proximo): threshold mais baixo (prop hits mais frequentes).
static constexpr float STUCK_SPEED_KMH        = 3.0f;   // < 3 km/h = "parado/encravado"
static constexpr int   STUCK_DETECT_FRAMES    = 75;     // 1.25s @ 60fps
static constexpr int   STUCK_RECOVER_COOLDOWN = 150;    // 2.5s entre recuperacoes

// ── TempAction speed penalty ────────────────────────────────────────
// Quando o autopilot detecta colisao iminente (HEADON_COLLISION=19) ou
// encravamento (STUCK_TRAFFIC=12), reduzir velocidade de cruzeiro para
// dar mais tempo de manobragem e reduzir forca de impacto.
static constexpr float HEADON_SPEED_FACTOR    = 0.5f;   // 50% velocidade em HEADON_COLLISION
static constexpr float STUCK_TRAFFIC_SPEED_FACTOR = 0.4f; // 40% velocidade em STUCK_TRAFFIC
static constexpr float SWERVE_SPEED_FACTOR    = 0.75f;  // 75% velocidade durante SWERVE (simula AVOID+SLOW)
static constexpr int   REVERSE_STUCK_FRAMES   = 120;    // 2.0s em marcha-atrás -> re-snap road-graph

// ── Persistent HEADON recovery ─────────────────────────────────────
// Se HEADON_COLLISION(19) persistir por HEADON_PERSISTENT_FRAMES consecutivos,
// o recruta esta preso contra um prop/parede ou muito colado na traseira do alvo
// e o autopilot nao consegue sair rapidamente.
// Recovery agressiva: JoinCarWithRoadSystem + re-snap — forcado mesmo com cooldown.
// (Distinto do stuck-detection que se baseia em physSpeed < threshold.)
static constexpr int HEADON_PERSISTENT_FRAMES = 30;   // 0.5s com HEADON = recovery mais cedo
static constexpr int HEADON_RECOVER_COOLDOWN  = 90;   // 1.5s cooldown HEADON (mais curto: recovery rapida)

// ── Dist-trend logging thresholds ───────────────────────────────────
// Limiar de delta-distancia (metros) para classificar tendencia APROXIMAR/AFASTAR.
// |delta| < DIST_TREND_STABLE = ESTAVEL; < -threshold = APROXIMAR; > +threshold = AFASTAR.
static constexpr float DIST_TREND_STABLE_M = 1.5f;  // +/-1.5m por LOG_AI_INTERVAL = estavel

// ───────────────────────────────────────────────────────────────────
// Outros limites
// ───────────────────────────────────────────────────────────────────
static constexpr float    FIND_CAR_RADIUS    = 50.0f;
static constexpr unsigned MAX_VALID_LINK_ID  = 50000u;

// Maximo de tentativas FOLLOW_FALLBACK (POST_FOLLOW_CHECK) por ciclo.
// Apos este limite a re-armacao do timer para — o RESCAN periodico (120fr)
// e a rajada inicial tratam do follow. Evita loop infinito de SetAllocatorType.
static constexpr int MAX_FOLLOW_FALLBACK_RETRIES = 5;

// Limiares de desvio de heading para diagnostico de direccao:
//   > WRONG_DIR_THRESHOLD_RAD: recruta em sentido contrario (WRONG_DIR!)
//   > WRONG_DIR_THRESHOLD_CLOSE_RAD: threshold relaxado para close-range/intersecoes
//   > MISALIGNED_THRESHOLD_RAD: recruta desalinhado mas nao invertido
static constexpr float WRONG_DIR_THRESHOLD_RAD       = 1.5f;  // ~86° - padrão
static constexpr float WRONG_DIR_THRESHOLD_CLOSE_RAD = 2.3f;  // ~130° - close-range/intersecoes
// Limiar de "reta": abaixo deste angulo usa SPEED_CIVICO_HIGH (reta).
// 0.20 rad ≈ 11.5 graus — começa a abrandar mais cedo nas curvas.
// (era 0.3 rad ≈ 17 graus; baixar = mais conservador = menos erros em curvas)
static constexpr float MISALIGNED_THRESHOLD_RAD = 0.20f;

// Reducao de velocidade maxima em curvas (AdaptiveSpeed, modo CIVICO).
// O multiplicador e interpolado linearmente de 1.0 ate (1.0 - CURVE_SPEED_REDUCTION)
// quando o desalinhamento de heading vai de MISALIGNED_THRESHOLD_RAD ate
// WRONG_DIR_THRESHOLD_RAD.
// Ex (actual): REDUCTION=0.60 → mult mínimo = 0.40 → 46*0.40 ≈ 18 km/h na curva máxima.
// Balanceado: permite seguir jogador em alta velocidade mantendo segurança em curvas.
static constexpr float CURVE_SPEED_REDUCTION = 0.60f;

// ── Prevencao de close-range chase mode ────────────────────────────────
// O SA engine transiciona MC52→MC53 quando dist ≤ m_nStraightLineDistance.
// m_nStraightLineDistance padrao = 20 → troca para MC53 (chase off-road) a 20m.
// FIX: forcar m_nStraightLineDistance = CLOSE_RANGE_STRAIGHT_LINE_DIST cada frame.
//   → SA engine so transiciona MC52→MC53 quando dist < 5m (dentro da STOP_ZONE).
//   → MC52 (road-graph) permanece activo para todo o range de seguimento normal.
static constexpr unsigned char CLOSE_RANGE_STRAIGHT_LINE_DIST = 5u; // metros; < STOP_ZONE_M=6m

// Distancia proxima (metros) abaixo da qual CIVICO_F substitui
// MC_ESCORT_REAR(31) por MC_FOLLOWCAR_FARAWAY(52) em ProcessDrivingAI.
// ESCORT_REAR tenta posicionar-se geometricamente-exacto atras do jogador,
// o que pode causar "chase mode" off-road quando proximo. FOLLOWCAR_FARAWAY
// segue a mesma rota do jogador pelo road-graph sem posicionamento exacto.
// v4.3: Aumentado 22m→30m para dar mais tempo de desaceleracao antes da transicao
// para close-range. Previne colisoes traseiras e ultrapassagens.
static constexpr float CLOSE_RANGE_SWITCH_DIST = 30.0f;  // era 22.0f

// ───────────────────────────────────────────────────────────────────
// Aliases de missao eCarMission (nomes gta-reversed para clareza)
// Plugin-sdk usa nomes hexadecimais (MISSION_43=67, MISSION_34=52, etc.)
// que nao correspondem ao valor inteiro. Aliases abaixo usam nomes
// gta-reversed/CCarCtrl que descrevem o comportamento correctamente.
//
//   MC_ESCORT_REAR_FARAWAY (67) — escolta atras, longe; usa road-graph;
//     transiciona para MC_ESCORT_REAR(31) quando proximo. Usado em CIVICO_F.
//   MC_FOLLOWCAR_FARAWAY   (52) — segue carro, longe; road-graph.
//     transiciona para MC_FOLLOWCAR_CLOSE(53) quando proximo. Usado em CIVICO_H.
//   MC_FOLLOWCAR_CLOSE     (53) — segue carro, proximo; road-graph.
//     modo base para CIVICO_G (seguimento proximo directo).
// ───────────────────────────────────────────────────────────────────
static constexpr eCarMission MC_ESCORT_REAR_FARAWAY   = MISSION_43;           // int=67
static constexpr eCarMission MC_FOLLOWCAR_FARAWAY     = MISSION_34;           // int=52
static constexpr eCarMission MC_FOLLOWCAR_CLOSE       = MISSION_35;           // int=53
static constexpr eCarMission MC_APPROACHPLAYER_FARAWAY = MISSION_POLICE_BIKE; // int=43
static constexpr eCarMission MC_ESCORT_REAR           = MISSION_1F;           // int=31

// Validacao em tempo de compilacao: garantir que os nomes hexadecimais do plugin-sdk
// correspondem aos valores inteiros esperados (gta-reversed/SASCM.ini).
static_assert((int)MC_ESCORT_REAR_FARAWAY == 67, "MC_ESCORT_REAR_FARAWAY deve ser 67");
static_assert((int)MC_FOLLOWCAR_FARAWAY   == 52, "MC_FOLLOWCAR_FARAWAY deve ser 52");
static_assert((int)MC_FOLLOWCAR_CLOSE     == 53, "MC_FOLLOWCAR_CLOSE deve ser 53");
static_assert((int)MC_ESCORT_REAR         == 31, "MC_ESCORT_REAR deve ser 31");

// ───────────────────────────────────────────────────────────────────
// Distancias e offsets (metros)
// ───────────────────────────────────────────────────────────────────

// Distancia maxima (recruta-jogador) para auto-entrada no carro como passageiro.
// Se o recruta estiver mais longe que isto quando o jogador entrar no carro,
// nao emitir CTaskComplexEnterCarAsPassenger (o recruta nao conseguiria chegar).
static constexpr float RECRUIT_AUTO_ENTER_DIST = 60.0f;

// Offset de destino para modo DIRETO (metros atras do jogador).
// Evita que o recruta colida com o carro do jogador: o destino e calculado
// X metros atras da posicao/heading do jogador em vez de exactamente em cima.
static constexpr float DIRETO_FOLLOW_OFFSET = 10.0f;

// ───────────────────────────────────────────────────────────────────
// Multi-recruit
// ───────────────────────────────────────────────────────────────────
// Maximo de recrutas rastreados pelo mod (inclui o primario + recrutas vanilla).
// GTA SA permite ate 7 seguidores no grupo do jogador.
static constexpr int MAX_TRACKED_RECRUITS = 7;

// ── Multi-recruit car ─────────────────────────────────────────────
// Snap periodico ao road-graph para recrutas secundarios (conducao
// simplificada, sem o AI completo do primario).
static constexpr int MULTI_RECRUIT_SNAP_INTERVAL   = 150;  // 2.5s @ 60fps
// Restauracao periodica de saude do carro de recrutas secundarios.
static constexpr int MULTI_RECRUIT_HEALTH_INTERVAL = 300;  // 5.0s @ 60fps

// Tipos de allocator de tarefa padrao do grupo (ePedGroupDefaultTaskAllocatorType):
//   1 = FOLLOW_LIMITED  — follow em formacao (padrao a pe)
//   4 = SIT_IN_LEADER_CAR — todos tentam entrar no carro do lider
//   5 = RANDOM          — wander aleatorio
static constexpr int ALLOCATOR_FOLLOW          = 1;
static constexpr int ALLOCATOR_SIT_IN_CAR      = 4;

// ───────────────────────────────────────────────────────────────────
// Virtual Keys — acoes existentes + menu
// ───────────────────────────────────────────────────────────────────
static constexpr int VK_RECRUIT    = 0x31;  // 1
static constexpr int VK_CAR        = 0x32;  // 2
static constexpr int VK_PASSENGER  = 0x33;  // 3
static constexpr int VK_MODE       = 0x34;  // 4
static constexpr int VK_WAYPOINT   = 0x35;  // 5
static constexpr int VK_AGGRO      = 0x4E;  // N
static constexpr int VK_DRIVEBY    = 0x42;  // B

// Menu
static constexpr int VK_MENU_OPEN  = 0x2D;  // INSERT
static constexpr int VK_MENU_UP    = 0x26;  // UP arrow
static constexpr int VK_MENU_DOWN  = 0x28;  // DOWN arrow
static constexpr int VK_MENU_LEFT  = 0x25;  // LEFT arrow
static constexpr int VK_MENU_RIGHT = 0x27;  // RIGHT arrow
static constexpr int VK_MENU_BACK  = 0x1B;  // ESC

// ───────────────────────────────────────────────────────────────────
// Enumeracoes de estado
// ───────────────────────────────────────────────────────────────────
enum class ModState : int
{
    INACTIVE     = 0,
    ON_FOOT      = 1,
    ENTER_CAR    = 2,
    DRIVING      = 3,
    PASSENGER    = 4,   // jogador e passageiro no carro do recruta (tecla 3)
    RIDING       = 5,   // recruta e passageiro no carro do jogador (auto ou tecla 2 manual)
    WAYPOINT_SOLO = 6,  // v4.4: recruta conduz sozinho ao waypoint, jogador pode segui-lo (tecla 5)
};

enum class DriveMode : int
{
    CIVICO_F = 0,   // MC_ESCORT_REAR_FARAWAY(67) road-following, AVOID_CARS
    CIVICO_G = 1,   // MC_FOLLOWCAR_CLOSE(53)     seguimento proximo, AVOID_CARS
    CIVICO_H = 2,   // MC_FOLLOWCAR_FARAWAY(52)   road-following, AVOID_CARS  (melhor combo)
    DIRETO   = 3,   // MISSION_GOTOCOORDS(8)      vai directo, offset atras do jogador
    PARADO   = 4,   // MISSION_STOP_FOREVER(11)   para
    COUNT    = 5,
};

// ───────────────────────────────────────────────────────────────────
// Helpers inline (apenas logica de tipo, sem acesso a estado global)
// ───────────────────────────────────────────────────────────────────
inline const char* StateName(ModState s)
{
    switch (s) {
        case ModState::INACTIVE:      return "INACTIVE";
        case ModState::ON_FOOT:       return "ON_FOOT";
        case ModState::ENTER_CAR:     return "ENTER_CAR";
        case ModState::DRIVING:       return "DRIVING";
        case ModState::PASSENGER:     return "PASSENGER";
        case ModState::RIDING:        return "RIDING";
        case ModState::WAYPOINT_SOLO: return "WAYPOINT_SOLO";
        default:                      return "UNKNOWN";
    }
}

inline const char* DriveModeName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_F: return "CIVICO_F(MC67+AVOID)";
        case DriveMode::CIVICO_G: return "CIVICO_G(MC53+AVOID)";
        case DriveMode::CIVICO_H: return "CIVICO_H(MC52+AVOID)";
        case DriveMode::DIRETO:   return "DIRETO(GOTOCOORDS)";
        case DriveMode::PARADO:   return "PARADO(STOP_FOREVER)";
        default:                  return "UNKNOWN";
    }
}

// Nome curto para UI do menu
inline const char* DriveModeShortName(DriveMode m)
{
    switch (m) {
        case DriveMode::CIVICO_F: return "CIVICO-F";
        case DriveMode::CIVICO_G: return "CIVICO-G";
        case DriveMode::CIVICO_H: return "CIVICO-H";
        case DriveMode::DIRETO:   return "DIRETO";
        case DriveMode::PARADO:   return "PARADO";
        default:                  return "???";
    }
}

// Verdadeiro se o modo usa o road-graph (CIVICO_F/G/H).
inline bool IsCivicoMode(DriveMode m)
{
    return m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G ||
           m == DriveMode::CIVICO_H;
}

// Missao AutoPilot base para um dado modo CIVICO.
// CIVICO_F → MC_ESCORT_REAR_FARAWAY (67)
// CIVICO_H → MC_FOLLOWCAR_FARAWAY   (52)
// CIVICO_G → MC_FOLLOWCAR_CLOSE     (53)
inline eCarMission GetExpectedMission(DriveMode m)
{
    if (m == DriveMode::CIVICO_F) return MC_ESCORT_REAR_FARAWAY;
    if (m == DriveMode::CIVICO_H) return MC_FOLLOWCAR_FARAWAY;
    if (m == DriveMode::CIVICO_G) return MC_FOLLOWCAR_CLOSE;
    return MISSION_STOP_FOREVER;   // sentinela para DIRETO/PARADO
}

// DriveStyle para um dado modo CIVICO (para MISSION_RECOVERY e ProcessDrivingAI).
// Todos os 3 modos CIVICO restantes usam AVOID_CARS.
inline eCarDrivingStyle GetExpectedDriveStyle(DriveMode m)
{
    if (m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G ||
        m == DriveMode::CIVICO_H) return DRIVINGSTYLE_AVOID_CARS;
    return DRIVINGSTYLE_STOP_FOR_CARS_IGNORE_LIGHTS;
}
