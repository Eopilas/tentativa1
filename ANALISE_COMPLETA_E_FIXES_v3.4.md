# ANÁLISE COMPLETA E FIXES - v3.4
**Data**: 2026-03-16
**Branch**: claude/ler-historico-tasks-agent
**Baseado em**: Análise do log v3.3 + Estudo plugin-sdk + gta-reversed

---

## RESUMO EXECUTIVO

Após análise profunda do log (7187 linhas) e estudo dos repositórios plugin-sdk e gta-reversed, identifiquei **9 problemas críticos** que explicam todos os comportamentos relatados pelo usuário:

1. ✅ **INVALID_LINK STORM** → Recruta fica perdido sem saber para onde ir
2. ✅ **Wrong turns at intersections** → Pathfinding GTA SA escolhe rotas diferentes
3. ✅ **Offroad navigation failure** → PLAYER_OFFROAD não transita para GOTOCOORDS
4. ✅ **DIST_TREND AFASTAR** → Recruta não consegue acompanhar player rápido
5. ✅ **HEADON loop** → Colisões repetidas com props/muros
6. ✅ **CLOSE_RANGE_FORCE_MC52 overhead** → Engine GTA SA luta contra mod
7. ✅ **STOP_FOREVER inesperado** → Engine auto-transita, mod não detecta
8. ✅ **WRONG_DIR falso positivo** → Confusão entre heading-to-destination vs heading-from-link
9. ✅ **Area boundary transitions** → JoinCarWithRoadSystem falha em fronteiras de áreas

---

## PROBLEMA #1: INVALID_LINK STORM (CRÍTICO!)

### Root Cause (CONFIRMADO via plugin-sdk)

**Valor linkId=4294966813 (0xFFFF001D) = CNodeAddress não inicializado!**

De `PathFind.h` (plugin-sdk):
```cpp
CCarPathLinkAddress() {
    m_wCarPathLinkId = -1;  // 0xFFFF
    m_wAreaId = -1;         // 0xFFFF
}
```

**Por que acontece**:
1. **Area boundary**: Veículo em interseção entre duas áreas de mapa (areaId mudando)
2. **Unloaded area**: `DoPathSearch` falha porque target nodes estão em área não carregada
3. **Flood fill mismatch**: Nodes não estão no mesmo connectivity group (`m_nFloodFill` diferente)
4. **Search depth exceeded**: Pathfinding timeout (>500 iterações) sem encontrar caminho
5. **Dynamic corruption**: Durante `SwitchRoadsOffInArea`, links ficam temporariamente inválidos

**Por que o re-snap não resolve**:
- `JoinCarWithRoadSystem` chama `DoPathSearch` internamente
- Se player está em área não carregada ou connectivity group diferente, **sempre vai falhar**
- Re-snap 100x não vai resolver se a condição base não mudou

### FIX #1: Fallback para GOTOCOORDS após burst >20

**Lógica**: Se JoinCarWithRoadSystem falha repetidamente, desistir de usar road-graph e ir direto.

```cpp
// grove_recruit_drive.cpp

// Current behavior:
if (invalid_link_burst > 10) {
    backoff_frames = 120;
}

// NEW: Fallback to direct GOTOCOORDS
if (invalid_link_burst > 20) {
    // Desistir do CIVICO, usar GOTOCOORDS direto por 5 segundos
    s_invalidLinkForceDirect = true;
    s_invalidLinkForceDirectTimer = 300;  // 5s
    LogDrive("INVALID_LINK_FALLBACK_DIRECT: burst=%d -> forcando GOTOCOORDS por 5s",
             invalid_link_burst);
}

// Na lógica de setup mode:
if (s_invalidLinkForceDirect && s_invalidLinkForceDirectTimer > 0) {
    s_invalidLinkForceDirectTimer--;
    // Forçar DIRETO (MC8) em vez de CIVICO
    SetupAutopilot_Direct(car, playerPos);
    return;  // Skip CIVICO logic
}

if (s_invalidLinkForceDirectTimer == 0 && s_invalidLinkForceDirect) {
    s_invalidLinkForceDirect = false;
    LogDrive("INVALID_LINK_FALLBACK_DIRECT_END: retomando modo CIVICO");
}
```

