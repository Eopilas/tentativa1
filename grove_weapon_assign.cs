// ===============================================================
// grove_weapon_assign.cs
// CLEO 4 script independente — GTA SA (versao 1.0 US)
//
// Entrega ou remove armas dos membros do grupo do jogador.
//
// TECLAS:
//   5 (VK 53) — Dar a arma ACTUAL do jogador a todos os membros
//               do grupo com 300 municoes. Cada pressao adiciona
//               mais 300 a stock existente (util para repor balas).
//   6 (VK 54) — Retirar TODAS as armas de todos os membros do grupo.
//               Util para trocar de arma: 6 para limpar, depois
//               equipa nova arma e prime 5.
//
// FUNCIONAMENTO:
//   O script itera os slots 0 a 6 do grupo nativo SA (CPedGroup).
//   07AF le o handle do grupo do jogador.
//   07F6 verifica se ha membros — evita iteracao vazia.
//   092B le o handle do membro em cada slot (0=sem membro).
//   056D confirma que o handle e valido antes de operar.
//   0470 le o ID da arma actualmente equipada pelo jogador.
//   01B2 entrega a arma + municao ao membro.
//   048F remove todas as armas do membro.
//
// COMPATIBILIDADE:
//   Funciona com recrutas do mod (grove_recruit_follow.cs) e com
//   membros recrutados pelo sistema vanilla de grupo do SA.
//   Nao requer comunicacao entre scripts — usa a API de grupo SA.
//
// VARIAVEIS LOCAIS:
//   0@  handle do actor do jogador (01F5)
//   1@  weapon ID actual (0470)
//   2@  handle do grupo do jogador (07AF)
//   3@  numero de lideres do grupo (07F6, descartado)
//   4@  numero de membros do grupo (07F6, gate)
//   5@  handle do membro no slot actual (092B)
//   6@  contador de slot (loop 0-6)
// ===============================================================
{$CLEO .cs}
0000: NOP

:WEAPON_ASSIGN_INIT
0001: wait 2000 ms
00D6: if
    0256: player $PLAYER_CHAR defined
004D: jump_if_false @WEAPON_ASSIGN_INIT

0ACD: show_text_highpriority "Grove Weapon Assign: 5=Dar arma | 6=Retirar armas" time 3000

// ---------------------------------------------------------------
// LOOP PRINCIPAL — 300ms
// ---------------------------------------------------------------
:WEAPON_MAIN_LOOP
0001: wait 300 ms

01F5: 0 0@

// Tecla 5 (VK 53): dar arma actual do jogador a todos os membros
00D6: if
    0AB0: key_pressed 53
004D: jump_if_false @WA_CHECK_KEY6

// Obter grupo do jogador
07AF: 0 2@
00D6: if
    0019: 2@ > 0
004D: jump_if_false @WA_NO_GROUP

// Verificar se ha membros no grupo
07F6: 2@ 3@ 4@
00D6: if
    0019: 4@ > 0
004D: jump_if_false @WA_NO_GROUP

// Obter ID da arma actual do jogador
// 0470: binario P1=actor, P2=→weapon_id
0470: 0@ 1@

// Rejeitar mao-vazia (ID 0 = sem arma)
00D6: if
    0019: 1@ > 0
004D: jump_if_false @WA_NO_WEAPON

// Dar a arma a cada membro valido nos slots 0-6
0006: 6@ = 0
:WA_GIVE_LOOP
// 092B: binario P1=group, P2=slot, P3=→ped_handle
092B: 2@ 6@ 5@
00D6: if
    056D: actor 5@ defined
004D: jump_if_false @WA_GIVE_NEXT
// 01B2: binario P1=actor, P2=weapon_id, P3=ammo
01B2: give_actor 5@ weapon 1@ ammo 300
// Nao largar a arma ao morrer (0=nao-droppable)
// 087E: binario P1=actor, P2=droppable_flag
087E: set_actor 5@ weapon_droppable 0
:WA_GIVE_NEXT
000A: 6@ += 1
00D6: if
    0019: 6@ > 6
004D: jump_if_false @WA_GIVE_LOOP

0ACD: show_text_highpriority "Arma entregue ao grupo! (6 para retirar)" 2500
0001: wait 600 ms
0002: jump @WEAPON_MAIN_LOOP

:WA_NO_GROUP
0ACD: show_text_highpriority "Sem membros no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP

:WA_NO_WEAPON
0ACD: show_text_highpriority "Equipa uma arma antes de usar 5." 2000
0002: jump @WEAPON_MAIN_LOOP

// ---------------------------------------------------------------
// Tecla 6 (VK 54): retirar todas as armas de todos os membros
// ---------------------------------------------------------------
:WA_CHECK_KEY6
00D6: if
    0AB0: key_pressed 54
004D: jump_if_false @WEAPON_MAIN_LOOP

// Obter grupo do jogador
07AF: 0 2@
00D6: if
    0019: 2@ > 0
004D: jump_if_false @WA_STRIP_NO_GROUP

// Verificar se ha membros no grupo
07F6: 2@ 3@ 4@
00D6: if
    0019: 4@ > 0
004D: jump_if_false @WA_STRIP_NO_GROUP

// Retirar armas de cada membro valido
0006: 6@ = 0
:WA_STRIP_LOOP
092B: 2@ 6@ 5@
00D6: if
    056D: actor 5@ defined
004D: jump_if_false @WA_STRIP_NEXT
// 048F: remove todas as armas do ped
// SASCM.ini: 048F=1,actor %1d% remove_weapons
048F: actor 5@ remove_weapons
:WA_STRIP_NEXT
000A: 6@ += 1
00D6: if
    0019: 6@ > 6
004D: jump_if_false @WA_STRIP_LOOP

0ACD: show_text_highpriority "Armas retiradas do grupo. Equipa nova e usa 5." 2500
0001: wait 600 ms
0002: jump @WEAPON_MAIN_LOOP

:WA_STRIP_NO_GROUP
0ACD: show_text_highpriority "Sem membros no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP
