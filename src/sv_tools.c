/* sv_tools.c -- the editor's tool palette.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The DUX palette (res/weditor.tcl): the tool's name and cost on top, then
 * the tool icons (ic*.xpm, ic*hi.xpm for the selected tool) at the DUX
 * places.  Clicking an icon picks the tool.
 *
 * In black and white the palette is the one of Micropolis for the DMD
 * 5620: 16x16 1 bit icons drawn for that port (and here Chalk and
 * Eraser), in 2 columns of framed buttons with a shadow, on a grey ground,
 * the selected one inverted.  It is narrow, so the editor's menu bar shows
 * the tool's name and cost.
 */

#include "sv.h"

Canvas SvToolCanvas = NULL;

static struct {
  int state;
  char *icon;
  int x, y;
} tl_layout[] = {
  { residentialState, "icres",   9,  48 },
  { commercialState,  "iccom",  47,  48 },
  { industrialState,  "icind",  85,  48 },
  { fireState,        "icfire",  9, 102 },
  { queryState,       "icqry",  47, 102 },
  { policeState,      "icpol",  85, 102 },
  { wireState,        "icwire", 28, 140 },
  { dozeState,        "icdozr", 66, 140 },
  { rrState,          "icrail",  6, 178 },
  { roadState,        "icroad", 66, 178 },
  { chalkState,       "icchlk", 28, 206 },
  { eraserState,      "icersr", 66, 206 },
  { stadiumState,     "icstad",  1, 244 },
  { parkState,        "icpark", 47, 244 },
  { seaportState,     "icseap", 85, 244 },
  { powerState,       "iccoal",  1, 290 },
  { nuclearState,     "icnuc",  85, 290 },
  { airportState,     "icairp", 35, 336 },
  { -1, NULL, 0, 0 }
};

/* the DUX ToolInfo: name and cost, by tool state */
static char *tl_name[] = {
  "Residential Zone", "Commercial Zone", "Industrial Zone", "Fire Station",
  "Query", "Police Station", "Wire", "Bulldozer", "Rail", "Road", "Chalk",
  "Eraser", "Stadium", "Park", "Seaport", "Coal Power Plant",
  "Nuclear Power Plant", "Airport", "Network"
};
static char *tl_cost[] = {
  "$100", "$100", "$100", "$500", "free", "$500", "$5", "$1", "$20", "$10",
  "free", "free", "$5,000", "$20", "$3,000", "$3,000", "$5,000", "$10,000",
  "$1,000"
};

static SvImage *tl_img[32][2];
static Pixwin *tl_pw = NULL;
static Pixrect *tl_buf = NULL;
static int tl_w = SV_TOOLS_W, tl_h = SV_TOOLS_H;
static int tl_dirty = 1;
static int tl_state = -1;


/* ---- the 1 bit palette ---------------------------------------------------- */

#define MI_N		16		/* icon size */
#define MI_MARGIN	6		/* around the buttons */
#define MI_PITCH	24		/* button to button */
#define MI_NUM		18		/* 9 rows of 2 */
#define MI_W		(2 * MI_MARGIN + MI_PITCH + MI_N + 6)
#define MI_X(i)		(MI_MARGIN + ((i) & 1) * MI_PITCH + 2)	/* icon i */
#define MI_Y(i)		(MI_MARGIN + ((i) >> 1) * MI_PITCH + 2)

/* the tools, row by row, left then right */
static int mi_state[MI_NUM] = {
  dozeState, roadState, wireState, rrState, parkState, residentialState,
  commercialState, industrialState, policeState, fireState, stadiumState,
  powerState, seaportState, airportState, nuclearState, queryState,
  chalkState, eraserState
};

