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
#include "CAutomobile.h"
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
#define PLUGIN_VERSION "5.14"

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
// v5.11: SLOW_ZONE aumentado de 10m para 12m para evitar recruta parar muito perto.
// Log v5.10 mostrou recruta a 5-8m em STOP_FOREVER repetidamente — muito proximo!
static constexpr float STOP_ZONE_M   = 6.0f;    // para completamente
static constexpr float SLOW_ZONE_M   = 12.0f;   // abranda (era 10m)

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
// v5.9: Limiares ajustados para evitar retorno prematuro a estrada.
// Aumentado de 22m/18m para 35m/15m: maior threshold ON + maior hysteresis (20m).
// Isto permite que o jogador va mais longe do grafo antes do modo direto activar,
// e que volte MUITO mais proximo da estrada antes de voltar ao road-graph.
static constexpr float PLAYER_OFFROAD_ON_DIST_M  = 35.0f;  // ativa GOTOCOORDS direto (era 22m)
static constexpr float PLAYER_OFFROAD_OFF_DIST_M = 15.0f;  // retorna a CIVICO (era 18m)

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
static constexpr unsigned char SPEED_CIVICO_HIGH  = 70;   // retas longas (catch-up)
static constexpr unsigned char SPEED_CIVICO_TURN  = 25;   // v5.7: curvas apertadas em CIVICO GOTOCOORDS
                                                            //   Ligeiramente mais alto que SPEED_PASSENGER_TURN(20)
                                                            //   para nao perder muito terreno em curvas longas.
static constexpr unsigned char SPEED_CATCHUP      = 62;   // v5.3: 55→62 catch-up base (dist 40-60m)
static constexpr unsigned char SPEED_CATCHUP_FAR  = 80;   // v5.3: 70→80 catch-up longe (dist 60-80m)
static constexpr unsigned char SPEED_CATCHUP_VERY_FAR = 85; // velocidade catch-up muito longe (dist >80m)
// v5.3: Velocidades de catchup aumentadas para recuperar distancia mais rapidamente.
// v5.7: SPEED_CIVICO_TURN adicionado — curve brake restaurado para GOTOCOORDS puro.

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

// v5.9: Constantes para deteccao de trafego pesado e boost de velocidade
static constexpr float TRAFFIC_DETECT_RADIUS_M = 30.0f;  // raio de deteccao de trafego
static constexpr int   TRAFFIC_HEAVY_THRESHOLD = 8;      // numero de carros para considerar trafego pesado
static constexpr float TRAFFIC_SPEED_BOOST = 15.0f;      // boost de velocidade em trafego pesado (kmh)
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

// ── CIVICO close-blocked WAIT (lane hold) ────────────────────────
// v5.13: Activado em TODOS os modos CIVICO (era apenas CIVICO_H).
// Quando o JOGADOR esta parado (< CLOSE_BLOCKED_MIN_KMH) e o recruta
// esta proximo (< CIVICO_CLOSE_ALIGN_DIST=30m), o recruta para na
// sua faixa actual (STOP_FOREVER) em vez de continuar GOTOCOORDS.
// GOTOCOORDS+AVOID_CARS com destino atras do jogador causa o SA engine
// a rotear por faixas adjacentes ("embicar para o lado") ao tentar
// chegar ao ponto 20m atras. Com STOP_FOREVER, o recruta fica onde esta.
// Retoma GOTOCOORDS quando o jogador voltar a andar (> CLOSE_BLOCKED_RESUME_KMH).
// Nao requer CLOSE_BLOCKED_FRAMES de espera: activa IMEDIATAMENTE quando
// jogador esta parado e recruta proximo. Timer so usado para debounce de log.
static constexpr float CLOSE_BLOCKED_MIN_KMH     = 3.0f;  // velocidade < 3 km/h = "parado"
static constexpr float CLOSE_BLOCKED_RESUME_KMH  = 8.0f;  // velocidade minima do jogador para retomar CIVICO

// ── Offroad direct-follow (canal/zona sem estrada) ─────────────────
// Em zonas sem estrada (ex: canal), o road-graph n existe → o recruta
// trava em WRONG_DIR tentando voltar a estrada que n esta la.
// Fix: apos OFFROAD_DIRECT_FOLLOW_FRAMES frames consecutivos em offroad,
// mudar para GOTOCOORDS directo (como DIRETO mas a velocidade CIVICO+AVOID_CARS).
// Retoma road-follow CIVICO quando o g_isOffroad limpar.
// v5.2: Reduzido 90→45 para activacao mais rapida em offroad. Evita que o
// recruta fique perdido tentando voltar a estrada quando o jogador esta offroad.
static constexpr int OFFROAD_DIRECT_FOLLOW_FRAMES = 45;  // 0.75s @ 60fps: offroad sustentado p/ activar beeline

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

