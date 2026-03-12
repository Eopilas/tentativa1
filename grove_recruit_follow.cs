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
//   1. Tem sua posicao obtida via 0407.
//   2. Localiza o carro mais proximo via 0176 (car nearest_to_point).
//   3. Valida que nao e o veiculo do jogador.
//   4. Executa entrada animada como motorista via Action Sequence
//      Pack (opcodes 0615-0618-061B).
//   5. Aguarda confirmacao de que esta dirigindo (00DF) com
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
//
//   Transicoes:
//     0 --(Y pressionado)--> 1
//     1 --(U + carro encontrado + entrou)--> 2
//     2 --(carro destruido)--> 1
//     1 ou 2 --(recruta morreu)--> CLEANUP -> 0
//
// ---------------------------------------------------------------
// VARIAVEIS LOCAIS
// ---------------------------------------------------------------
//    0@- 2@   Coords de spawn calculadas com offset do jogador
//    6@- 8@   Coords temporarias do jogador (para drive_to)
//   10@        Handle do recruta (ped) — obrigatorio checar com 056D
//   11@        Handle do veiculo do recruta — checar com 056E
//   12@        Estado da maquina de estados (ver acima)
//   13@-15@   Coords do recruta para 0176 (car nearest_to_point)
//   16@        Contador de timeout para entrada no veiculo
//   20@        Handle do Action Sequence Pack (liberado com 061B)
//   21@        ID do modelo: 105=fam1 | 106=fam2 | 107=fam3
//   22@        Handle do veiculo do jogador (extraido com 03C0)
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

// $PLAYER_CHAR  = handle do jogador (tipo player, para verificacoes)
// $PLAYER_ACTOR = handle do ped do jogador (tipo actor, para posicao/estado)
// Ambas sao variaveis globais definidas pelo main.scm do jogo.

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

0ADE: show_text_highpriority "Grove Recruit Mod: Y=Spawnar | U=Buscar Veiculo" 4000

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
// MODULO 2 — TECLA U (VK_U = 85): Ativa busca de veiculo
//
// So executa se recruta existir e estiver no estado "a pe" (12@==1).
// 056D confirma que o handle 10@ ainda e valido antes de prosseguir.
// Operar sobre handle de ped morto/invalido causa crash imediato.
// ---------------------------------------------------------------
:CHECK_KEY_U
00D6: if
    0AB0: key_pressed 85
004D: jump_if_false @FOLLOW_LOGIC

00D6: if
    0038: 12@ == 1
004D: jump_if_false @FOLLOW_LOGIC

00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT

0002: jump @FIND_VEHICLE

// ---------------------------------------------------------------
// MODULO 1 — LOGICA DE SEGUIMENTO VEICULAR (estado 2)
//
// Executada a cada iteracao do loop quando 12@ == 2.
// 056D/056E verificam validade dos handles antes de operar;
// handles invalidos (ped morto, carro destruido) causam crash.
// 00DF verifica se o $PLAYER_ACTOR esta efetivamente dirigindo
// antes de chamar 03C0 — 03C0 so deve ser chamado apos 00DF.
// ---------------------------------------------------------------
:FOLLOW_LOGIC
00D6: if
    0038: 12@ == 2
004D: jump_if_false @MAIN_LOOP

00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_RECRUIT

// 056E: car defined — true se handle e valido e carro nao destruido
// Ref: Sanny Builder Library — opcode 056E
00D6: if
    056E: car 11@ defined
004D: jump_if_false @RECRUIT_LOST_CAR

// Zera 22@ antes de tentar obter carro do jogador
// (garante valor limpo se jogador estiver a pe)
0006: 22@ = 0
00D6: if
    00DF: actor $PLAYER_ACTOR driving
004D: jump_if_false @FOLLOW_PLAYER_ON_FOOT

// 03C0: extrai handle do carro que $PLAYER_ACTOR esta dirigindo.
// Ref: GTAMods Wiki — opcode 03C0
03C0: 22@ = actor $PLAYER_ACTOR car

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

