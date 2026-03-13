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
//   Opcode: 00AE set_car_driving_style, parametro = DrivingMode (enum).
//   Todos os 7 valores confirmados em enums.txt do repositorio.
//
//   Valor 0 (StopForCars)               — obedece semaforos, para para
//                                          carros/obstaculos. PADRAO dos
//                                          NPCs civis do trafego do jogo.
//   Valor 1 (SlowDownForCars)           — obedece semaforos, desacelera
//                                          mas ainda pode bater.
//   Valor 2 (AvoidCars)                 — ignora semaforos, desvia de carros.
//   Valor 3 (PloughThrough)             — ignora tudo, bate em tudo.
//   Valor 4 (StopForCarsIgnoreLights)   — IGNORA semaforos, para obstaculos.
//                                          (nome enganoso — nao respeita luz)
//   Valor 5 (AvoidCarsObeyLights)       — obedece semaforos E desvia. Hibrido.
//   Valor 6 (AvoidCarsStopForPedsObeyLights) — obedece semaforos, desvia de
//                                          carros E para para pedestres.
//                                          Mais cauteloso que o 5. Existe, sim.
//
// Modulo 3b — MODOS DE CONDUCAO (tecla 4)
//
//   10 modos — tecla 4 cicla: 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 0
//
//   OBJECTIVO: modos CIVICO tentam manter o recruta na faixa como um NPC
//   normal do SA (road graph navigation). DIRETO e o fallback quando ele
//   fica para tras ou nao ha estradas proximas. AUTONOMO permite avaliar
//   a conducao do recruta independentemente do jogador.
//   073B REMOVIDO: contramao permitida em todos os modos — melhora navegacao
//   em cruzamentos onde o caminho correto exigia nodo de sentido oposto.
//   DrivingMode=AvoidCars(2) em todos os CIVICO: nao para, apenas desvia.
//
//   CIVICO-0 (29@=0) — 0407(-20m) + 04D3 + 00A7 + AvoidCars(2):
//     Alvo: 20m ATRAS do carro do jogador (0407 Y=-20 espaco local).
//     04D3: snap desse ponto para no de estrada SA mais proximo.
//     00A7 (GotoCoords=8): drive_to via road graph (CarMission=GotoCoords).
//     Recruta nunca atinge posicao exacta do jogador — evita colisao estruturalmente.
//     AvoidCars(2): nao para em semaforos. max 40 km/h. Threshold 3 ticks (0.9s).
//
//   CIVICO-A (29@=1) — 0407(-20m) + 04D3 + 02C2 (GotoCoordsAccurate=12) + AvoidCars(2):
//     Alvo: 20m ATRAS (0407 Y=-20). 02C2 usa GotoCoordsAccurate internamente —
//     caminho mais cuidadoso pelos road nodes vs GotoCoords(8) do CIVICO-0.
//     AvoidCars(2). max 40 km/h. Threshold 3 ticks (0.9s).
//
//   CIVICO-B (29@=2) — 0407(-20m) + 04D3 + 05D1 DriveMode=Normal(0) + AvoidCars(2):
//     Alvo: 20m ATRAS (0407 Y=-20). DIFERENCA vs CIVICO-A: usa actor task (05D1)
//     em vez de CarMission directa (02C2).
//     DriveMode=Normal(0) → CCarAI_GetCarToGoToCoors → CarMission=GotoCoords(8) interno.
//     Actor task gere "stuck" (TempAction=Reverse quando preso) — 02C2 nao tem isso.
//     AvoidCars(2). max 40 km/h. Threshold 3 ticks (0.9s).
//
//   CIVICO-C (29@=3) — 0407(-20m) + 04D3 + 05D1 DriveMode=Accurate(1) + AvoidCars(2):
//     Alvo: 20m ATRAS (0407 Y=-20). DriveMode=Accurate(1) → CCarAI_GetCarToGoToCoorsAccurate
//     → CarMission=GotoCoordsAccurate(12).
//     Diferenca vs Normal: usa CCarCtrl_ClipTargetOrientationToLink para se alinhar
//     melhor a faixa da estrada em curvas, e calcula travagem mais cedo.
//     Normal pode cortar curvas; Accurate respeita a linha de faixa.
//     Actor task (vs 02C2 directo do CIVICO-A): adiciona gestao de stuck.
//     AvoidCars(2). max 45 km/h. Threshold 7 ticks (2.1s).
//
//   CIVICO-D (29@=4) ★ MELHOR — 06E1 EscortRearFaraway(67) + AvoidCars(2):
//     Formacao geometrica atras do carro do jogador. Road nodes.
//     AvoidCars(2): desvia obstaculos sem parar. Sem 073B: contramao ok.
//     max 35 km/h (cruise 35). Dedup por carro do jogador.
//
//   CIVICO-E (29@=5) ★ MELHOR — 06E1 FollowCarFaraway(52) + AvoidCars(2):
//     Segue carro do jogador via road nodes. AvoidCars(2). Sem 073B.
//     max 35 km/h (cruise 35). Dedup por carro do jogador.
//
//   CIVICO-F (29@=6) — 0407(-30m) + 04D3 + 02C2 + AvoidCars(2):
//     0407: ponto 30m ATRAS do carro do jogador (Y=-30 eixo local).
//     04D3: snap desse ponto para no de estrada SA mais proximo.
//     02C2 (GotoCoordsAccurate=12): recruta sempre alvo ATRAS — nao bloqueia.
//     Guarda anti-colisao: STOP (<10m), SLOW 15 km/h (<20m) — evita "derrapar na frente".
//     AvoidCars(2). Sem 073B. max 30 km/h. Threshold 5 ticks (1.5s).
//
//   DIRETO (29@=7):
//     07F8: follow_car + AvoidCars(2). Direto A→B, ignora semaforos. Raio 10m.
//     max 60 km/h (reduzido de 100 para evitar colisao a alta velocidade).
//     Usar quando o recruta ficar para tras em CIVICO ou em zonas sem estradas.
//
//   AUTONOMO (29@=8) — 0AB6 GPS waypoint + 05D1 DriveMode=Accurate + AvoidCars(2):
//     0AB6: le coords do waypoint do mapa (IF+SET).
//     Se nao ha waypoint: para e pede para marcar ponto no mapa.
//     05D1 DriveMode=Accurate(1): navegacao mais precisa para avaliacao.
//     AvoidCars(2). Sem 073B. max 50 km/h. Threshold 30 ticks (9s): destino fixo.
//     Uso: marcar waypoint no mapa, activar modo, seguir o recruta para avaliar.
//
//   PARADO (29@=9):
//     00A9 cancela task activa + max_speed 0. Para completamente.
//
//   Dedup por tipo:
//     CIVICO-0/A/B/C/F/AUTONOMO (0,1,2,3,6,8): contagem regressiva (30@>0=aguardar).
//     CIVICO-D/E (4,5) e DIRETO (7): dedup por carro do jogador (22@!=30@ re-emite).
//     Reset 30@=0 em CHECK_KEY_H (troca de modo): forca re-emissao imediata.
//     Thresholds: 0/A/B=3 ticks (0.9s), C=7 ticks (2.1s), F=5 ticks (1.5s), AUTONOMO=30 ticks (9s).
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
//   12@ = 2  Recruta em veiculo -> 05D1/06E1: modos CIVICO / 07F8: follow_car (DIRETO)
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
//    6@- 8@   Coords temporarias do jogador (para drive_to / GPS waypoint / teleporte)
//   10@        Handle do recruta (ped) — obrigatorio checar com 056D
//   11@        Handle do veiculo do recruta — checar com 056E
//              PERSISTENTE: preservado mesmo apos morte do recruta.
//              Recruta novo retoma automaticamente se 11@ e valido.
//   12@        Estado da maquina de estados (ver acima)
//   13@        Handle temporario para ped mais proximo (output descartado de 0AB5)
//   14@        Flag drive-by do CJ passageiro: 0=OFF, 1=ON (0501, tecla B)
//   15@        Modo agressividade do recruta: 1=AGRESSIVO(padrao), 0=PASSIVO (tecla N)
//   16@        Contador de timeout para entrada no veiculo (FIND_VEHICLE)
//   20@        Handle do Action Sequence Pack (liberado com 061B)
//   21@        ID do modelo: 105=fam1 | 106=fam2 | 107=fam3
//   22@        Handle do veiculo do jogador (extraido com 03C0)
//   23@        Flag de origem do recruta:
//              0 = spawnado pelo mod (01C2 deve ser chamado no cleanup)
//              1 = recruta vanilla adotado (ped pertence ao motor — nunca chamar 01C2)
//   24@        Handle do grupo do jogador (output de 07AF — usado em VANILLA_SCAN e SPAWN_RECRUIT)
//              Reutilizado como temp de interior ID em INTERIOR_SYNC (seguro: paths exclusivos).
//   25@        Contagem de lideres do grupo (output de 07F6, descartado)
//   26@        Contagem de membros do grupo (output de 07F6, gate de deteccao)
//   27@        Handle do ped candidato a adocao (output de 092B slot 0, temp)
//   28@        Contador de timeout para entrada do JOGADOR no carro (CHECK_KEY_G)
//   29@        Modo de conducao (tecla 4): 0=CIVICO-0(04D3+00A7+AC2) | 1=CIVICO-A(04D3+02C2+AC2) | 2=CIVICO-B(05D1 Normal+AC2) | 3=CIVICO-C(05D1 Accurate+AC2) | 4=CIVICO-D(EscortRear+AC2) | 5=CIVICO-E(FollowFar+AC2) | 6=CIVICO-F(Lookahead-30m+02C2+AC2) | 7=DIRETO | 8=AUTONOMO(GPS waypoint+05D1 Accurate+AC2) | 9=PARADO
//   30@        Dedup de re-emissao:
//              CIVICO-0/A/B/C/F/AUTONOMO (29@==0/1/2/3/6/8): contagem regressiva (>0=aguardar; 0=re-emitir)
//              CIVICO-D/E (29@==4/5) + DIRETO (29@==7): handle carro jogador (22@) — re-emite quando muda
//              Reset para 0 em CHECK_KEY_H, CLEANUP_DONE, SF_MODE_CHECK(PARADO),
//              STATE2_TOO_FAR, SF_CHECK_BOAT, SF_CHECK_FLYING.
//   31@        Interior ID do jogador na ultima sincronizacao (evita 0860 redundante)
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
0006: 14@ = 0
0006: 15@ = 1
0006: 23@ = 0
0006: 28@ = 0
0006: 29@ = 0
0006: 30@ = 0
0006: 31@ = 0

0ACD: show_text_highpriority "Grove Recruit Mod: 1=Spawn|2=Veiculo|3=Passageiro|4=Modo|B=DrivBy|N=Agredir" time 5000

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
// SINCRONIZACAO DE INTERIOR
//
// Quando o jogador entra num interior (casa, loja, etc.), o recruta
// deve ser vinculado ao mesmo interior via 0860 (link_actor_to_interior)
// para continuar visivel e ativo. Sem isso, o motor filtra o ped e
// ele "desaparece" ao entrar em ambientes fechados.
//
// 077E: get_active_interior — retorna ID do interior ativo (0=exterior).
//   SASCM.ini: 077E=1,get_active_interior_to %1d%
// 0860: link_actor to_interior — vincula o ped ao interior especificado.
//   SASCM.ini: 0860=2,link_actor %1d% to_interior %2h%
// 0840: link_car to_interior — vincula o veiculo ao interior.
//   SASCM.ini: 0840=2,link_car %1d% to_interior %2d%
//
// Otimizacao: 31@ guarda o ultimo interior sincronizado; 0860/0840
// so sao chamados quando o interior muda — evita overhead desnecessario.
// 24@ e reutilizado aqui como temp (VANILLA_SCAN usa 24@ so quando
// 12@==0, e estamos no bloco 12@>0 — paths mutuamente exclusivos).
// ---------------------------------------------------------------
077E: get_active_interior_to 24@
00D6: if
    0038: 24@ == 31@
004D: jump_if_false @DO_INTERIOR_SYNC
0002: jump @INTERIOR_SYNC_SKIP
:DO_INTERIOR_SYNC
0006: 31@ = 24@
0860: link_actor 10@ to_interior 24@
00D6: if
    0019: 11@ > 0
004D: jump_if_false @INTERIOR_SYNC_SKIP
0840: link_car 11@ to_interior 24@
:INTERIOR_SYNC_SKIP

// ---------------------------------------------------------------
// DETECCAO DE DISPENSA DE GRUPO (STATE 1 — recruta a pe)
//
// Quando o jogador usa o mecanismo nativo SA para dispensar o
// recruta (apontar arma para membro e premir botao de recrutamento
// de novo), o ped permanece vivo (056D ok) mas e removido do grupo
// SA. O script continuaria a gerir um ped ja dispensado, impedindo
// novo spawn (12@>0). Este bloco detecta essa situacao.
//
// 06EE: is_actor_in_group — retorna true se o ped esta no grupo.
//   SASCM.ini: 06EE=2, actor %1d% in_group %2d%
//   Condicional: pode ser usado em bloco 00D6: if directamente.
//
// So verifica em STATE1 (a pe): em STATE2/3, 06C9 foi chamado
// intencionalmente em DO_ENTER_VEHICLE para remover o recruta do
// grupo SA (evita bug de saida automatica em carros 4 portas) —
// nesse caso a ausencia do grupo e esperada, nao e dispensa.
//
// Vanilla recrutas (23@==1) tambem sao verificados: se o jogador
// os dispensar via mecanismo nativo, o mod reseta o estado.
// ---------------------------------------------------------------
00D6: if
    0038: 12@ == 1
004D: jump_if_false @DISBAND_CHECK_SKIP
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @DISBAND_CHECK_SKIP
00D6: if
    06EE: actor 10@ in_group 24@
004D: jump_if_false @DISBAND_DETECTED
0002: jump @DISBAND_CHECK_SKIP
:DISBAND_DETECTED
0ACD: show_text_highpriority "Recruta dispensado do grupo. 1 para novo recruta." 2500
0002: jump @CLEANUP_RECRUIT
:DISBAND_CHECK_SKIP

// ---------------------------------------------------------------
// TELEPORTE DE SEGURANCA — ESTADO 1 (recruta a pe)
//
// Quando o recruta a pe excede 120m do jogador, o sistema de
// separacao de grupo (06F0: 100m) devia teletransporta-lo
// automaticamente. Este bloco e um fallback para o caso raro
// em que esse teleporte falha (ped preso em geometria, bloqueio
// de missao, conflito de tasks).
//
// Fluxo:
//   00F2: true = recruta ESTA dentro de 120m → skip (sem acao).
//   00F2: false = recruta FORA dos 120m → teleportar junto ao jogador
//           e re-emitir 0850 (follow_actor) para garantir retoma.
//
// Offset +2m em X evita sobreposicao com o ped do jogador.
// 0850: re-issue da task de seguimento para sair de qualquer state
//   interno que possa estar a bloquear o recruta.
//
// Apenas executa para STATE1 (12@==1) — STATE2/3 tem logica propria.
// ---------------------------------------------------------------
00D6: if
    0038: 12@ == 1
004D: jump_if_false @TELEPORT1_SKIP
00D6: if
    00F2: 10@ 3@ 120.0 120.0 0
004D: jump_if_false @TELEPORT1_TOO_FAR
0002: jump @TELEPORT1_SKIP
:TELEPORT1_TOO_FAR
00A0: 3@ 6@ 7@ 8@
000B: 6@ += 2.0
00A1: 10@ 6@ 7@ 8@
0850: AS_actor 10@ follow_actor 3@
:TELEPORT1_SKIP

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
0ACD: show_text_highpriority "Recruta vanilla adotado! Aperte 2 para dar veiculo." 3000
:VANILLA_SCAN_DONE

// ---------------------------------------------------------------
// MODULO 1 — TECLA 1 (VK = 49): Spawn do recruta a pe
//
// 0AB0: key_pressed usa VK codes do Windows (CLEO 4+).
// Alternativa para controle/joypad: 00E1: player 0 pressed_key X
// Com 12@==0: spawn normal.
// Com 12@>0: limpeza forcada do recruta atual + spawn imediato.
//   Util quando o recruta desaparece mas o handle continua valido
//   no motor (056D retorna true mas recruta ja nao e visivel/acessivel).
//   Nao requer um segundo modulo ou tecla separada — basta pressionar
//   1 novamente para substituir. Inclui debounce de 200ms.
// ---------------------------------------------------------------
00D6: if
    0AB0: key_pressed 49
004D: jump_if_false @CHECK_KEY_U

00D6: if
    0038: 12@ == 0
004D: jump_if_false @KEY1_FORCE_RESPAWN

0002: jump @SPAWN_RECRUIT

// KEY1_FORCE_RESPAWN: preservar carro se ainda existe
:KEY1_FORCE_RESPAWN
0ACD: show_text_highpriority "A substituir recruta..." 1500
0001: wait 200 ms
00D6: if
    0038: 23@ == 0
004D: jump_if_false @K1_SKIP_PED
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @K1_SKIP_PED
06C9: remove_actor 10@ from_group
01C2: mark_actor_as_no_longer_needed 10@
:K1_SKIP_PED
// Carro: preservar handle se carro ainda existe (persistencia)
00D6: if
    0019: 11@ > 0
004D: jump_if_false @K1_SKIP_CAR
00D6: if
    056E: car 11@ defined
004D: jump_if_false @K1_CAR_GONE
0002: jump @K1_SKIP_CAR
:K1_CAR_GONE
0006: 11@ = 0
:K1_SKIP_CAR
0006: 10@ = 0
// 11@ preservado se carro ainda existe
0006: 12@ = 0
0006: 23@ = 0
0006: 29@ = 0
0006: 30@ = 0
0006: 31@ = 0
0002: jump @SPAWN_RECRUIT

// ---------------------------------------------------------------
// MODULO 2 — TECLA 2 (VK = 50): Toggle veiculo do recruta
//
// Comportamento DUAL conforme estado atual (12@):
//   12@ == 1 (a pe)        → recruta busca veiculo desocupado (FIND_VEHICLE)
//   12@ == 2 (em veiculo)  → recruta sai do veiculo (0633: exit_car)
//   12@ == 3 (passageiro)  → ignorado (usar 3 para sair)
//
// 056D confirma handle valido antes de qualquer operacao —
// operar sobre ped morto/invalido causa crash imediato na VM.
// ---------------------------------------------------------------
:CHECK_KEY_U
00D6: if
    0AB0: key_pressed 50
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
// 11@ preservado: carro fica guardado (aperte 2 para recruta re-entrar)
0006: 12@ = 1
0ACD: show_text_highpriority "Recruta saindo! Aperte 2 para retornar ao carro." 2500
0001: wait 800 ms
// Re-ativa follow_actor para recrutas do mod (23@==0) e re-integra ao grupo.
// 06C9 foi chamado em DO_ENTER_VEHICLE (antes de 05CB) — re-adicionar aqui.
00D6: if
    0038: 23@ == 0
004D: jump_if_false @U_EXIT_DONE
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @U_EXIT_REJOIN_DONE
0631: 24@ 10@
087F: 10@ 1
:U_EXIT_REJOIN_DONE
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
// Verificar carro guardado da sessao anterior do recruta
00D6: if
    0019: 11@ > 0
004D: jump_if_false @U_FIND_NEW_VEHICLE
00D6: if
    056E: car 11@ defined
004D: jump_if_false @U_SAVED_CAR_GONE
// Carro guardado disponivel: recruta retoma directamente
0ACD: show_text_highpriority "Retomando carro guardado do recruta!" 2000
0002: jump @DO_ENTER_VEHICLE
:U_SAVED_CAR_GONE
// Carro guardado foi destruido — limpar e buscar novo
0006: 11@ = 0
:U_FIND_NEW_VEHICLE
0002: jump @FIND_VEHICLE

// ---------------------------------------------------------------
// MODULO 3 — TECLA 3 (VK = 51): Recruta dirige o jogador
//
// 12@==2 → jogador entra no carro do recruta como passageiro.
//   Recruta navega automaticamente usando 0407 (offset 150m a frente)
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
//   0407: 11@ 0.0 150.0 0.0 6@ 7@ 8@ → ponto 150m a frente do carro ✓
//
// 0449: actor in_a_car — verifica se jogador ainda esta num veiculo.
// ---------------------------------------------------------------
:CHECK_KEY_G
00D6: if
    0AB0: key_pressed 51
004D: jump_if_false @CHECK_KEY_H

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
0ACD: show_text_highpriority "Recruta te carregando! 3 para sair." 3000
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
0006: 14@ = 0
0501: set_player 0 driveby_mode 0
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
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @G_LOST_REJOIN_DONE
0631: 24@ 10@
087F: 10@ 1
:G_LOST_REJOIN_DONE
0850: AS_actor 10@ follow_actor 3@
:G_LOST_CAR_DONE
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// MODULO 4 — TECLA 4 (VK = 52): Modo de conducao do recruta
//
//   29@ = 0  CIVICO-0  — 0407(-20m)+04D3+00A7(GotoCoords=8) AvoidCars(2), 40kmh, thresh 3
//   29@ = 1  CIVICO-A  — 0407(-20m)+04D3+02C2(GotoCoordsAccurate=12) AvoidCars(2), 40kmh, thresh 3
//   29@ = 2  CIVICO-B  — 0407(-20m)+04D3+05D1 DriveNormal(actor task) AvoidCars(2), 40kmh, thresh 3
//   29@ = 3  CIVICO-C  — 0407(-20m)+04D3+05D1 DriveAccurate(actor task) AvoidCars(2), 45kmh, thresh 7
//   29@ = 4  CIVICO-D  — 06E1 EscortRearFaraway(67)+AvoidCars(2), 35kmh ★
//   29@ = 5  CIVICO-E  — 06E1 FollowCarFaraway(52)+AvoidCars(2), 35kmh ★
//   29@ = 6  CIVICO-F  — 0407(-30m)+04D3+02C2 AvoidCars(2), 30kmh, thresh 5, guard STOP10m/SLOW20m
//   29@ = 7  DIRETO    — 07F8 + AvoidCars(2), raio 10m, max 60kmh
//   29@ = 8  AUTONOMO  — 0AB6 GPS waypoint + 05D1 DriveAccurate + AvoidCars(2), 50kmh
//   29@ = 9  PARADO    — 00A9 cancela task + max_speed 0
//
// Aplicado em STATE2 (recruta segue jogador) e STATE3 (recruta dirige jogador).
// Resetar 30@=0 forca re-emissao na proxima iteracao do loop.
// ---------------------------------------------------------------
:CHECK_KEY_H
00D6: if
    0AB0: key_pressed 52
004D: jump_if_false @CHECK_KEY_B
00D6: if
    0019: 12@ > 0
004D: jump_if_false @FOLLOW_LOGIC
// Cicla modo: 0→1→2→3→4→5→6→7→8→9→0
000A: 29@ += 1
00D6: if
    0019: 29@ > 9
004D: jump_if_false @KEY_H_MSG
0006: 29@ = 0
:KEY_H_MSG
// Forca re-emissao na proxima iteracao do loop de seguimento
0006: 30@ = 0
// Exibe mensagem de feedback para cada modo
00D6: if
    0038: 29@ == 0
004D: jump_if_false @KH_CHECK1
0ACD: show_text_highpriority "CIVICO-0: 04D3+00A7 AvoidCars 40kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK1
00D6: if
    0038: 29@ == 1
004D: jump_if_false @KH_CHECK2
0ACD: show_text_highpriority "CIVICO-A: 04D3+02C2 GotoCoordsAcc AvoidCars 40kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK2
00D6: if
    0038: 29@ == 2
004D: jump_if_false @KH_CHECK3
0ACD: show_text_highpriority "CIVICO-B: 04D3+05D1 Normal(task) AvoidCars 40kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK3
00D6: if
    0038: 29@ == 3
004D: jump_if_false @KH_CHECK4
0ACD: show_text_highpriority "CIVICO-C: 04D3+05D1 Accurate(task) AvoidCars 45kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK4
00D6: if
    0038: 29@ == 4
004D: jump_if_false @KH_CHECK5
0ACD: show_text_highpriority "CIVICO-D: 06E1 EscortRear+AvoidCars 35kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK5
00D6: if
    0038: 29@ == 5
004D: jump_if_false @KH_CHECK6
0ACD: show_text_highpriority "CIVICO-E: 06E1 FollowFar+AvoidCars 35kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK6
00D6: if
    0038: 29@ == 6
004D: jump_if_false @KH_CHECK7
0ACD: show_text_highpriority "CIVICO-F: Lookahead-30m(atras)+02C2 AvoidCars 30kmh guard (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK7
00D6: if
    0038: 29@ == 7
004D: jump_if_false @KH_CHECK8
0ACD: show_text_highpriority "DIRETO: vai direto ignora semaforos 60kmh (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK8
00D6: if
    0038: 29@ == 8
004D: jump_if_false @KH_CHECK9
0ACD: show_text_highpriority "AUTONOMO: marca waypoint no mapa e sigo-te! (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC
:KH_CHECK9
0ACD: show_text_highpriority "PARADO: recruta estacionado (4 mudar)" 2500
0002: jump @FOLLOW_LOGIC

// ---------------------------------------------------------------
// MODULO 5 — TECLA B (VK = 66): Toggle drive-by CJ passageiro
//
// Quando CJ e passageiro (12@==3), activa/desactiva modo drive-by
// via 0501: set_player driveby_mode. 14@=0=OFF, 14@=1=ON.
// Resetado automaticamente ao sair do estado 3 (G_EXIT_CAR,
// PLAYER_LEFT_RECRUIT_CAR, CLEANUP_DONE).
// ---------------------------------------------------------------
:CHECK_KEY_B
00D6: if
    0AB0: key_pressed 66
004D: jump_if_false @CHECK_KEY_N
00D6: if
    0038: 12@ == 3
004D: jump_if_false @FOLLOW_LOGIC
00D6: if
    0038: 14@ == 0
004D: jump_if_false @DB_DISABLE
0006: 14@ = 1
0501: set_player 0 driveby_mode 1
0ACD: show_text_highpriority "Drive-by ATIVADO (B para desativar)" 2500
0001: wait 500 ms
0002: jump @MAIN_LOOP
:DB_DISABLE
0006: 14@ = 0
0501: set_player 0 driveby_mode 0
0ACD: show_text_highpriority "Drive-by DESATIVADO (B para ativar)" 2500
0001: wait 500 ms
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// MODULO 6 — TECLA N (VK = 78): Toggle agressividade do recruta
//
// 15@=1 (agressivo/padrao): IA de grupo SA gere combate naturalmente.
// 15@=0 (passivo): 0850 reemitido em cada loop no estado 1 (a pe)
// para manter o recruta em modo seguimento e suprimir aggressao.
// O recruta ainda pode reagir brevemente antes de cada reemissao
// (intervalo 300ms) mas nunca mantém perseguicao prolongada.
// ---------------------------------------------------------------
:CHECK_KEY_N
00D6: if
    0AB0: key_pressed 78
004D: jump_if_false @FOLLOW_LOGIC
00D6: if
    0038: 15@ == 1
004D: jump_if_false @AGG_ENABLE
0006: 15@ = 0
0ACD: show_text_highpriority "Recruta PASSIVO: so segue, nao ataca. (N para reverter)" 2500
0001: wait 500 ms
0002: jump @MAIN_LOOP
:AGG_ENABLE
0006: 15@ = 1
0ACD: show_text_highpriority "Recruta AGRESSIVO: ataca inimigos. (N para passivo)" 2500
0001: wait 500 ms
0002: jump @MAIN_LOOP

//
// Estado 2: recruta em veiculo segue o jogador.
// Estado 3: jogador e passageiro, recruta navega a frente (0407+00A7+04D3 snap CIVICO).
//
// 056D/056E verificam validade dos handles antes de operar;
// handles invalidos (ped morto, carro destruido) causam crash.
// 00DF verifica se o $PLAYER_ACTOR esta efetivamente dirigindo
// antes de chamar 03C0 — 03C0 so deve ser chamado apos 00DF.
// ---------------------------------------------------------------
:FOLLOW_LOGIC
// MODO PASSIVO (15@==0): reemitir 0850 em estado 1 para suprimir combate.
// Apenas em estado 1 (a pe) — nao interferir em estado 2/3 (em veiculo).
00D6: if
    0038: 12@ == 1
004D: jump_if_false @FL_STATE_CHECK
00D6: if
    0038: 15@ == 0
004D: jump_if_false @FL_STATE_CHECK
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @FL_STATE_CHECK
0850: AS_actor 10@ follow_actor 3@
:FL_STATE_CHECK
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

// Estado 3: recruta dirige, jogador e passageiro.
// Se o jogador tiver um waypoint marcado no mapa, o recruta navega
// ate esse ponto. Caso contrario, navega 150m a frente (comportamento
// original), evitando paradas bruscas ao atingir um alvo fixo.
//
// GPS waypoint SA PC 1.0:
//   0xBA831C — bTargetSet (byte): 1=waypoint marcado, 0=sem waypoint
//   0xBA8310 — TargetX (float IEEE754, 4 bytes)
//   0xBA8314 — TargetY (float IEEE754, 4 bytes)
// Para Z: usa posicao atual do carro (mapa armazena Z=0 invalido).
//
// 0A8D: read_memory — le bytes brutos da memoria do processo.
//   SASCM.ini: 0A8D=4,%4d% = read_memory %1d% size %2d% virtual_protect %3d%
// 00AA: store_car position — le Z atual do veiculo.
//   SASCM.ini: 00aa=4,store_car %1d% position_to %2d% %3d% %4d%
//
// traffic_behaviour 0 (STOPFORCARS): para em semaforos, respeita leis
//   de transito igual a um NPC normal — nao atropela pedestres nem
//   furar sinal vermelho.
// 00AF driver_behaviour_to 0: motorista passivo (nao-agressivo), igual NPC normal.
//   Nao usa 00A9 (to_normal_driver) pois ela reseta m_nCruiseSpeed para 20 km/h.
//
// Nota: 0xBA831C/0xBA8310 sao enderecos do SA PC (EXE versao 1.0 US).
// Se o EXE for diferente, a leitura retorna lixo mas nao causa crash —
// o fallback para 150m frente acontece naturalmente (6@==0 → sem waypoint).
0A8D: 6@ = read_memory 0xBA831C size 1 virtual_protect 0
00D6: if
    0019: 6@ > 0
004D: jump_if_false @STATE3_FORWARD
// Waypoint definido — le X e Y do mapa
0A8D: 6@ = read_memory 0xBA8310 size 4 virtual_protect 0
0A8D: 7@ = read_memory 0xBA8314 size 4 virtual_protect 0
// Obtem Z real do carro (o waypoint do mapa armazena Z=0.0)
// 25@/26@ sao usadas aqui como outputs descartados de 00AA (store_car_position).
// Reutilizacao segura: 25@/26@ sao contadores de grupo usados APENAS em VANILLA_SCAN
// (12@==0), e este bloco so executa quando 12@==3 — paths mutuamente exclusivos.
00AA: 11@ 25@ 26@ 8@
0002: jump @STATE3_DRIVE
:STATE3_FORWARD
// Sem waypoint — navega 150m a frente em espaco local do carro.
// Todos os modos (incluindo DIRETO) usam 00A7 aqui — snap 04D3 beneficia todos.
0407: 11@ 0.0 150.0 0.0 6@ 7@ 8@
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
:STATE3_DRIVE
// PARADO (29@==9): recruta para enquanto CJ e passageiro
// 00A9: cancela task drive_to activa. Sem 00A9, o carro continua a
// tentar atingir o ultimo destino apesar de max_speed 0.0.
00D6: if
    0038: 29@ == 9
004D: jump_if_false @STATE3_MOVING
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0002: jump @MAIN_LOOP
// STATE3 usa sempre 00A7 drive_to (sem actor tasks — sem carro-alvo para 06E1).
// DIRETO (29@==7): ignora semaforos, AvoidCars, max 80 km/h.
// Todos os CIVICO (qualquer outro modo): AvoidCars(2), max 50 km/h.
// AvoidCars(2) para STATE3: recruta a conduzir nao para em semaforos — menos
// paragens bruscas para o passageiro (jogador). Consistente com STATE2.
// DIRETO: 00AF=0 (sem road-following — velocidade alta 80kmh, recruta nao-agressivo).
// CIVICO: 00AF=1 (follow road + drive back if blocked) — melhora 00A7 road navigation.
:STATE3_MOVING
00D6: if
    0038: 29@ == 7
004D: jump_if_false @STATE3_CIV
00AD: set_car 11@ max_speed_to 80.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 0
0002: jump @STATE3_EXEC
:STATE3_CIV
// Todos os modos CIVICO (incluindo AUTONOMO): AvoidCars(2) + max 50 km/h.
// Velocidade unica para STATE3 — evita diferenca de comportamento entre modos.
// 00AF=1: follow road + drive back if blocked — melhor road navigation com 00A7.
00AD: set_car 11@ max_speed_to 50.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 1
:STATE3_EXEC
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP

:PLAYER_LEFT_RECRUIT_CAR
// Jogador saiu voluntariamente (ex: pressionou F) — volta ao modo seguimento
0006: 12@ = 2
0006: 14@ = 0
0501: set_player 0 driveby_mode 0
0ACD: show_text_highpriority "Voltando ao modo seguimento. 3 para recruta dirigir." 2500
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// ESTADO 2: recruta em veiculo segue o jogador
// ---------------------------------------------------------------
:STATE2_FOLLOW
// Desync check: carro valido (056E ok) mas recruta pode ter saido.
// 0449: true se ator esta em qualquer veiculo (motorista OU passageiro).
// Caso residual: vanilla recruits (23@==1) ainda estao no grupo SA —
// a IA de grupo pode emitir exit_car para eles (comportamento vanilla).
// Para recrutas do mod (23@==0) isso nao ocorre: 06C9 ja foi chamado
// em DO_ENTER_VEHICLE antes de 05CB.
00D6: if
    0449: actor 10@ in_a_car
004D: jump_if_false @RECRUIT_EXITED_VOLUNTARILY

// ---------------------------------------------------------------
// TELEPORTE DE SEGURANCA — ESTADO 2 (recruta em veiculo)
//
// Se o carro do recruta estiver a mais de 150m do jogador, o
// seguimento por 07F8 falhou (preso em geometria, colisao, etc.).
// Teleportamos o carro para o no de estrada SA mais proximo do
// jogador (04D3: nearest_car_path_node) e forcamos re-emissao de
// 07F8 (30@=0) para retomar o seguimento imediatamente.
//
// 04D3: nearest car path node → posiciona o carro numa via real
//   do mapa, evitando paredes, passeios e terrenos sem estrada.
//   SASCM.ini: 04D3=7,get_nearest_car_path_coords_from %1d% %2d% %3d% type %4h% store_to %5d% %6d% %7d%
// 00AB: put_car at coords — teletransporta o carro de forma directa.
//   SASCM.ini: 00ab=4,put_car %1d% at %2d% %3d% %4d%
// Offset +5m em X afasta o carro do veiculo do jogador (evita colisao
//   imediata ao aparecer ao lado).
// ---------------------------------------------------------------
00D6: if
    00F2: 10@ 3@ 150.0 150.0 0
004D: jump_if_false @STATE2_TOO_FAR
0002: jump @STATE2_DIST_OK
:STATE2_TOO_FAR
00A0: 3@ 6@ 7@ 8@
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
000B: 6@ += 5.0
00AB: 11@ 6@ 7@ 8@
0006: 30@ = 0
:STATE2_DIST_OK

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
// CIVICO-A/B/C: 04D3 (nearest road node ao jogador) + 00A7 (drive_to) com
// dedup por contagem regressiva em 30@ (>0=aguardar, 0=re-emitir).
// O AI navega o grafo de nos de estrada ate ao destino — igual ao trafego normal.
// DIRETO: 07F8 (follow_car) com dedup 22@==30@ (carro do jogador).
//
// Veiculos aereos/maritimos: recruta terrestre nao pode seguir.
// 04C8 cobre helicoptero E aviao. 04A7 cobre barcos.
// 30@=0 forca re-emissao quando jogador voltar ao solo.
00D6: if
    04C8: actor 3@ driving_flying_vehicle
004D: jump_if_false @SF_CHECK_BOAT
00AD: set_car 11@ max_speed_to 0.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SF_CHECK_BOAT
00D6: if
    04A7: actor 3@ driving_boat
004D: jump_if_false @SF_MODE_CHECK
00AD: set_car 11@ max_speed_to 0.0
0006: 30@ = 0
0002: jump @MAIN_LOOP

// Modo PARADO (29@==9): para imediatamente, sem emitir follow.
// 00A9: cancela task activa (07F8 ou 06E1). Apenas max_speed 0.0 nao e suficiente
// — o motor de IA continua a tentar seguir mesmo com velocidade maxima 0.
// 30@=0: forca re-emissao quando o modo for alterado para nao-PARADO.
:SF_MODE_CHECK
00D6: if
    0038: 29@ == 9
004D: jump_if_false @SF_DRIVE_MODE
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SF_DRIVE_MODE
// DIRETO (29@==7): 07F8 raio 10m, dedup 22@==30@.
// CIVICO-F (29@==6): Lookahead -30m(atras) 04D3+02C2, dedup countdown 30@.
// AUTONOMO (29@==8): 0AB6 GPS waypoint + 05D1 DriveAccurate, dedup countdown 30@.
// CIVICO-D/E (29@==4/5): 06E1 EscortFar/FollowFar, dedup 22@==30@.
// CIVICO-0/A/B/C (29@<=3): coord-based + speed guard, dedup countdown 30@.
:SF_APPLY_FOLLOW
00D6: if
    0038: 29@ == 7
004D: jump_if_false @SF_CIVICO_F_CHECK
// DIRETO (7): AvoidCars (2) + raio 10m, dedup por carro do jogador.
00D6: if
    0038: 22@ == 30@
004D: jump_if_false @SF_DIRETO_ISSUE
0002: jump @MAIN_LOOP
:SF_DIRETO_ISSUE
0006: 30@ = 22@
00AD: set_car 11@ max_speed_to 60.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 0
07F8: car 11@ follow_car 22@ radius 10.0
0002: jump @MAIN_LOOP
:SF_CIVICO_F_CHECK
00D6: if
    0038: 29@ == 6
004D: jump_if_false @SF_CIVICO_AUTO_CHECK
// CIVICO-F (6): Lookahead -30m — ponto 30m ATRAS do carro do jogador.
//
// Logica: 0407 com offset Y=-30m aponta para onde o jogador ESTEVE.
// Recruta sempre navega para tras do jogador → nunca se coloca a frente.
// 04D3: snap desse ponto para o no de estrada mais proximo.
// 02C2 (GotoCoordsAccurate=12): path preciso via road nodes.
// Guarda: STOP <10m, SLOW <20m — impede que recruta derrape para a frente do jogador.
// AvoidCars(2): nao para em semaforos, desvia de obstaculos. Sem 073B.
// max 30 km/h. Threshold 5 ticks (1.5s): mais estavel que 3.
00D6: if
    0019: 30@ > 0
004D: jump_if_false @SF_CIVICO_F_ISSUE
000F: 30@ -= 1
0002: jump @MAIN_LOOP
:SF_CIVICO_F_ISSUE
// CIVICO-F: guarda anti-colisao — "derrapa na frente" ocorre quando
// o recruta aborda a alta velocidade e o jogador trava. Guard impede isso.
// 00F2: ator-a-ator (funciona com ambos dentro do carro).
// Zona STOP (<=10m): cancela task + max_speed 0. Para completamente.
00D6: if
    00F2: 10@ 3@ 10.0 10.0 0
004D: jump_if_false @SF_F_SLOW_CHECK
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SF_F_SLOW_CHECK
// Zona SLOW (10-20m inclusive): limita a 15 km/h, re-avalia no proximo tick.
00D6: if
    00F2: 10@ 3@ 20.0 20.0 0
004D: jump_if_false @SF_F_DISPATCH
00AD: set_car 11@ max_speed_to 15.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SF_F_DISPATCH
0407: 22@ 0.0 -30.0 0.0 6@ 7@ 8@  // 30m ATRAS do carro do jogador
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 30.0
00AE: set_car 11@ traffic_behaviour_to 2
// 00AF=1 (follow road + drive back if blocked): road navigation via 02C2.
00AF: set_car 11@ driver_behaviour_to 1
02C2: car 11@ drive_to 6@ 7@ 8@
0006: 30@ = 5
0002: jump @MAIN_LOOP
:SF_CIVICO_AUTO_CHECK
00D6: if
    0038: 29@ == 8
004D: jump_if_false @SF_CIVICO_06E1_CHECK
// ---------------------------------------------------------------
// AUTONOMO (8): recruta navega para o waypoint GPS do mapa.
// Jogador pode seguir atras para avaliar a conducao de forma
// independente — sem interferir na trajetoria do recruta.
//
// 0AB6: le coords do waypoint radar do mapa (IF+SET).
//   SASCM: 0AB6=3,store_target_marker_coords_to %1d% %2d% %3d%
//   Retorna true+coords se waypoint activo; false se nao ha waypoint.
// 05D1 DriveMode=Accurate(1): TaskCarDriveToCoordDriver com pathfinding
//   mais preciso. DriveMode=Accurate optimiza faixa em road nodes.
// AvoidCars(2): nao para em semaforos, desvia de obstaculos.
// Threshold 30 ticks (9s): destino e fixo — pouco re-emissao necessaria.
// NOTA: Mapa SA armazena Z=0 para o waypoint. 00AA le o Z real do carro
//   do recruta para nao dar destino subterraneo.
// ---------------------------------------------------------------
00D6: if
    0019: 30@ > 0
