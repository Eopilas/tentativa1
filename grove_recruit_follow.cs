{$CLEO .cs}

// =============================================================
// GROVE STREET RECRUIT — VEHICLE AI FOLLOWER
// Versão: 1.0
//
// Compilar com: Sanny Builder 3 em modo "GTA San Andreas"
//   https://sannybuilder.com
//
// Requer: CLEO 4 ou superior
//   https://cleo.li
//
// ---------------------------------------------------------------
// FONTES E REFERENCIAS
// ---------------------------------------------------------------
// - Sanny Builder Library (documentacao completa de opcodes SA):
//     https://library.sannybuilder.com/
// - GTAMods Wiki — List of opcodes:
//     https://gtamods.com/wiki/List_of_opcodes
// - ThirteenAG/III.VC.SA.CLEOScripts (exemplos praticos de AS packs,
//   follow_car, driver_behaviour):
//     https://github.com/ThirteenAG/III.VC.SA.CLEOScripts
// - yugecin/scmcleoscripts (referencias de IA veicular avancada):
//     https://github.com/yugecin/scmcleoscripts
// - MTA Wiki — Ped Models e Car Model IDs:
//     https://wiki.multitheftauto.com/wiki/Ped_Models
// - Project Cerbera — GTA SA Handling (velocidades e fisicas):
//     https://projectcerbera.com/gta/sa/tutorials/handling
//
// ---------------------------------------------------------------
// ARQUITETURA — 3 MODULOS
// ---------------------------------------------------------------
// Modulo 1 — DETECCAO DE ESTADO DO JOGADOR
//   O loop principal (300ms) verifica continuamente se o jogador
//   esta a pe ou em veiculo. O estado do recruta (12@) muda de
//   acordo, alternando entre follow_actor e as rotinas de IA
//   veicular (follow_car / drive_to).
//
// Modulo 2 — AQUISICAO DE VEICULO
//   Ao pressionar U, o recruta:
//   1. Localiza o carro mais proximo via 0AB5 (store_closest_entities).
//   2. Valida que nao e o veiculo do jogador.
//   3. Executa entrada como motorista via 05CB (task_enter_car_as_driver).
//   4. Aguarda confirmacao de que esta dirigindo (00DF) com
//      timeout de 5 segundos para evitar travamento.
//
// Modulo 3 — ESTILOS DE CONDUCAO (traffic_behaviour — 00AE)
//   Valor 0 (STOPFORCARS)          — para em semaforos/transito
//   Valor 1 (SLOWDOWNFORCARS)      — desacelera perto de carros
//   Valor 2 (AVOIDCARS)            — ignora semaforo, desvia carros
//   Valor 3 (PLOUGHTHROUGH)        — ignora tudo (nao usar p/ aliados)
//   Valor 5 (FOLLOWTRAFFIC_AVOIDCARS) — respeita sinal + desvia lentidao
//   >>> Este mod usa modo 2 (seguimento) e modo 1 (aproximacao a pe).
//   >>> Para situacao mais realista, substituir modo 2 por modo 5.
//
// Nota — erro 0097 (parameter type mismatch):
//   Todos os handles de ped/carro sao inteiros; coordenadas sao
//   float. A tipagem incorreta ou ordem errada de parametros causa
//   o erro 0097 na VM do RenderWare. Atencao especial aos opcodes
//   04C4 (float offset), 00AD (float speed) e 07F8 (float radius).
//
// ---------------------------------------------------------------
// MAQUINA DE ESTADOS (variavel 12@)
// ---------------------------------------------------------------
//   12@ = 0  Nenhum recruta ativo
//   12@ = 1  Recruta a pe  -> seguimento via 0850: task_follow_footsteps
//   12@ = 2  Recruta em veiculo -> 07F8: follow_car / 00A7: drive_to
//   12@ = 3  Recruta dirige, jogador passageiro (05CA) -> 0407+00A7 navega a frente
//
//   Transicoes:
//     0 --(Y pressionado)--> 1
//     0 --(vanilla scan: grupo nativo tem membro)--> 1 (adocao)
//     1 --(U + carro encontrado + entrou)--> 2
//     2 --(U pressionado)--> 1 (exit_car recruta, 0633)
//     2 --(G + jogador entra passageiro, 05CA)--> 3
//     2 --(carro destruido / recruta perdeu veiculo)--> 1
//     3 --(G ou jogador sai voluntariamente)--> 2
//     3 --(carro destruido)--> 1
//     1, 2, ou 3 --(recruta morreu)--> CLEANUP -> 0
//
// ---------------------------------------------------------------
// VARIAVEIS LOCAIS
// ---------------------------------------------------------------
//    3@          Handle do ped do jogador — obtido via 01F5 a cada iteração do loop.
//               NÃO usar $PLAYER_ACTOR em CLEO externo (índice global não mapeado = 0).
//    0@- 2@   Coords de spawn calculadas com offset do jogador
//    6@- 8@   Coords temporarias do jogador (para drive_to)
//   10@        Handle do recruta (ped) — obrigatorio checar com 056D
//   11@        Handle do veiculo do recruta — checar com 056E
//   12@        Estado da maquina de estados (ver acima)
//   13@        Handle temporario para ped mais proximo (output descartado de 0AB5)
//   16@        Contador de timeout para entrada no veiculo (FIND_VEHICLE)
//   20@        Handle do Action Sequence Pack (liberado com 061B)
//   21@        ID do modelo: 105=fam1 | 106=fam2 | 107=fam3
//   22@        Handle do veiculo do jogador (extraido com 03C0)
//   23@        Flag de origem do recruta:
//              0 = spawnado pelo mod (01C2 deve ser chamado no cleanup)
//              1 = recruta vanilla adotado (ped pertence ao motor — nunca chamar 01C2)
//   24@        Handle do grupo do jogador (output de 07AF — usado em VANILLA_SCAN e SPAWN_RECRUIT)
//   25@        Contagem de lideres do grupo (output de 07F6, descartado)
//   26@        Contagem de membros do grupo (output de 07F6, gate de deteccao)
//   27@        Handle do ped candidato a adocao (output de 092B slot 0, temp)
//   28@        Contador de timeout para entrada do JOGADOR no carro (CHECK_KEY_G)
//
// NOTA: Variaveis 32@ e 33@ sao RESERVADAS pelo motor RenderWare
//       para timers internos. Nunca usar 32@ ou 33@.
//
// ---------------------------------------------------------------
// GESTAO DE MEMORIA
// ---------------------------------------------------------------
//   01C2: mark_actor_as_no_longer_needed — remove ped dos pools
//   01C3: mark_car_as_no_longer_needed   — remove veiculo dos pools
//   Estas operacoes NAO destroem as entidades imediatamente;
//   transferem a responsabilidade ao motor, que fara despawn natural
//   por distancia. Essencial para nao saturar os pools do jogo
//   (limite: ~110 peds, ~110 veiculos simultaneos).
//
// ---------------------------------------------------------------
// EXTENSIBILIDADE FUTURA
// ---------------------------------------------------------------
//   Este script foca em 1 recruta spawnado manualmente. A logica
//   pode ser expandida para cobrir TODOS os recrutas normais do jogo
//   interceptando o recrutamento nativo via leitura da Ped Pool
//   (endereco 0xB74494) e verificando flags de grupo/faccao com
//   0A8D (read_memory). Cada recruta recrutado receberia um slot
//   neste sistema de IA veicular avancada.
// =============================================================

0000: NOP
03A4: name_thread 'GRECRUIT'

// $PLAYER_CHAR  = índice do jogador (player 0) — usado apenas em 0256 (player defined).
// $PLAYER_ACTOR NÃO é mapeado de forma confiável em scripts CLEO externos:
//   SB3 aloca variáveis globais nomeadas em índices livres, não nos índices do
//   main.scm (onde global[1] = handle do ped do jogador). Em scripts externos o
//   índice alocado para $PLAYER_ACTOR nunca é inicializado → valor = 0 → crash.
//   SOLUÇÃO: usar 01F5 para obter o handle correto em local var 3@ a cada iteração.

// ---------------------------------------------------------------
// FASE 1: Aguarda o jogo carregar completamente
//
// Sem esta espera, operacoes sobre $PLAYER_CHAR antes de estar
// definido causam crash imediato (segmentation fault na VM).
// O loop de 0256 garante que o jogador existe antes de qualquer
// acao. Ref: Sanny Builder Library — opcode 0256.
// ---------------------------------------------------------------
:INIT
0001: wait 2000 ms
00D6: if
    0256: player $PLAYER_CHAR defined
004D: jump_if_false @INIT

// Inicializa handles e estado zerados
0006: 10@ = 0
0006: 11@ = 0
0006: 12@ = 0
0006: 23@ = 0
0006: 28@ = 0

0ACD: show_text_highpriority "Grove Recruit Mod: Y=Spawnar | U=Veiculo | G=Recruta dirige" time 4000

// ===============================================================
// LOOP PRINCIPAL
//
// Intervalo de 300ms: balanceia responsividade e uso de CPU.
// O opcode 0001:wait e OBRIGATORIO em qualquer loop — sem ele a
// maquina virtual do RenderWare atinge o timeout de thread e
// mata o script inteiro.
// ===============================================================
:MAIN_LOOP
0001: wait 300 ms

// Revalida existencia do jogador a cada iteracao
// (morte, carregamento de save ou mission failed reiniciam o estado)
00D6: if
    0256: player $PLAYER_CHAR defined
004D: jump_if_false @INIT

// Obtém handle do ped do jogador a cada iteração.
// $PLAYER_ACTOR (var global nomeada) é alocada pelo SB3 em índice
// livre que nunca é inicializado em CLEO externo → valor = 0 → crash.
// 01F5 binário: param1=player_num (0), param2=→output handle (3@).
// Equivale a: 3@ = GetPlayerChar(0)
01F5: 0 3@

// ---------------------------------------------------------------
// DETECCAO AUTOMATICA DE MORTE DO RECRUTA
//
// Quando 12@ > 0 mas o ped em 10@ nao existe mais (morte por dano,
// despawn por distancia, reset de missao), o modulo de cleanup e
// acionado AUTOMATICAMENTE — sem exigir que o jogador aperte U.
//
// Corrige o bug onde o recruta morria enquanto a pe (12@==1) e o
// script ficava preso: 12@!=0 impedia novo Y, e como U nao era
// pressionado, o check de "actor defined" nunca era atingido.
//
// 056D: actor_defined = true se o ped existe e esta vivo.
// ---------------------------------------------------------------
00D6: if
    0019: 12@ > 0
004D: jump_if_false @VANILLA_SCAN
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT

// ---------------------------------------------------------------
// DETECCAO DE RECRUTAS VANILLA (apenas quando nenhum recruta ativo)
//
// Quando 12@==0, verifica se o jogador ja tem membros no grupo
// nativo (recrutamento vanilla com apontamento de arma).
// Se sim, adota o primeiro membro como recruta do mod usando o
// handle direto — sem interferir na IA de grupo do motor.
//
// 07AF: P1=player_num, P2=→group_handle. Output ULTIMO (SB3 positional).
// 07F6: P1=group, P2=→leaders, P3=→members. Outputs ULTIMOS.
// 092B: P1=group, P2=slot_index(0), P3=→ped_handle. Output ULTIMO.
//   Busca por slot e mais confiavel que 073F (espacial) + 06EE (filiacao):
//   nao depende de raio, pedtype ou posicao — acesso direto ao CPedGroup.
// ---------------------------------------------------------------
:VANILLA_SCAN
00D6: if
    0038: 12@ == 0
004D: jump_if_false @VANILLA_SCAN_DONE
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @VANILLA_SCAN_DONE
07F6: 24@ 25@ 26@
00D6: if
    0019: 26@ > 0
004D: jump_if_false @VANILLA_SCAN_DONE
// 092B: get_group_member por slot — mais confiavel que busca espacial 073F.
// SASCM.ini: 092B=3,%3d% = group %1d% member %2d%
// Positional (SB3 left-to-right): P1=group_handle, P2=slot_index, P3=→output_handle.
// Slot 0 = primeiro membro recrutado (o mais recente em grupos pequenos).
// Nao depende de coordenadas nem flags de pedtype — retorna handle direto.
092B: 24@ 0 27@
00D6: if
    056D: actor 27@ defined
004D: jump_if_false @VANILLA_SCAN_DONE
// Recruta vanilla confirmado — adotar sem sobrescrever IA de grupo nativa.
// 23@=1 impede 01C2 no cleanup (ped pertence ao motor, nao ao mod).
0006: 10@ = 27@
0006: 23@ = 1
0006: 12@ = 1
0ACD: show_text_highpriority "Recruta vanilla adotado! Aperte U para dar veiculo." 3000
:VANILLA_SCAN_DONE

// ---------------------------------------------------------------
// MODULO 1 — TECLA Y (VK_Y = 89): Spawn do recruta a pe
//
// 0AB0: key_pressed usa VK codes do Windows (CLEO 4+).
// Alternativa para controle/joypad: 00E1: player 0 pressed_key X
// So permite spawn se nenhum recruta estiver ativo (12@ == 0).
// ---------------------------------------------------------------
00D6: if
    0AB0: key_pressed 89
004D: jump_if_false @CHECK_KEY_U

00D6: if
    0038: 12@ == 0
004D: jump_if_false @CHECK_KEY_U

0002: jump @SPAWN_RECRUIT

// ---------------------------------------------------------------
// MODULO 2 — TECLA U (VK_U = 85): Toggle veiculo do recruta
//
// Comportamento DUAL conforme estado atual (12@):
//   12@ == 1 (a pe)        → recruta busca veiculo desocupado (FIND_VEHICLE)
//   12@ == 2 (em veiculo)  → recruta sai do veiculo (0633: exit_car)
//   12@ == 3 (passageiro)  → ignorado (usar G para sair)
//
// 056D confirma handle valido antes de qualquer operacao —
// operar sobre ped morto/invalido causa crash imediato na VM.
// ---------------------------------------------------------------
:CHECK_KEY_U
00D6: if
    0AB0: key_pressed 85
004D: jump_if_false @CHECK_KEY_G

// CASO A — recruta EM VEICULO: U faz ele sair
00D6: if
    0038: 12@ == 2
004D: jump_if_false @U_FIND_VEHICLE
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT
// 0633: AS_actor exit_car — tarefa nativa de saida com animacao completa.
// param1=ped handle. O motor gerencia a animacao de abrir porta e descer.
// Ref: SASCM.ini — 0633=1,AS_actor %1d% exit_car
0633: AS_actor 10@ exit_car
0006: 11@ = 0
0006: 12@ = 1
0ACD: show_text_highpriority "Recruta saindo do veiculo! Seguindo a pe..." 2500
0001: wait 800 ms
// Re-ativa follow_actor apenas para recrutas do mod (23@==0).
// Recrutas vanilla tem IA de grupo nativa que assume o seguimento.
00D6: if
    0038: 23@ == 0
004D: jump_if_false @U_EXIT_DONE
0850: AS_actor 10@ follow_actor 3@
:U_EXIT_DONE
0002: jump @MAIN_LOOP

// CASO B — recruta A PE: U faz ele buscar veiculo
:U_FIND_VEHICLE
00D6: if
    0038: 12@ == 1
004D: jump_if_false @CHECK_KEY_G
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT
0002: jump @FIND_VEHICLE

// ---------------------------------------------------------------
// MODULO 3 — TECLA G (VK_G = 71): Recruta dirige o jogador
//
// 12@==2 → jogador entra no carro do recruta como passageiro.
//   Recruta navega automaticamente usando 0407 (offset 50m a frente)
//   + 00A7 a cada loop — vai em frente desviando de obstaculos.
//   Pressionar G de novo (12@==3) ejecta o jogador (0633 no player).
//
// 05CA: AS_actor P1 enter_car P2 time P3 seat P4
//   SASCM: 05CA=4,AS_actor %1d% enter_car %2d% passenger_seat %4h% time %3d%
//   Compilacao SB3 left-to-right: P1=actor, P2=car, P3=time, P4=seat.
//   Display template exibe P4 antes de P3 (apenas visual).
//   Source: 05CA: AS_actor 3@ enter_car 11@ time -1 passenger_seat 0
//           → P1=3@, P2=11@, P3=-1 (sem timeout), P4=0 (banco dianteiro)
//
// 0407: store_coords_to from_car with_offset
//   SASCM: 0407=7,store_coords_to %5d% %6d% %7d% from_car %1d% with_offset %2d% %3d% %4d%
//   Binary: P1=car, P2=offset_x, P3=offset_y, P4=offset_z, P5→x, P6→y, P7→z
//   No SA, eixo +Y em espaco local do carro = frente do veiculo.
//   0407: 11@ 0.0 50.0 0.0 6@ 7@ 8@ → ponto 50m a frente do carro ✓
//
// 0449: actor in_a_car — verifica se jogador ainda esta num veiculo.
// ---------------------------------------------------------------
:CHECK_KEY_G
00D6: if
    0AB0: key_pressed 71
004D: jump_if_false @FOLLOW_LOGIC

// CASO A: 12@==2, recruta em veiculo → jogador entra como passageiro
00D6: if
    0038: 12@ == 2
004D: jump_if_false @G_EXIT_CAR
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT
00D6: if
    056E: car 11@ defined
004D: jump_if_false @G_LOST_CAR
// Ignora se jogador ja estiver em algum veiculo
00D6: if
    0449: actor 3@ in_a_car
004D: jump_if_false @G_DO_ENTER_CAR
0002: jump @MAIN_LOOP

:G_DO_ENTER_CAR
// 05CA: positional P1=player(3@), P2=car(11@), P3=time(-1), P4=seat(0)
05CA: AS_actor 3@ enter_car 11@ time -1 passenger_seat 0
// Aguardar jogador entrar — timeout 5s (10 x 500ms)
0006: 28@ = 0
:G_WAIT_ENTER
0001: wait 500 ms
000A: 28@ += 1
00D6: if
    0449: actor 3@ in_a_car
004D: jump_if_false @G_CHECK_TIMEOUT
// Entrou! Ativa modo passageiro
0006: 12@ = 3
0ACD: show_text_highpriority "Recruta te carregando! G para sair." 3000
0002: jump @MAIN_LOOP
:G_CHECK_TIMEOUT
00D6: if
    0019: 28@ > 10
004D: jump_if_false @G_WAIT_ENTER
// Timeout: jogador nao entrou
0002: jump @MAIN_LOOP

// CASO B: 12@==3, jogador passageiro → sai do carro do recruta
:G_EXIT_CAR
00D6: if
    0038: 12@ == 3
004D: jump_if_false @FOLLOW_LOGIC
// 0633 no player: sai do carro com animacao nativa
0633: AS_actor 3@ exit_car
0006: 12@ = 2
0ACD: show_text_highpriority "Saiu do carro. Recruta voltando a seguir." 2500
0001: wait 800 ms
0002: jump @MAIN_LOOP

// Carro perdido ao tentar entrar — volta a estado a pe
:G_LOST_CAR
0006: 11@ = 0
0006: 12@ = 1
00D6: if
    0038: 23@ == 0
004D: jump_if_false @G_LOST_CAR_DONE
0850: AS_actor 10@ follow_actor 3@
:G_LOST_CAR_DONE
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// MODULO 1 — LOGICA DE SEGUIMENTO VEICULAR (estados 2 e 3)
//
// Estado 2: recruta em veiculo segue o jogador.
// Estado 3: jogador e passageiro, recruta navega a frente (0407+00A7).
//
// 056D/056E verificam validade dos handles antes de operar;
// handles invalidos (ped morto, carro destruido) causam crash.
// 00DF verifica se o $PLAYER_ACTOR esta efetivamente dirigindo
// antes de chamar 03C0 — 03C0 so deve ser chamado apos 00DF.
// ---------------------------------------------------------------
:FOLLOW_LOGIC
00D6: if
    0019: 12@ > 1
004D: jump_if_false @MAIN_LOOP

00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT

// 056E: car defined — true se handle e valido e carro nao destruido
// Ref: Sanny Builder Library — opcode 056E
00D6: if
    056E: car 11@ defined
004D: jump_if_false @RECRUIT_LOST_CAR

// ---------------------------------------------------------------
// ESTADO 3: recruta dirige, jogador e passageiro
// ---------------------------------------------------------------
00D6: if
    0038: 12@ == 3
004D: jump_if_false @STATE2_FOLLOW

// Verifica se jogador ainda esta no veiculo (0449: actor in_a_car)
// Se saiu voluntariamente (F no SA), volta ao estado 2 automaticamente.
00D6: if
    0449: actor 3@ in_a_car
004D: jump_if_false @PLAYER_LEFT_RECRUIT_CAR

// Navega 50m a frente em espaco local do carro.
// 0407: P1=car, P2=offset_x(0), P3=offset_y(50=frente +Y), P4=offset_z(0)
// P5..P7: coordenadas de saida em espaco mundo → usadas em 00A7.
// +Y no espaco local do SA = direcao da frente do veiculo.
0407: 11@ 0.0 50.0 0.0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 30.0
00AE: set_car 11@ traffic_behaviour_to 2
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP

:PLAYER_LEFT_RECRUIT_CAR
// Jogador saiu voluntariamente (ex: pressionou F) — volta ao modo seguimento
0006: 12@ = 2
0ACD: show_text_highpriority "Voltando ao modo seguimento. G para recruta dirigir." 2500
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// ESTADO 2: recruta em veiculo segue o jogador
// ---------------------------------------------------------------
:STATE2_FOLLOW
// Zera 22@ antes de tentar obter carro do jogador
// (garante valor limpo se jogador estiver a pe)
0006: 22@ = 0
00D6: if
    00DF: actor 3@ driving
004D: jump_if_false @FOLLOW_PLAYER_ON_FOOT
// Armazena em 22@ o handle do veículo que o jogador está dirigindo.
// 3@ = player actor handle (obtido via 01F5 no topo do MAIN_LOOP).
03C0: 3@ 22@

// JOGADOR EM VEICULO:
// 07F8: car follow_car radius — IA de perseguicao dinamica nativa.
// O motor RenderWare calcula e recalcula rotas em tempo real,
// contorna obstaculos, respeita o raio de seguranca e mantem
// o recruta na mesma faixa que o jogador sempre que possivel.
// Muito mais estavel que drive_to para alvos moveis: follow_car
// rastreia o handle dinamico, nao uma coordenada estatica.
// Raio 10m: distancia ideal — nem colide, nem perde o jogador.
// Ref: GTAMods Wiki — opcode 07F8
// Ref: ThirteenAG/III.VC.SA.CLEOScripts (exemplos de follow_car)
00AD: set_car 11@ max_speed_to 50.0
00AE: set_car 11@ traffic_behaviour_to 2
07F8: car 11@ follow_car 22@ radius 10.0
0002: jump @MAIN_LOOP

// JOGADOR A PE — Zona de seguranca anti-atropelamento (3 niveis)
//
// Problema original: drive_to na posicao exata do jogador fazia o
// carro seguir ate colisao, atropelando o recruta a qualquer velocidade.
//
// Solucao em tres zonas concentricas (00F2: actor near_actor rx ry sphere):
//   < 4m  (STOP)     → max_speed 0.0: para completamente, sem inertia residual.
//   4m-12m (CREEP)   → max_speed 5.0: abordagem lenta, sem emitir novo drive_to.
//   >= 12m (CHASE)   → drive_to a 20 km/h, traffic_behaviour 1 (desacelera p/ obs).
//
// 00F2: 5 params posicionais — actor1, actor2, radius_x, radius_y, sphere_flag(0=circulo)
// Funciona mesmo quando o recruta esta DENTRO do carro: a posicao
// do ator e a posicao do veiculo quando ele esta dirigindo.
// Ref: SASCM.ini — 00F2=5, actor %1d% near_actor %2d% radius %3d% %4d% %5h%
:FOLLOW_PLAYER_ON_FOOT
// 00A0: binary order (char_handle, →outX, →outY, →outZ)
00A0: 3@ 6@ 7@ 8@
// Zona STOP: recruta muito proximo → para completamente
00D6: if
    00F2: 10@ 3@ 4.0 4.0 0
004D: jump_if_false @FPF_CREEP_ZONE
00AD: set_car 11@ max_speed_to 0.0
00AE: set_car 11@ traffic_behaviour_to 1
0002: jump @MAIN_LOOP
// Zona CREEP: entre 4m e 12m → avanca devagar sem drive_to
:FPF_CREEP_ZONE
00D6: if
    00F2: 10@ 3@ 12.0 12.0 0
004D: jump_if_false @FPF_DRIVE_CLOSER
00AD: set_car 11@ max_speed_to 5.0
00AE: set_car 11@ traffic_behaviour_to 1
0002: jump @MAIN_LOOP
// Zona CHASE: fora dos 12m → dirigir em direcao ao jogador com cautela
:FPF_DRIVE_CLOSER
00AD: set_car 11@ max_speed_to 20.0
00AE: set_car 11@ traffic_behaviour_to 1
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP

// Veiculo destruido: recruta volta ao estado "a pe" (estados 2 e 3).
// Se 12@==3 (jogador era passageiro), o motor ejeta automaticamente
// o jogador do carro destruido — sem necessidade de 0633 aqui.
// 0850 so e chamado para recrutas do mod (23@==0) — recrutas vanilla
// ja tem IA de grupo nativa que reativa o seguimento automaticamente.
:RECRUIT_LOST_CAR
0006: 11@ = 0
0006: 12@ = 1
00D6: if
    0038: 23@ == 0
004D: jump_if_false @RLC_VANILLA_OK
0850: AS_actor 10@ follow_actor 3@
:RLC_VANILLA_OK
0ACD: show_text_highpriority "Recruta perdeu o veiculo! Seguindo a pe..." 2500
0002: jump @MAIN_LOOP


// ===============================================================
// MODULO 1 — SPAWN DO RECRUTA A PE
//
// pedtype 8 = gang member na classificacao interna do SA.
// Garante animacoes de gangue, vozes Grove Street e
// comportamento de grupo corretos.
// Ref: MTA Wiki — Ped Models (105=fam1, 106=fam2, 107=fam3)
//
// REGRA INVIOLAVEL de carregamento de modelos:
// Nunca criar uma entidade cujo modelo nao esta carregado na RAM.
// Resultado garantido: crash instantaneo (tela preta).
// Sequencia obrigatoria: request -> wait available -> load
// Ref: Sanny Builder Library — opcodes 0247, 0248, 038B
// ===============================================================
:SPAWN_RECRUIT

// 0209: GENERATE_RANDOM_INT_IN_RANGE — binary order: (min, max, →result_var).
// SB3 reads values left-to-right: min first, max second, output var LAST.
// WRONG: "21@ = random_int_in_ranges 105 108" → SB3 compiles param1=21@(=0),
//        param2=105, param3=108(literal) → min=0, max=105, corrupts bytecode.
// Seleciona modelo aleatorio: 105=fam1, 106=fam2, 107=fam3 (max=108 exclui 108).
// Binary order: (min, max, →result_var) — output var LAST.
0209: random_int_in_ranges 105 108 21@

// Carrega modelo solicitado no streaming de assets
0247: request_model 21@

:WAIT_PED_MODEL
0001: wait 0 ms
00D6: if
    0248: model 21@ available
004D: jump_if_false @WAIT_PED_MODEL

038B: load_requested_models

// 00A0: binary order (char_handle, →outX, →outY, →outZ) — 3@ = player actor handle.
// 000B: ADD_VAL_TO_FLOAT_LVAR — adiciona 3.0m ao eixo X do mundo para offset de spawn.
00A0: 3@ 0@ 1@ 2@
000B: 0@ += 3.0

// 0395: CLEAR_AREA — binary order: (x, y, z, radius, clearParticles_flag).
// Pure values without the 'clear_area' keyword to avoid SASCM.ini template
// reordering (%5d%=flag displayed first, would misplace coords if reorder applies).
// clearParticles_flag=1: também limpa efeitos de partículas na área.
0395: 0@ 1@ 2@ 3.0 1

// 009A: CREATE_CHAR — binary order: (pedType, model, x, y, z, →handle_var).
// SB3 reads left-to-right: all inputs first, output handle LAST.
// pedtype 8 = PEDTYPE_GANG1 (Grove Street Families)
009A: create_actor pedtype 8 model 21@ at 0@ 1@ 2@ 10@

// 0249: release_model — libera espaco de cache de streaming.
// NAO destroi o ped criado. Obrigatorio para nao acumular
// modelos na VRAM apos criacao. Ref: Sanny Builder Library 0249.
0249: release_model 21@

0850: AS_actor 10@ follow_actor 3@

// Adiciona recruta ao grupo nativo do jogador — IA de grupo, vozes e
// respostas a eventos de combate ficam ativas como aliados vanilla.
// 07AF: P1=player_num(0), P2=→group_handle. Output ULTIMO (SB3 positional).
// 0631: P1=group_handle, P2=actor_handle. Adiciona como membro (nao lider).
// 06F0: P1=group, P2=float. Separa 100m antes de teletransportar.
// 087F: P1=actor, P2=bool(1). Garante que nunca sai voluntariamente.
// SASCM.ini: 0631=2,put_actor %2d% in_group %1d%
//            06F0=2,set_group %1d% distance_limit_to %2d%
//            087F=2,set_actor %1d% never_leave_group %2h%
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @SPAWN_GROUP_DONE
0631: 24@ 10@
06F0: 24@ 100.0
087F: 10@ 1
:SPAWN_GROUP_DONE

// Marca como recruta do mod (nao vanilla) — permite 01C2 no cleanup
0006: 23@ = 0
0006: 12@ = 1

0ACD: show_text_highpriority "Recruta spawnado! Aperte U para buscar veiculo." 3000

// Debounce: evita re-disparo imediato da tecla Y
0001: wait 600 ms
0002: jump @MAIN_LOOP


// ===============================================================
// MODULO 2 — AQUISICAO DE VEICULO
//
// Localiza o carro mais proximo ao recruta, valida que nao e o
// veiculo do jogador, e executa entrada como motorista.
// Ref: Sanny Builder CLEO Library — opcode 0AB5, 05CB
// ===============================================================
:FIND_VEHICLE
0ACD: show_text_highpriority "Recruta procurando veiculo disponivel..." 2000

// 0AB5: STORE_CLOSEST_ENTITIES — retorna o carro e o ped mais proximos
// ao recruta. Param 1: handle do recruta (Char, input). Param 2: handle
// do carro mais proximo (Car, output → 11@; -1 se nenhum). Param 3:
// handle do ped mais proximo (Char, output → 13@, descartado).
// Ignora entidades criadas por script (evita retornar o proprio recruta).
// Ref: Sanny Builder CLEO Library — opcode 0AB5
0AB5: store_actor 10@ closest_vehicle_to 11@ closest_ped_to 13@

// 056E: valida o handle antes de qualquer operacao subsequente
00D6: if
    056E: car 11@ defined
004D: jump_if_false @NO_VEHICLE_FOUND

// Verifica que o carro encontrado nao e o do jogador.
// Se jogador estiver a pe, 22@ permanece 0 e a comparacao
// sera falsa (handle valido != 0), o que e o comportamento correto.
0006: 22@ = 0
00D6: if
    00DF: actor 3@ driving
004D: jump_if_false @CHECK_CAR_NOT_PLAYER
// 03C0: binary order (char_handle, →car_var) — 3@ = player actor handle
03C0: 3@ 22@

:CHECK_CAR_NOT_PLAYER
00D6: if
    0038: 11@ == 22@
004D: jump_if_false @DO_ENTER_VEHICLE

// Carro mais proximo pertence ao jogador — aborta
0006: 11@ = 0
0ACD: show_text_highpriority "Veiculo mais proximo e do jogador! Posicione outro por perto." 2500
0002: jump @MAIN_LOOP

:NO_VEHICLE_FOUND
0006: 11@ = 0
0ACD: show_text_highpriority "Nenhum veiculo disponivel por perto!" 2000
0001: wait 500 ms
0002: jump @MAIN_LOOP


// ---------------------------------------------------------------
// MODULO 2 — ENTRADA NO VEICULO via task_enter_car_as_driver
//
// 05CB: TASK_ENTER_CAR_AS_DRIVER — faz o recruta se aproximar do
// veiculo e ocupar o banco do motorista com animacoes corretas.
// 3 params: Char handle, Car handle, time limit (-1 = sem limite).
// O motor gerencia automaticamente as animacoes de abrir porta,
// sentar e assumir o controle — sem necessidade de sequence packs.
//
// Action Sequence Packs (0615-0616-0618) exigem um opcode especifico
// para adicionar cada tarefa dentro da sequencia; o opcode correto
// para entrar em veiculo dentro de um pack nao esta disponivel no
// conjunto padrao de opcodes do SA. 05CB diretamente e mais simples,
// correto e estavel para este caso de uso.
//
// Ref: Sanny Builder Library — opcode 05CB
// ---------------------------------------------------------------
:DO_ENTER_VEHICLE
05CB: AS_actor 10@ enter_car 11@ as_driver -1 ms

// Aguarda recruta assumir o volante — timeout: 5 segundos
// (10 iteracoes x 500ms). 00DF: actor driving retorna true
// somente quando o ator esta efetivamente controlando o veiculo.
0006: 16@ = 0

:WAIT_ENTER_CAR
0001: wait 500 ms
// 000A: ADD_VAL_TO_INT_LVAR — incrementa 16@ em 1 por iteracao.
// 0006 (SET_LVAR_INT) seria atribuicao, nao adicao — usa 000A.
000A: 16@ += 1

00D6: if
    00DF: actor 10@ driving
004D: jump_if_false @CHECK_ENTER_TIMEOUT

// Recruta esta ao volante — configura IA veicular
0002: jump @SETUP_VEHICLE_AI

:CHECK_ENTER_TIMEOUT
// 0019: IS_INT_LVAR_GREATER_THAN_NUMBER — timeout apos 10 iteracoes x 500ms.
// 0039 e IS_INT_LVAR_EQUAL_TO_NUMBER (==), nao > — opcode errado causaria
// verificacao de igualdade; 0019 e o opcode correto para greater-than.
00D6: if
    0019: 16@ > 10
004D: jump_if_false @WAIT_ENTER_CAR

// Timeout: recruta nao conseguiu entrar (bloqueio, colisao, etc.)
0ACD: show_text_highpriority "Recruta nao conseguiu entrar! Tente U novamente." 2500
0006: 11@ = 0
00D6: if
    0038: 23@ == 0
004D: jump_if_false @WE_TIMEOUT_DONE
0850: AS_actor 10@ follow_actor 3@
:WE_TIMEOUT_DONE
0002: jump @MAIN_LOOP
//
// Os tres opcodes abaixo controlam o comportamento do motorista
// usando a API nativa do RenderWare:
//
// 00AD: set_car max_speed_to (float, unidades internas ~km/h)
//   50.0 = acompanha o jogador sem ultrapassar nem ficar para tras.
//   Ref: Project Cerbera — velocidades reais de handling SA
//        https://projectcerbera.com/gta/sa/tutorials/handling
//
// 00AE: set_car traffic_behaviour_to (int, ver tabela no README)
//   Modo 2 (AVOIDCARS): ignora semaforos mas desvia ativamente
//   de outros carros. Escolhido pois o recruta e aliado — nao
//   pode atropelar civis (geraria wanted level desnecessario).
//   Alternativa mais realista: modo 5 (FOLLOWTRAFFIC_AVOIDCARS).
//   Ref: GTAMods Wiki — opcode 00AE
//   Ref: yugecin/scmcleoscripts (comparacao de modos de conducao)
//
// 00AF: set_car driver_behaviour_to (int)
//   Valor 5: motorista responsivo — corrige trajetoria rapidamente,
//   reage a obstaculos imprevistos com menos latencia.
//   Ref: Sanny Builder Library — opcode 00AF
//
// Nota — erro 0097: todos os parametros float (max_speed, radius)
// devem ter o sufixo .0 explicitamente no Sanny Builder para evitar
// interpretacao como int e causar o erro de tipo 0097.
// ===============================================================
:SETUP_VEHICLE_AI

00AD: set_car 11@ max_speed_to 50.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 5

0006: 12@ = 2

0ACD: show_text_highpriority "Recruta seguindo em veiculo! (IA 07F8 ativa)" 3000

// Debounce
0001: wait 600 ms
0002: jump @MAIN_LOOP


// ===============================================================
// LIMPEZA DE MEMORIA (Sanitizacao dos Pools)
//
// 01C2: mark_actor_as_no_longer_needed
//   Transfere responsabilidade do ped de volta ao motor.
//   O motor faz despawn natural quando o jogador se afasta.
//   NAO e o mesmo que deletar — deletar handle vivo = crash.
//
// 01C3: mark_car_as_no_longer_needed
//   Idem para o veiculo.
//
// Esta dupla e o equivalente ao free() de C para entidades SA.
// Essencial para nao saturar os pools (limite ~110 peds,
// ~110 veiculos) e evitar crashes apos varios respawns.
// Ref: Sanny Builder Library — opcodes 01C2, 01C3
// ===============================================================
:CLEANUP_RECRUIT
// 01C2 so e chamado para recrutas do mod (23@==0).
// Para recrutas vanilla (23@==1) o ped pertence ao motor do jogo:
// chamar 01C2 causaria despawn indesejado do membro do grupo nativo.
00D6: if
    0038: 23@ == 0
004D: jump_if_false @CLEANUP_CAR
// 06C9: remove_actor from_group — desfaz o 0631 feito em SPAWN_RECRUIT.
// Remove a referencia ao ped do CPedGroup antes de liberar o handle.
// Evita referencia morta no grupo nativo apos 01C2.
// SASCM.ini: 06C9=1,remove_actor %1d% from_group
06C9: 10@
01C2: mark_actor_as_no_longer_needed 10@

// 0019: IS_INT_LVAR_GREATER_THAN_NUMBER — libera veiculo apenas se handle valido (>0)
:CLEANUP_CAR
00D6: if
    0019: 11@ > 0
004D: jump_if_false @CLEANUP_DONE
01C3: mark_car_as_no_longer_needed 11@

:CLEANUP_DONE
0006: 10@ = 0
0006: 11@ = 0
0006: 12@ = 0
0006: 23@ = 0
0ACD: show_text_highpriority "Recruta liberado da memoria." 2000
0001: wait 500 ms
0002: jump @MAIN_LOOP
