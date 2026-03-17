/*
 * grove_recruit_log.cpp
 *
 * Sistema de logging e helpers de introspeccao de tarefas.
 *
 * Grava grove_recruit.log na pasta do GTA SA (directorio de trabalho).
 * Modo "w": ficheiro truncado em cada sessao (log sempre fresco).
 * Buffer de linha (_IOLBF): flush automatico em cada '\n' — crash-safe.
 *
 * g_logFrame e incrementado externamente em ProcessFrame (Main.cpp)
 * e declarado como extern em grove_recruit_shared.h.
 */
#include "grove_recruit_log.h"

// Contador de frames partilhado com Main.cpp (extern)
// Definido em Main.cpp; aqui apenas acedido via extern.
extern int g_logFrame;

// Handle do ficheiro de log — interno a este modulo
static FILE* g_logFile = nullptr;

// Nomes dos 6 slots secundarios (usados por BuildSecondaryTaskBuf)
static const char* const s_secSlotNames[6] = { "ATK","DCK","SAY","FAC","PAR","IK" };

// ───────────────────────────────────────────────────────────────────
// LogInit — abre/recria o ficheiro e escreve o cabecalho de referencia
// ───────────────────────────────────────────────────────────────────
void LogInit()
{
    if (fopen_s(&g_logFile, "grove_recruit.log", "w") != 0)
    {
        g_logFile = nullptr;
        return;
    }
    setvbuf(g_logFile, NULL, _IOLBF, 1024);

    fprintf(g_logFile,
        "===== grove_recruit_standalone.asi v3.9 — log iniciado =====\n"
        "  Formato: [FFFFFFF][NIVEL] mensagem\n"
        "  Niveis: EVENT GROUP TASK DRIVE AI    KEY   WARN  ERROR OBSV  WORLD RECR  MULTI MENU\n"
        "    RECR:  multi-recruit/vanilla scan (ScanPlayerGroup, ApplyEnhancement, SIT_IN_CAR)\n"
        "    MULTI: multi-recruit per-recruta (conducao, snap, saude, passageiros)\n"
        "           [recr:N] = slot em g_allRecruits[]; campo 'modo' = g_driveMode global\n"
        "    MENU: menu in-game (abertura, navegacao, alteracoes de modo/aggro)\n"
        "\n"
        "  MULTI — DIAGNOSTICO DE CONDUCAO POR RECRUTA SECUNDARIO:\n"
        "  MULTI_STATUS: snap periodico — dump completo de cada recruta:\n"
        "    [recr:N] MULTI_STATUS ped=P car=C dist=D.Dm speed=S(Skmh)\n"
        "             mission=X(NOME) style=X(NOME) tempAction=X(NOME)\n"
        "             linkId=X(OK/INVALID) stuck=X inStopZone=0/1 inSlowZone=0/1\n"
        "  MULTI_RIDING: recruta viaja como passageiro (ridesWithPlayer=true):\n"
        "    [recr:N] MULTI_RIDING ped=P veh=V (passageiro no carro do jogador/recruta)\n"
        "  MULTI_RIDING_EXIT: recruta saiu do veiculo (ridesWithPlayer cleared):\n"
        "    [recr:N] MULTI_RIDING_EXIT ped=P — de volta a pe\n"
        "  MULTI_MISSION_CHANGE: missao mudou (recovery, snap, etc.):\n"
        "    [recr:N] MULTI_MISSION_CHANGE: X(NOME) -> Y(NOME)\n"
        "  MULTI_TEMPACTION_CHANGE: tempAction mudou (colisao, swerve, etc.):\n"
        "    [recr:N] MULTI_TEMPACTION_CHANGE: X(NOME) -> Y(NOME) dist=Dm physSpeed=Skmh\n"
        "  MULTI_STOP_ZONE: recruta entrou/saiu da STOP_ZONE (<6m):\n"
        "    [recr:N] MULTI_STOP_ZONE_ENTER/EXIT dist=Dm\n"
        "  MULTI_SLOW_ZONE: recruta entrou/saiu da SLOW_ZONE (<10m):\n"
        "    [recr:N] MULTI_SLOW_ZONE_ENTER/EXIT dist=Dm\n"
        "  MULTI_STUCK_RECOVER: recruta parado por >=1.25s -> JoinCarWithRoadSystem:\n"
        "    [recr:N] MULTI_STUCK_RECOVER physSpeed=Skmh dist=Dm mission=X tempAction=X\n"
        "\n"
        "  PASSAGEIRO NO CARRO DO JOGADOR/RECRUTA (ridesWithPlayer=true):\n"
        "  AUTO_ENTER_PASSENGER_SECONDARY: recruta secundario entra no carro do jogador.\n"
        "    Trigger: jogador entrou num veiculo (justEnteredVehicle) com lugar livre.\n"
        "    Emite CTaskComplexEnterCarAsPassenger. Seat atribuido sequencialmente.\n"
        "    Log: [recr:N] AUTO_ENTER_PASSENGER_SECONDARY ped=P -> veh=V seat=S dist=Dm\n"
        "  AUTO_EXIT_SECONDARY: recruta secundario sai quando jogador sai do veiculo.\n"
        "    Log: [recr:N] AUTO_EXIT_SECONDARY ped=P LeaveCar emitido\n"
        "  KEY3_PASSENGER_SECONDARY: recruta secundario entra no carro do recruta primario\n"
        "    quando jogador prime KEY3 (DRIVING->PASSENGER).\n"
        "    Log: [recr:N] KEY3_PASSENGER_SECONDARY ped=P -> g_car=V seat=S\n"
        "  KEY3_EXIT_SECONDARY: recruta secundario sai quando jogador prime KEY3 para sair.\n"
        "    Log: [recr:N] KEY3_EXIT_SECONDARY ped=P LeaveCar emitido\n"
        "\n"
        "  VANILLA COMPAT + MULTI-RECRUIT:\n"
        "  ScanPlayerGroup: a cada 3s, detecta membros do grupo recrutados pelo metodo vanilla\n"
        "    (tecla Y + respeito). Aplica bNeverLeavesGroup, bKeepTasksAfterCleanUp, e\n"
        "    m_acquaintance.m_nRespect bit 0. Promove 1.o vanilla a recruta primario se INACTIVE.\n"
        "  OnPlayerEnterVehicle: activa SIT_IN_LEADER_CAR(4) para todo o grupo quando jogador\n"
        "    entra num veiculo — todos os membros tentam entrar no carro do jogador (vanilla SA).\n"
        "  OnPlayerExitVehicle: restaura FOLLOW_LIMITED(1) — formacao normal a pe.\n"
        "  g_allRecruits[7]: tabela de todos os peds rastreados (spawned + vanilla).\n"
        "\n"
        "  DIAGNOSTICO ON-FOOT (campos-chave para depurar congelamento):\n"
        "  ON_FOOT_1: dist, rPos, initTimer, passiveTimer, rescanTimer\n"
        "  ON_FOOT_2: aggr, doFollow, pedType, respect, playerSpeed, tasks\n"
        "  tasks: P:[0]...[4] S:[ATK][DCK][SAY][FAC][PAR][IK]\n"
        "    P = Primary tasks (5 slots) — tarefa principal da IA\n"
        "      [0]PHYSICAL_RESPONSE  [1]EVENT_TEMP  [2]EVENT_NONTEMP  [3]PRIMARY  [4]DEFAULT\n"
        "    S = Secondary tasks (6 slots) — estado engine por defeito\n"
        "      [ATK]=combate/disparo  [DCK]=agachar  [SAY]=voiceline\n"
        "      [FAC]=anim facial      [PAR]=partial-anim  [IK]=inverse-kinematics\n"
        "  IDs importantes primarios:\n"
        "    -1=NO_TASK | 200=TASK_NONE | 203=STAND_STILL(congelado!)\n"
        "    243=BE_IN_GROUP(slot[3] deve ter SEMPRE este ID quando recruta esta no grupo!)\n"
        "    400=SIMPLE_ANIM(animacao de spawn) | 1219=COMPLEX_GANG_JOIN_RESPOND(wrapper spawn)\n"
        "    600=INVESTIGATE_DEAD_PED | 700=ENTER_CAR_PASSENGER | 701=ENTER_CAR_DRIVER\n"
        "    709=CAR_DRIVE(a conduzir OK) | 913=FOLLOW_LEADER_FORMATION(follow OK)\n"
        "    1207=COMPLEX_GANG_FOLLOWER(follow OK) | 1500=GROUP_FOLLOW_LEADER_ANY_MEANS(OK)\n"
        "  IDs de follow validos (POST_FOLLOW_CHECK aceita como OK):\n"
        "    1207, 1500, 913, 243 (em qualquer dos dois: activeTask ou primaryTask)\n"
        "  IDs transitórios de spawn (POST_FOLLOW_CHECK aguarda sem contar falha):\n"
        "    400=SIMPLE_ANIM, 900=SIMPLE_GO_TO_POINT, 902=SIMPLE_ACHIEVE_HEADING, 1219\n"
        "  IDs corrigidos vs observacoes anteriores (nomes hipoteticos ERRADOS):\n"
        "    207=SIMPLE_FALL (NAO walk-to-point; NAO e follow OK!)\n"
        "    208=COMPLEX_FALL_AND_GET_UP (NAO follow-leader-formation; NAO e follow OK!)\n"
        "    900=SIMPLE_GO_TO_POINT (NAO react-to-cmd)\n"
        "    902=SIMPLE_ACHIEVE_HEADING (NAO flee)\n"
        "    1219=COMPLEX_GANG_JOIN_RESPOND (NAO gang-spawn-complex)\n"
        "  IDs observados em saida de carro (sequencia normal):\n"
        "    719=LEAVE_CAR_ANIM(?)    — inicio de anim de saida\n"
        "    801=EXIT_CAR_STAND_2(?)  — variante de saida de carro\n"
        "    802=EXIT_CAR_STUMBLE(?)  — stumble apos saida\n"
        "    806=EXIT_CAR_STAND(?)    — pose de pe apos saida\n"
        "    807=ANIM_STAND_UP(?)     — animacao de levantar\n"
        "    809=CAR_DRIVE_AS_FOLLOW(?)— conducao em modo follow\n"
        "    811=CAR_SLOW_DOWN(?)     — abrandar o carro\n"
        "    812=CAR_ENTER_DONE(?)    — entrada no carro concluida\n"
        "    813=EXIT_CAR_IDLE(?)     — idle imediato apos saida\n"
        "    824=CAR_EXIT_ANIM(?)     — animacao pre-saida de carro\n"
        "  IDs observados em queda (slot[1]=EVENT_TEMP + slot IK=IN_AIR(265)):\n"
        "    501=SIMPLE_EVASIVE_STEP  502=COMPLEX_EVASIVE_STEP  265=SIMPLE_IN_AIR\n"
        "  IDs importantes secundarios:\n"
        "    [ATK]: 30=SHOOT_PED | 31=SHOOT_CAR | 130/131=2ND_SHOOT | 134=SHOT_REACT\n"
        "    [DCK]: 158=DUCK | 159=CROUCH\n"
        "    [SAY]: 164=SAY (voiceline activa — pode ser GANG_RECRUIT_REFUSE!)\n"
        "    [FAC]: 169=FACIAL_COMPLEX | 305=COMPLEX_FACIAL_TALK(normal no spawn)\n"
        "    [PAR]: 174=PARTIAL_ANIM\n"
        "    [IK]:  180=INVERSE_KINEMATICS | 265=IN_AIR(queda)\n"
        "  pedType:  8=GANG2=GSF(OK)  7=GANG1=Ballas(ERRADO->congelado e inimigo)\n"
        "  respeito: CPedIntelligence::Respects(0x601C90) verifica\n"
        "            m_acquaintance.m_nRespect bit PED_TYPE_PLAYER1(bit 0).\n"
        "            AddRecruitToGroup define este bit (ver ACQUAINTANCE_FIX no log).\n"
        "            STAT_RESPECT nao tem efeito no check de recrutamento.\n"
        "            CLEO nao precisava disto porque 0850(AS_actor follow_actor) bypassa\n"
        "            o sistema de grupo completamente (atribuicao directa de tarefa).\n"
        "\n"
        "  FOLLOW CORRIGIDO (on-foot) — v4:\n"
        "  ACQUAINTANCE_FIX: AddRecruitToGroup define m_acquaintance.m_nRespect bit 0\n"
        "    (PED_TYPE_PLAYER1) para que Respects() retorne true.\n"
        "  EnsureBeInGroup: apos MakeThisPedJoinOurGroup, verifica e atribui manualmente\n"
        "    TASK_COMPLEX_BE_IN_GROUP(243) a TASK_PRIMARY_PRIMARY(slot[3]) se ausente.\n"
        "    Sem BE_IN_GROUP, GetTaskMain(recruit) nunca e chamado e eventos GATHER\n"
        "    (SeekEntity de TellGroupToStartFollowingPlayer) sao ignorados.\n"
        "  GANG_SPAWN_ANIM_END: quando GetSimplestActiveTask transita de 400(ANIM), o mod\n"
        "    limpa slots[1-2], verifica BE_IN_GROUP e re-emite TellGroupFollowWithRespect.\n"
        "  POST_FOLLOW_CHECK: 3 frames apos TellGroupFollowWithRespect, verifica DUAS tasks:\n"
        "    activeTask  = GetSimplestActiveTask() — tarefa folha\n"
        "    primaryTask = GetActiveTask()         — tarefa primaria (slot[3]=PRIMARY)\n"
        "    VALIDOS (qualquer um): 1207/1500/913/243\n"
        "    Se invalido: EnsureBeInGroup + TellGroupFollowWithRespect (FALLBACK).\n"
        "    Limite MAX_FOLLOW_FALLBACK_RETRIES=%d tentativas por ciclo — evita loop.\n"
        "    Estados transitórios (902=ACHIEVE_HEADING, 400=ANIM, 900=GO_TO_POINT, 1219): aguarda.\n"
        "\n"
        "  DIAGNOSTICO CONDUCAO:\n"
        "  DRIVING_1: dist, speed_ap(autopilot), physSpeed(km/h real), mission(nome),\n"
        "             driveStyle(nome), tempAction(nome), offroad, stuck(timer/max), modo,\n"
        "             heading, targetH, roadH, align, deltaH, speedMult\n"
        "  DRIVING_2: straight, lane, linkId(OK/INVALID), areaId, dest(xyz),\n"
        "             targetCar, snapTimer, catchup(0/1), invalidBurst, tasks(P:5primary + S:6secondary)\n"
        "  physSpeed: velocidade fisica real (m_vecMoveSpeed x 180 ≈ km/h)\n"
        "  speed_ap:  velocidade de cruzeiro do AutoPilot (unidades SA)\n"
        "  mission:   valor de m_nCarMission (eCarMission):\n"
        "               8=GOTOCOORDS(DIRETO) | 11=STOP_FOREVER(PARADO/STOP_ZONE)\n"
        "               12=GOTOCOORDS_ACCURATE\n"
        "               31=ESCORT_REAR | 52=FOLLOWCAR_FARAWAY(CIVICO_H base)\n"
        "               53=FOLLOWCAR_CLOSE(CIVICO_G base) | 67=ESCORT_REAR_FARAWAY(CIVICO_F base)\n"
        "  CIVICO_F: MC67+AVOID  CIVICO_G: MC53+AVOID  CIVICO_H: MC52+AVOID\n"
        "  driveStyle: valor de m_nDrivingStyle (eCarDrivingStyle — APENAS 5 valores, 0-4):\n"
        "    0=STOP_FOR_CARS       — para atras dos carros E obedece semaforos (NAO queremos!)\n"
        "    1=SLOW_DOWN_FOR_CARS  — abranda atras dos carros, ignora semaforos\n"
        "    2=AVOID_CARS          — tenta desviar dos carros (todos os modos CIVICO)\n"
        "    3=PLOUGH_THROUGH      — ignora tudo, colide com carros e semaforos\n"
        "    4=STOP_IGNORE_LIGHTS  — para atras dos carros, ignora semaforos (ex-CIVICO_D/E)\n"
        "    NOTA: OBEY_LIGHTS = STOP_FOR_CARS(0) — faria o recruta parar no vermelho.\n"
        "          NAO foi adicionado, NAO queremos isso.\n"
        "    NOTA2: NENHUM destes 5 valores afecta props/postes estaticos (postes,\n"
        "           paredes, gradeamentos). O motor ja abranda para props com\n"
        "           CCarCtrl::SlowCarDownForObject mas o autopilot nao swerve.\n"
        "           Para props/postes: usamos HEADON_PERSISTENT + STUCK_RECOVER\n"
        "           (JoinCarWithRoadSystem forcado). Ver explicacao completa abaixo.\n"
        "  tempAction: m_nTempAction — accao temporaria de avoidance/steering:\n"
        "              0=NONE | 1=WAIT | 3=REVERSE | 4=HANDBRAKE_LEFT | 5=HANDBRAKE_RIGHT\n"
        "              6=HANDBRAKE_STRAIGHT | 7=TURN_LEFT | 8=TURN_RIGHT | 9=GOFORWARD\n"
        "              10=SWERVE_LEFT | 11=SWERVE_RIGHT | 12=STUCK_TRAFFIC\n"
        "              13=REVERSE_LEFT | 14=REVERSE_RIGHT | 19=HEADON_COLLISION | 24=BRAKE\n"
        "  stuck:     contador de frames com physSpeed < STUCK_SPEED_KMH (%.1f km/h) / max %d\n"
        "             ao atingir max: STUCK_RECOVER (JoinCarWithRoadSystem)\n"
        "  catchup:   1 = FAR_CATCHUP activo (dist>%.0fm, usa SPEED_CATCHUP=%d)\n"
        "  dest:      coordenadas de destino actuais no AutoPilot\n"
        "  targetCar: apontador ao carro do jogador (CIVICO); nullptr em DIRETO\n"
        "  heading:   orientacao do veiculo (GetHeading(), rad, 0=Norte)\n"
        "  targetH:   heading final usado pela IA para curva/alinhamento\n"
        "  roadH:     heading inferido pelos 2 road nodes mais proximos (fallback diagnostico)\n"
        "  align:     origem do targetH:\n"
        "               CLIPPED_LINK        = heading do link/path actual do AutoPilot\n"
        "               ROAD_NODE_INVALID   = link invalido -> usar roadH dos nodes\n"
        "               ROAD_NODE_MISMATCH  = link valido mas roadH parecia mais coerente\n"
        "               CURRENT_HEADING     = sem correcao util; manteve orientacao actual\n"
        "  deltaH:    diff heading-targetH — >1.5rad=WRONG_DIR (sentido contrario!)\n"
        "             NOTA: 'linkId_invalido' se linkId>50000 (targetH seria lixo)\n"
        "  speedMult: factor de curva 0.0-1.0 (heading-diff baseado)\n"
        "  invalidBurst: numero de frames consecutivos recentes com link invalido\n"
        "  JOIN_ROAD: diff linkId/heading antes/apos JoinCarWithRoadSystem\n"
        "\n"
        "  FindNearestFreeCar: DOIS PASSES de seleccao:\n"
        "    1.o passe: prefere carros com linkId<=50000 (ja no road-graph CIVICO).\n"
        "    2.o passe: aceita qualquer carro se nenhum snapped encontrado.\n"
        "    Log: 'FindNearestFreeCar: veh=... linkId=X(OK/INVALID) (road-snapped...)'\n"
        "\n"
        "  CONDUCAO CIVICO:\n"
        "  Todos os modos CIVICO (F/G/H) usam AVOID_CARS(2): o motor tenta desviar\n"
        "    do trafego em vez de parar atras dele. O road-following e controlado\n"
        "    pela missao (MC52/53/67), NAO pelo driveStyle.\n"
        "\n"
        "  PROPS / POSTES / MUROS — O QUE O jogo FAZ E O QUE O MOD FAZ:\n"
        "  ---------------------------------------------------------------\n"
        "  Q: 'obey lights' ajuda a nao bater em postes?\n"
        "  R: NAO. OBEY_LIGHTS = STOP_FOR_CARS(0) faz parar no semaforo vermelho.\n"
        "     Nao tem QUALQUER relacao com props estaticos (postes, muros, arbustos).\n"
        "  Q: algum dos 5 drive styles ajuda com props?\n"
        "  R: NAO. Os 5 valores afectam APENAS outros VEICULOS (carros, motos).\n"
        "     Para props o motor usa CCarCtrl::SlowCarDownForObject (0x426220):\n"
        "     -> abranda automaticamente quando ha um objecto estatico a frente.\n"
        "     -> mas o autopilot NAO faz swerve para contornar postes (so faz para carros).\n"
        "  FERRAMENTAS DO MOD para props/postes/muros:\n"
        "     1) HEADON_PERSISTENT (30 frames): HEADON_COLLISION(19) continuado por\n"
        "        0.5s = recruta encravado contra prop/traseira. JoinCarWithRoadSystem forcado.\n"
        "        -> 'escapa' do prop voltando ao road-graph.\n"
        "     2) STUCK_RECOVER (75 frames): physSpeed < 3 km/h por 1.25s = totalmente\n"
        "        parado. JoinCarWithRoadSystem forcado. (mais lento mas cobre mais casos)\n"
        "     3) SWERVE speed penalty (75%%): quando tempAction=SWERVE, reduz velocidade,\n"
        "        dando ao motor mais tempo para calcular o swerve.\n"
        "  CONCLUSAO: nao ha nada mais a fazer com driveStyle para props.\n"
        "     Se HEADON_PERSISTENT/STUCK_RECOVER aparecerem frequentemente no log,\n"
        "     o recruta esta a bater com frequencia — considera reduzir SPEED_CIVICO.\n"
        "\n"
        "  PERIODIC_ROAD_SNAP: JoinCarWithRoadSystem re-chamado a cada 1.0s em CIVICO\n"
        "    para manter alinhamento com nos de estrada. Agora inclui physSpeed,\n"
        "    heading/targetH/roadH e origem de alinhamento (align).\n"
        "    g_civicRoadSnapTimer negativo = pausa de snap (INVALID_LINK_STORM).\n"
        "  PERIODIC_ROAD_SNAP_SKIP: explica porque o snap foi limpo/pulado\n"
        "    (mode_change/offroad/wrong_dir) antes de chegar ao intervalo.\n"
        "  PERIODIC_ROAD_SNAP_PAUSE_END: pausa de INVALID_LINK_STORM terminou.\n"
        "  INVALID_LINK_STORM: se JoinCarWithRoadSystem retornar link invalido >5\n"
        "    vezes consecutivas, snap e pausado por 120 frames para evitar loop.\n"
        "  INVALID_LINK_BURST_END: fim de uma rajada de frames seguidos com link invalido.\n"
        "  WRONG_DIR_RECOVER: apenas dispara quando dist > 30m (v2 fix).\n"
        "    NOVO: apenas a dist > 30m; range proximo usa snap periodico + soft-recovery.\n"
        "\n"
        "  SISTEMA DE OBSERVACAO VANILLA [OBSV]:\n"
        "  ProcessObserver (a cada 2s) loga estado do motor do jogo SEM o mod:\n"
        "    NearestTrafficCar: CAutoPilot completo do NPC de trafego mais proximo\n"
        "      mission(nome) driveStyle(nome) tempAction(nome) mostrados.\n"
        "      linkId(OK/INVALID) mostrado explicitamente; WRONG_DIR omitido se INVALID.\n"
        "    NearestGSFPed:     tasks do ped GSF (pedType=8) mais proximo a pe\n"
        "      activeTask=GetSimplestActiveTask() + primaryTask=GetActiveTask() mostrados.\n"
        "    PlayerGroup:       estado do grupo do jogador (todos os slots 0-6 + tasks)\n"
        "      activeTask + primaryTask por membro.\n"
        "    PlayerState:       estado do jogador (a pe ou em carro + CAutoPilot)\n"
        "    RecruitCar/RecruitPed: estado actual do recruta para comparacao directa\n"
        "  Usar NearestTrafficCar como referencia vanilla para comparar linkId/mission\n"
        "  do recruta — NPC vanilla tem sempre linkId valido e mission=1(CRUISE).\n"
        "\n"
        "  [WORLD] LOG (a cada 5s / 300 frames):\n"
        "  CTimer::m_snTimeInMilliseconds — tempo de jogo em ms (nome correcto no plugin-sdk SA)\n"
        "  CTimer::ms_fTimeStep           — passo de frame (aprox 0.02s @50fps)\n"
        "\n"
        "  OUTROS EVENTOS DE DIAGNOSTICO:\n"
        "  PRE_JOIN: dump antes de MakeThisPedJoinOurGroup\n"
        "  TASK_CHANGE: mudanca real-time da tarefa activa (usa GetTaskName()).\n"
        "  GANG_SPAWN_ANIM_END: TASK_SIMPLE_ANIM(400) terminou — limpa slots e re-emite follow.\n"
        "  POST_FOLLOW_CHECK: tarefa 3 frames apos TellGroupFollowWithRespect.\n"
        "  FOLLOW_FALLBACK: EnsureBeInGroup+TellGroupFollow se follow nao confirmado.\n"
        "  WRONG_DIR_START/END: transicoes com physSpeed, roadH e align para ver se\n"
        "    o erro veio do link clipado ou do fallback por road nodes.\n"
        "  WRONG_DIR_RECOVER: SetupDriveMode apenas quando dist > 30m (v2 fix).\n"
        "  INVALID_LINK: linkId>50000 — re-snap imediato (sem beelining).\n"
        "  INVALID_LINK_STORM: >5 snaps invalidos consecutivos — pausa 120 frames.\n"
        "  MISSION_RECOVERY: STOP_FOREVER detectado fora das zonas — restaurar.\n"
        "    NOTA: estados 31/52/53/67 (road-SM internos) NAO sao recuperados.\n"
        "    NOVO: quando CLOSE_BLOCKED esta activo, STOP_FOREVER nao e restaurado\n"
        "      imediatamente — o recruta continua a esperar o jogador/transito limpar.\n"
        "  SLOW_ZONE/SLOW_ZONE saiu: entrada/saida da zona de abrandamento.\n"
        "  TEMPACTION_CHANGE: mudanca de m_nTempAction (avoidance/colisao). Util para\n"
        "    diagnosticar quando recruta colide com obstaculos (HEADON/STUCK/SWERVE).\n"
        "    HEADON_COLLISION(19): speed reduzido a 50%% para manobragem.\n"
        "      + Se HEADON persistir >= 30 frames (0.5s): HEADON_PERSISTENT -> JoinRoadSystem.\n"
        "    STUCK_TRAFFIC(12):    speed reduzido a 40%% para destravar devagar.\n"
        "    SWERVE_LEFT(10)/SWERVE_RIGHT(11): speed reduzido a 75%% (simula AVOID+SLOW).\n"
        "  HEADON_PERSISTENT: HEADON_COLLISION mantido por >= 30 frames consecutivos.\n"
        "    Indica encravamento contra prop/poste/muro. JoinCarWithRoadSystem forcado.\n"
        "    (Distinto de STUCK_RECOVER: activa mais rapido, mas apenas em HEADON.)\n"
        "  CLOSE_RANGE_ENTER/EXIT: recruta entrou/saiu de dist < %.0fm (close range).\n"
        "    Nesta zona: style de seguranca (STOP_IGNORE_LIGHTS) + force MC52 road-graph.\n"
        "  CLOSE_RANGE_FORCE_MC52: SA engine transitou para missao != MC52 em close range.\n"
        "    O mod forcou MC52 de volta. Agora inclui heading/targetH/roadH/align.\n"
        "    Esperado uma vez por entrada em close range.\n"
        "    Se aparecer repetidamente: SA engine mantem a transicao apesar do fix.\n"
        "  PASSENGER_NAV: recruta em modo PASSAGEIRO navegando ao waypoint (GOTOCOORDS).\n"
        "    Aguarda waypoint: sem ponto marcado → STOP_FOREVER (PASSENGER_WAIT).\n"
        "    Chegou: distToWaypoint < 12m → STOP_FOREVER (PASSENGER_ARRIVED).\n"
        "    PASSENGER_DRIVING: dump periodico de estado em modo passageiro (a cada 1s).\n"
        "      curveBrake=1 = PASSENGER_CURVE_BRAKE activo (cruise cap a SPEED_PASSENGER_TURN=20).\n"
        "    PASSENGER_CURVE_BRAKE_START/END: curve brake activou/desactivou (linkId=OK/INVALID).\n"
        "    PASSENGER_WAIT: sem waypoint → recruta parado aguardando ponto do mapa.\n"
        "    PASSENGER_ARRIVED: chegou ao waypoint → STOP_FOREVER aguardando novo ponto.\n"
        "    PASSENGER_STUCK_RECOVER: recruta parado em modo passageiro -> JoinRoadSystem.\n"
        "  CURVE_BRAKE_GOTO_ON/OFF: CIVICO curve brake muda para GOTOCOORDS/restaura MC67\n"
        "    para que MC67 (ESCORT_REAR_FARAWAY) respeite o cruise cap em curvas (40-62kmh).\n"
        "  DIST_TREND: tendencia de distancia a cada 1s (APROXIMAR / AFASTAR / ESTAVEL).\n"
        "    delta > +1.5m = AFASTAR; delta < -1.5m = APROXIMAR; resto = ESTAVEL.\n"
        "  FAR_CATCHUP_ON/OFF: speed boost activo (dist > %.0fm -> SPEED_CATCHUP=%d).\n"
        "    Apenas em estrada (nao offroad) e sentido correcto (nao WRONG_DIR).\n"
        "  STUCK_RECOVER: recruta encravado (physSpeed<%.1fkmh por %ds) — JoinCarWithRoadSystem.\n"
        "    Cooldown de %.1fs entre recuperacoes para evitar loop.\n"
        "    NOVO: se o caso se parecer com CLOSE_BLOCKED (jogador e recruta parados perto),\n"
        "      o STUCK_RECOVER e adiado para nao quebrar a espera normal no transito.\n"
        "\n"
        "========================================================\n"
        "  GUIA IN-GAME: O QUE ESPERAR DE CADA MODO E O QUE VERIFICAR\n"
        "========================================================\n"
        "\n"
        "  CIVICO_F (MC67+AVOID) — EscortRearFaraway + AvoidCars\n"
        "  --------------------------------------------------------\n"
        "  COMPORTAMENTO ESPERADO:\n"
        "    - Longe (dist > 22m): segue pelo road-graph ficando ATRAS do jogador.\n"
        "      Entre 22m e 35m entra em faixa de aproximacao conservadora (sem boost de reta)\n"
        "      para reduzir batidas traseiras e excessos em intersecoes.\n"
        "      O motor usa MC_ESCORT_REAR_FARAWAY(67) que navega por nos de estrada.\n"
        "      Deve fazer curvas e seguir a rua correctamente.\n"
        "    - Perto (dist < 22m): CLOSE_RANGE_SMOOTH substitui MC31->MC52 automaticamente\n"
        "      para evitar 'chase geometrico' (posicionamento exacto-atras que sobe passeio).\n"
        "    - Trafego: tenta desviar com AVOID_CARS. Pode fazer swerve esquerda/direita.\n"
        "    - Dist > 45m: FAR_CATCHUP activa SPEED_CATCHUP=62 em retas para aproximar.\n"
        "  O QUE VERIFICAR:\n"
        "    + Recruta segue a rua sem sair para passeio ou campo?\n"
        "    + Quando proximo, faz curvas suaves ou corta caminho pelo passeio?\n"
        "    + CLOSE_RANGE_ENTER aparece no log e o comportamento muda?\n"
        "    + DIST_TREND mostra APROXIMAR ou ESTAVEL na maior parte do tempo?\n"
        "    + FAR_CATCHUP_ON aparece quando distante e recruta recupera distancia?\n"
        "    + TEMPACTION_CHANGE com HEADON/STUCK frequente = colidindo com obstaculos.\n"
        "    + STUCK_RECOVER frequente = recruta encravando muito.\n"
        "\n"
        "  CIVICO_G (MC53+AVOID) — FollowCarClose + AvoidCars\n"
        "  --------------------------------------------------------\n"
        "  COMPORTAMENTO ESPERADO:\n"
        "    - Segue mais de PERTO o trajecto do jogador (MC53 e design para seguimento proximo).\n"
        "    - Tende a fazer as mesmas curvas que o jogador fez momentos antes.\n"
        "    - AVOID_CARS tenta desviar do trafego lateral.\n"
        "    - Curvas proximas usam AdaptiveSpeed + style de seguranca (STOP_IGNORE_LIGHTS).\n"
        "    - A dist > 45m: FAR_CATCHUP activo para recuperar distancia em retas.\n"
        "  ATENCAO / LIMITACOES CONHECIDAS:\n"
        "    - MC53 pode ser mais agressivo em curvas — maior risco de subir passeio.\n"
        "    - Em interseccoes complicadas pode tomar atalho diagonal (beeline curto).\n"
        "  O QUE VERIFICAR:\n"
        "    + Recruta segue o trajecto exacto do jogador ou corta caminho?\n"
        "    + Subida de passeio mais frequente que CIVICO_H?\n"
        "    + TEMPACTION_CHANGE SWERVE/HEADON mais frequente que outros modos?\n"
        "    + STUCK_RECOVER frequente = MC53 a colidir com bordas de passeio/props.\n"
        "    + DIST_TREND: recruta mantem distancia razoavel ou fica sempre muito proximo?\n"
        "\n"
        "  CIVICO_H (MC52+AVOID) — FollowCarFaraway + AvoidCars  [MELHOR MODO]\n"
        "  --------------------------------------------------------\n"
        "  COMPORTAMENTO ESPERADO:\n"
        "    - Segue pelo road-graph mantendo distancia de seguimento (MC52 e o mais fluido).\n"
        "    - Faz as curvas CORRETAS seguindo os nos de estrada — o 'modo perfeito'.\n"
        "    - AVOID_CARS desvia do trafego sem parar completamente.\n"
        "    - CLOSE_BLOCKED_WAIT: se ambos parados > 1.5s na zone proxima, espera\n"
        "      em STOP_FOREVER em vez de subir passeio. Retoma quando jogador anda.\n"
        "    - Dist > 45m: FAR_CATCHUP para recuperar quando ficou para tras.\n"
        "  ATENCAO / LIMITACOES CONHECIDAS:\n"
        "    - Quando muito perto, pode entrar em chase-close — CLOSE_BLOCKED mitiga.\n"
        "    - Em zonas sem road-graph (canal, terreno): OFFROAD_DIRECT activa GOTOCOORDS.\n"
        "      Nesse fallback, o heading alvo passa a vir do vector ate ao destino,\n"
        "      e ao regressar a estrada o modo CIVICO e restaurado automaticamente.\n"
        "  O QUE VERIFICAR:\n"
        "    + Recruta faz TODAS as curvas seguindo a rua correctamente? (objectivo!)\n"
        "    + CLOSE_BLOCKED_START aparece no log durante trafego parado? Boa sinal.\n"
        "    + CLOSE_BLOCKED_END aparece quando jogador retoma? Comportamento correcto.\n"
        "    + Quando aparece FAR_CATCHUP_ON, recruta efectivamente se aproxima?\n"
        "      Ver DIST_TREND logo apos: deve mudar de AFASTAR para ESTAVEL/APROXIMAR.\n"
        "    + TEMPACTION_CHANGE SWERVE frequente = desvio activo de trafego (normal).\n"
        "    + HEADON_COLLISION ou STUCK_TRAFFIC frequente = ajustar HEADON_SPEED_FACTOR\n"
        "      ou STUCK_TRAFFIC_SPEED_FACTOR nas constantes de config.\n"
        "\n"
        "  MUDANCAS DESTA VERSAO — O QUE OBSERVAR E AVALIAR\n"
        "  --------------------------------------------------------\n"
        "  1. MODOS REMOVIDOS (D=MC67+STOP, E=MC52+STOP, I=MC67+SLOW):\n"
        "     Estes modos paravam para o trafego — o recruta ficava preso em trafego\n"
        "     enquanto o jogador continuava. Com AVOID_CARS os 3 modos restantes\n"
        "     tentam desviar. Se sentires falta de um comportamento especifico,\n"
        "     descreve o cenario e podemos adicionar um novo modo especializado.\n"
        "\n"
        "  2. FAR_CATCHUP (novo): quando dist > 45m em retas/estrada correcta,\n"
        "     usa SPEED_CATCHUP=62 (era max 55). Ver no log: FAR_CATCHUP_ON/OFF.\n"
        "     VERIFICAR: recruta efectivamente aproxima depois de FAR_CATCHUP_ON?\n"
        "     Se nao (DIST_TREND ainda AFASTAR), aumentar FAR_CATCHUP_DIST_M ou SPEED_CATCHUP.\n"
        "\n"
        "  3. STUCK_RECOVER (novo): quando physSpeed<3.0km/h por 1.25s, forca re-snap.\n"
        "     VERIFICAR: STUCK_RECOVER aparece no log? Quantas vezes por sessao?\n"
        "     Muitos = recruta encravando muito (paredes/props). Pocos = OK.\n"
        "     Apos STUCK_RECOVER, recruta retoma movimento normal?\n"
        "\n"
        "  4. HEADON_PERSISTENT (novo): prop/poste/muro/traseira recovery mais rapido.\n"
        "     Quando HEADON_COLLISION(19) dura >= 30 frames (0.5s) sem sair:\n"
        "     JoinCarWithRoadSystem forcado para escapar da colisao persistente.\n"
        "     VERIFICAR: HEADON_PERSISTENT aparece e recruta retoma depois?\n"
        "     NOTA sobre driveStyle e props:\n"
        "       OBEY_LIGHTS(STOP_FOR_CARS=0) NAO ajuda com props — so para no vermelho.\n"
        "       Nenhum dos 5 drive styles afecta objectos estaticos. Ver PROPS/POSTES\n"
        "       na seccao CONDUCAO CIVICO acima para a explicacao completa.\n"
        "\n"
        "  5. AVOID_CARS + SLOW_DOWN — INVESTIGACAO (nao e possivel combinar):\n"
        "     eCarDrivingStyle e um enum de 5 valores (0-4) no plugin-sdk SA.\n"
        "     AVOID_CARS(2) | SLOW_DOWN(1) = 3 = PLOUGH_THROUGH — COLISAO DE VALORES!\n"
        "     Nao existe modo 'AVOID+SLOW' nativo. Solucao implementada:\n"
        "     a) AVOID_CARS base (faz swerve de veiculos)\n"
        "     b) Nos reduzimos speed quando tempAction activo:\n"
        "        HEADON(19)=50%% STUCK_TRAFFIC(12)=40%% SWERVE(10/11)=75%%\n"
        "     Resultado: o recruta desvia de carros (AVOID) E abranda durante as\n"
        "     manobras (nosso SLOW simulado). VERIFICAR: com SWERVE frequente, o\n"
        "     recruta colide menos do que antes da penalizacao de 75%%?\n"
        "\n"
        "  6. TEMPACTION_CHANGE (novo log): cada mudanca de tempAction e registada.\n"
        "     VERIFICAR: HEADON_COLLISION(19) ou STUCK_TRAFFIC(12) frequentes?\n"
        "     Se sim, recruta esta a colidir muito. Estes eventos reduzem a velocidade\n"
        "     automaticamente (50%%, 40%%, 75%%) para suavizar o impacto.\n"
        "     SWERVE_LEFT/RIGHT frequente = recruta a desviar activamente do trafego (BOM).\n"
        "\n"
        "  7. CLOSE_RANGE_ENTER/EXIT (novo log): regista quando recruta entra/sai da\n"
        "     zona proxima (< 22m). VERIFICAR: comportamento muda visivelmente ao entrar?\n"
        "     Subida de passeio ocorre ANTES ou DEPOIS de CLOSE_RANGE_ENTER?\n"
        "     Isso ajuda a perceber se o problema e no modo longe ou proximo.\n"
        "\n"
        "  8. DIST_TREND (novo log): a cada 1s mostra se recruta aproxima/afasta.\n"
        "     IDEAL: maioritariamente ESTAVEL ou APROXIMAR. AFASTAR muito = recruta lento.\n"
        "     Usar para calibrar SPEED_CIVICO (46), SPEED_CATCHUP (62), FAR_CATCHUP_DIST_M (45m).\n"
        "\n"
        "  9. DUMPS AI a cada 1s (era 2s): mais granular para ver mudancas rapidas.\n"
        "     DRIVING_1 inclui stuck=X/75 — ver se o contador cresce antes de STUCK_RECOVER.\n"
        "\n"
        "========================================================\n\n",
        MAX_FOLLOW_FALLBACK_RETRIES,
        (double)STUCK_SPEED_KMH, STUCK_DETECT_FRAMES,
        (double)FAR_CATCHUP_DIST_M, (int)SPEED_CATCHUP,
        (double)CLOSE_RANGE_SWITCH_DIST,
        (double)FAR_CATCHUP_DIST_M, (int)SPEED_CATCHUP,
        (double)STUCK_SPEED_KMH, STUCK_DETECT_FRAMES / 60,
        STUCK_RECOVER_COOLDOWN / 60.0);
}

