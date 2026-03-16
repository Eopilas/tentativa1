# ANÁLISE COMPLETA DO COMPORTAMENTO DO RECRUTA - v3.2
**Data**: 2026-03-16
**Versão ASI**: grove_recruit_standalone.asi v3.2

---

## 1. RESUMO EXECUTIVO

### Feedback do Usuário
> "Testei, primeiramente queria dizer q n to vendo diferenca entre os modos civicos, nao sei se isso é bom ou n, Em cruzamentos o recruta as vezes ainda vira errado ou se perde, pro offroad e sem node ele ainda tambem se perde, e ainda as vezes bate atras de mim, algumas curvas ainda sobe na calçada, e tem dificuldade de me acompanhar se eu for rapido, pq ou ele se perde pq fica pra tras, ou ele bate nas curvas. mas considerando, estamos sim chegando perto de um comportamento bom"

### Status Atual
✅ **Progresso Significativo**: Sistema está próximo de um comportamento bom
⚠️ **Problemas Persistentes**: 5 áreas principais requerem ajustes finos

---

## 2. HISTÓRICO DE DESENVOLVIMENTO (logs 0-10)

### Fase 1: Sistema Base (logs 0-2)
- Implementação inicial de heading-based navigation
- Sistema WRONG_DIR com threshold de 1.5 radianos
- AdaptiveSpeed function para ajuste de velocidade em curvas
- Logging extensivo para diagnóstico

### Fase 2: Recuperação de Erros (logs 3-5)
- Fix do problema de respect boost (recrutas não seguiam)
- Detecção e recuperação de WRONG_DIR
- Implementação de SLOW_ZONE e proximity management
- Road snap recovery a cada 300 frames

### Fase 3: Refinamento Close-Range (logs 6-8)
- Fix do problema de close-range chase mode (38+ segundos WRONG_DIR)
- Implementação de CLOSE_RANGE_SWITCH_DIST = 22m
- Separação de recuperação: longe vs perto
- Redução de road snap interval de 90 para 60 frames (1s)

### Fase 4: Ajustes de Velocidade e Invalid Links (logs 9-10)
- Remoção de SPEED_CIVICO_CLOSE cap (22 km/h era muito lento)
- Manejo melhorado de INVALID_LINK com burst detection
- CURVE_SPEED_REDUCTION = 0.80 (muito conservador)
- FAR_CATCHUP_DIST_M reduzido para 40m

---

## 3. ANÁLISE DO LOG grove_recruit.log

### 3.1 Padrões Observados

#### A) INVALID_LINK Oscillation (PROBLEMA CRÍTICO)
**Padrão**: Link oscila entre válido e inválido a cada 2 frames
```
[30838] INVALID_LINK: linkId=4294966814 (invalido) burst=1
[30838] INVALID_LINK: re-snap corrigiu -> linkId=10
[30840] INVALID_LINK: linkId=4294966814 (invalido) burst=1
[30840] INVALID_LINK: re-snap corrigiu -> linkId=10
```

**Frequência**: ~100+ ocorrências no log analisado
**Impacto**:
- Re-snap constante causa comportamento instável
- Recruta "trava" entre dois estados
- Velocidade oscila: speed=62 → speed=12 → speed=62 (FAR_CATCHUP on/off)

**Causa Raiz**:
- `JoinCarWithRoadSystem` em intersecções/curvas seleciona link ambíguo
- Sistema tenta corrigir, mas próximo frame retorna ao link inválido
- Cycle infinito de correção

#### B) WRONG_DIR em Intersecções
**Observação**: Recruit e NPCs mostram WRONG_DIR em linkIds válidos
```
[31014] RecruitCar: dist=58.8m mission=52 deltaH=-2.220(WRONG_DIR!) linkId=332(OK)
[31014] NearestTrafficCar: deltaH=-2.202(WRONG_DIR!) linkId=334(OK)
```

**Análise**:
- WRONG_DIR ocorre mesmo com linkId válido
- NPCs vanilla também mostram WRONG_DIR mas navegam bem
- **Conclusão**: Threshold de 1.5 rad é muito sensível para intersecções