/* the icons, 16 rows each, # is black */
static char *mi_art[MI_NUM * MI_N] = {
  /* dozeState */
  "................",
  "................",
  "........####....",
  "........#..#.#..",
  "........#..#.#..",
  ".#......#######.",
  ".##....#########",
  ".#.#..##########",
  ".#..############",
  ".#.#..##########",
  ".##.............",
  "......#########.",
  ".....#.#.#.#.#.#",
  ".....#.#.#.#.#.#",
  "......#########.",
  "................",
  /* roadState */
  "................",
  "................",
  "................",
  "................",
  "################",
  ".#.#.#.#.#.#.#.#",
  "#.#.#.#.#.#.#.#.",
  ".#.#.#.#.#.#.#.#",
  "#.#.#.#.#.#.#.#.",
  ".#.#.#.#.#.#.#.#",
  "#.#.#.#.#.#.#.#.",
  "################",
  "................",
  "................",
  "................",
  "................",
  /* wireState */
  "...#............",
  "#######.........",
  "#..#..#.........",
  "...#............",
  ".#####..........",
  ".#.#.#...#......",
  "...#...#####....",
  "...#...#.#.#....",
  "...#.....#......",
  "...#.....#...#..",
  "...#.....#..###.",
  "...#.....#..#.#.",
  "...#.....#...#..",
  "...#.....#...#..",
  "...#.....#...#..",
  "..###...###.###.",
  /* rrState */
  "................",
  "................",
  "................",
  ".##..##..##..##.",
  ".##..##..##..##.",
  "################",
  "################",
  ".##..##..##..##.",
  ".##..##..##..##.",
  "################",
  "################",
  ".##..##..##..##.",
  ".##..##..##..##.",
  "................",
  "................",
  "................",
  /* parkState */
  "..####..........",
  ".#.#.##.........",
  "#.#.#.##........",
  ".#.#.#.#........",
  "#.#.#.##........",
  ".#.#.#.#........",
  "..##.##..#######",
  "...##....#.#.#.#",
  "...##....#.#.#.#",
  "...##....#.#.#.#",
  "...##....#.#.#.#",
  "...##....#.###.#",
  "...##....#.....#",
  "...##....#.....#",
  "...##....#.....#",
  "################",
  /* residentialState */
  "................",
  ".......#...##...",
  "......###..##...",
  ".....#####.##...",
  "....#########...",
  "...#########....",
  "..###########...",
  ".#############..",
  "###############.",
  ".#...........#..",
  ".#.##.###.##.#..",
  ".#.##.#.#.##.#..",
  ".#....#.#....#..",
  ".#....#.#....#..",
  ".#############..",
  "................",
  /* commercialState */
  ".....######.....",
  ".....#....#.....",
  ".....#.##.#.....",
  ".....#....#.....",
  ".....#.##.#.....",
  "######....#.....",
  "#....#.##.#####.",
  "#.##.#....#...#.",
  "#....#.##.#.#.#.",
  "#.##.#....#...#.",
  "#....#.##.#.#.#.",
  "#.##.#....#...#.",
  "#....#.##.#.#.#.",
  "#.##.#....#...#.",
  "#....#.##.#.#.#.",
  "################",
  /* industrialState */
  ".........##.##..",
  "........#..#..#.",
  ".........##.##..",
  "...........##...",
  "...........##...",
  "...........##...",
  "...........##...",
  "#...#...#..##...",
  "##..##..##.##...",
  "###.###.###.##..",
  "################",
  "#..............#",
  "#.##.##.##.##..#",
  "#..............#",
  "#..............#",
  "################",
  /* policeState */
  ".##############.",
  ".#............#.",
  ".#.###..###...#.",
  ".#.#.#..#..#..#.",
  ".#.###..#..#..#.",
  ".#.#....#..#..#.",
  ".#.#....###...#.",
  ".#............#.",
  ".#............#.",
  "..#..........#..",
  "..#..........#..",
  "...#........#...",
  "....#......#....",
  ".....##..##.....",
  ".......##.......",
  "................",
  /* fireState */
  "................",
  "...#####........",
  "......##........",
  "......####......",
  ".....####.#.....",
  "....######.#....",
  "....######..#...",
  "....######..#...",
  "....#....#..#...",
  "....#....#..#...",
  "....######.###..",
  "....######.###..",
  "....######......",
  "....######......",
  "....######......",
  ".....####.......",
  /* stadiumState */
  "................",
  ".....#######....",
  "...##.......##..",
  "..#############.",
  ".##############.",
  ".#######..#####.",
  ".#######..#####.",
  ".##############.",
  ".##############.",
  "######.########.",
  "#.....#########.",
  "######.#######..",
  "#.....#######...",
  "######.###......",
  "................",
  "................",
  /* powerState */
  "..##.........##.",
  ".#..#.......##..",
  "..##.......##...",
  ".##.......#####.",
  ".##.........##..",
  ".##........##...",
  ".##.......##....",
  ".##......##.....",
  ".##......#......",
  "#####...........",
  "#...#...........",
  "#...############",
  "#.#.#..........#",
  "#...#.##.##.##.#",
  "#.#.#..........#",
  "################",
  /* seaportState */
  "......####......",
  ".....#....#.....",
  "......####......",
  ".......##.......",
  "...##########...",
  ".......##.......",
  ".......##.......",
  ".......##.......",
  ".#.....##.....#.",
  ".##....##....##.",
  ".##....##....##.",
  "..##...##...##..",
  "...###.##.###...",
  ".....######.....",
  ".......##.......",
  "................",
  /* airportState */
  ".......##.......",
  "......####......",
  "......####......",
  ".......##.......",
  "......####......",
  "....########....",
  "..############..",
  "################",
  "###...####...###",
  ".......##.......",
  ".......##.......",
  ".......##.......",
  ".....######.....",
  "....########....",
  "....##....##....",
  "................",
  /* nuclearState */
  "................",
  "...#........#...",
  "..###......###..",
  ".####......####.",
  ".#####....#####.",
  "#######..#######",
  "######.##.######",
  "#####.####.#####",
  "......####......",
  ".......##.......",
  "................",
  "......####......",
  ".....######.....",
  ".....######.....",
  "....########....",
  ".....######.....",
  /* queryState */
  "................",
  ".....######.....",
  "....##....##....",
  "....##....##....",
  "..........##....",
  ".........##.....",
  "........##......",
  ".......##.......",
  ".......##.......",
  ".......##.......",
  "................",
  ".......##.......",
  ".......##.......",
  "................",
  "................",
  "................",
  /* chalkState */
  "............###.",
  "...........#..##",
  "..........#..#.#",
  ".........#..#..#",
  "........#..#..#.",
  ".......#..#..#..",
  "......#..#..#...",
  ".....#..#..#....",
  "....#..#..#.....",
  "....#.#..#......",
  "....##..#.......",
  "....#..#........",
  "....####........",
  "................",
  ".##....##....##.",
  "#..#..#..#..#..#",
  /* eraserState */
  "................",
  "................",
  ".........######.",
  "........#.....##",
  ".......#.....#.#",
  "......#.....#..#",
  ".....#.....#..#.",
  "....#.....#..#..",
  "...#.....#..#...",
  "..#######..#....",
  "..#######.#.....",
  "..#######.......",
  "................",
  "................",
  "................",
  "................"
};