004D: jump_if_false @SF_AUTONOMO_ISSUE
000F: 30@ -= 1
0002: jump @MAIN_LOOP
:SF_AUTONOMO_ISSUE
00D6: if
    0AB6: 6@ 7@ 8@
004D: jump_if_false @SF_AUTONOMO_STOP
// Waypoint definido: snap para no de estrada e navega via actor task.
// 04D3 corrige X/Y/Z para o no de estrada real — Z do waypoint do mapa (tipicamente 0)
// e substituido pelo Z do no de estrada mais proximo, evitando destino subterraneo.
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 50.0
00AE: set_car 11@ traffic_behaviour_to 2
// 05D1: actor=10@ car=11@ destX=6@ destY=7@ destZ=8@ speed=50 DriveMode=Accurate(1) model=0 DrivingMode=AvoidCars(2)
05D1: 10@ 11@ 6@ 7@ 8@ 50.0 1 0 2
0006: 30@ = 30
0002: jump @MAIN_LOOP
:SF_AUTONOMO_STOP
// Sem waypoint: para e pede para marcar ponto no mapa.
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0ACD: show_text_highpriority "AUTONOMO: marque um waypoint no mapa! (4 mudar)" 3000
0006: 30@ = 10
0002: jump @MAIN_LOOP
:SF_CIVICO_06E1_CHECK
// CIVICO-D (4) e CIVICO-E (5): 06E1 auto-gere; dedup por carro do jogador.
// 06E1 tem gestao de distancia interna — nao usa speed guard.
// Velocidade 35 km/h: mais baixa para melhor navegacao em curvas.
00D6: if
    0019: 29@ > 3
004D: jump_if_false @SF_CIVICO_COUNTDOWN
00D6: if
    0038: 22@ == 30@
004D: jump_if_false @SF_06E1_ISSUE
0002: jump @MAIN_LOOP
:SF_06E1_ISSUE
00D6: if
    0038: 29@ == 5
004D: jump_if_false @SF_CIVICO_D
// CIVICO-E (5): 06E1 FollowCarFaraway(52) + AvoidCars(2).
// AvoidCars(2): nao para em semaforos, desvia de obstaculos. Sem 073B.
// Velocidade 35 km/h: permite que SA complete curvas sem overshooting.
00AD: set_car 11@ max_speed_to 35.0
00AE: set_car 11@ traffic_behaviour_to 2
06E1: 10@ 11@ 22@ 52 35.0 0
0006: 30@ = 22@
0002: jump @MAIN_LOOP
:SF_CIVICO_D
// CIVICO-D (4): 06E1 EscortRearFaraway(67) + AvoidCars(2).
// AvoidCars(2): nao para em semaforos, desvia de obstaculos. Sem 073B.
// Velocidade 35 km/h: formacao geometrica mais estavel em curvas.
00AD: set_car 11@ max_speed_to 35.0
00AE: set_car 11@ traffic_behaviour_to 2
06E1: 10@ 11@ 22@ 67 35.0 0
0006: 30@ = 22@
0002: jump @MAIN_LOOP
:SF_CIVICO_COUNTDOWN
// CIVICO-0/A/B/C (0-3): coord-based tasks, contagem regressiva.
00D6: if
    0019: 30@ > 0
004D: jump_if_false @SF_COORD_ISSUE
000F: 30@ -= 1
0002: jump @MAIN_LOOP
:SF_COORD_ISSUE
// ---------------------------------------------------------------
// GUARDA DE VELOCIDADE ANTI-COLISAO (req: nao bater no jogador)
//
// Alvo: 20m ATRAS do carro do jogador (0407 Y=-20 espaco local).
//   Recruta nunca alcanca a posicao exacta do jogador — colisao evitada
//   estruturalmente. 0407 requer jogador em veiculo (22@ valido):
//   garantido pois este bloco so executa dentro de "actor 3@ driving".
//
// Solucao em duas zonas usando 00F2 (ator-a-ator):
//   00F2 funciona quando ator esta dentro de carro — posicao do ator
//   equivale a posicao do veiculo quando ele esta a conduzir.
//   Ref: SASCM — 00F2=5, actor %1d% near_actor %2d% radius %3d% %4d% %5h%
//
//   Zona STOP (<8m): 00A9 cancela task activa + max_speed 0.
//     00A9 necessario: sem cancelar, o carro continua a tentar atingir
//     o alvo mesmo com max_speed 0.
//   Zona SLOW (8-18m): max_speed 15 km/h, 30@=0 para re-avaliar.
//     Acompanha abrandamentos dinamicos do jogador sem re-emitir destino.
//     Quando jogador acelera e fica >18m, 30@=0 → dispatch normal.
//   Com alvo a -20m: recruta natural em ~20m do jogador (fora da zona SLOW).
//   Zonas so activam se o recruta ULTRAPASSAR o alvo (ex: jogador travou).
//
// Como o SA road graph lida com curvas:
//   00A7/02C2/05D1 define destino final. SA internamente planeia o
//   caminho no-a-no via CCarCtrl_PickNextNodeAccordingStrategy.
//   Em cada no, CCarCtrl_FindSpeedMultiplierWithSpeedFromNodes le o
//   limite de velocidade do LINK (conexao entre nos) e reduz velocidade
//   automaticamente em curvas fechadas. Scripts nao podem ler esses
//   valores mas BENEFICIAM deles ao usar 00A7/02C2/05D1 com destino snapped.
//   CHAVE: nao re-emitir demasiado rapido (< 3 ticks) — interrompe a
//   transicao no-a-no no meio da curva. Com alvo -20m o caminho e mais
//   longo (recruta nunca chega ao alvo antes do proximo dispatch), pelo
//   que 3 ticks (0.9s) sao suficientes para CIVICO-0/A/B.
//   CIVICO-C (Accurate) usa 7 ticks (2.1s): ClipToLink precisa de mais tempo.
// ---------------------------------------------------------------
// Alvo: 20m ATRAS do carro do jogador em espaco local → 0407 Y=-20.
// 0407: get_offset_from_car car=22@ right=0.0 front=-20.0 up=0.0 → outX=6@ outY=7@ outZ=8@
// 04D3 dentro de cada modo faz o snap para no de estrada.
0407: 22@ 0.0 -20.0 0.0 6@ 7@ 8@
// Zona STOP: recruta demasiado proximo → cancela task e para.
00D6: if
    00F2: 10@ 3@ 8.0 8.0 0
004D: jump_if_false @SC_SLOW_CHECK
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SC_SLOW_CHECK
// Zona SLOW: recruta proximo → abrandamento, re-avalia no proximo tick.
00D6: if
    00F2: 10@ 3@ 18.0 18.0 0
004D: jump_if_false @SC_DISPATCH
00AD: set_car 11@ max_speed_to 15.0
0006: 30@ = 0
0002: jump @MAIN_LOOP
:SC_DISPATCH
// Recruta fora das zonas de guarda → dispatch normal por modo.
// 6@/7@/8@ ja contem o ponto -20m (calculado antes de SF_COORD_ISSUE):
// cada modo faz 04D3 para snap ao no de estrada mais proximo.
00D6: if
    0038: 29@ == 3
