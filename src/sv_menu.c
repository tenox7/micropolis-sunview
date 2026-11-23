/* sv_menu.c -- the menus, the command dispatcher and the menu bars.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The menus are the DUX ones: Micropolis (the DUX SimCity menu), Options,
 * Disasters, Time, Priority and Windows on the controls window; Zones and
 * Overlays on the map;
 * Display and Options on the editor, and the tool menu (the DUX tool pie)
 * on the editor's right button.  Each menu is made once; its check marks
 * are refreshed every time it is shown.  Every menu starts with a harmless
 * title item, because releasing over a pull-right selects the first item
 * of its submenu.
 *
 * A menu bar is a canvas drawn like the DUX Tk menu buttons; pressing a
 * button pops its menu up.
 */

#include "sv.h"

int SvPriority = 1;			/* Zoom Zoom */
int SvBarH = 24;

static char *lv_name[3] = { "Easy", "Medium", "Hard" };


/* ==== helpers ============================================================ */

static Menu_item
add_item(m, label, value)
  Menu m;
  char *label;
  int value;
{
  Menu_item it;

  it = menu_create_item(MENU_STRING, label, MENU_VALUE, value, 0);
  menu_set(m, MENU_APPEND_ITEM, it, 0);
  return it;
}


static void
add_pullright(m, label, sub)
  Menu m;
  char *label;
  Menu sub;
{
  menu_set(m, MENU_APPEND_ITEM,
	   menu_create_item(MENU_PULLRIGHT_ITEM, label, sub, 0), 0);
}


static Menu
new_menu(title)
  char *title;
{
  Menu m = menu_create(0);

  add_item(m, title, 0);		/* harmless default */
  return m;
}


/* A menu of check or radio items whose labels get a mark. */
#define MK_MAX	12

typedef struct Marked {
  Menu menu;
  int n;
  char *name[MK_MAX];
  Menu_item item[MK_MAX];
  char label[MK_MAX][48];
} Marked;


static void
mk_add(mk, name, value)
  Marked *mk;
  char *name;
  int value;
{
  int i = mk->n++;

  mk->name[i] = name;
  strcpy(mk->label[i], name);
  mk->item[i] = add_item(mk->menu, mk->label[i], value);
}


/* check: "[x] name" or "[ ] name"; radio: "* name" or "  name" */
static void
mk_set(mk, i, on, check)
  Marked *mk;
  int i, on, check;
{
  if (check)
    sprintf(mk->label[i], "%s %s", on ? "[x]" : "[ ]", mk->name[i]);
  else
    sprintf(mk->label[i], "%s %s", on ? "*" : " ", mk->name[i]);
  menu_set(mk->item[i], MENU_STRING, mk->label[i], 0);
}


/* ==== the controls window menus ========================================= */

static char *sc_name[9] = {
  "", "Dullsville 1900", "San Francisco 1906", "Hamburg 1944",
  "Bern 1965", "Tokyo 1957", "Detroit 1972", "Boston 2010",
  "Rio de Janeiro 2047"
};


Menu
sv_simcity_menu()
{
  static Menu m = NULL;
  Menu newc, scen;
  int i;

  if (m != NULL)
    return m;
  newc = new_menu("New City:");
  for (i = 0; i < 3; i++)
    add_item(newc, lv_name[i], SV_CMD_NEW + i);
  scen = new_menu("Scenarios:");
  for (i = 1; i <= 8; i++)
    add_item(scen, sc_name[i], SV_CMD_SCENARIO + i);

  m = new_menu("Micropolis:");
  add_item(m, "About...", SV_CMD_ABOUT);
  add_pullright(m, "New City", newc);
  add_item(m, "Load City...", SV_CMD_LOAD);
  add_pullright(m, "Load Scenario", scen);
  add_item(m, "Save City", SV_CMD_SAVE);
  add_item(m, "Save City as...", SV_CMD_SAVE_AS);
  add_item(m, "Quit Playing!", SV_CMD_QUIT);
  return m;
}


#define NOPTS 7

static int
opt_on(i)
  int i;
{
  switch (i) {
  case 0: return autoBudget;
  case 1: return autoBulldoze;
  case 2: return !NoDisasters;
  case 3: return UserSoundOn;
  case 4: return DoAnimation;
  case 5: return DoMessages;
  case 6: return DoNotices;
  }
  return 0;
}


Menu
sv_options_menu()
{
  static Marked mk;
  static char *names[NOPTS] = {
    "Auto Budget", "Auto Bulldoze", "Disasters", "Sound", "Animation",
    "Messages", "Notices"
  };
  static char *levels[3] = { "Level: Easy", "Level: Medium", "Level: Hard" };
  int i;

  if (mk.menu == NULL) {
    mk.menu = new_menu("Options:");
    for (i = 0; i < NOPTS; i++)
      mk_add(&mk, names[i], SV_CMD_OPTION + i);
    for (i = 0; i < 3; i++)
      mk_add(&mk, levels[i], SV_CMD_LEVEL + i);
  }
  for (i = 0; i < NOPTS; i++)
    mk_set(&mk, i, opt_on(i), 1);
  for (i = 0; i < 3; i++)
    mk_set(&mk, NOPTS + i, i == GameLevel, 0);
  return mk.menu;
}


static char *ds_name[6] = {
  "Monster", "Fire", "Flood", "Meltdown", "Tornado", "Earthquake"
};
static char *ds_action[6] = {
  "release a monster?", "start a fire?", "bring on a flood?",
  "have a nuclear meltdown?", "spin up a tornado?", "cause an earthquake?"
};


Menu
sv_disasters_menu()
{
  static Menu m = NULL;
  int i;

  if (m == NULL) {
    m = new_menu("Disasters:");
    for (i = 0; i < 6; i++)
      add_item(m, ds_name[i], SV_CMD_DISASTER + i);
  }
  return m;
}


Menu
sv_time_menu()
{
  static Marked mk;
  static char *names[4] = { "Pause", "Slow", "Medium", "Fast" };
  int i, cur;

  if (mk.menu == NULL) {
    mk.menu = new_menu("Time:");
    for (i = 0; i < 4; i++)
      mk_add(&mk, names[i], SV_CMD_SPEED + i);
  }
  cur = sim_paused ? 0 : SimMetaSpeed;
  for (i = 0; i < 4; i++)
    mk_set(&mk, i, i == cur, 0);
  return mk.menu;
}


/* The DUX "Priority": how often the simulation runs (milliseconds). */
static char *pr_name[5] = {
  "Flat Out!", "Zoom Zoom", "Buzz Buzz", "Putter Putter", "Snore Snore"
};
static int pr_delay[5] = { 10, 50, 100, 250, 1000 };


Menu
sv_priority_menu()
{
  static Marked mk;
  int i;

  if (mk.menu == NULL) {
    mk.menu = new_menu("Priority:");
    for (i = 0; i < 5; i++)
      mk_add(&mk, pr_name[i], SV_CMD_PRIORITY + i);
  }
  for (i = 0; i < 5; i++)
    mk_set(&mk, i, i == SvPriority, 0);
  return mk.menu;
}


Menu
sv_windows_menu()
{
  static Menu m = NULL;

  if (m == NULL) {
    m = new_menu("Windows:");
    add_item(m, "Budget", SV_CMD_BUDGET);
    add_item(m, "Evaluation", SV_CMD_EVAL);
    add_item(m, "Graph", SV_CMD_GRAPH);
    add_item(m, "Map", SV_CMD_SHOW_MAP);
    add_item(m, "Editor", SV_CMD_SHOW_ED);
    add_item(m, "Last Notice", SV_CMD_NOTICE);
  }
  return m;
}


/* ==== the editor menus =================================================== */

static char *sk_name[5] = { "Always", "Often", "Sometimes", "Seldom",
			    "Rarely" };
static int sk_skip[5] = { 0, 1, 4, 16, 64 };


Menu
sv_display_menu()
{
  static Marked mk;
  int i;

  if (mk.menu == NULL) {
    mk.menu = new_menu("Display:");
    for (i = 0; i < 5; i++)
      mk_add(&mk, sk_name[i], SV_CMD_SKIP + i);
  }
  for (i = 0; i < 5; i++)
    mk_set(&mk, i, sk_skip[i] == SvEditorSkip, 0);
  return mk.menu;
}


Menu
sv_edopt_menu()
{
  static Marked mk;

  if (mk.menu == NULL) {
    mk.menu = new_menu("Options:");
    mk_add(&mk, "Auto Goto", SV_CMD_EDOPT + 0);
    mk_add(&mk, "Pallet Panel", SV_CMD_EDOPT + 1);
    mk_add(&mk, "Chalk Overlay", SV_CMD_EDOPT + 2);
  }
  mk_set(&mk, 0, autoGo, 1);
  mk_set(&mk, 1, SvEditorTools, 1);
  mk_set(&mk, 2, SvEditorChalk, 1);
  return mk.menu;
}