static Pixrect *mi_icon[MI_NUM];


static void
mi_load()
{
  int i, x, y;
  char *row;

  for (i = 0; i < MI_NUM; i++) {
    if ((mi_icon[i] = mem_create(MI_N, MI_N, 1)) == NULL) {
      fprintf(stderr, "micropolis: out of memory for the palette\n");
      exit(1);
    }
    pr_rop(mi_icon[i], 0, 0, MI_N, MI_N, PIX_CLR, (Pixrect *)NULL, 0, 0);
    for (y = 0; y < MI_N; y++) {
      row = mi_art[i * MI_N + y];
      for (x = 0; x < MI_N && row[x]; x++)
	if (row[x] == '#')
	  pr_put(mi_icon[i], x, y, 1);
    }
  }
}


/* 50% grey, as the desktop of the DMD and of the Mac */
static void
mi_grey(pr)
  Pixrect *pr;
{
  struct mpr_data *md = mpr_d(pr);
  unsigned char *p = (unsigned char *)md->md_image;
  int x, y;

  for (y = 0; y < pr->pr_height; y++, p += md->md_linebytes)
    for (x = 0; x < md->md_linebytes; x++)
      p[x] = (y & 1) ? 0x55 : 0xaa;
}


/* Each icon in its button: a black frame, a shadow right and below.  The
 * selected one is inverted, as on the Mac. */
