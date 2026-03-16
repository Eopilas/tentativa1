# ANÁLISE DE PROBLEMAS - LOG v3.3
**Data**: 2026-03-16
**Log Analisado**: grove_recruit.log (7187 linhas)
**Branch**: claude/ler-historico-tasks-agent

---

## ESTATÍSTICAS DO LOG

- **Total de linhas**: 7,187
- **INVALID_LINK ocorrências**: 2,627 (36.5% do log!)
- **WRONG_DIR ocorrências**: 292 (4.1% do log)
- **DIST_TREND AFASTAR**: 105 vezes (recruit caindo para trás)
- **HEADON_PERSISTENT + STUCK_RECOVER**: 44 eventos de colisão

---

## PROBLEMA CRÍTICO #1: INVALID_LINK STORM (MUITO GRAVE)

### Evidências do Log
```
[0008517-0008552] INVALID_LINK: burst=1-35 (35 frames consecutivos!)
[0010597-0010614] INVALID_LINK oscilação: linkId válido→inválido a cada 2-4 frames
[0017752-0017829] INVALID_LINK: invalidBurst=366!!!
```

### Análise
O exponential backoff implementado em v3.3 **NÃO ESTÁ FUNCIONANDO** como esperado:
- Deveria pausar 120 frames após >10 bursts consecutivos
- Mas o log mostra bursts de **366 frames** e **35 frames** sem pausa efetiva
- Counter reseta mas burst continua crescendo
- `linkId=4294966813` e `linkId=4294966814` (valores próximos a UINT_MAX) indicam falha de JoinCarWithRoadSystem

### Root Cause (Hipótese)
**O problema está no JoinCarWithRoadSystem, não no backoff**:
1. O veículo está em uma posição onde a engine GTA SA **não consegue** encontrar link válido
2. Possíveis causas:
   - Veículo está em área sem road-graph (parking lots, interior de áreas)
   - Veículo está em transição entre PathAreas (areaId mudando)
   - Heading está WRONG_DIR e a engine só encontra links no sentido oposto
   - Veículo está "preso" entre dois nodes sem link conectando

### Impacto no Comportamento
- **Recruta fica "perdido"**: sem link válido, targetHeading vem de roadH (fallback), que pode estar errado
- **Oscilação de velocidade**: CIVICO reduzido → speed baixo → não consegue sair da área problemática
- **Não avança**: fica tentando re-snap indefinidamente na mesma posição

---

## PROBLEMA CRÍTICO #2: WRONG_DIR em OBSV mas NÃO no DRIVE (CONFUSÃO DE HEADING)

### Evidências do Log
```
[0006660] RecruitCar(OBSV): heading=-1.843 targetH=2.529 deltaH=-1.912(WRONG_DIR!) mission=52
[0006780] RecruitCar(OBSV): heading=-1.842 targetH=2.529 deltaH=-1.912(WRONG_DIR!) mission=11(STOP_FOREVER)
[0007020] RecruitCar(OBSV): heading=-0.634 targetH=2.530 deltaH=-3.119(WRONG_DIR!)
[0010020] RecruitCar(OBSV): heading=-0.660 targetH=2.467 deltaH=3.127(WRONG_DIR!) mission=11(STOP_FOREVER)
```

**Mas ao mesmo tempo**:
```
[0006822] CLOSE_RANGE_FORCE_MC52: heading=-1.842 targetH=-3.116 roadH=-3.116 deltaH=-1.274 align=ROAD_NODE_MISMATCH
```

### Análise
**Existe uma discrepância entre:**
- **targetH usado em DRIVE**: vem de `align` (CLIPPED_LINK, ROAD_NODE_MISMATCH, etc.) - pode ser positivo ou negativo
- **targetH mostrado em OBSV**: sempre valor fixo ~2.5 rad (parece ser o heading do road-graph em direção ao player)

Isso sugere que:
1. O sistema DRIVE calcula targetH baseado no **link atual ou roadH**
2. O sistema OBSV mostra targetH baseado no **CAutoPilot::m_vecDestinationCoors** (heading para o destino)
3. **São duas coisas diferentes!**

### Root Cause
O log OBSV mostra deltaH "WRONG_DIR!" quando o recruta está:
- Indo em direção OPOSTA ao destino final (player)
- Mas pode estar CORRETO em relação ao road-graph (fazendo curva para eventualmente chegar ao player)

**Exemplo**: Player ao Norte, recruta ao Sul, mas road-graph requer ir primeiro Leste depois curvar Norte.
- OBSV: heading=Sul, targetH=Norte → deltaH=WRONG_DIR! ❌ (falso positivo)
- DRIVE: heading=Sul, targetH=Leste → deltaH=OK ✓ (correto)

### Impacto no Comportamento
- **Dificulta debugging**: parece que recruta está WRONG_DIR mas na verdade está seguindo road-graph corretamente
- **Pode explicar "viradas erradas em interseções"**: se recruta precisa fazer curva à esquerda mas player vai reto, OBSV mostra WRONG_DIR mas é comportamento correto do pathfinding

---

## PROBLEMA #3: DIST_TREND AFASTAR EXCESSIVO (105 vezes)

### Evidências do Log
```
[0007448] DIST_TREND: dist=24.7m prev=17.5m delta=7.3m -> AFASTAR
[0007508] DIST_TREND: dist=38.2m prev=24.7m delta=13.5m -> AFASTAR (13.5m em 1 segundo!)
[0007568] DIST_TREND: dist=48.2m prev=38.2m delta=10.0m -> AFASTAR
[0008108] DIST_TREND: dist=18.2m prev=11.9m delta=6.4m -> AFASTAR
[0008168] DIST_TREND: dist=30.2m prev=18.2m delta=12.0m -> AFASTAR
[0008228] DIST_TREND: dist=42.1m prev=30.2m delta=11.9m -> AFASTAR
[0008288] DIST_TREND: dist=56.8m prev=42.1m delta=14.7m -> AFASTAR
[0008348] DIST_TREND: dist=71.0m prev=56.8m delta=14.1m -> AFASTAR
[0008408] DIST_TREND: dist=81.2m prev=71.0m delta=10.2m -> AFASTAR
[0008468] DIST_TREND: dist=88.7m prev=81.2m delta=7.5m -> AFASTAR
[0008528] DIST_TREND: dist=94.4m prev=88.7m delta=5.7m -> AFASTAR
[0008588] DIST_TREND: dist=100.4m prev=94.4m delta=6.0m -> AFASTAR
[0008648] DIST_TREND: dist=102.7m prev=100.4m delta=2.3m -> AFASTAR
[0012036] dist=96.8m delta=19.4m -> AFASTAR (19.4m/s!)
[0012096] dist=121.9m delta=25.2m -> AFASTAR (25.2m/s!)
[0012156] dist=148.4m delta=26.4m -> AFASTAR (26.4m/s!)
```

### Análise
- Sequências de AFASTAR de 17.5m → 102.7m (85m de gap em ~15 segundos)
- Pior caso: 96.8m → 148.4m (51.6m de gap em 2 segundos = ~93 km/h de diferença!)
- FAR_CATCHUP ativa em dist>40m mas **não consegue recuperar**

### Root Causes Possíveis
1. **INVALID_LINK paralisa o recruta**: durante burst de INVALID_LINK, recruta fica parado tentando re-snap enquanto player acelera
2. **HEADON_COLLISION reduz velocidade a 50%**: se recruta está em HEADON, mesmo com FAR_CATCHUP ativo, velocidade é penalizada
3. **CURVE_SPEED_REDUCTION ainda muito agressivo**: 0.60 pode não ser suficiente em curvas longas
4. **Dynamic catchup insuficiente**: SPEED_CATCHUP_VERY_FAR=90 kmh não acompanha player a 100+ kmh

---

## PROBLEMA #4: HEADON_PERSISTENT e STUCK_RECOVER (44 ocorrências)

### Evidências do Log
```
[0008851] TEMPACTION_CHANGE: 0(NONE) -> 19(HEADON_COLLISION) penalty=50% dist=61.0m
[0008880] HEADON_PERSISTENT: HEADON_COLLISION por >=30 frames -> JoinCarWithRoadSystem dist=65.4m
[0008888] DRIVING_1: tempAction=19(HEADON_COLLISION) stuck=9/75 physSpeed=0kmh
[0008940] DRIVING_1: tempAction=19(HEADON_COLLISION) stuck=69/75 physSpeed=0kmh
```

```
[0022928] STUCK_RECOVER: physSpeed=0.0kmh por >=75 frames dist=82.5m mission=52 tempAction=19(HEADON_COLLISION)
```

### Análise
- HEADON_COLLISION frequente indica colisões com props/carros/muros
- Sequência típica: HEADON → physSpeed cai a 0 → stuck counter cresce → HEADON_PERSISTENT ou STUCK_RECOVER
- Após recovery, frequentemente volta a HEADON novamente (ciclo)

### Root Causes Possíveis
1. **Road-graph leva através de áreas estreitas**: pathfinding GTA SA pode escolher rotas através de becos/passagens estreitas
2. **AVOID_CARS não desvia de props estáticos**: apenas desvia de veículos, não de postes/muros
3. **JoinCarWithRoadSystem coloca veículo em posição ruim**: após re-snap, pode ficar de frente para obstáculo
4. **Velocidade muito alta para manobras**: mesmo com penalty 50%, pode estar entrando em curvas rápido demais

---

## PROBLEMA #5: CLOSE_RANGE_FORCE_MC52 Repetitivo

### Evidências do Log
```
[0004875] CLOSE_RANGE_FORCE_MC52: dist=16.0m 9(?)->MC52
[0006451] CLOSE_RANGE_FORCE_MC52: dist=14.3m 67(ESCORT_REAR_FARAWAY)->MC52
[0006822] CLOSE_RANGE_FORCE_MC52: dist=17.9m 11(STOP_FOREVER)->MC52
[0009518] CLOSE_RANGE_FORCE_MC52: dist=10.2m 67(ESCORT_REAR_FARAWAY)->MC52
```

### Análise
- Engine GTA SA transita automaticamente de MC52 para MC67 ou MC31 em close-range
- Mod força de volta para MC52
- Acontece **repetidamente** em close-range
- Indica que o "force" não é permanente - engine continua tentando mudar

### Root Cause
CAutoPilot state machine **internamente** decide mission baseado em distância ao targetCar:
- Provavelmente: dist<20m → MC31 (ESCORT_REAR) ou MC9 (GOTOCOORDS_RACING)
- Mod sobrescreve de volta para MC52 a cada frame
- **Não há como "travar" a mission** - é um loop infinito de sobrescrita

### Impacto no Comportamento
- Overhead de processamento (pequeno)
- Pode causar "hesitação" do veículo (mission mudando a cada frame)
- Não explica bugs graves, mas contribui para comportamento irregular

---

## PROBLEMA #6: MISSION=11 (STOP_FOREVER) Inesperado

### Evidências do Log
```
[0006660] mission=52(FOLLOWCAR_FARAWAY) → [0006780] mission=11(STOP_FOREVER)
[0010020] mission=11(STOP_FOREVER) enquanto physSpeed=33kmh (não parado!)
[0014580] mission=11(STOP_FOREVER) com tempAction=0(NONE) physSpeed=0kmh
```

### Análise
STOP_FOREVER aparece em dois contextos:
1. **USER_TRIGGERED**: KEY 4 muda modo para PARADO(STOP_FOREVER) - esperado
2. **CLOSE_BLOCKED_START**: dist<22m ambos parados → STOP_FOREVER para evitar subir calçada
3. **INESPERADO**: mission vira STOP_FOREVER sem log CLOSE_BLOCKED ou KEY

### Root Cause Provável
**CCarAI ou CAutoPilot internamente** pode estar mudando para STOP_FOREVER quando:
- Detecta obstáculo à frente
- Está em tempAction=WAIT ou BRAKE por muito tempo
- pTarget (targetCar) ficou nullptr ou inválido

**Possibilidade**: quando CLOSE_BLOCKED clear targetCar temporariamente, engine muda para STOP_FOREVER.

---

## PROBLEMA #7: Recruit Getting Lost (Relatado pelo Usuário)

### Conexão com Achados
Baseado na análise acima, "recruta perdido sem saber para onde ir" pode ser causado por:

1. **INVALID_LINK prolongado** (burst >100 frames):
   - Sem link válido, recruta fica em loop de re-snap
   - targetHeading vem de roadH (fallback) que pode estar errado
   - Não consegue sair da área problemática

