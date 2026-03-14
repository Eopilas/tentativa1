#pragma once
/*
 * grove_recruit_log.h
 *
 * Declaracoes do sistema de logging e helpers de introspeccao de tarefas.
 * Implementacao em grove_recruit_log.cpp.
 */
#include "grove_recruit_config.h"

// ───────────────────────────────────────────────────────────────────
// Log — niveis disponiveis
//   [EVENT] — acoes do jogador, transicoes, spawn/dismiss
//   [GROUP] — operacoes de grupo (join, leave, rescan)
//   [TASK ] — tarefas de IA (follow, enter-car, leave-car)
//   [DRIVE] — IA de conducao (missao, speed, offroad)
//   [AI   ] — dump per-frame throttled (~2s)
//   [KEY  ] — teclas pressionadas
//   [WARN ] — situacoes inesperadas recuperaveis
//   [ERROR] — falhas criticas
//   [OBSV ] — observacao vanilla: NPC trafego, peds GSF, grupo do jogador
// ───────────────────────────────────────────────────────────────────
void LogInit();
void LogEvent(const char* fmt, ...);
void LogGroup(const char* fmt, ...);
void LogTask (const char* fmt, ...);
void LogDrive(const char* fmt, ...);
void LogAI   (const char* fmt, ...);
void LogKey  (const char* fmt, ...);
void LogWarn (const char* fmt, ...);
void LogError(const char* fmt, ...);
void LogObsv (const char* fmt, ...);   // [OBSV ] — observacao vanilla engine

// ───────────────────────────────────────────────────────────────────
// Helpers de introspeccao de tarefas
// ───────────────────────────────────────────────────────────────────

// Converte task ID (eTaskType) em nome legivel para logs.
const char* GetTaskName(int id);

// Constroi string com os 5 slots primarios do CTaskManager.
// Formato: "P:[0]NAME(id) [1]NAME(id) ... [4]NAME(id)"
// Devolve bytes escritos em buf.
int BuildPrimaryTaskBuf(char* buf, int bufsz, CTaskManager& tm);

// Constroi string com os 6 slots secundarios do CTaskManager.
// Formato: " S:[ATK]NAME(id) [DCK]NAME(id) [SAY]NAME(id) [FAC]NAME(id) [PAR]NAME(id) [IK]NAME(id)"
// startOffset: bytes ja escritos (resultado de BuildPrimaryTaskBuf).
// Devolve bytes totais escritos em buf.
int BuildSecondaryTaskBuf(char* buf, int bufsz, int startOffset, CTaskManager& tm);
