# Grove Street Recruit — Vehicle AI Follower

Mod CLEO para **GTA San Andreas** que substitui o comportamento "ímã" dos companheiros de gangue por **IA veicular real**: o recruta dirige por conta própria, desvia de obstáculos, respeita o tráfego e mantém distância de segurança do jogador — tudo usando os opcodes nativos do motor **RenderWare**.

> **Contexto:** no jogo original, recrutas simplesmente teleportam ou correm em linha reta para o jogador, sem lógica de rua, sem desvio de carros, sem regras de trânsito. Este mod corrige isso com a API interna do próprio jogo.

---

## Controles

| Tecla | Ação |
|-------|------|
| **1** | Spawna 1 recruta Grove Street **a pé** (modelo aleatório: fam1, fam2 ou fam3) e o adiciona ao grupo nativo do jogador |
| **2** (recruta a pé) | O recruta **procura o veículo desocupado mais próximo**, entra como motorista e passa a seguir o jogador com IA veicular |
| **2** (recruta em veículo) | O recruta **sai do veículo** e volta a seguir o jogador a pé |
| **3** (recruta em veículo, jogador a pé) | Jogador **entra como passageiro** no carro do recruta — recruta passa a navegar para o waypoint do mapa (ou 150m à frente) como NPC normal, respeitando semáforos |
| **3** (estado 3 — jogador passageiro) | Jogador **sai do carro** do recruta e recruta volta ao modo seguimento (estado 2) |
| **4** (recruta ativo) | Cicla o **modo de velocidade** do seguimento: `PARADO` → `CALMO (20 km/h)` → `NORMAL (50 km/h)` → `URGENTE (80 km/h)` |

> **Integração Vanilla:** se você recrutou alguém normalmente no jogo (apontando arma), o mod **detecta automaticamente** o recruta vanilla no teu grupo e passa a controlá-lo também — sem precisar apertar 1. A detecção ocorre no topo de cada iteração do loop quando `12@==0`.

---

## Requisitos