/* The DUX tool pie as a menu, with the Zone and Build pies as
 * pull-rights.  The current tool is marked. */
static struct {
  int pie;				/* 0 main, 1 zone, 2 build */
  char *name;
  int state;
} tm_tab[] = {
  { 0, "Road", roadState },
  { 0, "Bulldozer", dozeState },
  { 0, "Wire", wireState },
  { 0, "Rail", rrState },
  { 0, "Chalk", chalkState },
  { 0, "Eraser", eraserState },
  { 1, "Query", queryState },
  { 1, "Police", policeState },
  { 1, "Industrial", industrialState },
  { 1, "Commercial", commercialState },
  { 1, "Residential", residentialState },
  { 1, "Fire", fireState },
  { 2, "Airport", airportState },
  { 2, "Nuclear", nuclearState },
  { 2, "Seaport", seaportState },
  { 2, "Park", parkState },
  { 2, "Stadium", stadiumState },
  { 2, "Coal", powerState },
  { -1, NULL, 0 }
};


Menu
sv_tool_menu()
{
  static Menu top = NULL;
  static Menu_item items[24];
  static char labels[24][40];
  Menu pie[3];
  int i, cur;

  if (top == NULL) {
    pie[0] = top = new_menu("Tool:");
    pie[1] = new_menu("Zone:");
    pie[2] = new_menu("Build:");
    for (i = 0; tm_tab[i].name != NULL; i++) {
      strcpy(labels[i], tm_tab[i].name);
      items[i] = add_item(pie[tm_tab[i].pie], labels[i],
			  SV_CMD_TOOL + tm_tab[i].state);
    }
    add_pullright(top, "Zone", pie[1]);
    add_pullright(top, "Build", pie[2]);
  }
  cur = SvEditor ? SvEditor->tool_state : -1;
  for (i = 0; tm_tab[i].name != NULL; i++) {
    sprintf(labels[i], "%s %s", (tm_tab[i].state == cur) ? "*" : " ",
	    tm_tab[i].name);
    menu_set(items[i], MENU_STRING, labels[i], 0);
  }
  return top;
}


/* ==== commands =========================================================== */

static void
toggle_option(i)
  int i;
{
  switch (i) {
  case 0:
    autoBudget = !autoBudget;
    break;
  case 1:
    autoBulldoze = !autoBulldoze;
    break;
  case 2:
    NoDisasters = !NoDisasters;
    break;
  case 3:
    UserSoundOn = !UserSoundOn;
    if (UserSoundOn)
      InitializeSound();
    else
      sv_stop_all_sounds();
    break;
  case 4:
    DoAnimation = !DoAnimation;
    break;
  case 5:
    DoMessages = !DoMessages;
    break;
  case 6:
    DoNotices = !DoNotices;
    break;
  }
  MustUpdateOptions = 1;
}


static void
disaster(i)
  int i;
{
  char msg[100];

  sprintf(msg, "Oh no!  Do you really want to %s", ds_action[i]);
  if (!sv_ask(msg, "I guess so.", "No way!"))
    return;
  switch (i) {
  case 0: MakeMonster(); break;
  case 1: MakeFire(); break;
  case 2: MakeFlood(); break;
  case 3: MakeMeltdown(); break;
  case 4: MakeTornado(); break;
  case 5: MakeEarthquake(); break;
  }
}


static void
editor_option(i)
  int i;
{
  switch (i) {
  case 0:
    autoGo = !autoGo;
    MustUpdateOptions = 1;
    break;
  case 1:
    sv_editor_show_tools(!SvEditorTools);
    break;
  case 2:
    SvEditorChalk = !SvEditorChalk;
    sv_editor_invalidate();
    break;
  }
}