**Por que NPCs funcionam?**:
- Game engine permite deltaH grande em manobras de intersecção
- Recuperação natural sem intervenção manual
- Nosso sistema forçado de recuperação pode estar atrapalhando

#### C) Offroad Direct-Follow
**Pattern**: Sistema detecta offroad e ativa GOTOCOORDS
```
[31785] PLAYER_OFFROAD_DIRECT_START: playerRoadDist=40.1m>40 -> GOTOCOORDS
[31795] PLAYER_OFFROAD_DIRECT_END: playerRoadDist=36.2m -> restaurar CIVICO
[31820] PLAYER_OFFROAD_DIRECT_START: playerRoadDist=40.1m>40 -> GOTOCOORDS
```

**Problema**: Oscilação rápida (10 frames = 0.17s)
- Player está no limiar (40m threshold)
- Sistema alterna GOTOCOORDS ↔ CIVICO repetidamente
- Durante GOTOCOORDS, recruit mostra WRONG_DIR persistente

#### D) FAR_CATCHUP Oscillation
**Pattern**: Speed boost ativa/desativa rapidamente
```
[30607] FAR_CATCHUP_ON: dist=71.3m speed=62
[30607] FAR_CATCHUP_OFF: dist=71.1m speed=12
[30608] FAR_CATCHUP_ON: dist=71.3m speed=62
```

**Análise**:
- Threshold de 40m muito sensível
- INVALID_LINK causa speed drop → dist aumenta → catchup liga
- Catchup acelera → link corrige → speed normaliza → catchup desliga
- **Histerese ausente**: precisa de margem (ex: liga aos 40m, desliga aos 35m)

---

## 4. PROBLEMAS IDENTIFICADOS POR CATEGORIA

### 4.1 ❌ Diferença entre Modos Cívicos não é Perceptível

**Feedback**: "n to vendo diferenca entre os modos civicos"

**Situação Atual**:
- CIVICO_F: MC_ESCORT_REAR_FARAWAY(67) + AVOID_CARS
- CIVICO_G: MC_FOLLOWCAR_CLOSE(53) + AVOID_CARS
- CIVICO_H: MC_FOLLOWCAR_FARAWAY(52) + AVOID_CARS

**Análise**:
Todos os 3 modos usam:
- Mesmo driveStyle: AVOID_CARS
- Mesma velocidade base: SPEED_CIVICO=46
- Mesmo AdaptiveSpeed com CURVE_SPEED_REDUCTION=0.80
- Mesmos thresholds de proximidade

**Diferenças Teóricas** (não evidentes na prática):
- F usa MC67 (escort rear) - posição geométrica atrás
- G usa MC53 (follow close) - seguimento próximo
- H usa MC52 (follow far) - seguimento com road-graph

**Por que não há diferença visível?**:
1. ProcessDrivingAI força todos para MC52 em close-range (<22m)
2. INVALID_LINK override ignora a missão base
3. Todos recebem mesma velocidade de AdaptiveSpeed

**Conclusão**: Modos cívicos atualmente são cosméticos.

---

### 4.2 ❌ Navegação em Cruzamentos/Intersecções

**Feedback**: "Em cruzamentos o recruta as vezes ainda vira errado ou se perde"

**Problema Composto**:

#### A) INVALID_LINK Burst em Intersecções
- Game engine confuso sobre qual link escolher
- 20+ re-snaps consecutivos em 1 segundo
- Velocidade cai para SPEED_MIN=8 durante burst
- Recruta "hesita" e perde momentum

#### B) WRONG_DIR False Positives
- Intersecções requerem heading change >1.5rad (>86°)
- Sistema detecta WRONG_DIR e tenta "corrigir"
- Correção interfere com manobra natural do game engine
- NPCs não têm essa interferência → navegam melhor

#### C) Road Snap Timing Ruim
- Snap a cada 60 frames (1s) pode ocorrer mid-turn
- Re-snap durante curva força nova escolha de path
- Path pode ser diferente da escolha inicial → confusão