// ───────────────────────────────────────────────────────────────────
// Escrita interna
// ───────────────────────────────────────────────────────────────────
static void LogWrite(const char* level, const char* fmt, va_list ap)
{
    if (!g_logFile) return;
    fprintf(g_logFile, "[%07d][%s] ", g_logFrame, level);
    vfprintf(g_logFile, fmt, ap);
    fputc('\n', g_logFile);
}

// ───────────────────────────────────────────────────────────────────
// Funcoes publicas por nivel
// ───────────────────────────────────────────────────────────────────
void LogEvent(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("EVENT", fmt, a); va_end(a); }
void LogGroup(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("GROUP", fmt, a); va_end(a); }
void LogTask (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("TASK ", fmt, a); va_end(a); }
void LogDrive(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("DRIVE", fmt, a); va_end(a); }
void LogAI   (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("AI   ", fmt, a); va_end(a); }
void LogKey  (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("KEY  ", fmt, a); va_end(a); }
void LogWarn (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("WARN ", fmt, a); va_end(a); }
void LogError(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("ERROR", fmt, a); va_end(a); }
void LogObsv (const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("OBSV ", fmt, a); va_end(a); }
void LogWorld(const char* fmt, ...) { va_list a; va_start(a, fmt); LogWrite("WORLD", fmt, a); va_end(a); }
void LogRecruit(const char* fmt,...){ va_list a; va_start(a, fmt); LogWrite("RECR ", fmt, a); va_end(a); }
void LogMulti(const char* fmt, ...)  { va_list a; va_start(a, fmt); LogWrite("MULTI", fmt, a); va_end(a); }
void LogMenu(const char* fmt, ...)  { va_list a; va_start(a, fmt); LogWrite("MENU ", fmt, a); va_end(a); }