void
sv_do_command(cmd)
  int cmd;
{
  if (cmd <= 0)
    return;
  if (cmd >= SV_CMD_TOOL && cmd <= SV_CMD_TOOL + lastState) {
    sv_select_tool(cmd - SV_CMD_TOOL);
  } else if (cmd >= SV_CMD_EDOPT && cmd < SV_CMD_EDOPT + 3) {
    editor_option(cmd - SV_CMD_EDOPT);
  } else if (cmd >= SV_CMD_SKIP && cmd < SV_CMD_SKIP + 5) {
    SvEditorSkip = sk_skip[cmd - SV_CMD_SKIP];
  } else if (cmd >= SV_CMD_PRIORITY && cmd < SV_CMD_PRIORITY + 5) {
    SvPriority = cmd - SV_CMD_PRIORITY;
    sv_set_priority(pr_delay[SvPriority]);
  } else if (cmd >= SV_CMD_MAP && cmd < SV_CMD_MAP + NMAPS) {
    sv_map_set_state(cmd - SV_CMD_MAP);
  } else if (cmd >= SV_CMD_NEW && cmd < SV_CMD_NEW + 3) {
    if (sv_ask("Start a new city?  The current city will be lost.",
	       "New City", "Keep playing."))
      sv_new_city(cmd - SV_CMD_NEW);
  } else if (cmd > SV_CMD_SCENARIO && cmd <= SV_CMD_SCENARIO + 8) {
    if (sv_ask("Load this scenario?  The current city will be lost.",
	       "Load Scenario", "Keep playing."))
      sv_load_scenario(cmd - SV_CMD_SCENARIO);
  } else if (cmd >= SV_CMD_OPTION && cmd < SV_CMD_OPTION + NOPTS) {
    toggle_option(cmd - SV_CMD_OPTION);
  } else if (cmd >= SV_CMD_DISASTER && cmd < SV_CMD_DISASTER + 6) {
    disaster(cmd - SV_CMD_DISASTER);
  } else if (cmd >= SV_CMD_SPEED && cmd < SV_CMD_SPEED + 4) {
    sv_set_sim_speed(cmd - SV_CMD_SPEED);
  } else if (cmd >= SV_CMD_LEVEL && cmd < SV_CMD_LEVEL + 3) {
    SetGameLevel(cmd - SV_CMD_LEVEL);
  } else {
    switch (cmd) {
    case SV_CMD_LOAD:
      sv_file_load();
      break;
    case SV_CMD_SAVE:
      sv_save_city();
      break;
    case SV_CMD_SAVE_AS:
      sv_file_save_as();
      break;
    case SV_CMD_ABOUT:
      sv_about();
      break;
    case SV_CMD_QUIT:
      sv_quit();
      break;
    case SV_CMD_BUDGET:
      sv_budget_show();
      break;
    case SV_CMD_EVAL:
      sv_eval_show();
      break;
    case SV_CMD_GRAPH:
      sv_graph_show();
      break;
    case SV_CMD_SHOW_MAP:
      sv_show_frame(SvMapFrame);
      break;
    case SV_CMD_SHOW_ED:
      sv_show_frame(SvEditorFrame);
      break;
    case SV_CMD_NOTICE:
      sv_notice_again();
      break;
    }
  }
  sv_head_redraw();
}


/* Pop a menu up at the event and carry out the choice. */
void
sv_menu_event(m, win, event)
  Menu m;
  Window win;
  Event *event;
{
  int cmd;

  if (m == NULL)
    return;
#ifdef SUNOS3
  /* SunOS 3.5 menus follow only the right button unless told */
  if (event_id(event) == MS_LEFT || event_id(event) == MS_MIDDLE)
    cmd = (int)menu_show(m, win, event, MENU_BUTTON, event_id(event), 0);
  else
#endif
  cmd = (int)menu_show(m, win, event, 0);
  if (cmd != 0)
    sv_do_command(cmd);
}


/* ==== menu bars ========================================================== */

#define BAR_MAX	4
#define BAR_EDGE	4		/* room before the first, after the last */
#define BAR_PAD		10		/* a button: its label's width plus this */

static SvBar *bar_tab[BAR_MAX];
static int bar_n = 0;


/* The width a bar needs to show every button of items. */
int
sv_bar_width(items)
  SvBarItem *items;
{
  int i, w = 2 * BAR_EDGE;

  for (i = 0; items[i].label != NULL && i < SV_BAR_MAX; i++)
    w += sv_text_width(SvBoldFont, items[i].label) + BAR_PAD;
  return w;
}


static SvBar *
bar_of(canvas)
  Canvas canvas;
{
  int i;

  for (i = 0; i < bar_n; i++)
    if (bar_tab[i]->canvas == canvas)
      return bar_tab[i];
  return NULL;
}