// ── v5.13: Teleport catch-up ────────────────────────────────────────
// Quando o recruta fica muito longe (> TELEPORT_CATCHUP_DIST), warpar o carro
// para um ponto TELEPORT_CATCHUP_BEHIND metros atras do jogador.
// Padrao em jogos open-world para NPCs escolta: evita perda definitiva do
// recruta sem precisar de velocidades altas (que causam batidas e curvas erradas).
// Cooldown: minimo TELEPORT_CATCHUP_COOLDOWN frames entre teleports (evita flicker).
// Apenas quando nao visivel na camera (off-screen) para nao parecer "magia".
// Apos teleport: JoinCarWithRoadSystem para alinhar com o road-graph local.
static constexpr float TELEPORT_CATCHUP_DIST    = 150.0f; // distancia de activacao (metros)
static constexpr float TELEPORT_CATCHUP_BEHIND  = 30.0f;  // metros atras do jogador apos warp
static constexpr int   TELEPORT_CATCHUP_COOLDOWN = 300;   // 5.0s @ 60fps entre teleports

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
// v5.4: Reduzido 5→3m. Log v5.1 mostrou MC31 (ESCORT_REAR) activo a 10-15m
// com STOP_FOR_CARS forcado — recruta parava atras do trafego.
// Com 3m, MC67 fica activa ate ~3m. MC31 so activa na STOP_ZONE (<6m)
// para posicionamento final muito proximo.
static constexpr unsigned char CLOSE_RANGE_STRAIGHT_LINE_DIST = 3u; // metros; < STOP_ZONE_M=6m

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

// v4.9: Offset de destino para CIVICO GOTOCOORDS (metros atras do jogador).
// Resolve problema v4.8: destino era a posicao exacta do jogador, o que fazia
// o recruta tentar chegar AO jogador → batia, ultrapassava, ficava ao lado.
// Com offset, destino e ATRAS do jogador → aproximacao natural sem colisao.
// v5.4: Offset aumentado 10→15m. Log v5.1 mostrou recruta a 10-13m batendo atras
// do jogador. Com offset maior, o ponto-alvo fica 15m atras do jogador,
// criando mais espaco de seguranca e permitindo desaceleracao natural.
// v5.10: Aumentado de 15m para 20m para manter recruta mais distante e reduzir confusao.
// MC67 (ESCORT_REAR_FARAWAY) segue pelo road-graph ate este ponto.
static constexpr float CIVICO_FOLLOW_OFFSET = 20.0f;

// Threshold de distancia para re-calculo de destino no CIVICO GOTOCOORDS.
// Quando a diferenca entre destino actual e novo destino > este valor,
// o destino e actualizado.
// v5.13: Aumentado 3m→8m. Analise PASSENGER vs CIVICO mostrou que PASSENGER
// actualiza destino ~nunca (waypoint fixo) → autopilot tem controlo total →
// navegacao suave. CIVICO com stale=3m actualizava demasiado frequentemente,
// E resetava m_nTempAction=0 a cada update (v5.12), interrompendo manobras
// de desvio do autopilot (SWERVE, etc.) → jitter. Com 8m: a 70kmh (~19.4m/s)
// update a cada ~0.4s vs 0.15s anterior. Timer minimo de 30 frames (~0.5s)
// adicionado em ProcessDrivingAI para garantir estabilidade adicional.
static constexpr float CIVICO_DEST_STALE_DIST = 8.0f;

// v5.13: Timer minimo entre actualizacoes de destino CIVICO (frames @ 60fps).
// Mesmo quando o destino se moveu > CIVICO_DEST_STALE_DIST, esperar pelo menos
// este numero de frames antes de actualizar. Permite ao autopilot completar
// manobras de desvio (SWERVE, etc.) sem interrupcao. 30 frames = 0.5s.
static constexpr int CIVICO_DEST_UPDATE_MIN_FRAMES = 30;

// v5.8/v5.10/v5.11/v5.12/v5.14: Limiar de alinhamento para prevenir lateral approach em close-range.
// Distancia abaixo da qual activar check de alinhamento (recruta perto do jogador).
// Alignment dot product: pFwd · (playerPos - recruitPos)  [vector recruta→jogador]
//   dot = +1.0: recruta directamente atras do jogador (alinhado, sem slowdown)
//   dot =  0.0: recruta perpendicular (ao lado esquerdo/direito → slowdown)
//   dot = -1.0: recruta a frente do jogador (→ slowdown)
// v5.14 FIX: v5.13 usava vPos-playerPos (player→recruit), invertendo o dot —
//   recruta ATRAS dava dot≈-1.0, activando slowdown SEMPRE dentro de 30m.
//   Corrigido para playerPos-vPos (recruta→jogador). Agora slowdown so activa
//   quando recruta esta AO LADO ou A FRENTE do jogador (dot < 0.75).
// v5.12: Abordagem REDUZIR VELOCIDADE (SPEED_SLOW) quando desalinhado.
//        Resolve o "embicar para o lado" sem causar comportamento erratico.
static constexpr float CIVICO_CLOSE_ALIGN_DIST = 30.0f;     // era 25m
static constexpr float CIVICO_ALIGN_DOT_THRESHOLD = 0.75f;  // cos(41°) ≈ 0.755 (era 0.7)