// ───────────────────────────────────────────────────────────────────
// GetTaskName — converte task ID em nome legivel
// ───────────────────────────────────────────────────────────────────
const char* GetTaskName(int tid)
{
    switch (tid) {
        case -1:   return "NO_TASK";
        case 0:    return "PHYSICAL_RESPONSE";
        case 1:    return "SIMPLE_NONE";
        case 2:    return "SIMPLE_STOP_RUNNING";
        case 4:    return "SIMPLE_DUCK";
        case 5:    return "SIMPLE_UNCUFF";
        case 10:   return "SIMPLE_JETPACK";
        case 15:   return "SIMPLE_DROWN";
        case 16:   return "SIMPLE_DIE_IN_WATER";
        case 17:   return "SIMPLE_DIED_IN_WATER";
        case 18:   return "SIMPLE_DEAD";
        case 19:   return "SIMPLE_DEAD_IN_CAR";
        case 23:   return "SIMPLE_DEAD_FALL";
        case 30:   return "SIMPLE_SHOOT_AT_PED";
        case 31:   return "SIMPLE_SHOOT_AT_CAR";
        case 123:  return "SIMPLE_AIMING";
        case 124:  return "SIMPLE_HOLDING_ENTITY";
        case 125:  return "SIMPLE_PLAYER_ON_FOOT";
        case 126:  return "SIMPLE_HELI_ESCAPE";
        // ─── Basic tasks (200+) — from gta-reversed eTaskType.h ─────────
        case 200:  return "TASK_NONE";
        case 201:  return "SIMPLE_UNINTERRUPTABLE";
        case 202:  return "SIMPLE_PAUSE";
        case 203:  return "STAND_STILL";
        case 204:  return "SET_STAY_IN_SAME_PLACE";
        case 205:  return "SIMPLE_GET_UP";
        case 206:  return "COMPLEX_GET_UP_AND_STAND_STILL";
        case 207:  return "SIMPLE_FALL";
        case 208:  return "COMPLEX_FALL_AND_GET_UP";
        case 209:  return "COMPLEX_FALL_AND_STAY_DOWN";
        case 210:  return "SIMPLE_JUMP";
        case 212:  return "SIMPLE_DIE_IN_CAR";
        // 243 = TASK_COMPLEX_BE_IN_GROUP: wrapper de grupo em TASK_PRIMARY_PRIMARY (slot[3]).
        // TellGroupToStartFollowingPlayer(aggressive) dispara evento GATHER →
        // ComputeResponseGather → SeekEntity em m_PedTaskPairs[recruit].
        // BE_IN_GROUP chama GetTaskMain(recruit) a cada frame e executa SeekEntity
        // como sub-tarefa → recruta segue o jogador.
        case 243:  return "BE_IN_GROUP";
        case 244:  return "COMPLEX_SEQUENCE";
        case 255:  return "SIMPLE_PLAYER_ON_FIRE";
        case 265:  return "SIMPLE_IN_AIR";
        case 281:  return "MELEE_COMBAT";
        case 282:  return "AVOID_DANGER";
        case 305:  return "COMPLEX_FACIAL_TALK";
        // ─── Anim tasks (400+) — from gta-reversed eTaskType.h ──────────
        // 400 = TASK_SIMPLE_ANIM: animacao de spawn (sub-tarefa de GANG_JOIN_RESPOND(1219))
        case 400:  return "SIMPLE_ANIM";
        case 401:  return "SIMPLE_NAMED_ANIM";
        case 402:  return "SIMPLE_TIMED_ANIM";
        case 411:  return "SIMPLE_HIT_BY_GUN_LEFT";
        // ─── Evasive tasks (500+) ────────────────────────────────────────
        case 501:  return "SIMPLE_EVASIVE_STEP";
        case 502:  return "COMPLEX_EVASIVE_STEP";
        // ─── Vehicle-related (600-700 range, from observed IDs) ──────────
        case 600:  return "COMPLEX_INVESTIGATE_DEAD_PED";
        case 601:  return "COMPLEX_REACT_TO_GUN_AIMED_AT";
        case 604:  return "COMPLEX_LEAVE_CAR_AND_FLEE";
        case 606:  return "COMPLEX_LEAVE_CAR_AND_WANDER";
        case 607:  return "COMPLEX_SCREAM_IN_CAR";
        // ─── Car enter/leave tasks (700+) — from gta-reversed eTaskType.h
        case 700:  return "COMPLEX_ENTER_CAR_AS_PASSENGER";
        case 701:  return "COMPLEX_ENTER_CAR_AS_DRIVER";
        case 702:  return "COMPLEX_STEAL_CAR";
        case 704:  return "COMPLEX_LEAVE_CAR";
        case 709:  return "SIMPLE_CAR_DRIVE";
        case 719:  return "LEAVE_CAR_ANIM(?)";
        case 756:  return "SEQUENCE(?)";
        case 801:  return "EXIT_CAR_STAND_2(?)";
        case 802:  return "EXIT_CAR_STUMBLE(?)";
        case 806:  return "EXIT_CAR_STAND(?)";
        case 807:  return "ANIM_STAND_UP(?)";
        case 809:  return "CAR_DRIVE_AS_FOLLOW(?)";
        case 811:  return "CAR_SLOW_DOWN(?)";
        case 812:  return "CAR_ENTER_DONE(?)";
        case 813:  return "EXIT_CAR_IDLE(?)";
        case 824:  return "CAR_EXIT_ANIM(?)";
        // ─── Navigation/follow tasks (900+) — from gta-reversed eTaskType.h
        // 900 = TASK_SIMPLE_GO_TO_POINT: navegacao directa; transitorio durante follow.
        case 900:  return "SIMPLE_GO_TO_POINT";
        // 902 = TASK_SIMPLE_ACHIEVE_HEADING: ajuste de orientacao 1-frame; transitorio.
        case 902:  return "SIMPLE_ACHIEVE_HEADING";
        case 903:  return "COMPLEX_GO_TO_POINT_AND_STAND_STILL";
        case 907:  return "COMPLEX_SEEK_ENTITY";
        case 908:  return "COMPLEX_FLEE_POINT";
        case 909:  return "COMPLEX_FLEE_ENTITY";
        case 912:  return "COMPLEX_WANDER";
        // 913 = TASK_COMPLEX_FOLLOW_LEADER_IN_FORMATION: sub-tarefa de GANG_FOLLOWER ou
        // directamente em slot[3] quando em formacao. Indica seguimento activo (follow OK).
        case 913:  return "COMPLEX_FOLLOW_LEADER_FORMATION";
        case 917:  return "COMPLEX_AVOID_OTHER_PED_WHILE_WANDERING";
        case 922:  return "COMPLEX_SEEK_ENTITY_ANY_MEANS";
        case 923:  return "COMPLEX_FOLLOW_LEADER_ANY_MEANS";
        case 932:  return "COMPLEX_GOTO_DOOR_AND_OPEN";
        case 936:  return "COMPLEX_FOLLOW_PED_FOOTSTEPS";
        // ─── Gang tasks (1200+) — from gta-reversed eTaskType.h ─────────
        case 1000: return "COMPLEX_ENTER_CAR_AS_LEADER";
        case 1008: return "STUMBLE_FALL(?)";
        case 1200: return "SIMPLE_INFORM_GROUP";
        case 1201: return "COMPLEX_GANG_LEADER";
        case 1207: return "COMPLEX_GANG_FOLLOWER";
        // 1219 = TASK_COMPLEX_GANG_JOIN_RESPOND: wrapper de spawn em slot[2]=EVENT_NONTEMP.
        // Sub-tarefa e TASK_SIMPLE_ANIM(400) — animacao de entrada no grupo.
        case 1219: return "COMPLEX_GANG_JOIN_RESPOND";
        // 1500 = TASK_GROUP_FOLLOW_LEADER_ANY_MEANS: tarefa de grupo de follow
        case 1500: return "GROUP_FOLLOW_LEADER_ANY_MEANS";
        case 1501: return "GROUP_FOLLOW_LEADER_WITH_LIMITS";
        // ─── Secondary task IDs ──────────────────────────────────────────
        case 130:  return "2ND_SHOOT_AT_PED";
        case 131:  return "2ND_SHOOT_AT_CAR";
        case 134:  return "2ND_SHOT_REACT";
        case 158:  return "2ND_DUCK";
        case 159:  return "2ND_CROUCH";
        case 164:  return "2ND_SAY";
        case 169:  return "2ND_FACIAL";
        case 174:  return "2ND_PARTIAL_ANIM";
        case 180:  return "2ND_IK";
        default:   return "?";
    }
}