static void
bar_draw(b)
  SvBar *b;
{
  int i, x, ty, h = b->h;

  if (b->w <= 0 || h <= 0)
    return;
  if (b->buf == NULL || b->buf->pr_width != b->w ||
      b->buf->pr_height != h) {
    if (b->buf != NULL)
      pr_destroy(b->buf);
    b->buf = mem_create(b->w, h, SvColor ? 8 : 1);
  }
  sv_fill(b->buf, 0, 0, b->w, h, SvUiBack);
  sv_bevel2(b->buf, 0, 0, b->w, h, 1);
  ty = (h - sv_font_height(SvBoldFont)) / 2;
  x = BAR_EDGE;
  for (i = 0; i < b->n; i++) {
    b->x[i] = x;
    b->lw[i] = sv_text_width(SvBoldFont, b->item[i].label) + BAR_PAD;
    if (i == b->pressed) {
      sv_bevel2(b->buf, x, 3, b->lw[i], h - 6, 1);
    }
    sv_text(b->buf, x + 5, ty, SvUiText, SvBoldFont, b->item[i].label);
    x += b->lw[i];
  }
  b->end = x + BAR_EDGE;
  if (b->extra != NULL)
    (*b->extra)(b);
}


static void
bar_blit(b)
  SvBar *b;
{
  if (b->buf != NULL)
    pw_rop(b->pw, 0, 0, b->w, b->h, PIX_SRC, b->buf, 0, 0);
}


void
sv_bar_redraw(b)
  SvBar *b;
{
  if (b == NULL || !SvReady)
    return;
  bar_draw(b);
  bar_blit(b);
}


void
sv_bar_cms(b)
  SvBar *b;
{
  if (b != NULL)
    sv_set_cms(b->pw);
}


static void
bar_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  SvBar *b = bar_of(canvas);

  if (b == NULL || !SvReady) {
    pw_writebackground(pw, 0, 0, 2000, 100, PIX_SRC);
    return;
  }
  bar_draw(b);
  bar_blit(b);
}


static void
bar_resize(canvas, w, h)
  Canvas canvas;
  int w, h;
{
  SvBar *b = bar_of(canvas);

  if (b == NULL)
    return;
  b->w = w;
  b->h = h;
}


static void
bar_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  SvBar *b = bar_of(canvas);
  int i, x = event_x(event), id = event_id(event);
  Menu m;

  if (b == NULL || !SvReady)
    return;
  if ((id == MS_LEFT || id == MS_MIDDLE || id == MS_RIGHT) &&
      event_is_down(event)) {
    for (i = 0; i < b->n; i++)
      if (x >= b->x[i] && x < b->x[i] + b->lw[i])
	break;
    if (i >= b->n)
      return;
    b->pressed = i;
    sv_bar_redraw(b);
    m = (*b->item[i].menu)();
    b->pressed = -1;
    sv_menu_event(m, canvas, canvas_window_event(canvas, event));
    sv_bar_redraw(b);
    return;
  }
  if (event_is_ascii(event) ||
      (id >= KEY_RIGHT(1) && id <= KEY_RIGHT(15)))
    sv_editor_key(event);
}


/* A menu bar across the top of a frame.  items ends with a NULL label;
 * extra, if not NULL, draws the right-hand part of the bar. */
SvBar *
sv_bar_create(frame, w, items, extra)
  Frame frame;
  int w;
  SvBarItem *items;
  void (*extra)();
{
  SvBar *b;

  b = (SvBar *)calloc(1, sizeof(SvBar));
  if (b == NULL || bar_n >= BAR_MAX) {
    fprintf(stderr, "micropolis: out of menu bars\n");
    exit(1);
  }
  SvBarH = sv_font_height(SvBigFont) + 10;
  if (sv_font_height(SvBoldFont) + 10 > SvBarH)
    SvBarH = sv_font_height(SvBoldFont) + 10;
  b->item = items;
  for (b->n = 0; items[b->n].label != NULL && b->n < SV_BAR_MAX; b->n++)
    ;
  b->pressed = -1;
  b->extra = extra;
  bar_tab[bar_n++] = b;
  b->canvas = window_create(frame, CANVAS,
			    WIN_X, 0,
			    WIN_Y, 0,
			    WIN_WIDTH, w,
			    WIN_HEIGHT, SvBarH,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, bar_repaint,
			    CANVAS_RESIZE_PROC, bar_resize,
			    WIN_EVENT_PROC, bar_event,
			    0);
  window_set(b->canvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  b->pw = canvas_pixwin(b->canvas);
  b->w = (int)window_get(b->canvas, CANVAS_WIDTH);
  b->h = (int)window_get(b->canvas, CANVAS_HEIGHT);
  return b;
}