static void
mi_draw()
{
  int i, x, y, sel;

  mi_grey(tl_buf);
  sv_frame_rect(tl_buf, 0, 0, tl_w, tl_h, SIMCOLOR_BLACK, 1);
  tl_state = SvEditor ? SvEditor->tool_state : -1;
  for (i = 0; i < MI_NUM; i++) {
    x = MI_X(i);
    y = MI_Y(i);
    sel = (mi_state[i] == tl_state);
    sv_fill(tl_buf, x - 2, y - 2, MI_N + 4, MI_N + 4, SIMCOLOR_BLACK);
    sv_fill(tl_buf, x - 1, y - 1, MI_N + 2, MI_N + 2,
	    sel ? SIMCOLOR_BLACK : SIMCOLOR_WHITE);
    sv_fill(tl_buf, x + MI_N + 2, y - 1, 1, MI_N + 4, SIMCOLOR_BLACK);
    sv_fill(tl_buf, x - 1, y + MI_N + 2, MI_N + 4, 1, SIMCOLOR_BLACK);
    if (mi_icon[i] != NULL)
      pr_rop(tl_buf, x, y, MI_N, MI_N,
	     sel ? (PIX_NOT(PIX_SRC) & PIX_DST) : (PIX_SRC | PIX_DST),
	     mi_icon[i], 0, 0);
  }
}


/* The tool's name and cost, as the DUX palette shows them on top. */
char *
sv_tool_label(state)
  int state;
{
  static char s[80];

  if (state < firstState || state > lastState)
    return "";
  sprintf(s, "%s  %s", tl_name[state], tl_cost[state]);
  return s;
}


/* ---- the palette ----------------------------------------------------------- */

void
sv_tools_load()
{
  int i;
  char name[64];

  if (!SvColor) {
    mi_load();
    return;
  }
  for (i = 0; tl_layout[i].icon != NULL; i++) {
    sprintf(name, "%s.xpm", tl_layout[i].icon);
    tl_img[i][0] = sv_load_image(name);
    sprintf(name, "%shi.xpm", tl_layout[i].icon);
    tl_img[i][1] = sv_load_image(name);
  }
}


static void
tl_center(y, font, s)
  int y;
  Pixfont *font;
  char *s;
{
  sv_text(tl_buf, (tl_w - sv_text_width(font, s)) / 2, y, SvUiText, font, s);
}


static void
tl_draw()
{
  int i, sel, x, y;
  SvImage *img;

  if (tl_w <= 0 || tl_h <= 0)
    return;
  if (tl_buf == NULL || tl_buf->pr_width != tl_w ||
      tl_buf->pr_height != tl_h) {
    if (tl_buf != NULL)
      pr_destroy(tl_buf);
    tl_buf = mem_create(tl_w, tl_h, SvColor ? 8 : 1);
  }
  if (!SvColor) {
    mi_draw();
    return;
  }
  sv_fill(tl_buf, 0, 0, tl_w, tl_h, SvUiBack);
  sv_bevel2(tl_buf, 0, 0, tl_w, tl_h, 1);

  tl_state = SvEditor ? SvEditor->tool_state : -1;
  if (tl_state >= firstState && tl_state <= lastState) {
    tl_center(6, SvBoldFont, tl_name[tl_state]);
    tl_center(8 + sv_font_height(SvBoldFont) + 4, SvBoldFont,
	      tl_cost[tl_state]);
  }

  for (i = 0; tl_layout[i].icon != NULL; i++) {
    sel = (tl_layout[i].state == tl_state);
    /* DUX shows the plain icons, and the hi one for the selected tool */
    img = tl_img[i][sel ? 1 : 0];
    if (img == NULL)
      img = tl_img[i][0];
    if (img == NULL)
      continue;
    x = tl_layout[i].x;
    y = tl_layout[i].y;
    /* Tk buttons, border 1: raised, the selected one sunken */
    sv_draw_image(tl_buf, x + 1, y + 1, img);
    sv_bevel(tl_buf, x, y, img->w + 2, img->h + 2, !sel);
  }
}


static void
tl_blit()
{
  if (tl_buf != NULL)
    pw_rop(tl_pw, 0, 0, tl_w, tl_h, PIX_SRC, tl_buf, 0, 0);
}


static void
tl_resize(canvas, w, h)
  Canvas canvas;
  int w, h;
{
  tl_w = w;
  tl_h = h;
  tl_dirty = 1;
}


void
sv_tools_redraw()
{
  tl_dirty = 1;
}