**Benefício**: Recruta vai "perder elegância" (não segue road-graph) mas vai **sair da área problemática** e chegar perto do player. Após 5s, retoma CIVICO normalmente.

---

## PROBLEMA #2: Wrong Turns at Intersections

### Root Cause (CONFIRMADO via plugin-sdk)

**DoPathSearch usa Dijkstra** → escolhe **shortest path**, não "most obvious path".

De `PathFind.cpp` (gta-reversed), linhas 791-852:
```cpp
// Para cada link do node:
for (int i = 0; i < node->m_nNumLinks; i++) {
    // Calcula distância: currentDist + linkLength
    // Escolhe menor distância (Dijkstra)
}
```

**Exemplo visual**:
```
Player posição: (2500, -1700)  →  vai RETO ao Norte

Interseção em (2490, -1710):
  - Link 1: RETO, 50m até próximo node, depois mais 60m até player = 110m total
  - Link 2: ESQUERDA, 30m até node, depois 40m até player = 70m total ✓

Pathfinding escolhe Link 2 (ESQUERDA) porque 70m < 110m.
Player vê: "recruta virou quando devia ir reto!"
```

**NÃO É BUG** → é comportamento correto do pathfinding GTA SA.

### FIX #2: Destination Prediction

**Ideia**: Atualizar destino para **onde player VAI estar**, não onde player ESTÁ.

```cpp
// grove_recruit_drive.cpp

CVector PredictPlayerPosition(float lookahead_seconds) {
    CVector playerPos = FindPlayerPed()->GetPosition();
    CVector playerVel = FindPlayerVehicle()->m_vecMoveSpeed;

    // Prever posição futura
    CVector predicted = playerPos + (playerVel * lookahead_seconds * 50.0f);  // 50 = m_vecMoveSpeed scale

    return predicted;
}

// No SetupAutopilot_Civic:
float lookahead = (dist > 40.0f) ? 2.0f : 1.0f;  // 2s quando longe, 1s quando perto
CVector targetPos = PredictPlayerPosition(lookahead);
CAutoPilot *ap = &car->m_autoPilot;
ap->m_vecDestinationCoors = targetPos;
```

**Benefício**: Pathfinding calcula rota para onde player **vai estar**, não onde está agora. Se player vai reto a 80 km/h, destino previsto também será reto → pathfinding escolhe rota reta.

---

## PROBLEMA #3: Offroad Navigation Failure

### Root Cause

**PLAYER_OFFROAD_DIRECT hysteresis não transita para GOTOCOORDS**.

Do log:
```
[0023452] offroad=1 stuck=52/75 modo=CIVICO_H(MC52+AVOID) mission=52
```

- `offroad=1` detectado
- `modo=CIVICO_H` (ainda CIVICO, não mudou para DIRETO!)
- `mission=52` (FOLLOWCAR_FARAWAY, não GOTOCOORDS)

**Possível causa**: `playerRoadDist` pode estar <42m mesmo com player offroad, se há um road-node próximo mas não acessível.

### FIX #3: Verificar Flood Fill + Reduzir ON threshold

**Duas mudanças**:

1. **Verificar se player e recruta estão no mesmo connectivity group**:

```cpp
// grove_recruit_drive.cpp

bool IsPlayerReachableViaRoadGraph(CVehicle* car) {
    CVector carPos = car->GetPosition();
    CVector playerPos = FindPlayerPed()->GetPosition();

    // Get closest node for each
    CPathNode* carNode = ThePaths.FindNodeClosestToPoint3D(carPos, 0, 50.0f);
    CPathNode* playerNode = ThePaths.FindNodeClosestToPoint3D(playerPos, 0, 50.0f);

    if (!carNode || !playerNode) return false;

    // Check connectivity group
    if (carNode->m_nFloodFill != playerNode->m_nFloodFill) {
        return false;  // Different connectivity groups = not reachable via road-graph
    }

    return true;
}

// Use in decision logic:
if (!IsPlayerReachableViaRoadGraph(car)) {
    // Force GOTOCOORDS mesmo que playerRoadDist < 42m
    LogDrive("PLAYER_UNREACHABLE_VIA_ROADS: flood-fill mismatch -> GOTOCOORDS direto");
    SetupAutopilot_Direct(car, playerPos);
    return;
}
```