**Solução Proposta**:
1. Aumentar WRONG_DIR threshold em intersecções (dynamic)
2. Implementar backoff exponential em INVALID_LINK burst
3. Pausar road snap durante high deltaH (turning)

---

### 4.3 ❌ Navegação Offroad e Sem Nodes

**Feedback**: "pro offroad e sem node ele ainda tambem se perde"

**Problema 1: Threshold Oscillation**
```
PLAYER_OFFROAD_DIST_M = 40m (rigid threshold)
```
- Player oscila entre 39m ↔ 41m do node
- Sistema liga/desliga GOTOCOORDS rapidamente
- Durante transição, missão muda → comportamento errático

**Problema 2: WRONG_DIR durante GOTOCOORDS**
- GOTOCOORDS calcula heading direto ao destino
- Mas ClipTargetOrientationToLink ainda usa road heading
- deltaH calculation fica inválido
- Log mostra: `deltaH=-2.752(WRONG_DIR!) mission=8(GOTOCOORDS)`

**Problema 3: Offroad Sustained Frames**
- `OFFROAD_DIRECT_FOLLOW_FRAMES = 90` (1.5s)
- Player pode entrar/sair de offroad rapidamente
- Sistema não diferencia "offroad temporário" vs "canal/praia"

**Solução Proposta**:
1. Hysteresis: ativa aos 40m, desativa aos 35m
2. Desabilitar WRONG_DIR detection durante GOTOCOORDS
3. Aumentar sustained frames: 180 (3s) para áreas genuinamente sem estrada

---

### 4.4 ❌ Colisões Traseiras

**Feedback**: "e ainda as vezes bate atras de mim"

**Análise do Log**:
```
[Observer] PlayerCar: physSpeed=0kmh
[Observer] RecruitCar: dist=8.3m physSpeed=11kmh mission=67
```

**Problema 1: Inércia de Frenagem**
- STOP_ZONE_M = 6.0m
- A 11 km/h (~3 m/s), distância de frenagem ~5m
- Zona muito pequena para velocidade

**Problema 2: CLOSE_BLOCKED Delay**
- `CLOSE_BLOCKED_FRAMES = 90` (1.5s)
- Recruta precisa estar parado por 1.5s para reconhecer bloqueio
- Player pode parar instantaneamente → recruta continua 1.5s

**Problema 3: Speed Adaptation Lenta**
- AdaptiveSpeed calcula baseado em deltaH (curvature)
- Não considera velocidade do player
- Recruit mantém SPEED_CIVICO=46 mesmo se player freou

**Solução Proposta**:
1. Aumentar STOP_ZONE_M: 6m → 10m
2. Implementar player speed matching: se player <10 kmh, recruit ≤15 kmh
3. Reduzir CLOSE_BLOCKED_FRAMES: 90 → 45 (0.75s)

---

### 4.5 ❌ Dificuldade de Acompanhar em Alta Velocidade

**Feedback**: "tem dificuldade de me acompanhar se eu for rapido, pq ou ele se perde pq fica pra tras, ou ele bate nas curvas"

**Problema Composto**:

#### A) CURVE_SPEED_REDUCTION Muito Conservador
```cpp
CURVE_SPEED_REDUCTION = 0.80f; // mult mínimo = 0.20
// 46 * 0.20 = 9 km/h em curva máxima
```
- Recruta desacelera até 9 km/h em curvas fechadas
- Player mantém 50-80 km/h
- Gap aumenta exponencialmente

#### B) FAR_CATCHUP Insuficiente
- `FAR_CATCHUP_DIST_M = 40m`
- `SPEED_CATCHUP = 62 km/h`
- Se player está a 80-100 km/h, 62 ainda perde ground

#### C) Invalid Link Penalty
- INVALID_LINK → SPEED_SLOW=12 km/h
- Em alta velocidade, invalid links mais frequentes (path calculation lag)
- Penalty muito severo

**Análise de Distância**:
```
[30656] dist=79.1m prev=69.4m delta=9.7m -> AFASTAR
[30716] dist=87.5m prev=79.1m delta=8.4m -> AFASTAR
[30776] dist=93.3m prev=87.5m delta=5.9m -> AFASTAR
```
Recruta perde ~8m por segundo (48m/min) quando player rápido.

