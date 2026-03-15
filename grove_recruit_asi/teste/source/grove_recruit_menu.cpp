/*
 * grove_recruit_menu.cpp
 *
 * Menu in-game do mod grove_recruit.
 *
 * ABERTURA/FECHO:
 *   INSERT — abre/fecha o menu
 *   ESC    — fecha (quando menu aberto)
 *
 * NAVEGACAO (quando menu aberto):
 *   UP/DOWN arrow    — mover seleccao
 *   LEFT/RIGHT arrow — alterar valor do item seleccionado
 *   Teclas 1-4, N, B — funcionam normalmente via HandleKeys quando menu fechado
 *
 * ITENS DO MENU:
 *   Modo Conducao  — ciclagem LEFT/RIGHT entre todos os modos CIVICO + DIRETO + PARADO
 *   Agressivo      — toggle LEFT/RIGHT (AGRESSIVO / PASSIVO)
 *   Drive-by       — toggle LEFT/RIGHT (ON / OFF, so em estado PASSENGER)
 *   Recrutar       — sem acoes directas; mostra tecla [1]
 *   Dispensar      — executa DismissRecruit quando Enter ou RIGHT
 *   Fechar         — fecha menu
 *
 * RENDERIZACAO:
 *   CFont::PrintString com coordenadas no espaco virtual SA (640x448).
 *   Registado em Events::drawHudEvent (durante renderizacao do HUD).
 *   Background via CFont::SetBackground.
 *
 * LAYOUT (topo-direito do ecra, 455, 80):
 *   ===GROVE RECRUIT v3===
 *   St: ON_FOOT  R:2 (1van)
 *   ───────────────────
 *  [>] Modo: < CIVICO-E >
 *  [ ] Aggro: < AGRESSIVO >
 *  [ ] Drive-by: < OFF >
 *  [ ] Recrutar [1]
 *  [ ] Dispensar [ENTER]
 *  [ ] Fechar [ESC/INS]
 *   ───────────────────
 *   Z/C=nav  X/V=val
 */
#include "grove_recruit_shared.h"

// ───────────────────────────────────────────────────────────────────
// Constantes de layout (coordenadas no espaco virtual 640x448 do SA)
// ───────────────────────────────────────────────────────────────────
static constexpr float MENU_X       = 455.0f;
static constexpr float MENU_Y_START = 80.0f;
static constexpr float MENU_LINE_H  = 15.0f;
static constexpr float MENU_SCALE_X = 0.32f;
static constexpr float MENU_SCALE_Y = 0.65f;

// ───────────────────────────────────────────────────────────────────
// Items do menu
// ───────────────────────────────────────────────────────────────────
enum MenuItemID
{
    MITEM_MODE     = 0,
    MITEM_AGGRO    = 1,
    MITEM_DRIVEBY  = 2,
    MITEM_RECRUIT  = 3,
    MITEM_DISMISS  = 4,
    MITEM_CLOSE    = 5,
    MITEM_COUNT    = 6,
};

// Taxa de repeticao de input (frames) para manter pressionado
static constexpr int MENU_INPUT_DELAY  = 14;
static constexpr int MENU_ENTER_DELAY  = 20;

// Contador interno de cooldown de input
static int s_menuDelay = 0;

