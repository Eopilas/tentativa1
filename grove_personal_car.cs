// ===============================================================
// grove_personal_car.cs
// CLEO 4 script independente — GTA SA (versao 1.0 US)
//
// Guarda um veiculo como carro pessoal, mantendo-o persistente
// entre sessoes de jogo (guarda/carrega estado).
//
// TECLAS:
//   P (VK 80) — Registar o veiculo actual como carro pessoal.
//               Converte o carro do mundo num carro de script
//               (00A5), preservando modelo, cores e mods instalados.
//               Guarda tudo em CLEO\grove_personal_car.dat.
//   O (VK 79) — Chamar o carro pessoal para junto do jogador.
//               Se o handle ainda for valido (mesma sessao): o
//               MESMO carro e teletransportado — nao e um clone.
//               Se for nova sessao (handle perdido apos load):
//               o carro e recriado com os atributos guardados no
//               ficheiro — modelo, cores e mods restaurados.
//
// PERSISTENCIA:
//   O carro e convertido para handle de script ao ser registado
//   (00A5 + 072A + 01C3). O motor SA nao elimina carros de script
//   por streaming enquanto o handle estiver numa variavel de script
//   e 01C3 (remove_references) nao tiver sido chamado. Isto garante
//   que o carro nao desaparece ao afastar o jogador dentro da mesma
//   sessao.
//   Entre sessoes (load/save), todos os handles sao invalidados pelo
//   motor. Ao recarregar: o script le grove_personal_car.dat e
//   recria o carro com os mesmos atributos ao chamar com 'O'.
//
// FORMATO DO FICHEIRO (grove_personal_car.dat — 100 bytes, binario):
//   Offset  0: versao (int32 = 1)
//   Offset  4: model_id (int32)
//   Offset  8: primary_color (int32)
//   Offset 12: secondary_color (int32)
//   Offset 16: last_x (float32)
//   Offset 20: last_y (float32)
//   Offset 24: last_z (float32)
//   Offset 28: last_angle (float32)
//   Offset 32-96: mods slots 0-15 (int32 cada, 0=slot vazio)
//                 96D devolve 0 se o slot nao tem componente.
//
// MODOS DE CONDUCAO:
//   O carro pessoal e criado com as mesmas configs de IA que o
//   jogador deixou quando o registou. Nao ha override de IA —
//   o carro fica parado ate o jogador entrar. Quando chamado,
//   aparece no no de estrada mais proximo do jogador.
//
// NOTA TECNICA — MODELO (SA 1.0 US):
//   SA nao tem opcode publico para ler o model_id de um car handle.
//   Usamos 0A97 (car_struct) + 0A8D (read_memory) no offset +0x22
//   (34 bytes) da estrutura CVehicle — campo CEntity::m_nModelIndex,
//   short (2 bytes). Este offset e especifico do executavel SA 1.0 US.
//   Em outros exes (1.01 EU, Steam) pode retornar valor errado.
//   Se errado: o carro registado usa o modelo devolvido (pode diferir
//   do original), mas nao causa crash.
//
// VARIAVEIS LOCAIS:
//   0@   handle do actor do jogador (01F5)
//   1@   handle do carro actual do jogador (03C0 / temp)
//   2@   handle do carro pessoal (0=sem carro na sessao)
//   3@   model_id do carro pessoal
//   4@   primary_color
//   5@   secondary_color
//   6@   last_x (float)
//   7@   last_y (float)
//   8@   last_z (float)
//   9@   last_angle (float)
//   10@  temp (struct pointer, file handle, loop counter)
//   11@  temp (mod model por slot, node coords output)
//   12@  12@ = 1 se ha dados de carro guardados (em memoria ou em ficheiro)
//   13@  y temp para nearest_path_node
//   14@  z temp para nearest_path_node
//   15@  flag de dados carregados do ficheiro (set em INIT)
// ===============================================================
{$CLEO .cs}
0000: NOP

:PERSONAL_CAR_INIT
0001: wait 2000 ms
00D6: if
    0256: player $PLAYER_CHAR defined
004D: jump_if_false @PERSONAL_CAR_INIT

0006: 2@ = 0
0006: 12@ = 0
0006: 15@ = 0

// ---------------------------------------------------------------
// Tentar carregar dados do ficheiro ao iniciar
// 0A9A: binario P1=filename_string, P2=mode(0=read), P3=→file_handle
//   Modo 0 = abre para leitura binaria ("rb"). Falha se nao existir.
//   IF and SET: se bem sucedido, a condicao e verdadeira.
// ---------------------------------------------------------------
00D6: if
    0A9A: "CLEO\grove_personal_car.dat" 0 10@
004D: jump_if_false @INIT_NO_FILE

// Verificar versao do ficheiro (4 bytes)
0A9D: 10@ 4 11@
00D6: if
    0038: 11@ == 1
004D: jump_if_false @INIT_BAD_VERSION

// Ler atributos: model, colors, position, angle
0A9D: 10@ 4 3@
0A9D: 10@ 4 4@
0A9D: 10@ 4 5@
0A9D: 10@ 4 6@
0A9D: 10@ 4 7@
0A9D: 10@ 4 8@
0A9D: 10@ 4 9@

// Ler slots de mods 0-15 — armazenados temporariamente
// numa zona de memoria do script que reusamos depois em RECREATE.
// Como nao temos arrays em CLEO, guardamos em variaveis 16@-31@.
// SA reserva 32@ e 33@ para RenderWare; usamos 16@-31@ (16 slots).
// Slot 16 (indice 16) e omitido — cobertos 0-15, suficiente para
// todos os tuning slots usados em carros padrao SA.
0A9D: 10@ 4 16@
0A9D: 10@ 4 17@
0A9D: 10@ 4 18@
0A9D: 10@ 4 19@
0A9D: 10@ 4 20@
0A9D: 10@ 4 21@
0A9D: 10@ 4 22@
0A9D: 10@ 4 23@
0A9D: 10@ 4 24@
0A9D: 10@ 4 25@
0A9D: 10@ 4 26@
0A9D: 10@ 4 27@
0A9D: 10@ 4 28@
0A9D: 10@ 4 29@
0A9D: 10@ 4 30@
0A9D: 10@ 4 31@

0A9B: 10@
0006: 12@ = 1
0006: 15@ = 1
0002: jump @INIT_SHOW_MSG

:INIT_BAD_VERSION
0A9B: 10@
0002: jump @INIT_NO_FILE

:INIT_NO_FILE

:INIT_SHOW_MSG
00D6: if
    0038: 15@ == 1
004D: jump_if_false @INIT_MSG_EMPTY
0ACD: show_text_highpriority "Grove Personal Car: P=Registar | O=Chamar (carro guardado!)" time 4000
0002: jump @PC_MAIN_LOOP
:INIT_MSG_EMPTY
0ACD: show_text_highpriority "Grove Personal Car: P=Registar veiculo | O=Chamar" time 3000

// ---------------------------------------------------------------
// LOOP PRINCIPAL — 300ms
// ---------------------------------------------------------------
:PC_MAIN_LOOP
0001: wait 300 ms

01F5: 0 0@

// Actualizacao da posicao em cache — so quando o carro existe e e valido
00D6: if
    0038: 2@ > 0
004D: jump_if_false @PC_KEY_P

00D6: if
    056E: car 2@ defined
004D: jump_if_false @PC_CAR_LOST

00AA: 2@ 6@ 7@ 8@
0174: 9@ = car 2@ Z_angle
0002: jump @PC_KEY_P

:PC_CAR_LOST
// Handle perdeu validade (carro destruido, bug de script, etc.)
// Marcamos 2@ = 0 mas mantemos 12@=1 e os atributos para poder recriar.
0006: 2@ = 0
0ACD: show_text_highpriority "Carro pessoal perdido! O para recriar." 3000

// ---------------------------------------------------------------
// Tecla P (VK 80): Registar veiculo actual como pessoal
// ---------------------------------------------------------------
:PC_KEY_P
00D6: if
    0AB0: key_pressed 80
004D: jump_if_false @PC_KEY_O

// Jogador deve estar a conduzir
00D6: if
    00DF: actor 0@ driving
004D: jump_if_false @PC_NOT_IN_CAR

// Obter handle do carro actual
03C0: 0@ 1@

// Descartar handle de carro pessoal anterior (se existir)
00D6: if
    0038: 2@ > 0
004D: jump_if_false @PC_NO_OLD_CAR
00D6: if
    056E: car 2@ defined
004D: jump_if_false @PC_NO_OLD_CAR
// Nao destruir o carro — apenas libertar referencia de script.
// Se o jogador esta a registar outro carro, o anterior fica no mundo.
01C3: remove_references_to_car 2@
:PC_NO_OLD_CAR
0006: 2@ = 0

// Ler MODEL ID via struct de memoria:
//   0A97: binario P1=car_handle, P2=→struct_ptr
//   0A8D: binario P1=addr, P2=size, P3=vp_flag, P4=→output
//   Offset 0x22 (=34) = CEntity::m_nModelIndex (short, 2 bytes)
//   SA 1.0 US: offset confirmado por multiple reverse-engineering sources.
0A97: 1@ 10@
000A: 10@ += 34
0A8D: 10@ 2 0 3@

// Sanidade: modelo valido em SA esta entre 400-611 (veiculos).
// Se fora desse intervalo (leitura de memoria falhou, exe diferente),
// abortar para evitar crash em 00A5 com modelo invalido.
// 0019: P1>P2. Para checar upper bound usamos 612>3@ (3@<612 valido).
00D6: if
    0019: 3@ > 399
004D: jump_if_false @PC_BAD_MODEL
00D6: if
    0019: 612 > 3@
004D: jump_if_false @PC_BAD_MODEL

// Ler cores
// 03F3: binario P1=car, P2=→primary, P3=→secondary
03F3: 1@ 4@ 5@

// Ler posicao e angulo
00AA: 1@ 6@ 7@ 8@
0174: 9@ = car 1@ Z_angle

// Ler mods de todos os slots 0-15
// 096D: binario P1=car, P2=slot(h), P3=→model_id (0=slot vazio)
096D: 1@ 0 16@
096D: 1@ 1 17@
096D: 1@ 2 18@
096D: 1@ 3 19@
096D: 1@ 4 20@
096D: 1@ 5 21@
096D: 1@ 6 22@
096D: 1@ 7 23@
096D: 1@ 8 24@
096D: 1@ 9 25@
096D: 1@ 10 26@
096D: 1@ 11 27@
096D: 1@ 12 28@
096D: 1@ 13 29@
096D: 1@ 14 30@
096D: 1@ 15 31@

// Criar carro de script com os mesmos atributos no mesmo local
// 00A5: binario P1=model, P2=x, P3=y, P4=z, P5=→handle
00A5: 3@ 6@ 7@ 8@ 2@
0175: set_car 2@ Z_angle_to 9@
0229: set_car 2@ primary_color_to 4@ secondary_color_to 5@

// Restaurar mods no novo carro de script
// 06E7: binario P1=car, P2=model(o), P3=→handle (descartado em 10@)
00D6: if
    0019: 16@ > 0
004D: jump_if_false @PC_MOD1
06E7: 2@ 16@ 10@
:PC_MOD1
00D6: if
    0019: 17@ > 0
004D: jump_if_false @PC_MOD2
06E7: 2@ 17@ 10@
:PC_MOD2
00D6: if
    0019: 18@ > 0
004D: jump_if_false @PC_MOD3
06E7: 2@ 18@ 10@
:PC_MOD3
00D6: if
    0019: 19@ > 0
004D: jump_if_false @PC_MOD4
06E7: 2@ 19@ 10@
:PC_MOD4
00D6: if
    0019: 20@ > 0
004D: jump_if_false @PC_MOD5
06E7: 2@ 20@ 10@
:PC_MOD5
00D6: if
    0019: 21@ > 0
004D: jump_if_false @PC_MOD6
06E7: 2@ 21@ 10@
:PC_MOD6
00D6: if
    0019: 22@ > 0
004D: jump_if_false @PC_MOD7
06E7: 2@ 22@ 10@
:PC_MOD7
00D6: if
    0019: 23@ > 0
004D: jump_if_false @PC_MOD8
06E7: 2@ 23@ 10@
:PC_MOD8
00D6: if
    0019: 24@ > 0
004D: jump_if_false @PC_MOD9
06E7: 2@ 24@ 10@
:PC_MOD9
00D6: if
    0019: 25@ > 0
004D: jump_if_false @PC_MOD10
06E7: 2@ 25@ 10@
:PC_MOD10
00D6: if
    0019: 26@ > 0
004D: jump_if_false @PC_MOD11
06E7: 2@ 26@ 10@
:PC_MOD11
00D6: if
    0019: 27@ > 0
004D: jump_if_false @PC_MOD12
06E7: 2@ 27@ 10@
:PC_MOD12
00D6: if
    0019: 28@ > 0
004D: jump_if_false @PC_MOD13
06E7: 2@ 28@ 10@
:PC_MOD13
00D6: if
    0019: 29@ > 0
004D: jump_if_false @PC_MOD14
06E7: 2@ 29@ 10@
:PC_MOD14
00D6: if
    0019: 30@ > 0
004D: jump_if_false @PC_MOD15
06E7: 2@ 30@ 10@
:PC_MOD15
00D6: if
    0019: 31@ > 0
004D: jump_if_false @PC_MODS_DONE
06E7: 2@ 31@ 10@
:PC_MODS_DONE

// Resistencia do carro pessoal: sem dano visual (como carros de missao),
// saude reposta ao maximo. Fumo continua a aparecer quando saude e baixa
// pois e controlado pelo limiar de saude, nao pelo flag de dano visual.
// 0852: sem amassados/riscos visuais. 0224: HP total (int, max ~2000).
0852: set_car 2@ damages_visible 0
0224: set_car 2@ health_to 1750

// Transferir o jogador para o novo carro de script
// 072A: binario P1=actor, P2=car
072A: 0@ 2@

// Libertar referencia ao carro original do mundo
// O carro de script (2@) e agora o carro pessoal — o original pode
// desaparecer normalmente (tornamos-o "no longer needed" para o motor).
// Nota: 01C3 em 1@ nao destroi o carro — apenas permite que o motor
// o elimine quando o streaming o requerer. O jogador ja esta em 2@.
// Se 1@ e o mesmo que 2@ (caso raro de loop), skip.
00D6: if
    0038: 1@ == 2@
004D: jump_if_false @PC_RELEASE_OLD
0002: jump @PC_SAVE_FILE
:PC_RELEASE_OLD
01C3: remove_references_to_car 1@

// Guardar no ficheiro
:PC_SAVE_FILE
0006: 12@ = 1
0002: jump @PC_DO_SAVE

:PC_NOT_IN_CAR
0ACD: show_text_highpriority "Entra num veiculo para o registar como pessoal (P)." 2500
0002: jump @PC_MAIN_LOOP

:PC_BAD_MODEL
0ACD: show_text_highpriority "Erro: modelo nao reconhecido (EXE 1.0 US necessario)." 3000
0002: jump @PC_MAIN_LOOP

// ---------------------------------------------------------------
// Tecla O (VK 79): Chamar carro pessoal ao jogador
// ---------------------------------------------------------------
:PC_KEY_O
00D6: if
    0AB0: key_pressed 79
004D: jump_if_false @PC_MAIN_LOOP

00D6: if
    0038: 12@ == 0
004D: jump_if_false @PC_CALL_OK
0ACD: show_text_highpriority "Nenhum carro pessoal registado. Usa P para registar." 2500
0002: jump @PC_MAIN_LOOP

:PC_CALL_OK
// Se o carro pessoal ja existe nesta sessao (mesmo handle): teletransportar
00D6: if
    0038: 2@ > 0
004D: jump_if_false @PC_RECREATE
00D6: if
    056E: car 2@ defined
004D: jump_if_false @PC_RECREATE

// Carro valido — teletransportar o MESMO carro para junto do jogador
// Procurar no de estrada mais proximo do jogador
// 04D3: binario P1=x, P2=y, P3=z, P4=type(0=any), P5=→x, P6=→y, P7=→z
00A0: 0@ 10@ 11@ 14@
04D3: 10@ 11@ 14@ 0 10@ 11@ 14@
000B: 10@ += 5.0
00AB: 2@ 10@ 11@ 14@
0175: set_car 2@ Z_angle_to 9@
0ACD: show_text_highpriority "Carro pessoal chegou!" 2000
0001: wait 600 ms
0002: jump @PC_MAIN_LOOP

// Nova sessao (handle invalido) — recriar do ficheiro
:PC_RECREATE
00D6: if
    0019: 3@ > 0
004D: jump_if_false @PC_NO_DATA

// Criar novo carro de script com dados guardados
00A0: 0@ 10@ 11@ 14@
04D3: 10@ 11@ 14@ 0 10@ 11@ 14@
000B: 10@ += 5.0
00A5: 3@ 10@ 11@ 14@ 2@
0175: set_car 2@ Z_angle_to 9@
0229: set_car 2@ primary_color_to 4@ secondary_color_to 5@

// Restaurar mods
00D6: if
    0019: 16@ > 0
004D: jump_if_false @PC_RC_MOD1
06E7: 2@ 16@ 10@
:PC_RC_MOD1
00D6: if
    0019: 17@ > 0
004D: jump_if_false @PC_RC_MOD2
06E7: 2@ 17@ 10@
:PC_RC_MOD2
00D6: if
    0019: 18@ > 0
004D: jump_if_false @PC_RC_MOD3
06E7: 2@ 18@ 10@
:PC_RC_MOD3
00D6: if
    0019: 19@ > 0
004D: jump_if_false @PC_RC_MOD4
06E7: 2@ 19@ 10@
:PC_RC_MOD4
00D6: if
    0019: 20@ > 0
004D: jump_if_false @PC_RC_MOD5
06E7: 2@ 20@ 10@
:PC_RC_MOD5
00D6: if
    0019: 21@ > 0
004D: jump_if_false @PC_RC_MOD6
06E7: 2@ 21@ 10@
:PC_RC_MOD6
00D6: if
    0019: 22@ > 0
004D: jump_if_false @PC_RC_MOD7
06E7: 2@ 22@ 10@
:PC_RC_MOD7
00D6: if
    0019: 23@ > 0
004D: jump_if_false @PC_RC_MOD8
06E7: 2@ 23@ 10@
:PC_RC_MOD8
00D6: if
    0019: 24@ > 0
004D: jump_if_false @PC_RC_MOD9
06E7: 2@ 24@ 10@
:PC_RC_MOD9
00D6: if
    0019: 25@ > 0
004D: jump_if_false @PC_RC_MOD10
06E7: 2@ 25@ 10@
:PC_RC_MOD10
00D6: if
    0019: 26@ > 0
004D: jump_if_false @PC_RC_MOD11
06E7: 2@ 26@ 10@
:PC_RC_MOD11
00D6: if
    0019: 27@ > 0
004D: jump_if_false @PC_RC_MOD12
06E7: 2@ 27@ 10@
:PC_RC_MOD12
00D6: if
    0019: 28@ > 0
004D: jump_if_false @PC_RC_MOD13
06E7: 2@ 28@ 10@
:PC_RC_MOD13
00D6: if
    0019: 29@ > 0
004D: jump_if_false @PC_RC_MOD14
06E7: 2@ 29@ 10@
:PC_RC_MOD14
00D6: if
    0019: 30@ > 0
004D: jump_if_false @PC_RC_MOD15
06E7: 2@ 30@ 10@
:PC_RC_MOD15
00D6: if
    0019: 31@ > 0
004D: jump_if_false @PC_RC_MODS_DONE
06E7: 2@ 31@ 10@
:PC_RC_MODS_DONE

// Resistencia do carro pessoal recriado: sem dano visual, saude reposta.
0852: set_car 2@ damages_visible 0
0224: set_car 2@ health_to 1750

// Actualizar posicao em cache para o novo handle
00AA: 2@ 6@ 7@ 8@
0174: 9@ = car 2@ Z_angle

0ACD: show_text_highpriority "Carro pessoal recriado e chamado! (P para re-registar)" 3000
0001: wait 600 ms
0002: jump @PC_MAIN_LOOP

:PC_NO_DATA
0ACD: show_text_highpriority "Dados do carro perdidos. Usa P para registar novamente." 2500
0006: 12@ = 0
0002: jump @PC_MAIN_LOOP

// ---------------------------------------------------------------
// Guardar dados no ficheiro (chamado apos registo com P)
//
// 0A9A: modo 1 = abre para escrita binaria ("wb"), cria se nao existir.
// 0A9E: binario P1=file_handle, P2=size_in_bytes, P3=source_var.
//   Escreve 4 bytes do valor da variavel.
// ---------------------------------------------------------------
:PC_DO_SAVE
00D6: if
    0A9A: "CLEO\grove_personal_car.dat" 1 10@
004D: jump_if_false @PC_SAVE_FAIL

0006: 11@ = 1
0A9E: 10@ 4 11@
0A9E: 10@ 4 3@
0A9E: 10@ 4 4@
0A9E: 10@ 4 5@
0A9E: 10@ 4 6@
0A9E: 10@ 4 7@
0A9E: 10@ 4 8@
0A9E: 10@ 4 9@
0A9E: 10@ 4 16@
0A9E: 10@ 4 17@
0A9E: 10@ 4 18@
0A9E: 10@ 4 19@
0A9E: 10@ 4 20@
0A9E: 10@ 4 21@
0A9E: 10@ 4 22@
0A9E: 10@ 4 23@
0A9E: 10@ 4 24@
0A9E: 10@ 4 25@
0A9E: 10@ 4 26@
0A9E: 10@ 4 27@
0A9E: 10@ 4 28@
0A9E: 10@ 4 29@
0A9E: 10@ 4 30@
0A9E: 10@ 4 31@
0A9B: 10@

0ACD: show_text_highpriority "Carro pessoal registado! O=Chamar | P=Re-registar" 3000
0001: wait 600 ms
0002: jump @PC_MAIN_LOOP

:PC_SAVE_FAIL
0ACD: show_text_highpriority "Erro ao guardar carro pessoal. Pasta CLEO acessivel?" 3000
0002: jump @PC_MAIN_LOOP