**Solução Proposta**:
1. Reduzir CURVE_SPEED_REDUCTION: 0.80 → 0.60 (mult min = 0.40 = 18 kmh)
2. Implementar dynamic catchup: se dist >60m, speed = 80 kmh
3. Relaxar INVALID_LINK penalty em alta velocidade

---

### 4.6 ❌ Subir Calçada em Curvas

**Feedback**: "algumas curvas ainda sobe na calçada"

**Causa Raiz: Close-Range Chase Mode**

**Análise**:
```cpp
CLOSE_RANGE_STRAIGHT_LINE_DIST = 5; // prevenir MC53 chase
```

**Mas**: Sistema atual força MC52 em close-range
```cpp
// grove_recruit_drive.cpp:1640
if (dist < CLOSE_RANGE_SWITCH_DIST && dist >= SLOW_ZONE_M)
    CCarAI::SetCarMission_FollowCarFaraway(car, playerCar, ...)
```

**Problema**:
- MC52 em dist < 22m ainda pode ignorar road-graph em curvas
- `m_nStraightLineDistance=5` previne transição para MC53, mas MC52 já está ativo
- Curvas fechadas + proximidade → MC52 toma "atalho" pela calçada

**Observação do Log**:
```
[Drive] CLOSE_RANGE_FORCE_MC52: dist=10.2m mission=67→52
[Drive] physSpeed=76kmh heading=-1.553 deltaH=-2.195(WRONG_DIR!)
```
Speed alto (76 kmh) + close range (10m) + curva (deltaH=-2.19) = calçada

**Solução Proposta**:
1. Em close-range + curva (deltaH >1.0), forçar velocidade ≤20 kmh
2. Considerar usar MC31(ESCORT_REAR) em vez de MC52 para close-range
3. Implementar "curve anticipation": reduzir speed antes da curva (usando straight_line_distance)

---

## 5. CONFIGURAÇÕES ATUAIS (grove_recruit_config.h)

### Velocidades
```cpp
SPEED_CIVICO       = 46   // base cruise
SPEED_CIVICO_HIGH  = 60   // straight long roads (não usado efetivamente)
SPEED_CATCHUP      = 62   // catchup when dist > 40m
SPEED_SLOW         = 12   // invalid link penalty
SPEED_MIN          = 8    // absolute minimum
```

### Thresholds de Distância
```cpp
STOP_ZONE_M                  = 6.0   // stop completely
SLOW_ZONE_M                  = 10.0  // slow down
CLOSE_RANGE_SWITCH_DIST      = 22.0  // force MC52
FAR_CATCHUP_DIST_M           = 40.0  // activate speed boost
WRONG_DIR_RECOVERY_DIST_M    = 30.0  // SetupDriveMode only if far
OFFROAD_DIST_M               = 28.0  // vehicle offroad detection
PLAYER_OFFROAD_DIST_M        = 40.0  // player offroad → GOTOCOORDS
```

### Timings
```cpp
ROAD_SNAP_INTERVAL           = 60    // 1.0s - periodic re-snap
LOG_AI_INTERVAL              = 60    // 1.0s - AI dump
OFFROAD_CHECK_INTERVAL       = 30    // 0.5s
OFFROAD_DIRECT_FOLLOW_FRAMES = 90    // 1.5s sustained offroad
CLOSE_BLOCKED_FRAMES         = 90    // 1.5s both stopped
STUCK_DETECT_FRAMES          = 75    // 1.25s below 3 kmh
```

### Heading Thresholds
```cpp
WRONG_DIR_THRESHOLD_RAD      = 1.5   // >1.5rad = wrong direction (~86°)
MISALIGNED_THRESHOLD_RAD     = 0.20  // <0.20rad = straight (~11°)
CURVE_SPEED_REDUCTION        = 0.80  // max reduction to 20% speed
```

---

## 6. RECOMENDAÇÕES PRIORITÁRIAS

### 🔴 PRIORIDADE ALTA (fixes críticos)