// JOGADOR A PE:
// 00A7: car drive_to X Y Z — pathfinding nativo ate coordenada.
// Atualizado a cada 300ms para seguir o jogador enquanto caminha.
// traffic_behaviour 1 (SLOWDOWNFORCARS): abordagem mais cautelosa,
// evita atropelar civis quando o recruta se aproxima do jogador.
// Ref: GTAMods Wiki — opcode 00A7
:FOLLOW_PLAYER_ON_FOOT
04C4: store_coords_to 6@ 7@ 8@ from_actor $PLAYER_ACTOR with_offset 0.0 0.0 0.0
00AD: set_car 11@ max_speed_to 25.0
00AE: set_car 11@ traffic_behaviour_to 1
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP

// Veiculo destruido: recruta volta ao estado "a pe"
// 0850: task_follow_footsteps faz o recruta seguir o jogador a pe.
:RECRUIT_LOST_CAR
0006: 11@ = 0
0006: 12@ = 1
0850: task_follow_footsteps 10@ $PLAYER_ACTOR
0ADE: show_text_highpriority "Recruta perdeu o veiculo! Seguindo a pe..." 2500
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

// Seleciona modelo aleatorio entre fam1 (105), fam2 (106), fam3 (107)
// 0209: random_int_in_ranges — intervalo [min, max), exclui max.
// Logo 0209: X@ = random_int_in_ranges 105 108 retorna 105, 106 ou 107.
0209: 21@ = random_int_in_ranges 105 108

// Carrega modelo solicitado no streaming de assets
0247: request_model 21@

:WAIT_PED_MODEL
0001: wait 0 ms
00D6: if
    0248: model 21@ available
004D: jump_if_false @WAIT_PED_MODEL

038B: load_requested_models

// 04C4: store_coords_with_offset — calcula posicao relativa ao ator.
// +3m no eixo X lateral evita spawn em cima do jogador
// (o que causaria animacao de empurrao ou sobreposicao de colisao).
// Ref: Sanny Builder Library — opcode 04C4
04C4: store_coords_to 0@ 1@ 2@ from_actor $PLAYER_ACTOR with_offset 3.0 0.0 0.0

// Limpa peds/carros civis da area de spawn para evitar colisoes
0395: clear_area 1 at 0@ 1@ 2@ radius 3.0

// 009A: create_actor pedtype model at X Y Z
// pedtype 8 = PEDTYPE_GANG1 (Grove Street Families)
009A: 10@ = create_actor pedtype 8 model 21@ at 0@ 1@ 2@

// 0249: release_model — libera espaco de cache de streaming.
// NAO destroi o ped criado. Obrigatorio para nao acumular
// modelos na VRAM apos criacao. Ref: Sanny Builder Library 0249.
0249: release_model 21@

// Recruta segue jogador a pe — 0850: task_follow_footsteps (2 params: char, target)
0850: task_follow_footsteps 10@ $PLAYER_ACTOR

0006: 12@ = 1

0ADE: show_text_highpriority "Recruta spawnado! Aperte U para buscar veiculo." 3000

// Debounce: evita re-disparo imediato da tecla Y
0001: wait 600 ms
0002: jump @MAIN_LOOP


// ===============================================================
// MODULO 2 — AQUISICAO DE VEICULO
//
// Localiza o carro mais proximo ao recruta, valida que nao e o
// veiculo do jogador, e executa entrada animada como motorista.
// Ref: GTAMods Wiki — opcodes 0176, 0407
// Ref: Sanny Builder Library — Action Sequence opcodes 0600-061F
// Ref: ThirteenAG/III.VC.SA.CLEOScripts (exemplos de AS packs)
// ===============================================================
:FIND_VEHICLE
0ADE: show_text_highpriority "Recruta procurando veiculo disponivel..." 2000

// 0407: armazena coordenadas do recruta para uso em 0176
0407: store_coords_to 13@ 14@ 15@ from_actor 10@

// 0176: car nearest_to_point X Y Z — retorna handle do carro mais
// proximo ao ponto dado. Inclui carros estacionados e em trafego.
// Retorna handle invalido se nao houver carro no alcance.
// Ref: GTAMods Wiki — opcode 0176
0176: 11@ = car nearest_to_point 13@ 14@ 15@

// 056E: valida o handle antes de qualquer operacao subsequente
00D6: if
    056E: car 11@ defined
004D: jump_if_false @NO_VEHICLE_FOUND