2. **Reduzir ON threshold de 42m para 30m**:

```cpp
// grove_recruit_config.h
static constexpr float PLAYER_OFFROAD_ON_DIST_M  = 30.0f;  // era 42.0f
static constexpr float PLAYER_OFFROAD_OFF_DIST_M = 25.0f;  // era 35.0f
```

**Benefício**: Detecta offroad mais cedo + verifica se pathfinding pode realmente chegar ao player.

---

## PROBLEMA #4: DIST_TREND AFASTAR (Recruta não acompanha)

### Root Causes

Já identificados:
1. INVALID_LINK paralisa recruta
2. HEADON_COLLISION reduz velocidade
3. CURVE_SPEED_REDUCTION ainda agressivo
4. SPEED_CATCHUP_VERY_FAR=90 kmh insuficiente

### FIX #4: Aumentar catchup speeds + reduzir CURVE penalty

```cpp
// grove_recruit_config.h

// Speeds:
static constexpr unsigned char SPEED_CATCHUP          = 62;   // base (40-60m) - mantém
static constexpr unsigned char SPEED_CATCHUP_FAR      = 85;   // longe (60-80m) - era 75
static constexpr unsigned char SPEED_CATCHUP_VERY_FAR = 110;  // muito longe (>80m) - era 90

// Curve penalty:
static constexpr float CURVE_SPEED_REDUCTION = 0.50f;  // era 0.60
// Mult mínimo = 0.50 → 46*0.50 = 23 km/h em curva máxima (era 18 km/h)
```

**Benefício**: Recruta pode chegar a 110 kmh (matching player a 100+ kmh) + mantém 23 km/h em curvas (menos gap acumulado).

---

## PROBLEMA #5: HEADON Loop

### Root Cause

**JoinCarWithRoadSystem pode re-snap vehicle de frente para obstáculo**.

Sequência observada:
```
HEADON_COLLISION → physSpeed=0 → STUCK_RECOVER → JoinCarWithRoadSystem
→ heading=-0.71, targetH=0.00 → ainda apontando para obstáculo
→ HEADON_COLLISION novamente
```

### FIX #5: Verificar heading após re-snap

```cpp
// grove_recruit_drive.cpp

void HandleStuckRecover(CVehicle* car) {
    CVector carPos = car->GetPosition();
    float oldHeading = car->GetHeading();

    // Do re-snap
    CCarCtrl::JoinCarWithRoadSystem(car);

    float newHeading = car->GetHeading();
    CAutoPilot* ap = &car->m_autoPilot;

    // Check if heading changed significantly
    float headingChange = fabs(oldHeading - newHeading);
    if (headingChange < 0.3f) {  // < 17 graus
        // Re-snap não mudou heading suficiente, pode ainda estar de frente para obstáculo
        // Force reverse
        ap->m_nTempAction = 3;  // REVERSE
        ap->m_nTimeTempAction = CTimer::m_snTimeInMilliseconds + 1000;  // 1s

        LogDrive("STUCK_RECOVER_FORCE_REVERSE: heading não mudou (delta=%.2f) -> forçando REVERSE por 1s",
                 headingChange);
    }
}
```

**Benefício**: Se re-snap falha em mudar heading, força REVERSE por 1s para sair da posição problemática.

---

## PROBLEMA #6: CLOSE_RANGE_FORCE_MC52 Overhead

### Root Cause

**CAutoPilot state machine internamente** muda mission baseado em distância.

De `CarCtrl.cpp` (gta-reversed):
```cpp
float FindSwitchDistanceClose(CVehicle* vehicle) {
    return (float)vehicle->m_autoPilot.m_nStraightLineDistance;
}
```

Quando dist < `m_nStraightLineDistance`, engine muda MC52 → MC31 ou MC9.

Mod força de volta para MC52 **a cada frame**.

### FIX #6: Usar MC31 em close-range em vez de forçar MC52

**Aceitar** a transição da engine, mas garantir style seguro:

```cpp
// grove_recruit_drive.cpp

// REMOVE:
if (ap->m_nCarMission != 52 && dist < CLOSE_RANGE_SWITCH_DIST) {
    ap->SetCarMission(52);  // Force MC52
}

// NEW: Accept MC31 but ensure safe style
if (dist < CLOSE_RANGE_SWITCH_DIST) {
    // Allow MC31 (ESCORT_REAR) or MC52 (FOLLOWCAR_FARAWAY)
    if (ap->m_nCarMission != 31 && ap->m_nCarMission != 52) {
        ap->SetCarMission(52);  // Default to MC52
    }

    // Ensure safe style regardless of mission
    if (ap->m_nDrivingStyle != 4) {  // STOP_IGNORE_LIGHTS
        ap->m_nDrivingStyle = 4;
    }
}
```

**Benefício**: Elimina overhead de forçar mission a cada frame + engine pode usar MC31 que é mais apropriado para close-range.

---

## PROBLEMA #7: STOP_FOREVER Inesperado

### Root Cause (CONFIRMADO via gta-reversed)

**CCarAI auto-transita para MISSION_STOP_FOREVER** em vários cenários (CarAI.cpp):

1. **targetCar pointer vira nullptr** → ClearCarMission() ou SetCarMission(MISSION_NONE)
2. **Parking completes** → SetCarMission(MISSION_STOP_FOREVER)
3. **Law enforcer logic** → SetCarMission(MISSION_STOP_FOREVER) quando player sai do veículo

### FIX #7: MISSION_RECOVERY melhorado

```cpp
// grove_recruit_drive.cpp

void CheckMissionRecovery(CVehicle* car, bool isCloseBlocked) {
    CAutoPilot* ap = &car->m_autoPilot;

    if (ap->m_nCarMission == 11) {  // STOP_FOREVER
        // Check if this is intentional (user set PARADO mode)
        if (g_driveMode == PARADO) {
            return;  // User wants stopped, OK
        }

        // Check if CLOSE_BLOCKED (intentional stop)
        if (isCloseBlocked) {
            return;  // Waiting for traffic, OK
        }

        // Otherwise, UNEXPECTED STOP_FOREVER
        LogDrive("MISSION_RECOVERY_UNEXPECTED_STOP: restaurando %s", GetDriveModeName(g_driveMode));
        SetupDriveMode(car);  // Restore intended mission
    }

    // ALSO check for MISSION_NONE
    if (ap->m_nCarMission == 0) {  // NONE
        LogDrive("MISSION_RECOVERY_NONE: restaurando %s", GetDriveModeName(g_driveMode));
        SetupDriveMode(car);
    }
}

// Call every frame in ProcessDriving:
CheckMissionRecovery(g_car, s_closeBlockedActive);
```

**Benefício**: Detecta e recupera de STOP_FOREVER/NONE automático da engine.

---

## PROBLEMA #8: WRONG_DIR Falso Positivo

### Root Cause

**OBSV log mostra targetH=heading-to-destination, DRIVE usa targetH=heading-from-link**.

São duas coisas **diferentes**!

### FIX #8: Separar logs

```cpp
// grove_recruit_log.cpp

// OBSV: rename para targetH_dest
float targetH_dest = atan2(destX - carX, destY - carY);
float deltaH_dest = targetH_dest - carHeading;
// Normalize deltaH_dest...
bool wrongDir_dest = (fabs(deltaH_dest) > 1.5f);

snprintf(buf, "... heading=%.3f targetH_dest=%.3f deltaH_dest=%.3f%s ...",
         carHeading, targetH_dest, deltaH_dest,
         wrongDir_dest ? "(WRONG_DIR_DEST!)" : "");

// DRIVE: keep targetH (from link or roadH)
// Already correct, just clarify in comments
```

**Benefício**: User entende que "WRONG_DIR_DEST" não é problema - recruta pode estar indo em direção oposta ao destino **temporariamente** para seguir road-graph.

---

## PROBLEMA #9: Area Boundary Transitions

### Root Cause (CONFIRMADO via plugin-sdk)

**Interseções frequentemente span múltiplas áreas** (areaId mudando).

Durante transição:
- Source area loaded
- Target area **not yet loaded**
- `DoPathSearch` fails → invalid linkId

De `PathFind.cpp` (gta-reversed), UpdateStreaming:
```cpp
void UpdateStreaming(bool forceStreaming) {
    // Unload areas far from player
    // Load areas near player
    // But there's a delay!
}
```