004D: jump_if_false @SF_CIVICO_B_CHECK
// CIVICO-C (3): 0407(-20m) + 04D3 + 05D1 DriveMode=Accurate(1) actor task + AvoidCars(2).
// 6@/7@/8@ ja contem o ponto -20m calculado antes de SF_COORD_ISSUE.
// ---------------------------------------------------------------
// COMO O SA FAZ CURVAS (road node graph):
//   CCarCtrl_PickNextNodeAccordingStrategy: escolhe o proximo no da rota.
//   CCarCtrl_FindSpeedMultiplierWithSpeedFromNodes (0x424130): le o limite
//     de velocidade do LINK (conexao entre nos) e calcula multiplicador.
//     Em curvas fechadas → multiplicador baixo → SA trava automaticamente.
//     Este mecanismo corre a cada frame via CCarAI_UpdateCarAI — e
//     AUTOMATICO e correcto sem intervencao do script.
//   CCarCtrl_ClipTargetOrientationToLink (0x422760): alinha orientacao
//     alvo ao eixo da faixa da estrada — activo apenas em DriveMode=Accurate.
//   CCarCtrl_DealWithBend_Racing: olha 4 nos a frente (so modo Racing).
//
// Normal(0) vs Accurate(1) em 05D1:
//   Normal   → CCarAI_GetCarToGoToCoors         → CarMission=GotoCoords(8)
//   Accurate → CCarAI_GetCarToGoToCoorsAccurate → CarMission=GotoCoordsAccurate(12)
//   Accurate usa ClipTargetOrientationToLink: mantem faixa em curvas, trava
//   mais cedo e de forma mais suave. Normal pode cortar curvas.
//   Actor task (vs 02C2 directo do CIVICO-A): adiciona TempActions de gestao
//   de stuck (Reverse=3 quando preso, HandbrakeTurnLeft/Right em curvas).
//
// Threshold 7 ticks (2.1s): SA precisa de tempo para completar transicao
//   no-a-no. Re-emissao a meio da curva cancela o travamento automatico.
// ---------------------------------------------------------------
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 45.0
00AE: set_car 11@ traffic_behaviour_to 2
// 05D1: actor=10@ car=11@ destX=6@ destY=7@ destZ=8@ speed=45 DriveMode=Accurate(1) model=0 DrivingMode=AvoidCars(2)
05D1: 10@ 11@ 6@ 7@ 8@ 45.0 1 0 2
0006: 30@ = 7
0002: jump @MAIN_LOOP
:SF_CIVICO_B_CHECK
00D6: if
    0038: 29@ == 2
004D: jump_if_false @SF_CIVICO_A_CHECK
// CIVICO-B (2): 0407(-20m) + 04D3 + 05D1 DriveMode=Normal(0) actor task + AvoidCars(2).
// 6@/7@/8@ ja contem o ponto -20m calculado antes de SF_COORD_ISSUE.
// Diferenca vs CIVICO-A (02C2 directo):
//   05D1 usa o sistema de ACTOR TASKS do SA — o task monitoriza o progresso
//   e pode emitir TempActions (ex: Reverse=3 quando preso, HandbrakeTurn em
//   curvas fechadas). 02C2 (CarMission directa) nao tem este mecanismo.
// DriveMode=Normal(0) → CCarAI_GetCarToGoToCoors → CarMission=GotoCoords(8) interno.
// AvoidCars(2): nao para em semaforos. Sem 073B. max 40 km/h. Threshold 3 ticks.
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 40.0
00AE: set_car 11@ traffic_behaviour_to 2
// 05D1: actor=10@ car=11@ destX=6@ destY=7@ destZ=8@ speed=40 DriveMode=Normal(0) model=0 DrivingMode=AvoidCars(2)
05D1: 10@ 11@ 6@ 7@ 8@ 40.0 0 0 2
0006: 30@ = 3
0002: jump @MAIN_LOOP
:SF_CIVICO_A_CHECK
00D6: if
    0038: 29@ == 1
004D: jump_if_false @SF_CIVICO_0
// CIVICO-A (1): 0407(-20m) + 04D3 + 02C2 (GotoCoordsAccurate=12) CarMission directa + AvoidCars(2).
// 6@/7@/8@ ja contem o ponto -20m calculado antes de SF_COORD_ISSUE.
// 02C2 usa GotoCoordsAccurate=12: usa CCarAI_GetCarToGoToCoorsAccurate internamente.
// Sem actor task: CarMission gerida por CCarAI_UpdateCarAI a cada frame.
// 00AF=1 (follow road + drive back if blocked): complementa a road navigation de 02C2.
// AvoidCars(2): nao para em semaforos. Sem 073B. max 40 km/h. Threshold 3 ticks.
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 40.0
00AE: set_car 11@ traffic_behaviour_to 2
// 00AF bitmask (set_car_driver_behaviour, sa-db 2008 confirmed):
//   0001=1: follow road + drive back if blocked (correcto para road navigation)
//   0010=2: kill player | 0100=4: drive to player and stop | 1000=8: ignore road-paths
// Usamos 1 (follow road) para que a AI do SA siga a estrada correctamente.
00AF: set_car 11@ driver_behaviour_to 1
02C2: car 11@ drive_to 6@ 7@ 8@
0006: 30@ = 3
0002: jump @MAIN_LOOP
:SF_CIVICO_0
// CIVICO-0 (0): 0407(-20m) + 04D3 (snap road node) + 00A7 (GotoCoords=8) CarMission directa + AvoidCars(2).
// 6@/7@/8@ ja contem o ponto -20m calculado antes de SF_COORD_ISSUE.
// 00A7 usa GotoCoords=8: usa CCarAI_GetCarToGoToCoors internamente (menos preciso que A/B/C).
// 04D3 garante alvo num no de estrada valido. AvoidCars(2). Sem 073B. max 40 km/h.
// Threshold 3 ticks (0.9s): alvo -20m move-se com o jogador, sem risco de colisao directa.
// 00AF=1 (follow road): activa road-following behaviour para complementar a CarMission de 00A7.
04D3: 6@ 7@ 8@ 0 6@ 7@ 8@
00AD: set_car 11@ max_speed_to 40.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 1
00A7: car 11@ drive_to 6@ 7@ 8@
0006: 30@ = 3
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
// Modo PARADO (29@==9): recruta nao avanca mesmo com jogador a pe
// 00A9: cancela task 00A7 drive_to residual (ex: jogador saiu do carro
// enquanto recruta ainda se aproximava). Sem 00A9, o carro continua a
// dirigir para o ultimo destino mesmo com max_speed 0.0.
00D6: if
    0038: 29@ == 9
004D: jump_if_false @FPF_DO_ZONES
00A9: car 11@ to_normal_driver
00AD: set_car 11@ max_speed_to 0.0
0002: jump @MAIN_LOOP
:FPF_DO_ZONES
// 00A0: binary order (char_handle, →outX, →outY, →outZ)
00A0: 3@ 6@ 7@ 8@
// Zona STOP: recruta muito proximo → para completamente
00D6: if
    00F2: 10@ 3@ 4.0 4.0 0
004D: jump_if_false @FPF_CREEP_ZONE
00AD: set_car 11@ max_speed_to 0.0
00AE: set_car 11@ traffic_behaviour_to 0
0002: jump @MAIN_LOOP
// Zona CREEP: entre 4m e 12m → avanca devagar sem drive_to
:FPF_CREEP_ZONE
00D6: if
    00F2: 10@ 3@ 12.0 12.0 0
004D: jump_if_false @FPF_DRIVE_CLOSER
00AD: set_car 11@ max_speed_to 5.0
00AE: set_car 11@ traffic_behaviour_to 0
0002: jump @MAIN_LOOP
// Zona CHASE: fora dos 12m → dirigir em direcao ao jogador
// DIRETO (29@==7): AvoidCars + max 30 km/h.
// Todos os outros CIVICO (0-6, AUTONOMO=8): StopForCars + max 25 ou 20 km/h.
// Velocidades baixas → StopForCars(0) e aceitavel para abordagem pedonal cautelosa.
:FPF_DRIVE_CLOSER
00D6: if
    0038: 29@ == 7
004D: jump_if_false @FPF_CHASE_CIVICO_HIGH
00AD: set_car 11@ max_speed_to 30.0
00AE: set_car 11@ traffic_behaviour_to 2
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP
:FPF_CHASE_CIVICO_HIGH
// Modos CIVICO-C/D/E/F/AUTONOMO (3,4,5,6,8): StopForCars + max 25.
// 29@>2 cobre tambem AUTONOMO(8): comportamento identico em chase pedonal.
00D6: if
    0019: 29@ > 2
004D: jump_if_false @FPF_CHASE_CIVICO
00AD: set_car 11@ max_speed_to 25.0
00AE: set_car 11@ traffic_behaviour_to 0
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP
:FPF_CHASE_CIVICO
// CIVICO-0/A/B (0,1,2): StopForCars (0) — NPC padrao, para em fila
00AD: set_car 11@ max_speed_to 20.0
00AE: set_car 11@ traffic_behaviour_to 0
00A7: car 11@ drive_to 6@ 7@ 8@
0002: jump @MAIN_LOOP