// ───────────────────────────────────────────────────────────────────
// GetCarMissionName — converte eCarMission em nome legivel
// ───────────────────────────────────────────────────────────────────
const char* GetCarMissionName(int mission)
{
    switch (mission) {
        case  0:  return "NONE";
        case  1:  return "CRUISE";
        case  8:  return "GOTOCOORDS";
        case 11:  return "STOP_FOREVER";
        case 12:  return "GOTOCOORDS_ACCURATE";
        case 19:  return "RAM_CAR";
        case 20:  return "RAM_ROADBLOCK";
        case 21:  return "BLOCK_CAR";
        case 22:  return "WAIT_FOR_PED";
        case 25:  return "ATTACK_PED";
        case 27:  return "FOLLOW_PED";
        case 29:  return "LAND";
        case 30:  return "ATTACK_CAR";
        case 31:  return "ESCORT_REAR";
        case 32:  return "ESCORT_FRONT";
        case 33:  return "ESCORT_LEFT";
        case 34:  return "FOLLOW_RECORDED_PATH";
        case 35:  return "FLEE_SCENE";
        case 36:  return "FLEE_CAR";
        case 37:  return "FOLLOW_SPECIFIC_PED";
        case 38:  return "FLEE_ENTITY";
        case 40:  return "SLOW_DOWN";
        case 43:  return "APPROACHPLAYER_FARAWAY";
        case 44:  return "APPROACHPLAYER_CLOSE";
        case 45:  return "WAIT_AT_TRAFFIC_LIGHT";
        case 46:  return "OVERTAKE";
        case 48:  return "WAIT_FOR_PASSENGERS";
        case 49:  return "ENTER_CARPARK";
        case 50:  return "FOLLOW_CAR";
        case 51:  return "FIRE_AT_OBJECT";
        case 52:  return "FOLLOWCAR_FARAWAY";
        case 53:  return "FOLLOWCAR_CLOSE";
        case 55:  return "WAIT_AT_HELI";
        case 56:  return "GOTO_COORDS_DONT_STOP";
        case 57:  return "GOTO_COORDS_BOAT";
        case 58:  return "POLICE_PURSUIT";
        case 59:  return "SLOW_DOWN_AND_STOP";
        case 60:  return "FOLLOW_ROAD_FORWARD";
        case 61:  return "DRIVE_TO_TRAIN_STATION";
        case 62:  return "DRIVE_THROUGH_TRAIN";
        case 63:  return "DRIVE_BARGE";
        case 64:  return "TURN_BOAT";
        case 65:  return "DRIVE_OFF_JETSKI";
        case 66:  return "DRIVE_JETSKI_TO_DEST";
        case 67:  return "ESCORT_REAR_FARAWAY";
        case 68:  return "ESCORT_LEFT_FARAWAY";
        default:  return "?";
    }
}