void
sv_tools_update()
{
  static int label = -1;		/* the tool in the editor bar's label */

  if (!SvReady || tl_pw == NULL)
    return;
  if (SvEditor != NULL && SvEditor->tool_state != tl_state)
    tl_dirty = 1;
  if (!SvColor && SvEditor != NULL && SvEditor->tool_state != label) {
    label = SvEditor->tool_state;
    sv_editor_bar_redraw();		/* the tool's name and cost */
  }
  if (tl_dirty) {
    tl_draw();
    tl_blit();
    tl_dirty = 0;
  }
}


void
sv_select_tool(state)
  int state;
{
  if (SvEditor == NULL || state < firstState || state > lastState)
    return;
  SvEditor->tool_state_save = -1;
  setWandState(SvEditor, state);
}


static void
tl_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (!SvReady) {
    pw_writebackground(pw, 0, 0, tl_w, tl_h, PIX_SRC);
    return;
  }
  tl_draw();
  tl_blit();
}


static void
tl_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int i, x = event_x(event), y = event_y(event), id = event_id(event);
  SvImage *img;

  if (!SvReady)
    return;
  if (id == MS_LEFT && event_is_down(event) && !SvColor) {
    for (i = 0; i < MI_NUM; i++)
      if (x >= MI_X(i) - 3 && y >= MI_Y(i) - 3 &&
	  x < MI_X(i) + MI_N + 3 && y < MI_Y(i) + MI_N + 3) {
	sv_select_tool(mi_state[i]);
	break;
      }
  } else if (id == MS_LEFT && event_is_down(event)) {
    for (i = 0; tl_layout[i].icon != NULL; i++) {
      img = tl_img[i][0];
      if (img != NULL && x >= tl_layout[i].x && y >= tl_layout[i].y &&
	  x < tl_layout[i].x + img->w + 2 && y < tl_layout[i].y + img->h + 2) {
	sv_select_tool(tl_layout[i].state);
	break;
      }
    }
  } else if (id == MS_RIGHT && event_is_down(event)) {
    sv_menu_event(sv_tool_menu(), canvas, canvas_window_event(canvas, event));
  } else if (event_is_ascii(event) ||
	     (id >= KEY_RIGHT(1) && id <= KEY_RIGHT(15))) {
    sv_editor_key(event);
  }
}


/* The palette down the left side of frame, below the window "below". */
void
sv_tools_create(frame, below)
  Frame frame;
  Window below;
{
  SvToolCanvas = window_create(frame, CANVAS,
			       WIN_X, 0,
			       WIN_BELOW, below,
			       WIN_WIDTH, SvColor ? SV_TOOLS_W : MI_W,
			       WIN_HEIGHT, WIN_EXTEND_TO_EDGE,
			       CANVAS_RETAINED, FALSE,
			       CANVAS_REPAINT_PROC, tl_repaint,
			       CANVAS_RESIZE_PROC, tl_resize,
			       WIN_EVENT_PROC, tl_event,
			       0);
  window_set(SvToolCanvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  tl_pw = canvas_pixwin(SvToolCanvas);
  tl_w = (int)window_get(SvToolCanvas, CANVAS_WIDTH);
  tl_h = (int)window_get(SvToolCanvas, CANVAS_HEIGHT);
}


/* ==== engine hooks (w_tool.c) ============================================= */

void
sv_set_tool_state(view, state)
  SimView *view;
  int state;
{
  tl_dirty = 1;
}


/* Tool feedback sounds, from the original UIDidTool procs. */
static struct {
  char *tool, *sound;
  int speed;
} did_tool[] = {
  { "Res", "O", 140 }, { "Com", "A", 140 }, { "Ind", "E", 140 },
  { "Fire", "O", 130 }, { "Qry", "E", 200 }, { "Pol", "E", 130 },
  { "Wire", "O", 120 }, { "Dozr", "Rumble", 100 }, { "Rail", "O", 100 },
  { "Road", "E", 100 }, { "Stad", "O", 90 }, { "Park", "A", 130 },
  { "Seap", "E", 90 }, { "Coal", "O", 75 }, { "Nuc", "E", 75 },
  { "Airp", "A", 50 }, { NULL, NULL, 0 }
};


void
sv_did_tool(name, x, y)
  char *name;
  int x, y;
{
  int i;

  for (i = 0; did_tool[i].tool != NULL; i++)
    if (strcmp(did_tool[i].tool, name) == 0) {
      sv_play_sound_speed(did_tool[i].sound, did_tool[i].speed);
      break;
    }
  sv_map_tool_used();
}