2. **WRONG_DIR falso positivo em interseções**:
   - Road-graph requer fazer curva à esquerda
   - Player vai reto
   - Recruta segue road-graph (correto) mas parece "virar errado"

3. **STOP_FOREVER inesperado**:
   - Engine GTA SA coloca recruta em STOP_FOREVER
   - Mod não detecta ou não recupera a tempo
   - Recruta fica parado esperando indefinidamente

4. **HEADON loop**:
   - Recruta colide com obstáculo
   - JoinCarWithRoadSystem re-snap
   - Re-snap coloca recruta de frente para obstáculo novamente
   - Loop infinito de HEADON → STUCK → re-snap → HEADON

---

## PROBLEMA #8: Wrong Turns at Intersections (Relatado pelo Usuário)

### Conexão com Achados
"Recruta vira em interseção quando player vai reto" pode ser:

1. **Pathfinding GTA SA escolhe rota diferente**:
   - CPathFind::CalcPathNodes pode escolher rota alternativa ao destino
   - Player vai pela rota óbvia (reto)
   - Recruta segue road-graph que escolheu rota lateral (virar)
   - **Tecnicamente correto** para o pathfinding, mas visualmente parece errado

2. **INVALID_LINK em interseção**:
   - Interseções têm muitos links conectando (numLinks alto)
   - Se JoinCarWithRoadSystem retorna link inválido, targetH fica errado
   - Recruta vira em direção errada

3. **Destination update delay**:
   - Player muda de direção rapidamente em interseção
   - Recruta ainda tem destino antigo
   - Pathfinding calcula rota para destino antigo (vira)
   - Quando destino atualiza, recruta já virou

---

## PROBLEMA #9: Offroad Navigation (Relatado pelo Usuário)

### Evidências do Log
```
[0023452] offroad=1 stuck=52/75 modo=CIVICO_H(MC52+AVOID) mission=52
[0023938] offroad=1 modo=CIVICO_F(MC67+AVOID) mission=67
```

### Análise
- Sistema detecta offroad (playerRoadDist > PLAYER_OFFROAD_ON_DIST_M)
- **MAS** não transita para GOTOCOORDS (mission permanece 52/67)
- Recruta tenta seguir road-graph enquanto player está offroad
- Road-graph não tem conexão para onde player está → recruta fica perdido

### Root Cause
**PLAYER_OFFROAD_DIRECT não está ativando** quando deveria:
- Possível bug: hysteresis ON=42m/OFF=35m, mas threshold muito alto?
- Player pode estar em canal/campo a 30m do road-node → offroad=1 mas hysteresis não ativa
- Ou: PLAYER_OFFROAD ativa mas SetupDriveMode não muda para DIRETO(GOTOCOORDS)

---

## PRÓXIMAS AÇÕES RECOMENDADAS

### 1. Consultar plugin-sdk e gta-reversed
Necessário entender:
- `CCarCtrl::JoinCarWithRoadSystem`: quando pode falhar? Por que retorna linkId inválido?
- `CPathFind::CalcPathNodes`: como escolhe rota em interseções? Pode ser influenciado?
- `CAutoPilot` state machine: quando transita para STOP_FOREVER automaticamente?
- `CCarCtrl::FindNearestLink`: como determina link mais próximo?

### 2. Fixes Prioritários
1. **INVALID_LINK storm**: adicionar fallback para GOTOCOORDS quando burst > 30 frames
2. **DIST_TREND AFASTAR**: aumentar SPEED_CATCHUP_VERY_FAR para 100+ kmh
3. **HEADON loop**: após JoinCarWithRoadSystem, verificar se ainda está de frente para obstáculo
4. **PLAYER_OFFROAD**: reduzir ON threshold para 30m, aumentar OFF para 40m (inverter hysteresis se necessário)

### 3. Melhorias de Logging
- Separar targetH do OBSV (heading-to-destination) do targetH do DRIVE (heading-from-link)
- Adicionar log quando PLAYER_OFFROAD_DIRECT deveria ativar mas não ativa
- Log de CalcPathNodes decisions (se possível via plugin-sdk)

---

**Próximo Passo**: Estudar plugin-sdk e gta-reversed para entender internals do CPathFind e CCarCtrl.