// ───────────────────────────────────────────────────────────────────
// Helper: tecla mantida (raw)
// ───────────────────────────────────────────────────────────────────
static inline bool KeyHeld(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// ───────────────────────────────────────────────────────────────────
// HandleMenuKeys
// Chamado em vez de HandleKeys quando g_menuOpen == true.
// ───────────────────────────────────────────────────────────────────
void HandleMenuKeys(CPlayerPed* player)
{
    // Fechar com ESC (INSERT ja tratado em HandleKeys antes de chegar aqui)
    if (KeyJustPressed(VK_MENU_BACK))
    {
        g_menuOpen = false;
        LogMenu("MENU: fechado via ESC");
        return;
    }

    // Rate-limiting para teclas mantidas
    if (s_menuDelay > 0) { --s_menuDelay; return; }

    bool anyInput = false;

    // ── Navegacao UP/DOWN ────────────────────────────────────────
    if (KeyHeld(VK_MENU_UP))
    {
        g_menuSel = (g_menuSel - 1 + MITEM_COUNT) % MITEM_COUNT;
        s_menuDelay = MENU_INPUT_DELAY;
        anyInput = true;
    }
    else if (KeyHeld(VK_MENU_DOWN))
    {
        g_menuSel = (g_menuSel + 1) % MITEM_COUNT;
        s_menuDelay = MENU_INPUT_DELAY;
        anyInput = true;
    }

    // ── Alterar valor LEFT/RIGHT ─────────────────────────────────
    else if (KeyHeld(VK_MENU_LEFT) || KeyHeld(VK_MENU_RIGHT))
    {
        int dir = KeyHeld(VK_MENU_RIGHT) ? 1 : -1;
        s_menuDelay = MENU_INPUT_DELAY;
        anyInput = true;

        switch (g_menuSel)
        {
        case MITEM_MODE:
        {
            int cnt = static_cast<int>(DriveMode::COUNT);
            int nm  = (static_cast<int>(g_driveMode) + dir + cnt) % cnt;
            g_driveMode = static_cast<DriveMode>(nm);
            if (g_state == ModState::DRIVING || g_state == ModState::PASSENGER)
                SetupDriveMode(player, g_driveMode);
            LogMenu("MENU: modo -> %s", DriveModeName(g_driveMode));
            break;
        }
        case MITEM_AGGRO:
            g_aggressive = !g_aggressive;
            g_passiveTimer = 0;
            if (g_state == ModState::ON_FOOT)
                player->ForceGroupToAlwaysFollow(!g_aggressive);
            LogMenu("MENU: aggr -> %d", (int)g_aggressive);
            break;
        case MITEM_DRIVEBY:
            if (g_state == ModState::PASSENGER)
            {
                g_driveby = !g_driveby;
                LogMenu("MENU: driveby -> %d", (int)g_driveby);
            }
            break;
        case MITEM_DISMISS:
            if (dir > 0 && g_state != ModState::INACTIVE)
            {
                DismissRecruit(player);
                ShowMsg("~y~Recruta dispensado.");
                g_menuOpen = false;
                LogMenu("MENU: dismiss via RIGHT");
            }
            break;
        case MITEM_CLOSE:
            if (dir > 0)
            {
                g_menuOpen = false;
                LogMenu("MENU: fechado via RIGHT em FECHAR");
            }
            break;
        default:
            break;
        }
    }

    (void)anyInput;
}

// ───────────────────────────────────────────────────────────────────
// RenderMenu
// Chamado em Events::drawHudEvent quando g_menuOpen == true.
// Usa CFont para renderizar texto no ecra.
// ───────────────────────────────────────────────────────────────────
void RenderMenu(CPlayerPed* player)
{
    if (!player) return;

    // Contar recrutas rastreados: total e vanilla
    int nTotal   = 0;
    int nVanilla = 0;
    for (int i = 0; i < MAX_TRACKED_RECRUITS; ++i)
    {
        if (g_allRecruits[i].ped)
        {
            ++nTotal;
            if (g_allRecruits[i].isVanilla) ++nVanilla;
        }
    }

    float y = MENU_Y_START;
    char  buf[128];

    // ── Setup de fonte ───────────────────────────────────────────
    CFont::SetBackground(true, false);
    CFont::SetBackgroundColor(CRGBA(0, 0, 0, 160));
    CFont::SetScale(MENU_SCALE_X, MENU_SCALE_Y);
    CFont::SetProportional(true);
    CFont::SetJustification(ALIGN_LEFT);

    // ── Titulo ───────────────────────────────────────────────────
    CFont::SetColor(CRGBA(255, 200, 50, 255));
    CFont::PrintString(MENU_X, y, "==GROVE RECRUIT v3==");
    y += MENU_LINE_H;

    // ── Status ───────────────────────────────────────────────────
    CFont::SetColor(CRGBA(180, 220, 180, 255));
    snprintf(buf, sizeof(buf), "St:%-9s R:%d(%dv)",
        StateName(g_state), nTotal, nVanilla);
    CFont::PrintString(MENU_X, y, buf);
    y += MENU_LINE_H;

    // Separador
    CFont::SetColor(CRGBA(120, 120, 120, 200));
    CFont::PrintString(MENU_X, y, "--------------------");
    y += MENU_LINE_H;

    // ── Items ────────────────────────────────────────────────────
    struct MenuLine
    {
        int  id;
        char text[80];
        bool enabled;
    };
    MenuLine lines[MITEM_COUNT];

    // Modo Conducao
    lines[MITEM_MODE].id      = MITEM_MODE;
    lines[MITEM_MODE].enabled = true;
    snprintf(lines[MITEM_MODE].text, sizeof(lines[MITEM_MODE].text),
        "Modo: <%-8s>", DriveModeShortName(g_driveMode));

    // Agressivo
    lines[MITEM_AGGRO].id      = MITEM_AGGRO;
    lines[MITEM_AGGRO].enabled = (g_state != ModState::INACTIVE);
    snprintf(lines[MITEM_AGGRO].text, sizeof(lines[MITEM_AGGRO].text),
        "Aggro: <%s>", g_aggressive ? "AGRES " : "PASSI ");

    // Drive-by
    lines[MITEM_DRIVEBY].id      = MITEM_DRIVEBY;
    lines[MITEM_DRIVEBY].enabled = (g_state == ModState::PASSENGER);
    snprintf(lines[MITEM_DRIVEBY].text, sizeof(lines[MITEM_DRIVEBY].text),
        "Drive-by: <%s>", g_driveby ? "ON " : "OFF");

    // Recrutar
    lines[MITEM_RECRUIT].id      = MITEM_RECRUIT;
    lines[MITEM_RECRUIT].enabled = true;
    snprintf(lines[MITEM_RECRUIT].text, sizeof(lines[MITEM_RECRUIT].text),
        "Recrutar     [1]");

    // Dispensar
    lines[MITEM_DISMISS].id      = MITEM_DISMISS;
    lines[MITEM_DISMISS].enabled = (g_state != ModState::INACTIVE);
    snprintf(lines[MITEM_DISMISS].text, sizeof(lines[MITEM_DISMISS].text),
        "Dispensar    [>>]");

    // Fechar
    lines[MITEM_CLOSE].id      = MITEM_CLOSE;
    lines[MITEM_CLOSE].enabled = true;
    snprintf(lines[MITEM_CLOSE].text, sizeof(lines[MITEM_CLOSE].text),
        "Fechar  [ESC/>>]");

    for (int i = 0; i < MITEM_COUNT; ++i)
    {
        bool sel = (i == g_menuSel);
        if (!lines[i].enabled)
        {
            // Item desactivado: cinzento
            CFont::SetColor(CRGBA(90, 90, 90, 200));
        }
        else if (sel)
        {
            // Item seleccionado: amarelo brilhante
            CFont::SetColor(CRGBA(255, 230, 60, 255));
        }
        else
        {
            // Item normal: branco
            CFont::SetColor(CRGBA(210, 210, 210, 240));
        }

        // Prefixo de seleccao
        char lineBuf[96];
        snprintf(lineBuf, sizeof(lineBuf), "%s%s",
            sel ? ">>" : "  ",
            lines[i].text);
        CFont::PrintString(MENU_X, y, lineBuf);
        y += MENU_LINE_H;
    }

    // Separador + legenda
    CFont::SetColor(CRGBA(120, 120, 120, 200));
    CFont::PrintString(MENU_X, y, "--------------------");
    y += MENU_LINE_H;

    CFont::SetColor(CRGBA(160, 160, 160, 200));
    CFont::PrintString(MENU_X, y, "^v=nav <>|ENT=val");
    y += MENU_LINE_H;

    // Teclas de acoes rapidas (lembretes)
    CFont::SetColor(CRGBA(140, 180, 140, 200));
    snprintf(buf, sizeof(buf), "1=R/D 2=Car 3=Pax 4=Mod");
    CFont::PrintString(MENU_X, y, buf);
}