// ---------------------------------------------------------------
// RECRUTA SAIU DO VEICULO VOLUNTARIAMENTE (desync de estado)
//
// Chega aqui quando: 056E ok (carro existe) + 0449 falso (recruta fora).
// Causa principal: IA de grupo SA emite exit_car automaticamente
// quando o lider esta a pe perto — ocorre em carros 4 portas.
// 06C9 em SETUP_VEHICLE_AI previne para recrutas do mod; este bloco
// captura qualquer caso residual (vanilla, timing, etc.).
//
// Re-integra ao grupo e re-ativa follow_actor.
// 06C9 foi chamado em DO_ENTER_VEHICLE — recruta esta fora do grupo.
// ---------------------------------------------------------------
:RECRUIT_EXITED_VOLUNTARILY
// 11@ preservado: carro ainda existe no mundo, recruta pode re-entrar com 2
0006: 12@ = 1
00D6: if
    0038: 23@ == 0
004D: jump_if_false @REV_DONE
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @REV_REJOIN_DONE
0631: 24@ 10@
087F: 10@ 1
:REV_REJOIN_DONE
0850: AS_actor 10@ follow_actor 3@
:REV_DONE
0ACD: show_text_highpriority "Recruta saiu (grupo SA). 2 para retomar carro." 2500
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
// Re-integra ao grupo (removido em DO_ENTER_VEHICLE para evitar bug de saida).
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @RLC_REJOIN_DONE
0631: 24@ 10@
087F: 10@ 1
:RLC_REJOIN_DONE
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

// 0961: keep_tasks_after_cleanup — impede que o motor limpe as tarefas do recruta
// em eventos de "cleanup" (re-carregamento de area, reset de missao).
// Garante que o recruta nunca desaparece por despawn silencioso.
// SASCM.ini: 0961=2,set_actor %1d% keep_tasks_after_cleanup %2h%
0961: set_actor 10@ keep_tasks_after_cleanup 1

// Marca como recruta do mod (nao vanilla) — permite 01C2 no cleanup
0006: 23@ = 0
0006: 12@ = 1

// Verificar se ha carro guardado desta sessao para o recruta
00D6: if
    0019: 11@ > 0
004D: jump_if_false @SPAWN_MSG_NO_CAR
00D6: if
    056E: car 11@ defined
004D: jump_if_false @SPAWN_MSG_NO_CAR
0ACD: show_text_highpriority "Recruta spawnado! Aperte 2 para retomar carro guardado." 3500
0002: jump @SPAWN_DEBOUNCE
:SPAWN_MSG_NO_CAR
0ACD: show_text_highpriority "Recruta spawnado! Aperte 2 para buscar veiculo." 3000
:SPAWN_DEBOUNCE

// Debounce: evita re-disparo imediato da tecla 1
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
// Remove do grupo ANTES de 05CB.
// Causa raiz do bug "4 portas": a IA de grupo SA monitora seus membros
// e emite exit_car automaticamente quando o lider esta a pe perto de
// um membro que entrou num carro. Em bikes isso nao ocorre (SA trata
// 2 rodas diferente no grupo AI). Removendo o recruta do grupo ANTES
// de 05CB, a IA de grupo nunca tem autoridade sobre ele e nunca
// enfileira o exit. Reintegrado ao grupo em todos os caminhos de
// retorno ao estado a pe (U_EXIT, RECRUIT_LOST_CAR, etc.).
// Aplica apenas a recrutas do mod (23@==0).
00D6: if
    0038: 23@ == 0
004D: jump_if_false @DEV_GROUP_DONE
06C9: remove_actor_from_group 10@
:DEV_GROUP_DONE
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
// 06C9 foi chamado em DO_ENTER_VEHICLE — re-integrar ao grupo aqui.
0ACD: show_text_highpriority "Recruta nao conseguiu entrar! Tente 2 novamente." 2500
0006: 11@ = 0
00D6: if
    0038: 23@ == 0
004D: jump_if_false @WE_TIMEOUT_DONE
07AF: 0 24@
00D6: if
    0019: 24@ > 0
004D: jump_if_false @WE_REJOIN_DONE
0631: 24@ 10@
087F: 10@ 1
:WE_REJOIN_DONE
0850: AS_actor 10@ follow_actor 3@
:WE_TIMEOUT_DONE
0002: jump @MAIN_LOOP
//
// Os opcodes abaixo definem o comportamento inicial do motorista.
// Serao sobrescritos na primeira iteracao do MAIN_LOOP pelo modo 29@.
//
// 00AD: set_car max_speed_to — velocidade maxima em unidades internas (~km/h).
//   Ref: Project Cerbera — velocidades reais de handling SA
//        https://projectcerbera.com/gta/sa/tutorials/handling
//
// 00AE: set_car traffic_behaviour_to — estilo de trafego para modos 07F8.
//   Inicializado com 0 (StopForCars) como fallback. CIVICO-A/B usam 06E1
//   (que ignora 00AE) e CIVICO-C/DIRETO definem 00AE no proprio re-issue.
//   Ref: GTAMods Wiki — opcode 00AE; enums.txt DrivingMode.
//
// 00AF: set_car_mission — usar sempre 0 (None) para modos 07F8; nao chamar
//   para modos 06E1 (a task 06E1 define internamente a CarMission=52/53).
//
// Nota — erro 0097: parametros float (max_speed, radius) devem ter
// sufixo .0 explicitamente no Sanny Builder para evitar erro de tipo.
// ===============================================================
:SETUP_VEHICLE_AI

// Resistencia do carro do recruta: sem dano visual (como carros de missao).
// Fumo continua a aparecer via limiar de saude (comportamento vanilla).
// 0852: desativa amassados e riscos visuais. 0224: repoe HP total (int).
0852: set_car 11@ damages_visible 0
0224: set_car 11@ health_to 1750

// Inicializa com CIVICO-0 (modo padrao 29@=0). 04D3+00A7 sera emitido no primeiro loop.
// AvoidCars(2): nao para em semaforos, desvia de carros — padrao para todos os modos.
// 073B REMOVIDO: contramao permitida — melhora navegacao em cruzamentos e curvas
// onde o no de estrada correcto esta no sentido oposto.
00AD: set_car 11@ max_speed_to 40.0
00AE: set_car 11@ traffic_behaviour_to 2
00AF: set_car 11@ driver_behaviour_to 0

// 0526: previne que o recruta seja arrancado do banco do motorista por NPCs.
// Ref: SASCM.ini — 0526=2,set_actor %1d% stay_in_car_when_jacked %2d%
0526: set_actor 10@ stay_in_car_when_jacked 1

// 06C9 ja foi chamado em DO_ENTER_VEHICLE antes de 05CB.
// Nao repetir aqui — recruta ja esta fora do grupo desde antes de entrar.

// Reset 30@ para forcar re-emissao na primeira iteracao do loop.
0006: 30@ = 0

0006: 12@ = 2

0ACD: show_text_highpriority "Seguindo! 4=0:CIV0 1:A 2:B 3:C 4:D 5:E 6:F 7:DIR 8:AUTO 9:STOP" 3000

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
// 056D check: operar com handle so quando e valido (evita crash em 06C9)
00D6: if
    056D: actor 10@ defined
004D: jump_if_false @CLEANUP_CAR
// 06C9: remove_actor from_group — desfaz o 0631 feito em SPAWN_RECRUIT.
// Remove a referencia ao ped do CPedGroup antes de liberar o handle.
// Evita referencia morta no grupo nativo apos 01C2.
// SASCM.ini: 06C9=1,remove_actor %1d% from_group
06C9: 10@
01C2: mark_actor_as_no_longer_needed 10@

// Persistencia do carro: so libertar referencia se o carro foi destruido.
// Se o carro ainda existe no mundo, preservamos 11@ para que o proximo
// recruta possa retomar directamente (aperte 2 apos spawn).
:CLEANUP_CAR
00D6: if
    0019: 11@ > 0
004D: jump_if_false @CLEANUP_DONE
00D6: if
    056E: car 11@ defined
004D: jump_if_false @CLEANUP_CAR_GONE
// Carro ainda existe — preservar handle para persistencia
0002: jump @CLEANUP_DONE
:CLEANUP_CAR_GONE
// Carro destruido — liberar referencia
0006: 11@ = 0

:CLEANUP_DONE
0006: 10@ = 0
// 11@ preservado intencionalmente se carro existe (ver CLEANUP_CAR acima)
0006: 12@ = 0
0006: 14@ = 0
0006: 15@ = 1
0006: 23@ = 0
0006: 29@ = 0
0006: 30@ = 0
0006: 31@ = 0
// Resetar drive-by mode ao sair (seguranca)
0501: set_player 0 driveby_mode 0
0ACD: show_text_highpriority "Recruta liberado da memoria." 2000
0001: wait 500 ms
0002: jump @MAIN_LOOP