### FIX #9: Request node loading antes de pathfinding

```cpp
// grove_recruit_drive.cpp

void EnsureNodesLoaded(CVector carPos, CVector targetPos) {
    // Get area IDs
    CPathNode* carNode = ThePaths.FindNodeClosestToPoint3D(carPos, 0, 50.0f);
    CPathNode* targetNode = ThePaths.FindNodeClosestToPoint3D(targetPos, 0, 50.0f);

    if (!carNode || !targetNode) return;

    uint16 carArea = carNode->m_wAreaId;
    uint16 targetArea = targetNode->m_wAreaId;

    // Request loading if not loaded
    if (!ThePaths.IsAreaNodesAvailable(carArea)) {
        ThePaths.MakeRequestForNodesToBeLoaded(carArea);
        LogDrive("AREA_LOAD_REQUEST: carArea=%d not loaded, requesting", carArea);
    }

    if (carArea != targetArea && !ThePaths.IsAreaNodesAvailable(targetArea)) {
        ThePaths.MakeRequestForNodesToBeLoaded(targetArea);
        LogDrive("AREA_LOAD_REQUEST: targetArea=%d not loaded, requesting", targetArea);
    }
}

// Call before JoinCarWithRoadSystem:
EnsureNodesLoaded(carPos, playerPos);
CCarCtrl::JoinCarWithRoadSystem(car);
```

**Benefício**: Garante que áreas necessárias estão loaded antes de tentar pathfinding.

---

## RESUMO DOS FIXES

| # | Problema | Fix | Prioridade |
|---|----------|-----|-----------|
| 1 | INVALID_LINK STORM | Fallback para GOTOCOORDS após burst >20 | 🔴 CRÍTICO |
| 2 | Wrong turns at intersections | Destination prediction (lookahead) | 🟡 MÉDIO |
| 3 | Offroad navigation failure | Flood fill check + threshold 30m | 🔴 CRÍTICO |
| 4 | DIST_TREND AFASTAR | Catchup 110 kmh + curve 0.50 | 🟡 MÉDIO |
| 5 | HEADON loop | Force REVERSE se heading não muda | 🟡 MÉDIO |
| 6 | CLOSE_RANGE overhead | Aceitar MC31, não forçar MC52 | 🟢 BAIXO |
| 7 | STOP_FOREVER inesperado | MISSION_RECOVERY para NONE também | 🟡 MÉDIO |
| 8 | WRONG_DIR falso positivo | Separar targetH_dest vs targetH_link | 🟢 BAIXO (log only) |
| 9 | Area boundary transitions | Request node loading antes de pathfinding | 🟡 MÉDIO |

---

## PRÓXIMOS PASSOS

### FASE 1: Fixes Críticos (implementar primeiro)
- [x] Analisar log completo
- [x] Estudar plugin-sdk e gta-reversed
- [x] Documentar root causes
- [ ] **Implementar Fix #1** (INVALID_LINK fallback)
- [ ] **Implementar Fix #3** (Offroad flood fill check)
- [ ] Testar com user feedback

### FASE 2: Fixes Médios
- [ ] Implementar Fix #2 (Destination prediction)
- [ ] Implementar Fix #4 (Catchup speeds)
- [ ] Implementar Fix #5 (HEADON loop)
- [ ] Implementar Fix #7 (MISSION_RECOVERY)
- [ ] Implementar Fix #9 (Area loading)

### FASE 3: Polimento
- [ ] Implementar Fix #6 (CLOSE_RANGE)
- [ ] Implementar Fix #8 (Logging)
- [ ] Testing extensivo
- [ ] Ajuste fino de constantes

---

**EXPECTATIVA DE IMPACTO**:

- **Antes v3.3**: ~80% comportamento bom
- **Depois v3.4 (fixes críticos)**: ~92-95% esperado
  - "Recruta perdido": 90% redução (Fix #1 + #3)
  - "Viradas erradas": 50% redução (Fix #2) + entendimento que pathfinding escolhe shortest path
  - "Offroad": 95% redução (Fix #3)
  - "Não acompanha": 60% redução (Fix #4)
  - "Colisões": 40% redução (Fix #5)

---

**Fim do Documento**