// ───────────────────────────────────────────────────────────────────
// v5.3: CIVICO hibrido — ESCORT_REAR_FARAWAY primario + GOTOCOORDS catch-up
// ───────────────────────────────────────────────────────────────────

// Distancia (metros) abaixo da qual CIVICO usa ESCORT_REAR_FARAWAY nativo
// em vez de GOTOCOORDS. MC67 (ESCORT_REAR_FARAWAY) usa road-graph para
// navegar ATE a posicao atras do jogador — resolve posicionamento lateral,
// colisao traseira, e mantem o recruta na faixa correcta.
// v5.3: Usa MC67 em vez de MC31 (ESCORT_REAR). Log v5.1 mostrou MC31
// forcar driveStyle=STOP_FOR_CARS (override engine), causando o recruta
// parar atras do trafego e perder o jogador. MC67 preserva AVOID_CARS
// e usa road-graph nativamente — melhor fluxo em trafego.
// m_nStraightLineDistance=5 previne transicao prematura MC67→MC31.
static constexpr float CIVICO_ESCORT_SWITCH_DIST = 50.0f;

// Hysteresis de curve brake para PASSENGER e WAYPOINT_SOLO.
// Usa road-link heading (curva real da estrada) como referencia.
static constexpr float CURVE_BRAKE_ACT_RAD   = 0.35f;  // limiar de activacao (~20°)
static constexpr float CURVE_BRAKE_DEACT_RAD = 0.20f;  // limiar de desactivacao (~11°)

// v5.12: Curve brake CIVICO — usa heading ao DESTINO (nao road-link).
// Road-link heading nao reflete a direccao real em GOTOCOORDS (recruta no
// road-graph mas a rota pode divergir do link actual). Log v5.11 mostrou
// curveBrake=1 em ~95% das entries com deltaH 1.0-3.0 rad usando road-link,
// capping velocidade a 25kmh constantemente. Com destination-vector, deltaH
// reflecte a curva REAL do percurso ao destino — activacao correcta.
// Thresholds mais altos que PASSENGER porque destino CIVICO muda per-frame
// (ponto 20m atras do jogador), causando variacao natural de heading.
static constexpr float CIVICO_CURVE_BRAKE_ACT_RAD   = 0.60f;  // limiar activacao (~34°)
static constexpr float CIVICO_CURVE_BRAKE_DEACT_RAD = 0.35f;  // limiar desactivacao (~20°)

// Intervalo de reparacao visual do carro do recruta (frames @ 60fps).
// v5.3: Reduzido 120→60 para fechar portas abertas mais rapidamente apos colisoes.
// SEGURO — apenas escrita directa a membros (sem CloseAllDoors/FixDoor/FixPanel
// que causavam ESP crash). Limpa portas/paineis/luzes.
static constexpr int CAR_VISUAL_FIX_INTERVAL = 60;  // 1.0s @ 60fps (era 2.0s)
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
    CIVICO_F = 0,   // GOTOCOORDS(8) 15m atras do jogador, AVOID_CARS  (v5.6+)
    CIVICO_G = 1,   // GOTOCOORDS(8) 15m atras do jogador, AVOID_CARS  (v5.6+)
    CIVICO_H = 2,   // GOTOCOORDS(8) 15m atras do jogador, AVOID_CARS  (v5.6+, melhor combo)
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
        case DriveMode::CIVICO_F: return "CIVICO_F(GOTOCOORDS+AVOID)";
        case DriveMode::CIVICO_G: return "CIVICO_G(GOTOCOORDS+AVOID)";
        case DriveMode::CIVICO_H: return "CIVICO_H(GOTOCOORDS+AVOID)";
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

// Verdadeiro se o modo usa GOTOCOORDS atras do jogador (CIVICO_F/G/H).
inline bool IsCivicoMode(DriveMode m)
{
    return m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G ||
           m == DriveMode::CIVICO_H;
}

// Missao AutoPilot para um dado modo CIVICO.
// v5.6: todos os modos CIVICO usam MISSION_GOTOCOORDS com destino 15m atras do jogador.
// MC67/MC53/MC52 descontinuados — causavam posicao lateral e crash (null+offset).
inline eCarMission GetExpectedMission(DriveMode m)
{
    if (m == DriveMode::CIVICO_F || m == DriveMode::CIVICO_G || m == DriveMode::CIVICO_H)
        return MISSION_GOTOCOORDS;
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