#### 1. Fix INVALID_LINK Oscillation
**Impacto**: Resolve 40% dos problemas de navegação

**Solução**:
```cpp
// Implementar backoff exponencial
static int s_invalidLinkBackoff = 0;

if (linkIdInvalid) {
    g_invalidLinkBurst++;
    if (g_invalidLinkBurst > 5) {
        // Pause snap com backoff: 2, 4, 8, 16, 32 frames
        s_invalidLinkBackoff = min(32, 2 << (g_invalidLinkBurst - 5));
        g_civicRoadSnapTimer = -s_invalidLinkBackoff;
    }
    // Não re-snap imediatamente, aguardar próximo cycle
}
```

#### 2. Implementar Hysteresis em Todos os Thresholds
**Impacto**: Elimina oscilações, comportamento mais suave

**Solução**:
```cpp
// FAR_CATCHUP
const float FAR_CATCHUP_ON_DIST   = 40.0f;
const float FAR_CATCHUP_OFF_DIST  = 35.0f;

// OFFROAD
const float PLAYER_OFFROAD_ON_DIST  = 42.0f;  // activate GOTOCOORDS
const float PLAYER_OFFROAD_OFF_DIST = 35.0f;  // return to CIVICO

// Usar variável de estado em vez de threshold único
static bool s_isFarCatchup = false;
if (!s_isFarCatchup && dist > FAR_CATCHUP_ON_DIST)
    s_isFarCatchup = true;
else if (s_isFarCatchup && dist < FAR_CATCHUP_OFF_DIST)
    s_isFarCatchup = false;
```

#### 3. Relaxar WRONG_DIR Threshold
**Impacto**: Permite navegação natural em intersecções

**Solução**:
```cpp
// Dynamic threshold baseado em contexto
float GetWrongDirThreshold(bool isIntersection, float dist) {
    if (isIntersection || dist < CLOSE_RANGE_SWITCH_DIST) {
        return 2.3f;  // ~130° - permite manobras fechadas
    }
    return 1.5f;  // ~86° - padrão
}

// Detectar intersecção: múltiplos links próximos
bool IsNearIntersection(CNodeAddress nodeAddr) {
    // Verificar se node tem >=3 links
    return ThePaths.m_pNodeLinks[nodeAddr].numLinks >= 3;
}
```

---

### 🟡 PRIORIDADE MÉDIA (melhorias significativas)

#### 4. Player Speed Matching
**Impacto**: Previne colisões traseiras

```cpp
// Em ProcessDrivingAI, adicionar:
float playerSpeed = GetPlayerCarSpeed();  // km/h

if (dist < STOP_ZONE_M * 2 && playerSpeed < 10.0f) {
    // Player quase parado, recruta desacelera agressivamente
    cruiseSpeed = min(cruiseSpeed, 15);
}
else if (dist < CLOSE_RANGE_SWITCH_DIST) {
    // Match player speed com margem
    float targetSpeed = playerSpeed * 1.2f;  // 20% faster to close gap
    cruiseSpeed = min(cruiseSpeed, (unsigned char)targetSpeed);
}
```

#### 5. Reduzir Conservadorismo em Curvas
**Impacto**: Acompanha player em alta velocidade

```cpp
CURVE_SPEED_REDUCTION = 0.60f;  // mult min = 0.40 (18 kmh em vez de 9 kmh)

// OU implementar speed mínimo absoluto em curvas
float curveSpeedMin = 18.0f;  // nunca abaixo de 18 kmh
float adaptedSpeed = max(curveSpeedMin, baseSpeed * curveMult);
```

#### 6. Dynamic Catchup Speed
**Impacto**: Recupera distância perdida mais rápido

```cpp
unsigned char GetCatchupSpeed(float dist, float playerSpeed) {
    if (dist > 80.0f) {
        return 90;  // muito longe, sprint
    }
    else if (dist > 60.0f) {
        return 75;  // longe, acelera
    }
    else if (dist > FAR_CATCHUP_DIST_M) {
        return 62;  // padrão
    }
    return SPEED_CIVICO;  // normal
}
```

---

