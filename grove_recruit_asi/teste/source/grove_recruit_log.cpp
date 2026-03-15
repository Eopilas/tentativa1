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
        "    202=WALK_TO_TARGET(?)  204=IDLE | 206=WANDER | 264=BE_IN_GROUP\n"
        "    400=GANG_SPAWN_AI  411=GANG_CMD_RESPONSE(?)\n"
        "    600=ENTER_CAR_DRIVER | 601=ENTER_CAR_PASS | 604=LEAVE_CAR\n"
        "    709=CAR_DRIVE(a conduzir OK) | 1207=GANG_FOLLOWER(a seguir OK)\n"
        "    1219=GANG_SPAWN_COMPLEX(wrapper de spawn)\n"
        "    1500=FOLLOW_ANY_MEANS(OK)\n"
        "  IDs observados em runtime (nomes hipoteticos baseados em comportamento):\n"
        "    205=WALK_ARRIVE(?)       — fim de percurso a pe (sub de 208)\n"
        "    207=WALK_TO_POINT(?)     — recruta a caminhar p/ posicao (sub de 208)\n"
        "    208=FOLLOW_LEADER_FORM(?)— slot[0] PHYSICAL_RESPONSE; recruta SEGUE(OK!)\n"
        "      NOTA: 207/208 sao tarefas de seguimento VALIDAS — POST_FOLLOW_CHECK aceita-as.\n"
        "      Observado: engine cria 208 em slot[0] quando ComputeDefaultTasks funciona.\n"
        "    243=GANG_FOLLOW_PRIMARY(?)— slot[3] PRIMARY; task vanilla p/ gang recruit(OK!)\n"
        "      Observado: engine atribui 243 a peds GSF vanilla apos SetCharCreatedBy(1).\n"
        "      POST_FOLLOW_CHECK tambem aceita 243 como follow valido.\n"
        "    900=REACT_TO_CMD(?)      — 1 frame apos TellGroupFollow; leva a FLEE(902)\n"
        "      Ciclo 900->902->203 nao e erro fatal: acontece repetidamente mas recruta\n"
        "      pode estabilizar em 208/243 se ComputeDefaultTasks for chamado c/ player.\n"
        "    917=STAND_AND_SHOOT(?)   — observado em slot[1] durante combate parado\n"
        "    932=PLAYER_ON_FOOT(?)    — tarefa tipica do jogador a pe\n"
        "    1008=STUMBLE_FALL(?)     — tropeco/queda complexo\n"
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
        "    501=FALL_LANDING(?)      — aterragem de queda\n"
        "    502=IN_AIR_EVENT(?)      — evento de queda em slot[1]\n"
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
        "  FOLLOW CORRIGIDO (on-foot) — v3:\n"
        "  ACQUAINTANCE_FIX: AddRecruitToGroup define m_acquaintance.m_nRespect bit 0\n"
        "    (PED_TYPE_PLAYER1) para que Respects() retorne true sem alterar STAT_RESPECT.\n"
        "  GANG_SPAWN_AI_END: quando GetSimplestActiveTask transita de 400, o mod\n"
        "    re-emite TellGroupFollowWithRespect IMEDIATAMENTE.\n"
        "  POST_FOLLOW_CHECK (v3 fix): 3 frames apos TellGroupFollowWithRespect, verifica DUAS tasks:\n"
        "    activeTask  = GetSimplestActiveTask() — tarefa folha (slot mais baixo ocupado)\n"
        "    primaryTask = GetActiveTask()         — tarefa primaria (slot[3]=PRIMARY)\n"
        "    BUG CORRIGIDO: GetSimplestActiveTask devolve subtarefa de 243 (203/900/902)\n"
        "    quando slot[3]=243. Fix: verificar AMBAS activeTask E primaryTask.\n"
        "    VALIDOS (qualquer um dos dois): 1207/1500/207/208/243 — todos indicam seguimento.\n"
        "    Se invalido: SetAllocatorType(4)+ComputeDefaultTasks(player) (FALLBACK).\n"
        "    CORRECAO v2: ComputeDefaultTasks recebe PLAYER (lider) nao g_recruit!\n"
        "    Limite MAX_FOLLOW_FALLBACK_RETRIES=%d tentativas por ciclo — evita loop.\n"
        "    Estados transitórios (902=FLEE, 400=GANG_SPAWN_AI, 900=REACT, 1219): aguarda.\n"
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
        "  GANG_SPAWN_AI_END: transicao de saida de GANG_SPAWN_AI — re-emite follow.\n"
        "  POST_FOLLOW_CHECK: tarefa 3 frames apos TellGroupFollowWithRespect.\n"
        "    v3: verifica activeTask(GetSimplestActiveTask) E primaryTask(GetActiveTask).\n"
        "  FOLLOW_FALLBACK: SetAllocatorType(4)+ComputeDefault(player) se nao confirmado.\n"
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
        case 12:   return "SIMPLE_FALL";
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
        case 200:  return "TASK_NONE";
        case 202:  return "WALK_TO_TARGET(?)";      // observado: 203->202 e 202->900
        case 203:  return "STAND_STILL";
        case 204:  return "IDLE";
        case 205:  return "WALK_ARRIVE(?)";        // fim de percurso a pe (sub-tarefa de 208)
        case 206:  return "WANDER";
        case 207:  return "WALK_TO_POINT(?)";      // recruta a caminhar; sub-tarefa de 208 (follow OK!)
        // Tarefa 208 = follow-leader complexo em slot[0]=PHYSICAL_RESPONSE.
        // Observado: engine cria 208/207 em slot[0] quando recruta segue correctamente.
        // ComputeDefaultTasks(player) deve criar 208 e/ou 1207/243 — POST_FOLLOW aceita.
        case 208:  return "FOLLOW_LEADER_FORM(?)"; // follow em formacao, slot[0]; recruta SEGUE (OK!)
        case 212:  return "TURN_STAND";
        // Tarefa 243 = gang follow primary em slot[3].
        // Observado: engine atribui 243 automaticamente a peds GSF vanilla (SetCharCreatedBy=1).
        // Indica seguimento activo — POST_FOLLOW_CHECK aceita como valido.
        case 243:  return "GANG_FOLLOW_PRIMARY(?)";//follow vanilla slot[3] (OK!)
        case 255:  return "ARREST";
        case 264:  return "BE_IN_GROUP";
        case 265:  return "IN_AIR";
        case 281:  return "MELEE_COMBAT";
        case 282:  return "AVOID_DANGER";
        case 305:  return "COMPLEX_FACIAL_TALK";   // tarefa facial presente em [FAC] spawn
        case 400:  return "GANG_SPAWN_AI";
        case 401:  return "GANG_FIGHT";
        case 402:  return "GANG_HASSLE";
        case 411:  return "GANG_CMD_RESPONSE(?)";   // recruta reagindo a comando de gang
        case 501:  return "FALL_LANDING(?)";       // aterragem de queda (slot IK=IN_AIR(265))
        case 502:  return "IN_AIR_EVENT(?)";       // evento de queda em slot[1]=EVENT_TEMP
        case 600:  return "ENTER_CAR_AS_DRIVER";
        case 601:  return "ENTER_CAR_AS_PASSENGER";
        case 604:  return "LEAVE_CAR";
        case 606:  return "CAR_AS_DRIVER";
        case 607:  return "CAR_AS_PASSENGER";
        case 700:  return "COMPLEX_ENTER_CAR_DRIVER";
        case 701:  return "COMPLEX_ENTER_CAR_PASS";
        case 702:  return "COMPLEX_LEAVE_CAR";
        case 709:  return "CAR_DRIVE";
        case 719:  return "LEAVE_CAR_ANIM(?)";     // inicio anim saida de carro
        case 756:  return "SEQUENCE";
        case 806:  return "EXIT_CAR_STAND(?)";     // pose de pe imediata apos saida
        case 801:  return "EXIT_CAR_STAND_2(?)";   // variante de saida de carro
        case 802:  return "EXIT_CAR_STUMBLE(?)";   // stumble apos saida
        case 807:  return "ANIM_STAND_UP(?)";      // animacao de levantar
        case 809:  return "CAR_DRIVE_AS_FOLLOW(?)";// conducao em modo follow
        case 811:  return "CAR_SLOW_DOWN(?)";      // abrandar o carro
        case 812:  return "CAR_ENTER_DONE(?)";     // entrada no carro concluida
        case 813:  return "EXIT_CAR_IDLE(?)";      // idle apos sair do carro
        case 824:  return "CAR_EXIT_ANIM(?)";      // animacao pre-saida de carro
        // Tarefa 900 = react-to-command de 1 frame; surge 1 frame apos TellGroupFollow.
        // Leads imediatamente a FLEE(902). Ciclo 900->902->203 repetido e transitorio
        // — nao e erro fatal, mas impede estabilizacao do follow se ocorrer muito.
        case 900:  return "REACT_TO_CMD(?)";       // 1-frame react; leva a FLEE(902)
        case 902:  return "FLEE";
        case 917:  return "STAND_AND_SHOOT(?)";    // observado em slot[1] durante combate parado
        case 932:  return "PLAYER_ON_FOOT(?)";     // tarefa do jogador a pe
        case 1200: return "GANG_LEADER";
        case 1207: return "GANG_FOLLOWER";
        case 1008: return "STUMBLE_FALL(?)";       // tropeco/queda complexo
        // Tarefa 1219 vista em slot [2]=EVENT_NONTEMP durante spawn:
        // wrapper complexo do engine que contem GANG_SPAWN_AI(400) como sub-tarefa.
        case 1219: return "GANG_SPAWN_COMPLEX";
        case 1500: return "FOLLOW_ANY_MEANS";
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