// Verifica que o carro encontrado nao e o do jogador.
// Se jogador estiver a pe, 22@ permanece 0 e a comparacao
// sera falsa (handle valido != 0), o que e o comportamento correto.
0006: 22@ = 0
00D6: if
    00DF: actor $PLAYER_ACTOR driving
004D: jump_if_false @CHECK_CAR_NOT_PLAYER
03C0: 22@ = actor $PLAYER_ACTOR car

:CHECK_CAR_NOT_PLAYER
00D6: if
    0038: 11@ == 22@
004D: jump_if_false @DO_ENTER_VEHICLE

// Carro mais proximo pertence ao jogador — aborta
0006: 11@ = 0
0ADE: show_text_highpriority "Veiculo mais proximo e do jogador! Posicione outro por perto." 2500
0002: jump @MAIN_LOOP

:NO_VEHICLE_FOUND
0006: 11@ = 0
0ADE: show_text_highpriority "Nenhum veiculo disponivel por perto!" 2000
0001: wait 500 ms
0002: jump @MAIN_LOOP


// ---------------------------------------------------------------
// MODULO 2 — ENTRADA NO VEICULO via Action Sequence Pack
//
// Action Sequence Packs permitem encadear tarefas de IA com
// animacoes corretas do jogo (abrir porta, sentar, etc.).
//
// Sequencia:
//   0615: define_AS_pack_begin      — inicia definicao do pack
//   0604: AS_actor 0 enter_car_as_driver handle
//            actor 0 = "o ator que executara este AS pack"
//   0616: define_AS_pack_end 20@    — finaliza; armazena handle em 20@
//   0618: actor 10@ do_AS_pack 20@ — atribui e inicia no recruta
//   061B: remove_AS_pack 20@        — libera pack da memoria
//
// A tarefa de follow_actor e cancelada ANTES de atribuir o AS pack
// para evitar conflito de tarefas paralelas no motor de IA.
//
// Ref: Sanny Builder Library — Action Sequence opcodes
// Ref: ThirteenAG/III.VC.SA.CLEOScripts (uso real em mods)
// ---------------------------------------------------------------
:DO_ENTER_VEHICLE

// A tarefa anterior (follow_actor) sera automaticamente substituida
// pelo Action Sequence Pack — nao e necessario cancelar antes.

0615: define_AS_pack_begin
0604: AS_actor 0 enter_car_as_driver 11@
0616: define_AS_pack_end 20@
0618: actor 10@ do_AS_pack 20@
061B: remove_AS_pack 20@

// Aguarda recruta assumir o volante — timeout: 5 segundos
// (10 iteracoes x 500ms). 00DF: actor driving retorna true
// somente quando o ator esta efetivamente controlando o veiculo.
0006: 16@ = 0

:WAIT_ENTER_CAR
0001: wait 500 ms
0006: 16@ += 1

00D6: if
    00DF: actor 10@ driving
004D: jump_if_false @CHECK_ENTER_TIMEOUT

// Recruta esta ao volante — configura IA veicular
0002: jump @SETUP_VEHICLE_AI

:CHECK_ENTER_TIMEOUT
00D6: if
    0039: 16@ > 10
004D: jump_if_false @WAIT_ENTER_CAR

// Timeout: recruta nao conseguiu entrar (bloqueio, colisao, etc.)
0ADE: show_text_highpriority "Recruta nao conseguiu entrar! Tente U novamente." 2500
0006: 11@ = 0
0850: task_follow_footsteps 10@ $PLAYER_ACTOR
0002: jump @MAIN_LOOP


// ===============================================================
// MODULO 3 — CONFIGURACAO DA IA VEICULAR
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

0ADE: show_text_highpriority "Recruta seguindo em veiculo! (IA 07F8 ativa)" 3000

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
01C2: mark_actor_as_no_longer_needed 10@

00D6: if
    0039: 11@ > 0
004D: jump_if_false @CLEANUP_DONE
01C3: mark_car_as_no_longer_needed 11@

:CLEANUP_DONE
0006: 10@ = 0
0006: 11@ = 0
0006: 12@ = 0
0ADE: show_text_highpriority "Recruta liberado da memoria." 2000
0001: wait 500 ms
0002: jump @MAIN_LOOP
