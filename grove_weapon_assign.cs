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
//   7 (VK 55) — Ciclar slot seleccionado (0-6). Salta slots vazios.
//               HUD mostra qual membro esta seleccionado.
//   8 (VK 56) — Dar a arma ACTUAL do jogador APENAS ao membro no
//               slot seleccionado (7@). Permite dar armas diferentes
//               a cada membro do grupo individualmente.
//   9 (VK 57) — Retirar TODAS as armas APENAS do membro no slot
//               seleccionado (7@). Complemento de 8.
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
//   087E define droppable_flag 0 — o membro nao larga a arma ao morrer.
//
// CADEIA DE TECLAS (WEAPON_MAIN_LOOP):
//   5 → se nao pressionado → CHECK_KEY6
//   6 → se nao pressionado → CHECK_KEY7
//   7 → se nao pressionado → CHECK_KEY8
//   8 → se nao pressionado → CHECK_KEY9
//   9 → se nao pressionado → WEAPON_MAIN_LOOP
//   Cada handler aponta para o PROXIMO, nao para o inicio do loop.
//   Isto garante que todas as teclas sao testadas em cada iteracao
//   de 300ms sem re-entrar no loop prematuramente.
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
//   7@  slot seleccionado para operacoes individuais (teclas 7/8/9)
// ===============================================================
{$CLEO .cs}
0000: NOP
03A4: name_thread 'GWEAPON'

:WEAPON_ASSIGN_INIT
0001: wait 2000 ms
00D6: if
    0256: player $PLAYER_CHAR defined
004D: jump_if_false @WEAPON_ASSIGN_INIT

0006: 7@ = 0

0ACD: show_text_highpriority "Grove Weapon Assign: 5=Todos|6=Retirar|7=Ciclar|8=Dar slot|9=Retirar slot" time 4000

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
// BUG FIX: apontar para WA_CHECK_KEY7, nao WEAPON_MAIN_LOOP.
// Se apontar para WEAPON_MAIN_LOOP, as teclas 7/8/9 nunca sao testadas.
004D: jump_if_false @WA_CHECK_KEY7

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

// ---------------------------------------------------------------
// Tecla 7 (VK 55): ciclar slot seleccionado (salta slots vazios)
// ---------------------------------------------------------------
:WA_CHECK_KEY7
00D6: if
    0AB0: key_pressed 55
004D: jump_if_false @WA_CHECK_KEY8

07AF: 0 2@
00D6: if
    0019: 2@ > 0
004D: jump_if_false @WA_CYCLE_NO_GROUP

// Cicla 7@ de 0 a 6, saltando slots sem membro valido
// Faz no maximo 7 tentativas (tamanho maximo do grupo)
0006: 6@ = 0
:WA_CYCLE_LOOP
000A: 7@ += 1
00D6: if
    0019: 7@ > 6
004D: jump_if_false @WA_CYCLE_CHECK_SLOT
0006: 7@ = 0
:WA_CYCLE_CHECK_SLOT
092B: 2@ 7@ 5@
00D6: if
    056D: actor 5@ defined
004D: jump_if_false @WA_CYCLE_NEXT
// Slot valido encontrado — mostrar numero exacto via branch condicional
// (0ACD nao suporta interpolacao de variaveis)
00D6: if
    0038: 7@ == 0
004D: jump_if_false @WA_CYC_SHOW1
0ACD: show_text_highpriority "Membro selec: slot 0 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW1
00D6: if
    0038: 7@ == 1
004D: jump_if_false @WA_CYC_SHOW2
0ACD: show_text_highpriority "Membro selec: slot 1 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW2
00D6: if
    0038: 7@ == 2
004D: jump_if_false @WA_CYC_SHOW3
0ACD: show_text_highpriority "Membro selec: slot 2 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW3
00D6: if
    0038: 7@ == 3
004D: jump_if_false @WA_CYC_SHOW4
0ACD: show_text_highpriority "Membro selec: slot 3 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW4
00D6: if
    0038: 7@ == 4