// ───────────────────────────────────────────────────────────────────
// GetTempActionName — converte m_nTempAction em nome legivel
// ───────────────────────────────────────────────────────────────────
const char* GetTempActionName(int action)
{
    switch (action) {
        case  0:  return "NONE";
        case  1:  return "WAIT";
        case  3:  return "REVERSE";
        case  4:  return "HANDBRAKE_LEFT";
        case  5:  return "HANDBRAKE_RIGHT";
        case  6:  return "HANDBRAKE_STRAIGHT";
        case  7:  return "TURN_LEFT";
        case  8:  return "TURN_RIGHT";
        case  9:  return "GOFORWARD";
        case 10:  return "SWERVE_LEFT";
        case 11:  return "SWERVE_RIGHT";
        case 12:  return "STUCK_TRAFFIC";
        case 13:  return "REVERSE_LEFT";
        case 14:  return "REVERSE_RIGHT";
        case 19:  return "HEADON_COLLISION";
        case 24:  return "BRAKE";
        default:  return "?";
    }
}

// ───────────────────────────────────────────────────────────────────
// GetDriveStyleName — converte eDrivingStyle em nome legivel
// ───────────────────────────────────────────────────────────────────
const char* GetDriveStyleName(int style)
{
    switch (style) {
        case 0:  return "STOP_FOR_CARS";
        case 1:  return "SLOW_DOWN_FOR_CARS";
        case 2:  return "AVOID_CARS";
        case 3:  return "PLOUGH_THROUGH";
        case 4:  return "STOP_IGNORE_LIGHTS";
        // Nota: eCarDrivingStyle so tem 5 valores (0-4) no plugin-sdk SA.
        // Valores 5 e 6 NAO existem como constantes do enum — eram erros anteriores.
        // Valor 6 (4|2) e reconhecido internamente pelo police-yield code de CarAI
        // mas NAO deve ser usado como driving style intencional (comportamento indefinido).
        default: return "?";
    }
}

// ───────────────────────────────────────────────────────────────────
// BuildPrimaryTaskBuf / BuildSecondaryTaskBuf
// ───────────────────────────────────────────────────────────────────
int BuildPrimaryTaskBuf(char* buf, int bufsz, CTaskManager& tm)
{
    int written = 0;
    int n = snprintf(buf, bufsz, "P:");
    if (n > 0) written = std::min(written + n, bufsz - 1);
    for (int i = 0; i < 5; ++i)
    {
        CTask* t = tm.m_aPrimaryTasks[i];
        int id = t ? (int)t->GetId() : -1;
        n = snprintf(buf + written, bufsz - written,
            "%s[%d]%s(%d)", i ? " " : "", i, GetTaskName(id), id);
        if (n > 0) written = std::min(written + n, bufsz - 1);
    }
    return written;
}

int BuildSecondaryTaskBuf(char* buf, int bufsz, int startOffset, CTaskManager& tm)
{
    int written = startOffset;
    int n = snprintf(buf + written, bufsz - written, " S:");
    if (n > 0) written = std::min(written + n, bufsz - 1);
    for (int i = 0; i < 6; ++i)
    {
        CTask* t = tm.m_aSecondaryTasks[i];
        int id = t ? (int)t->GetId() : -1;
        n = snprintf(buf + written, bufsz - written,
            "%s[%s]%s(%d)", i ? " " : "", s_secSlotNames[i], GetTaskName(id), id);
        if (n > 0) written = std::min(written + n, bufsz - 1);
    }
    return written;
}