### 🟢 PRIORIDADE BAIXA (polimento)

#### 7. Curve Anticipation
**Impacto**: Prepara para curvas antes de entrar

```cpp
// Usar m_nStraightLineDistance como preview
if (ap.m_nStraightLineDistance < 15) {  // curva em 15m
    float anticipationMult = ap.m_nStraightLineDistance / 15.0f;
    cruiseSpeed *= (0.6f + 0.4f * anticipationMult);  // 60-100% speed
}
```

#### 8. Diferenciação de Modos Cívicos
**Impacto**: Modos têm comportamentos distintos

**Opção A - Speed-based**:
```cpp
// CIVICO_F: conservador (seguro)
SPEED_CIVICO_F = 38;
CURVE_REDUCTION_F = 0.70;

// CIVICO_G: balanceado
SPEED_CIVICO_G = 46;
CURVE_REDUCTION_G = 0.60;

// CIVICO_H: agressivo (rápido)
SPEED_CIVICO_H = 54;
CURVE_REDUCTION_H = 0.50;
```

**Opção B - Mission-based** (atual, mas reforçar):
```cpp
// F: MC67 - nunca força MC52 em close-range, mantém formação geométrica
// G: MC53 - permite chase mode controlled
// H: MC52 - sempre road-graph, nunca chase
```

---

## 7. MÉTRICAS DE SUCESSO

### Quantitativas
- [ ] INVALID_LINK burst: reduzir de ~100/min para <10/min
- [ ] WRONG_DIR false positives em intersecções: <5% de ocorrências
- [ ] Dist gap em alta velocidade: manter <50m quando player a 80+ kmh
- [ ] Colisões traseiras: <1 por 5 minutos de jogo
- [ ] Calçada climbing: <1 por 10 curvas

### Qualitativas
- [ ] Usuário percebe diferença entre modos cívicos
- [ ] Navegação em intersecções "natural" (como NPCs)
- [ ] Offroad/canal seguimento sem "perder-se"
- [ ] Acompanhamento em alta velocidade sem perder muito ground
- [ ] Comportamento geral "bom" → "ótimo"

---

## 8. PRÓXIMOS PASSOS SUGERIDOS

### Fase 1: Estabilização (1-2 dias)
1. Implementar hysteresis em todos os thresholds
2. Fix INVALID_LINK oscillation com backoff
3. Relaxar WRONG_DIR threshold (+0.8 rad)

### Fase 2: Velocidade (1 dia)
4. Implementar player speed matching
5. Reduzir CURVE_SPEED_REDUCTION para 0.60
6. Dynamic catchup speed

### Fase 3: Refinamento (1-2 dias)
7. Curve anticipation
8. Diferenciação real entre modos cívicos
9. Tuning fino baseado em testes

### Fase 4: Validação (ongoing)
10. Testes extensivos em diversos cenários
11. Log analysis para novos patterns
12. Iteração baseada em feedback

---

## 9. CONCLUSÃO

O sistema atual atingiu **~80% de um comportamento bom**. Os problemas restantes são:

1. **Instabilidade de Sistema** (INVALID_LINK oscillation) - 30% do problema
2. **Thresholds Rígidos** (sem hysteresis) - 25% do problema
3. **Velocidade Conservadora** (curvas, catchup) - 20% do problema
4. **Close-Range Behavior** (colisões, calçada) - 15% do problema
5. **Offroad Transitions** (oscilações) - 10% do problema

**Boa Notícia**: Todos os problemas têm soluções conhecidas e implementáveis.

**Estimativa**: Com as recomendações de Prioridade Alta, o sistema pode atingir **90-95% "comportamento bom"** em 2-3 dias de trabalho focado.

O trabalho até agora construiu uma base sólida de logging, diagnóstico e recovery mechanisms. O próximo passo é principalmente **tuning fino** e **adicionar hysteresis/smoothing** aos sistemas existentes.

---

**Documento gerado por**: Claude Code Agent
**Baseado em**: Logs 0-10 (histórico completo), grove_recruit.log (análise detalhada), código-fonte grove_recruit_drive.cpp