004D: jump_if_false @WA_CYC_SHOW5
0ACD: show_text_highpriority "Membro selec: slot 4 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW5
00D6: if
    0038: 7@ == 5
004D: jump_if_false @WA_CYC_SHOW6
0ACD: show_text_highpriority "Membro selec: slot 5 (8=dar arma | 9=retirar)" 2500
0002: jump @WA_CYCLE_MSG_DONE
:WA_CYC_SHOW6
0ACD: show_text_highpriority "Membro selec: slot 6 (8=dar arma | 9=retirar)" 2500
:WA_CYCLE_MSG_DONE
0001: wait 500 ms
0002: jump @WEAPON_MAIN_LOOP
:WA_CYCLE_NEXT
000A: 6@ += 1
00D6: if
    0019: 6@ > 6
004D: jump_if_false @WA_CYCLE_LOOP
// Nenhum membro valido encontrado
0006: 7@ = 0
0ACD: show_text_highpriority "Nenhum membro valido no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP

:WA_CYCLE_NO_GROUP
0ACD: show_text_highpriority "Sem membros no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP

// ---------------------------------------------------------------
// Tecla 8 (VK 56): dar arma actual do jogador ao slot seleccionado (7@)
// ---------------------------------------------------------------
:WA_CHECK_KEY8
00D6: if
    0AB0: key_pressed 56
004D: jump_if_false @WA_CHECK_KEY9

07AF: 0 2@
00D6: if
    0019: 2@ > 0
004D: jump_if_false @WA_SINGLE_NO_GROUP

// Obter membro do slot seleccionado
092B: 2@ 7@ 5@
00D6: if
    056D: actor 5@ defined
004D: jump_if_false @WA_SINGLE_EMPTY

// Obter arma actual do jogador
0470: 0@ 1@
00D6: if
    0019: 1@ > 0
004D: jump_if_false @WA_SINGLE_NO_WEAPON

01B2: give_actor 5@ weapon 1@ ammo 300
087E: set_actor 5@ weapon_droppable 0
0ACD: show_text_highpriority "Arma entregue ao membro seleccionado! (7 para ciclar)" 2500
0001: wait 600 ms
0002: jump @WEAPON_MAIN_LOOP

:WA_SINGLE_EMPTY
0ACD: show_text_highpriority "Slot seleccionado vazio. Prima 7 para ciclar." 2000
0002: jump @WEAPON_MAIN_LOOP

:WA_SINGLE_NO_WEAPON
0ACD: show_text_highpriority "Equipa uma arma antes de usar 8." 2000
0002: jump @WEAPON_MAIN_LOOP

:WA_SINGLE_NO_GROUP
0ACD: show_text_highpriority "Sem membros no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP

// ---------------------------------------------------------------
// Tecla 9 (VK 57): retirar armas do slot seleccionado (7@) apenas
// ---------------------------------------------------------------
:WA_CHECK_KEY9
00D6: if
    0AB0: key_pressed 57
004D: jump_if_false @WEAPON_MAIN_LOOP

07AF: 0 2@
00D6: if
    0019: 2@ > 0
004D: jump_if_false @WA_STRIP_ONE_NO_GROUP

092B: 2@ 7@ 5@
00D6: if
    056D: actor 5@ defined
004D: jump_if_false @WA_STRIP_ONE_EMPTY

048F: actor 5@ remove_weapons
0ACD: show_text_highpriority "Armas retiradas do membro seleccionado. (7 para ciclar)" 2500
0001: wait 600 ms
0002: jump @WEAPON_MAIN_LOOP

:WA_STRIP_ONE_EMPTY
0ACD: show_text_highpriority "Slot seleccionado vazio. Prima 7 para ciclar." 2000
0002: jump @WEAPON_MAIN_LOOP

:WA_STRIP_ONE_NO_GROUP
0ACD: show_text_highpriority "Sem membros no grupo." 2000
0002: jump @WEAPON_MAIN_LOOP
