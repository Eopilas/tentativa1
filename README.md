# Grove Street Recruit — Vehicle AI Follower

Mod CLEO para **GTA San Andreas** que substitui o comportamento "ímã" dos companheiros de gangue por **IA veicular real**: o recruta dirige por conta própria, desvia de obstáculos, respeita o tráfego e mantém distância de segurança do jogador — tudo usando os opcodes nativos do motor **RenderWare**.

> **Contexto:** no jogo original, recrutas simplesmente teleportam ou correm em linha reta para o jogador, sem lógica de rua, sem desvio de carros, sem regras de trânsito. Este mod corrige isso com a API interna do próprio jogo.

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

> **Nota:** se aparecer a mensagem *"Grove Recruit Mod: Y=Spawnar | U=Buscar Veiculo"* na tela após carregar o save, o mod está ativo.

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
     │ Tecla Y pressionada
     ▼
Estado 1 — Recruta a pé
     │   → task_follow_footsteps (0850): seguimento nativo a pé
     │
     │ Tecla U + carro encontrado + recruta entrou (00DF confirmado)
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
| `0AB0` | `key_pressed` | Detecta tecla do teclado via VK code do Windows (Y=89, U=85) |
| `0256` | `player defined` | Verifica se `$PLAYER_CHAR` está ativo (essencial antes de qualquer operação) |
| `0247` | `request_model` | Solicita carregamento de modelo no streaming de assets |
| `0248` | `model available` | Verifica se modelo está carregado na RAM |
| `038B` | `load_requested_models` | Finaliza o carregamento solicitado |
| `0249` | `release_model` | Libera cache do modelo sem destruir entidades criadas |
| `04C4` | `store_coords_with_offset` | Calcula posição com deslocamento relativo a um ator |
| `0395` | `clear_area` | Remove peds/veículos numa área (limpa zona de spawn) |
| `009A` | `create_actor` | Cria ped a pé no mundo |
| `0850` | `task_follow_footsteps` | Faz um personagem seguir outro a pé (params: char, target char) |
| `0407` | `store_coords_from_actor` | Armazena coordenadas de um ator em variáveis |
| `0176` | `car nearest_to_point` | Busca handle do carro mais próximo a um ponto |
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