- **GTA San Andreas** — versão PC 1.0 US (ou downgraded de 2.0)
- **CLEO 4** ou superior — [cleo.li](https://cleo.li)
- **Sanny Builder 3** — [sannybuilder.com](https://sannybuilder.com)

---

## Instalação

1. Abra `grove_recruit_follow.cs` no **Sanny Builder 3** com o modo **GTA San Andreas** selecionado.
2. Compile: menu **Run → Compile** ou pressione `F6`. O arquivo `.cs` compilado será gerado na mesma pasta.
3. Copie o `.cs` compilado para a pasta **CLEO** dentro do diretório do GTA SA:
   ```
   GTA San Andreas\CLEO\grove_recruit_follow.cs
   ```
4. Inicie o jogo. O CLEO carrega o mod automaticamente junto com o jogo.

> **Nota:** se aparecer a mensagem *"Grove Recruit Mod: 1=Spawnar | 2=Veiculo | 3=Recruta dirige | 4=Modo velocidade"* na tela após carregar o save, o mod está ativo.

---

## Arquitetura — 3 Módulos

O script é dividido em três módulos independentes que cooperam via máquina de estados (`12@`).

### Módulo 1 — Detecção de Estado do Jogador

O loop principal executa a cada **300 ms** e monitora continuamente:

```
Loop Principal (300ms):
├── Recruta morreu / despawnou? → CLEANUP automático (sem precisar apertar U)
├── Nenhum recruta ativo (12@==0)?
│   └── Scan do grupo nativo via 092B → adotar vanilla recruit se existir
├── Jogador está a pé?
│   └── Recruta em veículo → zona de segurança 12m:
│       ├── < 12m: max_speed 2.0 (freia sem atropelar)
│       └── ≥ 12m: drive_to a 20 km/h (abordagem cautelosa)
├── Jogador entrou num carro?
│   └── Recruta em veículo → recruta segue o carro do jogador (07F8)
└── Veículo do recruta destruído?
    └── Recruta volta ao modo a pé (0850)
```

### Módulo 2 — Aquisição de Veículo

Ao pressionar **U** (quando recruta está a pé):

1. **Localiza** o carro mais próximo ao recruta — `0AB5: store_closest_entities`
2. **Valida** o handle com `056E: car defined`
3. **Verifica** que não é o veículo do jogador (comparação de handles)
4. **Executa entrada** como motorista via `05CB: task_enter_car_as_driver`
5. **Aguarda confirmação** de que está dirigindo com `00DF`, com timeout de 5 segundos

Ao pressionar **U** (quando recruta está em veículo):

1. **Executa saída** via `0633: AS_actor exit_car` (animação nativa de sair)
2. **Reativa** seguimento a pé com `0850: task_follow_footsteps`

### Módulo 3 — Estilos de Condução (`traffic_behaviour`)

| Valor | Constante | Comportamento |
|-------|-----------|---------------|
| `0` | `STOPFORCARS` | Para em semáforos e no trânsito |
| `1` | `SLOWDOWNFORCARS` | Desacelera perto de outros carros (zona de segurança a pé) |
| `2` | **`AVOIDCARS`** | Ignora semáforos, desvia ativamente de carros (**modo padrão ao seguir**) |
| `3` | `PLOUGHTHROUGH` | Ignora absolutamente tudo — **nunca usar para aliados** |
| `5` | `FOLLOWTRAFFIC_AVOIDCARS` | Respeita semáforos **e** desvia de lentidão (alternativa mais realista) |

---

## Maquina de Estados

```
Estado 0 — Nenhum recruta ativo
     │                              ┌── Scan vanilla (092B): grupo tem membro?
     │ Tecla 1 pressionada          │   └── SIM → adoptar (23@=1) → Estado 1
     ▼                              ▼
Estado 1 — Recruta a pé ◄──────────────────────────────────────────────────┐
     │   → task_follow_footsteps (0850)                                      │
     │                                                                        │
     │ 2 + carro encontrado + recruta entrou (00DF confirmado)                │
     ▼                                                                        │
Estado 2 — Recruta em veículo                                                │
     │   ├── Jogador em carro → 07F8 follow_car radius 10.0                  │
     │   │     (velocidade controlada pela tecla 4: PARADO/CALMO/NORMAL/URGENTE)
     │   └── Jogador a pé    → 3 zonas: STOP(4m) / CREEP(12m) / CHASE       │
     │                                                                        │
     ├── 2 pressionado → 0633 exit_car recruta ──────────────────────────►  │
     ├── 3 pressionado → 05CA player entra passageiro ──────────────────── Estado 3
     ├── Veículo destruído (056E false) ──────────────────────────────────► Estado 1
     └── Recruta morreu (056D false) ─────────────────────────────────── CLEANUP → Estado 0

Estado 3 — Jogador passageiro, recruta dirige (NPC normal, respeita semáforos)
     │   → Se waypoint no mapa: drive_to waypoint GPS
     │   → Sem waypoint: 0407 offset +Y 150m → 00A7 drive_to (refresh 300ms)
     │   → traffic_behaviour 0 (STOPFORCARS) + 00A9 to_normal_driver
     │
     ├── 3 pressionado → 0633 player exit_car ──────────────────────────► Estado 2
     ├── Jogador saiu sozinho (0449 false) ──────────────────────────────► Estado 2
     ├── Veículo destruído (056E false) ──────────────────────────────────► Estado 1
     └── Recruta morreu (056D false) ─────────────────────────────────── CLEANUP → Estado 0
```

---

## Por Que Estas Escolhas?

| Decisão | Razão |
|---------|-------|
| **`07F8: follow_car`** em vez de `00A7: drive_to` | `follow_car` rastreia o **handle dinâmico** do carro, recalculando em tempo real. `drive_to` vai para coordenada **estática** — recruta pararia no ponto mesmo se jogador já saiu. |
| **`092B` slot** em vez de `073F`+`06EE` para vanilla scan | Acesso direto ao `CPedGroup` por índice — sem depender de raio, pedtype ou posição. Mais confiável e eficiente. |
| **`0631` + `06F0` + `087F`** no SPAWN | Mod-spawned recruits entram no grupo nativo — recebem vozes, IA de combate e cobertura idênticas aos recrutas vanilla. |
| **`06C9`** no CLEANUP | Remove referência ao ped do `CPedGroup` antes de `01C2`, evitando ponteiro morto. |
| **Zona de segurança 12m** (`00F2`) | Impede que o carro do recruta entre na hitbox do jogador quando ele está a pé — resolve o bug de atropelamento. |
| **`AVOIDCARS` (modo 2)** | Recruta é aliado — atropelar civis gera wanted level desnecessário. Modo 2 desvia ativamente sem ser lento. |
| **`pedtype 8`** | `PEDTYPE_GANG1` (Grove Street Families) — animações de gangue, vozes e comportamento de grupo corretos. |
| **`056D/056E`** antes de qualquer operação | Operar sobre handle de ped morto ou carro destruído causa **crash imediato**. Sempre validar. |
| **`01C2/01C3`** no cleanup | Não destroem — transferem responsabilidade ao motor para despawn natural. Essencial para os **pools** (~110 peds, ~110 veículos). |

---

## Opcodes Utilizados

| Opcode | Nome | Descrição |
|--------|------|-----------|
| `0AB0` | `key_pressed` | Detecta tecla do teclado via VK code (1=49, 2=50, 3=51, 4=52) |
| `0256` | `player defined` | Verifica se `$PLAYER_CHAR` está ativo |
| `01F5` | `get_player_actor` | Obtém handle do ped do jogador (P1=player_num, P2→output) |
| `07AF` | `player group` | Obtém handle do grupo nativo do jogador |
| `07F6` | `get_group_size` | Retorna contagem de líderes e membros do grupo |
| `092B` | `get_group_member` | Obtém handle do membro por slot (P1=group, P2=slot, P3→handle) |
| `0631` | `put_actor_in_group` | Adiciona ped ao grupo como membro (P1=group, P2=actor) |
| `06C9` | `remove_actor_from_group` | Remove ped do grupo (P1=actor) |
| `06F0` | `set_group_separation_range` | Distância antes de teleportar membro ao líder |
| `087F` | `never_leave_group` | Impede que o ped saia do grupo voluntariamente |
| `0247/0248/038B/0249` | model stream | Sequência obrigatória de carregamento de modelo |
| `009A` | `create_actor` | Cria ped a pé no mundo |
| `0850` | `task_follow_footsteps` | Faz ped seguir outro a pé |
| `0AB5` | `store_closest_entities` | Handle do carro e ped mais próximos a um ator |
| `056D/056E` | `actor/car defined` | Valida handles antes de operar |
| `00DF` | `actor driving` | Verifica se ator está dirigindo |
| `0449` | `actor in_a_car` | Verifica se ator está em qualquer veículo (como passageiro ou motorista) |
| `05CB` | `task_enter_car_as_driver` | Tarefa: entrar no carro como motorista (com animação) |
| `05CA` | `task_enter_car_as_passenger` | Tarefa: entrar no carro como passageiro (P1=actor, P2=car, P3=time, P4=seat) |
| `0633` | `task_leave_car` | Tarefa: sair do carro (com animação) — funciona no player e no recruta |
| `00F2` | `actor near_actor` | Cheque de proximidade 2D entre dois peds |
| `0407` | `store_coords_from_car_with_offset` | Ponto no espaço mundo a X/Y/Z offset em espaço local do carro (+Y=frente) |
| `00AD/00AE/00AF` | car behaviour | Velocidade máxima, estilo de trânsito, agressividade do motorista IA |
| `07F8` | `car follow_car` | IA de perseguição dinâmica por handle |
| `00A7` | `car drive_to` | Dirige até coordenadas fixas |
| `0395` | `clear_area` | Remove peds/veículos na zona de spawn |
| `01C2/01C3` | `mark_as_no_longer_needed` | Libera entidades dos pools de memória |
| `0ACD` | `show_text_highpriority` | Exibe mensagem imediata na tela |
| `0209` | `random_int_in_ranges` | Inteiro aleatório em `[min, max)` |

---

## Mapeamento de Variáveis Locais

| Variável | Tipo | Conteúdo |
|----------|------|----------|
| `0@–2@` | float | Coordenadas de spawn (offset do jogador) |
| `3@` | int (handle) | Ped do jogador — obtido com `01F5: 0 3@` a cada iteração |
| `6@–8@` | float | Coordenadas temporárias (drive_to / spawn / 0407 output) |
| `10@` | int (handle) | Recruta (ped) — sempre validar com `056D` |
| `11@` | int (handle) | Veículo do recruta — sempre validar com `056E` |
| `12@` | int | Estado: `0`=nenhum · `1`=a pé · `2`=em veículo · `3`=recruta dirige |
| `13@` | int (handle) | Ped mais próximo (descartado de `0AB5`) |
| `16@` | int | Timeout para recruta entrar no veículo (U key, máx: 10) |
| `21@` | int | ID do modelo: `105`=fam1 · `106`=fam2 · `107`=fam3 |
| `22@` | int (handle) | Veículo do jogador (extraído com `03C0`) |
| `23@` | int | Flag: `0`=spawned pelo mod · `1`=vanilla adotado |
| `24@` | int (handle) | Grupo do jogador (`07AF`) — usado em scan e spawn |
| `25@` | int | Contagem de líderes do grupo (descartada) |
| `26@` | int | Contagem de membros (gate de detecção vanilla) |
| `27@` | int (handle) | Candidato vanilla (`092B` slot 0, temp) |
| `28@` | int | Timeout para jogador entrar no carro (G key, máx: 10) |

> ⚠️ **Atenção:** variáveis `32@` e `33@` são **reservadas pelo RenderWare** para timers internos. Nunca utilizar.

---

## Desafios Técnicos Resolvidos

| Desafio | Solução |
|---------|---------|
| Recruta morria e não dava para respawnar | Auto-detecção de morte no topo do MAIN_LOOP com `056D` → CLEANUP automático |
| Carro atropelando jogador a pé | 3 zonas (4m STOP / 12m CREEP / fora CHASE) — `max_speed 0.0` elimina inércia residual |
| Sem forma de fazer recruta sair do carro | U toggle: `12@==2` → `0633: exit_car recruta` com animação nativa |
| Sem forma de o jogador ser passageiro | G key: `05CA: player enter_car as_passenger` → estado `12@==3` |
| Recruta parado com jogador como passageiro | `0407` offset +Y 50m → `00A7 drive_to` renovado a cada 300ms — vai em frente continuamente |
| Recrutas do mod separados do sistema vanilla | `0631` no spawn adiciona ao grupo nativo; `06C9` no cleanup remove limpo |
| Vanilla scan frágil (073F+06EE) | Substituído por `092B` (acesso direto por slot no CPedGroup) |
| `$PLAYER_ACTOR` = 0 em CLEO externo | `01F5: 0 3@` a cada iteração do loop |
| Erro 0097 (type mismatch) | Todos os floats com `.0` explícito; ordem de parâmetros validada contra SASCM.ini |
| Crash ao spawnar sem modelo | Sequência `0247` → wait `0248` → `038B` obrigatória |
| Vazamento de pools após vários respawns | `01C2`/`01C3` + `06C9` no CLEANUP |

---

## Modelos de Ped Utilizados

| ID | Nome | Grupo |
|----|------|-------|
| `105` | `fam1` | Grove Street Families (variante 1) |
| `106` | `fam2` | Grove Street Families (variante 2) |
| `107` | `fam3` | Grove Street Families (variante 3) |

**`pedtype 8`** = `PEDTYPE_GANG1` na classificação interna do SA.

---

## Aviso sobre o Erro 0097

O erro `0097` indica **incompatibilidade de tipo de parâmetro**. Pontos críticos:

- `00AD: set_car X max_speed_to **50.0**` — deve ter `.0` explícito.
- `07F8: car X follow_car Y radius **10.0**` — idem para o raio.
- `06F0: set_group X distance_limit_to **100.0**` — idem.
- A ordem dos parâmetros segue o [SASCM.ini](SASCM.ini) posicionalmente (SB3 compila da esquerda para a direita).

---

## Extensibilidade Futura

- **Múltiplos recrutas:** `092B` com slot 0, 1, 2... permite gerenciar cada membro individualmente. Cada recruta receberia seu próprio conjunto de variáveis (array ou slots fixos).
- **Recruta dirige para waypoint:** Quando no estado 3, capturar o marcador GPS do mapa com `0171: get_waypoint_coords` (se disponível via CLEO) e usar `05D1: task_car_drive_to_coord` com `DriveMode` e `DrivingMode` configurados.
- **Todos os recrutas nativos:** interceptar `0xB74494` (Ped Pool) via `0A8D: read_memory` para aplicar IA a qualquer ped recrutado.
- **Detecção de combate:** alternar `traffic_behaviour 2↔5` baseado em `0118` (wanted level) ou proximidade de inimigos com `0118: is_char_dead` + `00BE: is_char_in_area`.
- **Formações de escolta:** `05F2`/`05F3`/`05F4` para posicionar o carro do recruta à direita, atrás ou à frente do jogador.
- **Múltiplos passageiros:** se spawnar 3 recrutas, usar `05CA` com `seat=0` (co-piloto), `seat=1` (traseiro esquerdo), `seat=2` (traseiro direito).
- **Patrulha autônoma:** estado 4 — recruta anda em rota pré-definida com `05D8: task_follow_point_route` enquanto jogador está longe.

---

## Fontes e Referências

| Recurso | URL |
|---------|-----|
| **Sanny Builder Library** | https://library.sannybuilder.com/ |
| **GTAMods Wiki** | https://gtamods.com/wiki/List_of_opcodes |
| **CLEO Library** | https://cleo.li |
| **Sanny Builder** | https://sannybuilder.com |
| **ThirteenAG/III.VC.SA.CLEOScripts** | https://github.com/ThirteenAG/III.VC.SA.CLEOScripts |
| **yugecin/scmcleoscripts** | https://github.com/yugecin/scmcleoscripts |
| **MTA Wiki — Ped Models** | https://wiki.multitheftauto.com/wiki/Ped_Models |
| **Project Cerbera — Handling** | https://projectcerbera.com/gta/sa/tutorials/handling |

---

## Controles

| Tecla | Ação |
|-------|------|
| **Y** | Spawna 1 recruta Grove Street **a pé** (modelo aleatório: fam1, fam2 ou fam3) |
| **U** | O recruta **procura o veículo desocupado mais próximo**, entra como motorista e passa a seguir o jogador com IA veicular |

---

## Requisitos

- **GTA San Andreas** — versão PC 1.0 US (ou downgraded de 2.0)
- **CLEO 4** ou superior — [cleo.li](https://cleo.li)
- **Sanny Builder 3** — [sannybuilder.com](https://sannybuilder.com)

---

## Instalação

1. Abra `grove_recruit_follow.cs` no **Sanny Builder 3** com o modo **GTA San Andreas** selecionado.
2. Compile: menu **Run → Compile** ou pressione `F6`. O arquivo `.cs` compilado será gerado na mesma pasta.
3. Copie o `.cs` compilado para a pasta **CLEO** dentro do diretório do GTA SA:
   ```
   GTA San Andreas\CLEO\grove_recruit_follow.cs
   ```
4. Inicie o jogo. O CLEO carrega o mod automaticamente junto com o jogo.

> **Nota:** se aparecer a mensagem *"Grove Recruit Mod: 1=Spawnar | 2=Veiculo | 3=Recruta dirige | 4=Modo velocidade"* na tela após carregar o save, o mod está ativo.

---

## Arquitetura — 3 Módulos

O script é dividido em três módulos independentes que cooperam via máquina de estados (`12@`).

### Módulo 1 — Detecção de Estado do Jogador

O loop principal executa a cada **300 ms** e monitora continuamente:

```
Loop Principal (300ms):
├── Jogador está a pé?
│   └── Recruta em veículo → recruta dirige até posição do jogador (00A7)
├── Jogador entrou num carro?
│   └── Recruta em veículo → recruta segue o carro do jogador (07F8)
└── Recruta morreu ou veículo destruído?
    └── Limpeza de memória → volta ao estado inicial
```

O opcode `00DF: actor X driving` é checado **antes** de chamar `03C0: actor X car`, pois `03C0` só deve ser invocado quando o ator está efetivamente dirigindo.

### Módulo 2 — Aquisição de Veículo

Ao pressionar **U**, o sistema executa a sequência:

1. **Localiza** o carro mais próximo ao recruta — `0AB5: store_closest_entities` (retorna Car e Char mais próximos ao ped recruta)
2. **Valida** o handle com `056E: car defined`
3. **Verifica** que não é o veículo do jogador (comparação de handles)
4. **Executa entrada** como motorista via `05CB: task_enter_car_as_driver` (motor gerencia animações de abrir porta e sentar)
5. **Aguarda confirmação** de que está dirigindo com `00DF`, com timeout de 5 segundos para evitar travamento caso o caminho esteja bloqueado

> **Por que `05CB` e não warp direto?**  
> `task_enter_car_as_driver` usa as animações nativas do jogo (abrir porta, sentar, colocar cinto). Warp direto quebraria a imersão e pode causar sobreposição de colisão.

### Módulo 3 — Estilos de Condução (`traffic_behaviour`)

Este é o **coração** do mod. O opcode `00AE` define como o motorista IA interage com o trânsito:

| Valor | Constante | Comportamento |
|-------|-----------|---------------|
| `0` | `STOPFORCARS` | Para em semáforos e no trânsito |
| `1` | `SLOWDOWNFORCARS` | Desacelera perto de outros carros (usado quando jogador está **a pé**) |
| `2` | **`AVOIDCARS`** | Ignora semáforos, desvia ativamente de carros (**modo padrão ao seguir**) |
| `3` | `PLOUGHTHROUGH` | Ignora absolutamente tudo — **nunca usar para aliados** |
| `5` | `FOLLOWTRAFFIC_AVOIDCARS` | Respeita semáforos **e** desvia de lentidão (alternativa mais realista) |

O mod usa **modo 2** no seguimento (equilíbrio entre velocidade e segurança) e **modo 1** quando o recruta se aproxima do jogador a pé (abordagem cautelosa evita atropelamentos).

---

## Maquina de Estados

```
Estado 0 — Nenhum recruta ativo
     │
     │ Tecla 1 pressionada
     ▼
Estado 1 — Recruta a pé
     │   → task_follow_footsteps (0850): seguimento nativo a pé
     │
     │ Tecla 2 + carro encontrado + recruta entrou (00DF confirmado)
     ▼
Estado 2 — Recruta em veículo ◄──────────────────────────────┐
     │   ├── Jogador em carro → 07F8 follow_car radius 10.0  │
     │   └── Jogador a pé    → 00A7 drive_to (coords atuais) │
     │                                                        │
     └── Veículo destruído (056E false) ────────────────────► Estado 1
     └── Recruta morreu (056D false) ────────────────────────► CLEANUP → Estado 0
```

---

## Por Que Estas Escolhas?

| Decisão | Razão |
|---------|-------|
| **`07F8: follow_car`** em vez de `00A7: drive_to` | `follow_car` rastreia o **handle dinâmico** do carro, recalculando em tempo real. Se o jogador faz uma curva, o recruta recalcula imediatamente. `drive_to` vai para uma coordenada **estática** — o recruta chegaria no ponto e pararia, mesmo que o jogador já tivesse saído. |
| **`AVOIDCARS` (modo 2)** em vez de `PLOUGHTHROUGH` (3) | Recruta é **aliado**. Atropelar civis gera wanted level desnecessário e quebra a imersão. Modo 2 desvia ativamente sem ser lento demais. |
| **`pedtype 8`** para o recruta | Tipo 8 = `PEDTYPE_GANG1` (Grove Street Families) na classificação interna. Garante animações de gangue, vozes corretas em português/inglês e comportamento de grupo nativo. |
| **`wait 300 ms`** no loop principal | Boa frequência de atualização sem sobrecarregar a CPU. O motor já interpola o comportamento entre iterações. |
| **`056D/056E`** antes de qualquer operação | Operar sobre handle de ped morto ou carro destruído causa **crash imediato** (access violation na VM). Sempre validar antes de usar. |
| **`01C2/01C3 mark_as_no_longer_needed`** | Não destroem as entidades — transferem a responsabilidade ao motor para despawn natural. Essencial para não saturar os **pools** do jogo (~110 peds, ~110 veículos simultâneos). |
| **`0395: clear_area` no spawn** | Remove peds/carros civis que poderiam causar colisão de spawn, evitando animação de empurrão e sobreposição de hitbox. |

---

## Opcodes Utilizados

| Opcode | Nome | Descrição |
|--------|------|-----------|
| `0AB0` | `key_pressed` | Detecta tecla do teclado via VK code do Windows (1=49, 2=50, 3=51, 4=52) |
| `0256` | `player defined` | Verifica se `$PLAYER_CHAR` está ativo (essencial antes de qualquer operação) |
| `0247` | `request_model` | Solicita carregamento de modelo no streaming de assets |
| `0248` | `model available` | Verifica se modelo está carregado na RAM |
| `038B` | `load_requested_models` | Finaliza o carregamento solicitado |
| `0249` | `release_model` | Libera cache do modelo sem destruir entidades criadas |
| `00A0` | `store_actor position_to` | Recupera coordenadas mundiais do ator (substitui `04C4` — keywords compostos como `from_actor` não são reconhecidos pelo SB3 em modo fallback) |
| `0395` | `clear_area` | Remove peds/veículos numa área (limpa zona de spawn) |
| `009A` | `create_actor` | Cria ped a pé no mundo |
| `0850` | `task_follow_footsteps` | Faz um personagem seguir outro a pé (params: char, target char) |
| `0AB5` | `store_closest_entities` | Busca handle do carro e ped mais próximos a um ator |
| `056D` | `actor defined` | Verifica se handle de ped é válido e ator está vivo |
| `056E` | `car defined` | Verifica se handle de veículo é válido e carro existe |
| `00DF` | `actor driving` | Verifica se ator está efetivamente dirigindo um carro |
| `03C0` | `actor car` | Extrai handle do veículo que o ator está dirigindo |
| `0615` | `define_AS_pack_begin` | Inicia definição de Action Sequence Pack |
| `0604` | `AS_actor enter_car_as_driver` | Tarefa de IA: entrar no veículo como motorista (com animação) |
| `0616` | `define_AS_pack_end` | Finaliza e armazena o AS Pack |
| `0618` | `actor do_AS_pack` | Atribui e inicia o AS Pack num ator |
| `061B` | `remove_AS_pack` | Libera o AS Pack da memória |
| `00AD` | `set_car max_speed` | Velocidade máxima do veículo (float, unidades internas ~km/h) |
| `00AE` | `set_car traffic_behaviour` | Estilo de condução no trânsito (ver tabela acima) |
| `00AF` | `set_car driver_behaviour` | Agressividade/responsividade do motorista IA |
| `07F8` | `car follow_car` | **IA de perseguição dinâmica** — segue handle de outro carro com recálculo em tempo real |
| `00A7` | `car drive_to` | Dirige até coordenadas fixas usando pathfinding nativo |
| `01C2` | `mark_actor_as_no_longer_needed` | Libera ped dos pools de memória |
| `01C3` | `mark_car_as_no_longer_needed` | Libera veículo dos pools de memória |
| `0ACD` | `show_text_highpriority` | Exibe mensagem de texto na tela imediatamente (params: texto, duração ms). Opcode `PRINT_STRING_NOW` do CLEO |
| `0209` | `random_int_in_ranges` | Gera inteiro aleatório em intervalo `[min, max)` |

---

## Mapeamento de Variáveis Locais

| Variável | Tipo | Conteúdo |
|----------|------|----------|
| `0@–2@` | float | Coordenadas de spawn calculadas com offset |
| `6@–8@` | float | Coordenadas do jogador (para `drive_to`) |
| `10@` | int (handle) | Recruta (ped) — sempre validar com `056D` antes de usar |
| `11@` | int (handle) | Veículo do recruta — sempre validar com `056E` antes de usar |
| `12@` | int | Estado: `0`=nenhum · `1`=a pé · `2`=em veículo |
| `13@–15@` | float | Posição do recruta (para `0176`) |
| `16@` | int | Contador de timeout para entrada no veículo (máx: 10) |
| `20@` | int (handle) | Action Sequence Pack (liberado com `061B` após uso) |
| `21@` | int | ID do modelo: `105`=fam1 · `106`=fam2 · `107`=fam3 |
| `22@` | int (handle) | Veículo do jogador (extraído com `03C0`) |

> ⚠️ **Atenção:** variáveis `32@` e `33@` são **reservadas pelo RenderWare** para timers internos. Nunca utilizar.

---

## Modelos de Ped Utilizados

| ID | Nome | Grupo |
|----|------|-------|
| `105` | `fam1` | Grove Street Families (variante 1) |
| `106` | `fam2` | Grove Street Families (variante 2) |
| `107` | `fam3` | Grove Street Families (variante 3) |

**`pedtype 8`** = `PEDTYPE_GANG1` na classificação interna do SA — garante animações de gangue, vozes e comportamento de grupo corretos para os membros da Grove Street.

---

## Aviso sobre o Erro 0097

O erro `0097` na VM do RenderWare indica **incompatibilidade de tipo de parâmetro** (integer onde float é esperado, ou vice-versa, ou ordem errada de argumentos). Pontos críticos:

- `00AD: set_car X max_speed_to **50.0**` — o valor de velocidade **deve** ter `.0` explícito no Sanny Builder para ser compilado como float.
- `07F8: car X follow_car Y radius **10.0**` — idem para o raio.
- `04C4: store_coords_to ... with_offset **3.0 0.0 0.0**` — todos os offsets devem ser float.
- A ordem dos parâmetros deve seguir exatamente a documentação da [Sanny Builder Library](https://library.sannybuilder.com/).

---

## Desafios Técnicos Resolvidos

| Desafio | Solução |
|---------|---------|
| Detectar se recruta ainda está vivo | `056D: actor 10@ defined` antes de qualquer operação |
| Detectar se veículo foi destruído | `056E: car 11@ defined` — falso quando destruído |
| Recruta não entrar no carro do jogador | Comparação de handles: `0038: 11@ == 22@` |
| Recruta travar tentando entrar | Timeout de 5s (10 × 500ms) com contador `16@` |
| Conflito de tarefas (task_follow_footsteps vs AS pack) | O AS pack substitui automaticamente qualquer tarefa anterior do ator — não é necessário chamada prévia de limpeza |
| Crash ao spawnar sem modelo carregado | Sequência obrigatória `0247` → wait `0248` → `038B` |
| Vazamento de pools após vários respawns | `01C2`/`01C3: mark_as_no_longer_needed` a cada cleanup |

---

## Extensibilidade Futura

Este script foca em **1 recruta spawnado manualmente**. A arquitetura foi projetada para ser expandida:

- **Múltiplos recrutas:** adicionar variáveis `10@`–`10@+N` em array (com CLEO 4 extended vars) e replicar a lógica em loops.
- **Todos os recrutas nativos:** interceptar o recrutamento normal do jogo lendo a **Ped Pool** (endereço `0xB74494`) via `0A8D: read_memory` e verificando flags de grupo/facção para aplicar a IA veicular a qualquer ped recrutado pelo jogador, não apenas os spawnados pelo script.
- **Detecção de combate:** adicionar cheque de wanted level (`0118`) ou proximidade de inimigos para alternar dinamicamente entre `traffic_behaviour 2` (AVOIDCARS, normal) e `traffic_behaviour 5` (FOLLOWTRAFFIC, perseguição agressiva).
- **Formações:** usar opcodes `05F2`–`05F4` para posicionamento em formação relógio ao redor do jogador, em vez de todos seguirem no mesmo ponto.
- **Veículos de gangue preferenciais:** filtrar `0176` para preferir modelos típicos da Grove Street — Savanna (low rider), Voodoo, Blade — verificando o model ID do carro retornado antes de aceitar.

---

## Fontes e Referências

| Recurso | URL |
|---------|-----|
| **Sanny Builder Library** — documentação completa de opcodes SA | https://library.sannybuilder.com/ |
| **GTAMods Wiki** — lista de opcodes com parâmetros | https://gtamods.com/wiki/List_of_opcodes |
| **CLEO Library** — injetor de scripts para GTA SA/VC/III | https://cleo.li |
| **Sanny Builder** — IDE para compilar scripts `.cs` | https://sannybuilder.com |
| **ThirteenAG/III.VC.SA.CLEOScripts** — exemplos práticos de AS packs e follow_car | https://github.com/ThirteenAG/III.VC.SA.CLEOScripts |
| **yugecin/scmcleoscripts** — referências de IA veicular avançada | https://github.com/yugecin/scmcleoscripts |
| **MTA Wiki** — IDs de modelos de ped e carro | https://wiki.multitheftauto.com/wiki/Ped_Models |
| **Project Cerbera** — tutoriais de handling e velocidades reais do SA | https://projectcerbera.com/gta/sa/tutorials/handling |
