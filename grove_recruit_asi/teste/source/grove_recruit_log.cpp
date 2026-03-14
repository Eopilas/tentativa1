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
        "  Niveis: EVENT GROUP TASK DRIVE AI    KEY   WARN  ERROR OBSV\n"
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
        "    204=IDLE | 264=BE_IN_GROUP | 400=GANG_SPAWN_AI\n"
        "    600=ENTER_CAR_DRIVER | 601=ENTER_CAR_PASS | 604=LEAVE_CAR\n"
        "    709=CAR_DRIVE(a conduzir OK) | 1207=GANG_FOLLOWER(a seguir OK)\n"
        "    1219=GANG_SPAWN_COMPLEX(wrapper de spawn, deveria desaparecer com ForceFollow)\n"
        "    1500=FOLLOW_ANY_MEANS(OK)\n"
        "  IDs importantes secundarios:\n"
        "    [ATK]: 30=SHOOT_PED | 31=SHOOT_CAR | 130/131=2ND_SHOOT | 134=SHOT_REACT\n"
        "    [DCK]: 158=DUCK | 159=CROUCH\n"
        "    [SAY]: 164=SAY (voiceline activa — pode ser GANG_RECRUIT_REFUSE!)\n"
        "    [FAC]: 169=FACIAL_COMPLEX | 305=COMPLEX_FACIAL_TALK(normal no spawn)\n"
        "    [PAR]: 174=PARTIAL_ANIM\n"
        "    [IK]:  180=INVERSE_KINEMATICS\n"
        "  pedType:  8=GANG2=GSF(OK)  7=GANG1=Ballas(ERRADO->congelado e inimigo)\n"
        "  respeito: STAT_RESPECT=68 lido por CPedIntelligence::Respects (0x601C90)\n"
        "            Se respect<threshold: voiceline REFUSE + activeTask=203.\n"
        "  BOOST PERSISTENTE: ActivateRespectBoost() activo durante TODA a sessao\n"
        "    (spawn->dismiss). Procurar 'RESPECT_BOOST: ACTIVADO/DESACTIVADO'.\n"
        "\n"
        "  FOLLOW CORRIGIDO (on-foot):\n"
        "  ForceGroupToAlwaysFollow(true): chamado em AddRecruitToGroup (0961 equiv.).\n"
        "    SEM este call o motor atribui GANG_SPAWN_COMPLEX(1219) em vez de\n"
        "    GANG_FOLLOWER(1207) — recruta fica parado apos animacao de spawn!\n"
        "  GANG_SPAWN_AI_END: quando GetSimplestActiveTask transita de 400, o mod\n"
        "    re-emite TellGroupFollowWithRespect IMEDIATAMENTE.\n"
        "  POST_FOLLOW_CHECK: 3 frames apos TellGroupFollowWithRespect, verifica task.\n"
        "    Se nao for 1207/1500: SetAllocatorType(4)+ComputeDefaultTasks (FALLBACK).\n"
        "    Limite MAX_FOLLOW_FALLBACK_RETRIES=%d tentativas por ciclo — evita loop.\n"
        "    Estados transitórios (902=FLEE, 400=GANG_SPAWN_AI): aguarda sem erro.\n"
        "\n"
        "  DIAGNOSTICO CONDUCAO:\n"
        "  DRIVING_1: dist, speed_ap(autopilot), physSpeed(km/h real), mission,\n"
        "             driveStyle, offroad, modo, heading, targetH, deltaH, speedMult\n"
        "  DRIVING_2: straight, lane, linkId(OK/INVALID), areaId, dest(xyz),\n"
        "             targetCar, car, tasks(P:5primary + S:6secondary)\n"
        "  physSpeed: velocidade fisica real (m_vecMoveSpeed x 180 ≈ km/h)\n"
        "  speed_ap:  velocidade de cruzeiro do AutoPilot (unidades SA)\n"
        "  mission:   valor de m_nCarMission (eCarMission):\n"
        "               8=GOTOCOORDS(DIRETO) | 11=STOP_FOREVER(PARADO/STOP_ZONE)\n"
        "               34=MISSION_34(CIVICO_E) | 43=MISSION_43(CIVICO_D)\n"
        "               31,53=estados intermédios normais do road-SM (jogo interno, OK)\n"
        "  driveStyle: 0=STOP_FOR_CARS | 1=AVOID_CARS | 3=PLOUGH_THROUGH\n"
        "  dest:      coordenadas de destino actuais no AutoPilot\n"
        "  targetCar: apontador ao carro do jogador (CIVICO); nullptr em DIRETO\n"
        "  heading:   orientacao do veiculo (GetHeading(), rad, 0=Norte)\n"
        "  targetH:   heading clipado ao eixo da faixa (ClipTargetOrientationToLink)\n"
        "  deltaH:    diff heading-targetH — >1.5rad=WRONG_DIR (sentido contrario!)\n"
        "             NOTA: 'linkId_invalido' se linkId>50000 (targetH seria lixo)\n"
        "  speedMult: factor de curva 0.0-1.0 (FindSpeedMultiplierWithSpeedFromNodes)\n"
        "  JOIN_ROAD: diff linkId/heading antes/apos JoinCarWithRoadSystem\n"
        "\n"
        "  FindNearestFreeCar: DOIS PASSES de seleccao:\n"
        "    1.o passe: prefere carros com linkId<=50000 (ja no road-graph CIVICO).\n"
        "    2.o passe: aceita qualquer carro se nenhum snapped encontrado.\n"
        "    Log: 'FindNearestFreeCar: veh=... linkId=X(OK/INVALID) (road-snapped...)'\n"
        "\n"
        "  NOVAS FUNCIONALIDADES DE CONDUCAO:\n"
        "  CIVICO_D AUTO-DEGRADE: quando dist < MEDIUM_DIST_M(25m), CIVICO_D baixa\n"
        "    automaticamente para MISSION_34 (menos 'chase' ao virar perto do recruta).\n"
        "    Restaura MISSION_43 quando dist >= MEDIUM_DIST_M (road-following normal).\n"
        "  PERIODIC_ROAD_SNAP: JoinCarWithRoadSystem re-chamado a cada 5s em CIVICO\n"
        "    para manter alinhamento com nos de estrada. Reset ao sair da SLOW_ZONE.\n"
        "  SLOW_ZONE re-snap: JoinCarWithRoadSystem ao sair da SLOW_ZONE (dist>10m)\n"
        "    para re-alinhar imediatamente com a estrada apos pausa.\n"
        "\n"
        "  SISTEMA DE OBSERVACAO VANILLA [OBSV]:\n"
        "  ProcessObserver (a cada 2s) loga estado do motor do jogo SEM o mod:\n"
        "    NearestTrafficCar: CAutoPilot completo do NPC de trafego mais proximo\n"
        "      linkId(OK/INVALID) mostrado explicitamente; WRONG_DIR omitido se INVALID\n"
        "    NearestGSFPed:     tasks do ped GSF (pedType=8) mais proximo a pe\n"
        "    PlayerGroup:       estado do grupo do jogador (membros + tasks)\n"
        "    PlayerState:       estado do jogador (a pe ou em carro + CAutoPilot)\n"
        "    RecruitCar/RecruitPed: estado actual do recruta para comparacao directa\n"
        "  Usar NearestTrafficCar como referencia vanilla para comparar linkId/mission\n"
        "  do recruta — NPC vanilla tem sempre linkId valido e mission=1(CRUISE).\n"
        "\n"
        "  OUTROS EVENTOS DE DIAGNOSTICO:\n"
        "  PRE_JOIN: dump antes de MakeThisPedJoinOurGroup\n"
        "  TASK_CHANGE: mudanca real-time da tarefa activa (usa GetTaskName()).\n"
        "  GANG_SPAWN_AI_END: transicao de saida de GANG_SPAWN_AI — re-emite follow.\n"
        "  POST_FOLLOW_CHECK: tarefa 3 frames apos TellGroupFollowWithRespect.\n"
        "  FOLLOW_FALLBACK: SetAllocatorType(4)+ComputeDefault se follow nao confirmado.\n"
        "  WRONG_DIR_START/END: transicoes com physSpeed para verificar se movia.\n"
        "  INVALID_LINK: linkId>50000 — fallback DIRETO temporario.\n"
        "  MISSION_RECOVERY: STOP_FOREVER detectado fora das zonas — restaurar.\n"
        "    NOTA: estados 31/53 (road-SM internos) NAO sao recuperados — sao normais!\n"
        "  SLOW_ZONE: log apenas na transicao de entrada (dedup via g_slowZoneRestoring).\n"
        "  SLOW_ZONE saiu: log quando recruta sai da SLOW_ZONE + JoinCarWithRoadSystem.\n"
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
        case 203:  return "STAND_STILL";
        case 204:  return "IDLE";
        case 206:  return "WANDER";
        case 212:  return "TURN_STAND";
        case 255:  return "ARREST";
        case 264:  return "BE_IN_GROUP";
        case 265:  return "IN_AIR";
        case 281:  return "MELEE_COMBAT";
        case 282:  return "AVOID_DANGER";
        case 305:  return "COMPLEX_FACIAL_TALK";   // tarefa facial presente em [FAC] spawn
        case 400:  return "GANG_SPAWN_AI";
        case 401:  return "GANG_FIGHT";
        case 402:  return "GANG_HASSLE";
        case 600:  return "ENTER_CAR_AS_DRIVER";
        case 601:  return "ENTER_CAR_AS_PASSENGER";
        case 604:  return "LEAVE_CAR";
        case 606:  return "CAR_AS_DRIVER";
        case 607:  return "CAR_AS_PASSENGER";
        case 700:  return "COMPLEX_ENTER_CAR_DRIVER";
        case 701:  return "COMPLEX_ENTER_CAR_PASS";
        case 702:  return "COMPLEX_LEAVE_CAR";
        case 709:  return "CAR_DRIVE";
        case 756:  return "SEQUENCE";
        case 902:  return "FLEE";
        case 1200: return "GANG_LEADER";
        case 1207: return "GANG_FOLLOWER";
        // Tarefa 1219 vista em slot [2]=EVENT_NONTEMP durante spawn:
        // wrapper complexo do engine que contem GANG_SPAWN_AI(400) como sub-tarefa.
        // HIPOTESE (nao verificada): atribuido quando ForceGroupToAlwaysFollow=false;
        // com ForceGroupToAlwaysFollow=true deveria ser substituido por GANG_FOLLOWER.
        // Confirmar via TASK_CHANGE nos logs: se 1219 persistir apos spawn, hipotese falha.
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
