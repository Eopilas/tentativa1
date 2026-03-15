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
        "===== grove_recruit_standalone.asi — log iniciado =====\n"
        "  Formato: [FFFFFFF][NIVEL] mensagem\n"
        "  Niveis: EVENT GROUP TASK DRIVE AI    KEY   WARN  ERROR OBSV  WORLD\n"
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
        "             driveStyle(nome), tempAction(nome), offroad, modo,\n"
        "             heading, targetH, deltaH, speedMult\n"
        "  DRIVING_2: straight, lane, linkId(OK/INVALID), areaId, dest(xyz),\n"
        "             targetCar, car, snapTimer, tasks(P:5primary + S:6secondary)\n"
        "  physSpeed: velocidade fisica real (m_vecMoveSpeed x 180 ≈ km/h)\n"
        "  speed_ap:  velocidade de cruzeiro do AutoPilot (unidades SA)\n"
        "  mission:   valor de m_nCarMission (eCarMission):\n"
        "               8=GOTOCOORDS(DIRETO) | 11=STOP_FOREVER(PARADO/STOP_ZONE)\n"
        "               12=GOTOCOORDS_ACCURATE\n"
        "               31=ESCORT_REAR | 34=FOLLOW_RECORDED_PATH(CIVICO_E)\n"
        "               43=APPROACHPLAYER_FARAWAY(CIVICO_D) | 44=APPROACHPLAYER_CLOSE\n"
        "               52=FOLLOWCAR_FARAWAY | 53=FOLLOWCAR_CLOSE(estados road-SM normais)\n"
        "               67=ESCORT_REAR_FARAWAY(CIVICO_D apos transicao, normal)\n"
        "  driveStyle: 0=STOP_FOR_CARS | 1=SLOW_DOWN_FOR_CARS | 2=AVOID_CARS\n"
        "              3=PLOUGH_THROUGH | 4=STOP_IGNORE_LIGHTS | 5=AVOID_OBEYLIGHTS\n"
        "              6=AVOID_STOPFORPEDS_OBEYLIGHTS\n"
        "  tempAction: m_nTempAction — accao temporaria de avoidance/steering:\n"
        "              0=NONE | 1=WAIT | 3=REVERSE | 4=HANDBRAKE_LEFT | 5=HANDBRAKE_RIGHT\n"
        "              6=HANDBRAKE_STRAIGHT | 7=TURN_LEFT | 8=TURN_RIGHT | 9=GOFORWARD\n"
        "              10=SWERVE_LEFT | 11=SWERVE_RIGHT | 12=STUCK_TRAFFIC\n"
        "              13=REVERSE_LEFT | 14=REVERSE_RIGHT | 19=HEADON_COLLISION | 24=BRAKE\n"
        "  dest:      coordenadas de destino actuais no AutoPilot\n"
        "  targetCar: apontador ao carro do jogador (CIVICO); nullptr em DIRETO\n"
        "  heading:   orientacao do veiculo (GetHeading(), rad, 0=Norte)\n"
        "  targetH:   heading clipado ao eixo da faixa (ClipTargetOrientationToLink)\n"
        "  deltaH:    diff heading-targetH — >1.5rad=WRONG_DIR (sentido contrario!)\n"
        "             NOTA: 'linkId_invalido' se linkId>50000 (targetH seria lixo)\n"
        "             NOTA: NearestTrafficCar WRONG_DIR! pode ser normal p/ roads com\n"
        "             orientacao contraria ao sentido actual do NPC.\n"
        "  speedMult: factor de curva 0.0-1.0 (FindSpeedMultiplierWithSpeedFromNodes)\n"
        "  JOIN_ROAD: diff linkId/heading antes/apos JoinCarWithRoadSystem\n"
        "\n"
        "  FindNearestFreeCar: DOIS PASSES de seleccao:\n"
        "    1.o passe: prefere carros com linkId<=50000 (ja no road-graph CIVICO).\n"
        "    2.o passe: aceita qualquer carro se nenhum snapped encontrado.\n"
        "    Log: 'FindNearestFreeCar: veh=... linkId=X(OK/INVALID) (road-snapped...)'\n"
        "\n"
        "  CONDUCAO CIVICO:\n"
        "  driveStyle=STOP_FOR_CARS_IGNORE_LIGHTS(4): para para obstaculos,\n"
        "    ignora semaforos — evita recruta preso em semaforos enquanto jogador\n"
        "    continua. Road-following e controlado pela missao (MISSION_43/34),\n"
        "    NAO pelo driveStyle. Trafego vanilla usa MISSION_CRUISE(1) com\n"
        "    driveStyle 0 ou 4 — missoess fundamentalmente diferentes.\n"
        "  PERIODIC_ROAD_SNAP: JoinCarWithRoadSystem re-chamado a cada 3s em CIVICO\n"
        "    para manter alinhamento com nos de estrada.\n"
        "    g_civicRoadSnapTimer negativo = pausa de snap (INVALID_LINK_STORM).\n"
        "  INVALID_LINK_STORM: se JoinCarWithRoadSystem retornar link invalido >5\n"
        "    vezes consecutivas, snap e pausado por 120 frames para evitar loop.\n"
        "  WRONG_DIR_RECOVER: CORRECAO v2 — apenas dispara quando dist > 30m.\n"
        "    ANTERIOR (bug): disparava a dist < 30m → JoinCarWithRoadSystem em range\n"
        "    proximo → re-snap errado → WRONG_DIR por 38+ segundos consecutivos.\n"
        "    NOVO: apenas a dist > 30m (range longe); range proximo usa snap periodico.\n"
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
        "  WRONG_DIR_START/END: transicoes com physSpeed para verificar se movia.\n"
        "  WRONG_DIR_RECOVER: SetupDriveMode apenas quando dist > 30m (v2 fix).\n"
        "  INVALID_LINK: linkId>50000 — re-snap imediato (sem beelining).\n"
        "  INVALID_LINK_STORM: >5 snaps invalidos consecutivos — pausa 120 frames.\n"
        "  MISSION_RECOVERY: STOP_FOREVER detectado fora das zonas — restaurar.\n"
        "    NOTA: estados 31/53/67 (road-SM internos) NAO sao recuperados.\n"
        "  SLOW_ZONE: log apenas na transicao de entrada (dedup via g_slowZoneRestoring).\n"
        "  SLOW_ZONE saiu: log quando recruta sai da SLOW_ZONE.\n"
        "========================================================\n\n",
        MAX_FOLLOW_FALLBACK_RETRIES);
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
        case 5:  return "AVOID_OBEYLIGHTS";
        case 6:  return "AVOID_STOPFORPEDS_OBEYLIGHTS";
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
