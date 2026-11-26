/* sv_map.c -- the "Micropolis Overall Map" window and its overlays.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * Laid out like the DUX map window (res/wmap.tcl): Zones and Overlays
 * menus with the overlay legend, and the map, 3 pixels per tile.  The map
 * is composed as colormap indices (the drawing rules come from the
 * original g_map.c and g_smmaps.c), then copied to an 8 bit pixrect.
 *
 * In black and white the map is smaller and simpler, as in Micropolis for
 * the DMD 5620: a street map, 2 pixels per tile, drawn as 1 bit patterns
 * (below), with its own legend for the overlays.
 *
 * The editor's visible area is drawn on top as a rectangle.  Click or drag
 * to move the editor; the right button has every map in one menu.
 */

#include "sv.h"

Canvas SvMapCanvas = NULL;
Frame SvMapFrame = NULL;
int SvMapScale = 3;			/* pixels per tile; 2 in black and white */

static SvBar *mp_bar = NULL;
static SvImage *mp_legend[3];		/* none, low-high, minus-plus */

static SvBarItem mp_items[] = {
  { "Zones", sv_zones_menu },
  { "Overlays", sv_overlays_menu },
  { NULL, NULL }
};

static Pixwin *mp_pw = NULL;
static int mp_w = MAP_W, mp_h = MAP_H;	/* the map's size on the screen */
static Pixrect *mp_out = NULL;		/* map + view rectangle, screen depth */
static unsigned char *mp_img = NULL;	/* composed map: colormap indices */
static Pixrect *mp_mono = NULL;		/* composed map in black and white */
static int mp_dirty = 1;		/* recompose */
static int mp_view_dirty = 1;		/* copy to the screen again */
static int mp_tool_dirty = 0;		/* the player edited the city */
static int mp_last_compose = 0;
static int mp_state = ALMAP;
static int mp_button = 0;

#define VAL_NONE	0
#define VAL_LOW		1
#define VAL_MEDIUM	2
#define VAL_HIGH	3
#define VAL_VERYHIGH	4
#define VAL_PLUS	5
#define VAL_VERYPLUS	6
#define VAL_MINUS	7
#define VAL_VERYMINUS	8

static short val_color[9] = {
  -1, SIMCOLOR_LIGHTGRAY, SIMCOLOR_YELLOW, SIMCOLOR_ORANGE, SIMCOLOR_RED,
  SIMCOLOR_DARKGREEN, SIMCOLOR_LIGHTGREEN, SIMCOLOR_ORANGE, SIMCOLOR_YELLOW
};

static char *mp_names[NMAPS] = {
  "All", "Residential", "Commercial", "Industrial", "Power Grid",
  "Transportation", "Population Density", "Rate of Growth",
  "Traffic Density", "Pollution Density", "Crime Rate", "Land Value",
  "Fire Coverage", "Police Coverage", "Dynamic"
};

/* the DUX MapTitles, the window's label */
static char *mp_titles[NMAPS] = {
  "Micropolis Overall Map", "Residential Zone Map", "Commercial Zone Map",
  "Industrial Zone Map", "Power Grid Map", "Transportation Map",
  "Population Density Map", "Rate of Growth Map", "Traffic Density Map",
  "Pollution Density Map", "Crime Rate Map", "Land Value Map",
  "Fire Coverage Map", "Police Coverage Map", "Dynamic Filter Map"
};


char *
sv_map_state_name(state)
  int state;
{
  if (state < 0 || state >= NMAPS)
    return "?";
  return mp_names[state];
}


int
sv_map_state()
{
  return mp_state;
}


/* ---- composing in color --------------------------------------------------- */

static int
get_ci(x)
  int x;
{
  if (x < 50) return VAL_NONE;
  if (x < 100) return VAL_LOW;
  if (x < 150) return VAL_MEDIUM;
  if (x < 200) return VAL_HIGH;
  return VAL_VERYHIGH;
}


static void
put3x3(x, y, src)
  int x, y;
  unsigned char *src;
{
  register unsigned char *d = mp_img + (y * 3) * MAP_W + x * 3;

  d[0] = src[0]; d[1] = src[1]; d[2] = src[2];
  d += MAP_W;
  d[0] = src[3]; d[1] = src[4]; d[2] = src[5];
  d += MAP_W;
  d[0] = src[6]; d[1] = src[7]; d[2] = src[8];
}


static void
fill3x3(x, y, c)
  int x, y, c;
{
  register unsigned char *d = mp_img + (y * 3) * MAP_W + x * 3;

  d[0] = d[1] = d[2] = c;
  d += MAP_W;
  d[0] = d[1] = d[2] = c;
  d += MAP_W;
  d[0] = d[1] = d[2] = c;
}


/* The tiles, filtered for the zone and transport maps. */
static void
draw_tiles(state)
  int state;
{
  register int tile;
  int x, y;
  unsigned short *mp;

  for (x = 0; x < WORLD_X; x++) {
    mp = (unsigned short *)Map[x];
    for (y = 0; y < WORLD_Y; y++) {
      tile = mp[y] & LOMASK;
      if (tile >= TILE_COUNT)
	tile -= TILE_COUNT;
      switch (state) {
      case REMAP:
	if (tile > 422)
	  tile = 0;
	break;
      case COMAP:
	if (tile > 609 || (tile >= 232 && tile < 423))
	  tile = 0;
	break;
      case INMAP:
	if ((tile >= 240 && tile <= 611) || (tile >= 693 && tile <= 851) ||
	    (tile >= 860 && tile <= 883) || tile >= 932)
	  tile = 0;
	break;
      case RDMAP:
      case TDMAP:
	if (tile >= 240 || (tile >= 207 && tile <= 220) || tile == 223)
	  tile = 0;
	break;
      }
      put3x3(x, y, &SvSmallTiles[tile * 9]);
    }
  }
}


static void
draw_power()
{
  register unsigned short tile;
  int x, y, t;
  unsigned short *mp;

  for (x = 0; x < WORLD_X; x++) {
    mp = (unsigned short *)Map[x];
    for (y = 0; y < WORLD_Y; y++) {
      tile = mp[y];
      t = tile & LOMASK;
      if (t >= TILE_COUNT)
	t -= TILE_COUNT;
      if (t <= 63) {
	put3x3(x, y, &SvSmallTiles[t * 9]);
      } else if (tile & ZONEBIT) {
	fill3x3(x, y, (tile & PWRBIT) ? SIMCOLOR_RED : SIMCOLOR_LIGHTBLUE);
      } else if (tile & CONDBIT) {
	fill3x3(x, y, SIMCOLOR_LIGHTGRAY);
      } else {
	put3x3(x, y, &SvSmallTiles[0]);
      }
    }
  }
}


/* An overlay square: stippled color. */
static void
maybe_rect(val, x, y, w, h)
  int val, x, y, w, h;
{
  register unsigned char *d;
  register int i, j, c;

  if (val == VAL_NONE)
    return;
  if (x + w > MAP_W)
    w = MAP_W - x;
  if (y + h > MAP_H)
    h = MAP_H - y;
  if (w <= 0 || h <= 0)
    return;
  c = val_color[val];
  for (j = 0; j < h; j++) {
    d = mp_img + (y + j) * MAP_W + x;
    for (i = (x + y + j) & 1; i < w; i += 2)
      d[i] = c;
  }
}


static void
draw_half_overlay(mem)
  Byte **mem;
{
  int x, y;

  for (x = 0; x < HWLDX; x++)
    for (y = 0; y < HWLDY; y++)
      maybe_rect(get_ci(mem[x][y]), x * 6, y * 6, 6, 6);
}


static void
draw_compose()
{
  int x, y, z, val;

  switch (mp_state) {
  case PRMAP:
    draw_power();
    break;
  case REMAP:
  case COMAP:
  case INMAP:
  case RDMAP:
  case TDMAP:
    draw_tiles(mp_state);
    break;
  default:
    draw_tiles(ALMAP);
    break;
  }

  switch (mp_state) {
  case PDMAP:
    draw_half_overlay(PopDensity);
    break;
  case TDMAP:
    draw_half_overlay(TrfDensity);
    break;
  case CRMAP:
    draw_half_overlay(CrimeMem);
    break;
  case LVMAP:
    draw_half_overlay(LandValueMem);
    break;
  case PLMAP:
    for (x = 0; x < HWLDX; x++)
      for (y = 0; y < HWLDY; y++)
	maybe_rect(get_ci(10 + PollutionMem[x][y]), x * 6, y * 6, 6, 6);
    break;
  case RGMAP:
    for (x = 0; x < SmX; x++)
      for (y = 0; y < SmY; y++) {
	z = RateOGMem[x][y];
	if (z > 100) val = VAL_VERYPLUS;
	else if (z > 20) val = VAL_PLUS;
	else if (z < -100) val = VAL_VERYMINUS;
	else if (z < -20) val = VAL_MINUS;
	else val = VAL_NONE;
	maybe_rect(val, x * 24, y * 24, 24, 24);
      }
    break;
  case FIMAP:
    for (x = 0; x < SmX; x++)
      for (y = 0; y < SmY; y++)
	maybe_rect(get_ci(FireRate[x][y]), x * 24, y * 24, 24, 24);
    break;
  case POMAP:
    for (x = 0; x < SmX; x++)
      for (y = 0; y < SmY; y++)
	maybe_rect(get_ci(PoliceMapEffect[x][y]), x * 24, y * 24, 24, 24);
    break;
  }
}


/* ---- composing in black and white -----------------------------------------
 *
 * The street map of Micropolis for the DMD 5620, 2 pixels per tile: roads,
 * rails and water black, trees light dots, everything else white.  The
 * zone maps show their zones black, roads grey and water dots.  The power
 * map shows what conducts: black with power, grey without.  An overlay
 * shows 3 levels of the highest value on the map now: light dots, grey and
 * black.  A tile is a 2x2 pattern: bit 3 is its top left pixel, 2 top
 * right, 1 bottom left, 0 bottom right; 1 is black.  Lines are never dots:
 * a road 1 tile wide would get lost.
 */

#define P_NONE	0
#define P_DOTS	8			/* 10/00 */
#define P_GREY	9			/* 10/01 */
#define P_FULL	15

#define C_LAND	0
#define C_WATER	1
#define C_TREES	2
#define C_ROAD	3
#define C_WIRE	4
#define C_RES	5
#define C_COM	6
#define C_IND	7
#define C_OTHER	8

static unsigned char mo_class[LOMASK + 1];	/* tile -> C_* */
static int mo_classes_made = 0;
static int mo_max;			/* the overlay's highest value */

/* the "All" map, by class */
static unsigned char mo_all[] = {
  P_NONE, P_FULL, P_DOTS, P_FULL, P_NONE, P_NONE, P_NONE, P_NONE, P_NONE
};


static void
mo_classes()
{
  int t, k;

  for (t = 0; t <= LOMASK; t++) {
    if (t >= 2 && t <= 20)
      k = C_WATER;
    else if (t >= 21 && t <= 43)
      k = C_TREES;
    else if (t < 64)
      k = C_LAND;		/* dirt, rubble, flood, radiation, fire */
    else if (t <= 206 || (t >= 224 && t <= 238) || (t >= 828 && t <= 831) ||
	     (t >= 948 && t <= 951))
      k = C_ROAD;		/* roads, rails, bridges */
    else if (t >= 208 && t <= 222)
      k = C_WIRE;
    else if (t >= 240 && t <= 422)
      k = C_RES;
    else if (t >= 423 && t <= 611)
      k = C_COM;
    else if ((t >= 612 && t <= 692) || (t >= 844 && t <= 851))
      k = C_IND;
    else
      k = C_OTHER;		/* port, airport, power, police, fire, stadium */
    mo_class[t] = k;
  }
  mo_classes_made = 1;
}


/* The overlay's value at tile x, y.  The big maps hold one value for 2x2
 * tiles, the small ones for 8x8. */
static int
mo_val(k, x, y)
  int k, x, y;
{
  switch (k) {
  case PDMAP: return PopDensity[x >> 1][y >> 1];
  case RGMAP: return RateOGMem[x >> 3][y >> 3];
  case TDMAP: return TrfDensity[x >> 1][y >> 1];
  case PLMAP: return PollutionMem[x >> 1][y >> 1];
  case CRMAP: return CrimeMem[x >> 1][y >> 1];
  case LVMAP: return LandValueMem[x >> 1][y >> 1];
  case FIMAP: return FireRate[x >> 3][y >> 3];
  case POMAP: return PoliceMapEffect[x >> 3][y >> 3];
  }
  return 0;
}


static int
mo_getmax(k)
  int k;
{
  int x, y, v, m = 0;
  int step = (k == RGMAP || k == FIMAP || k == POMAP) ? 8 : 2;

  for (x = 0; x < WORLD_X; x += step)
    for (y = 0; y < WORLD_Y; y += step)
      if ((v = mo_val(k, x, y)) > m)
	m = v;
  return m;
}


/* 3 levels of the highest value: 1 below half of it, 2 below 3/4, 3 above. */
static int
mo_level(v)
  int v;
{
  if (v <= 0 || mo_max <= 0)
    return 0;
  v = (v * 4) / (mo_max + 1);
  return v < 1 ? 1 : v > 3 ? 3 : v;
}


/* The pattern of tile x, y on map k. */
static int
mo_pat(k, x, y)
  int k, x, y;
{
  int v = ((unsigned short *)Map[x])[y], c = mo_class[v & LOMASK];

  switch (k) {
  case ALMAP:
    return mo_all[c];
  case REMAP:
  case COMAP:
  case INMAP:
    return c == C_RES + k - REMAP ? P_FULL : c == C_ROAD ? P_GREY :
      c == C_WATER ? P_DOTS : P_NONE;
  case PRMAP:
    if (v & CONDBIT)
      return (PowerMap[POWERWORD(x, y)] & (1 << (x & 15))) ? P_FULL : P_GREY;
    return c == C_WATER ? P_DOTS : P_NONE;
  case RDMAP:
    return c == C_ROAD ? P_FULL : c == C_WATER ? P_DOTS : P_NONE;
  }
  switch (mo_level(mo_val(k, x, y))) {
  case 1: return P_DOTS;
  case 2: return P_GREY;
  case 3: return P_FULL;
  }
  return P_NONE;
}


/* The map into mp_mono: 4 tiles a byte, 2 rows of pixels a tile. */
static void
mo_compose()
{
  struct mpr_data *md = mpr_d(mp_mono);
  unsigned char *d = (unsigned char *)md->md_image;
  int lb = md->md_linebytes, k = mp_state, x, y, i, p, top, bot;

  if (!mo_classes_made)
    mo_classes();
  if (k < 0 || k >= DYMAP)
    k = ALMAP;
  if (k >= PDMAP)
    mo_max = mo_getmax(k);
  for (y = 0; y < WORLD_Y; y++, d += 2 * lb)
    for (x = 0; x < WORLD_X; x += 4) {
      top = bot = 0;
      for (i = 0; i < 4 && x + i < WORLD_X; i++) {
	p = mo_pat(k, x + i, y);
	top |= ((p >> 2) & 3) << (6 - 2 * i);
	bot |= (p & 3) << (6 - 2 * i);
      }
      d[x >> 2] = top;
      d[lb + (x >> 2)] = bot;
    }
}


/* The legend of the overlays: MIN, the 3 levels, MAX, in a 5 pixel font. */
static char *mo_min[5] = {
  "#...#.###.#..#",
  "##.##..#..##.#",
  "#.#.#..#..#.##",
  "#...#..#..#..#",
  "#...#.###.#..#"
};
static char *mo_maxs[5] = {
  "#...#..##..#...#",
  "##.##.#..#..#.#.",
  "#.#.#.####...#..",
  "#...#.#..#..#.#.",
  "#...#.#..#.#...#"
};

#define LG_BOX	40			/* 3 swatches of 12, 4 lines */
#define LG_H	12
#define LG_W	(14 + 4 + LG_BOX + 4 + 16)


static void
mo_text(pr, x, y, rows)
  Pixrect *pr;
  int x, y;
  char **rows;
{
  int i, j;

  for (j = 0; j < 5; j++)
    for (i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#')
	pr_put(pr, x + i, y + j, 1);
}


static void
mo_legend(b)
  SvBar *b;
{
  static int pat[3] = { P_DOTS, P_GREY, P_FULL };
  int x = b->w - LG_W - 6, y = (b->h - LG_H) / 2, sx, i, j, k;

  if (x < b->end)
    return;
  mo_text(b->buf, x, y + (LG_H - 5) / 2, mo_min);
  x += 14 + 4;
  sv_fill(b->buf, x, y, LG_BOX, LG_H, SIMCOLOR_BLACK);
  for (k = 0; k < 3; k++) {
    sx = x + 1 + k * 13;
    sv_fill(b->buf, sx, y + 1, 12, LG_H - 2, SIMCOLOR_WHITE);
    for (j = 0; j < LG_H - 2; j++)
      for (i = 0; i < 12; i++)
	if (pat[k] & (8 >> (((j & 1) << 1) | (i & 1))))
	  pr_put(b->buf, sx + i, y + 1 + j, 1);
  }
  mo_text(b->buf, x + LG_BOX + 4, y + (LG_H - 5) / 2, mo_maxs);
}


/* ---- to the screen ---------------------------------------------------------- */

/* Composed map -> the output pixrect, plus the editor rectangle: black
 * and white (DUX: white, yellow, black) so it shows on any map. */
static void
build_out()
{
  struct mpr_data *md = mpr_d(mp_out);
  unsigned char *d, *s;
  int y, lb = md->md_linebytes;
  SimView *v = SvEditor;
  int rx, ry, rw, rh;

  if (SvColor) {
    d = (unsigned char *)md->md_image;
    s = mp_img;
    for (y = 0; y < MAP_H; y++) {
      bcopy((char *)s, (char *)d, MAP_W);
      s += MAP_W;
      d += lb;
    }
  } else {
    pr_rop(mp_out, 0, 0, mp_w, mp_h, PIX_SRC, mp_mono, 0, 0);
  }

  if (v != NULL && v->w_width > 0) {
    rx = ((v->pan_x - (v->w_width >> 1)) * SvMapScale) >> 4;
    ry = ((v->pan_y - (v->w_height >> 1)) * SvMapScale) >> 4;
    rw = (v->w_width * SvMapScale) >> 4;
    rh = (v->w_height * SvMapScale) >> 4;
    if (!SvColor)
      sv_frame_rect(mp_out, rx - 2, ry - 2, rw + 4, rh + 4, SIMCOLOR_WHITE, 1);
    sv_frame_rect(mp_out, rx - 1, ry - 1, rw + 2, rh + 2, SIMCOLOR_BLACK, 1);
    sv_frame_rect(mp_out, rx, ry, rw, rh, SIMCOLOR_WHITE, 1);
  }
}


static void
mp_blit()
{
  pw_rop(mp_pw, 0, 0, mp_w, mp_h, PIX_SRC, mp_out, 0, 0);
}


static void
mp_alloc()
{
  if (mp_out != NULL)
    return;
  mp_out = mem_create(mp_w, mp_h, SvColor ? 8 : 1);
  if (SvColor) {
    if ((mp_img = (unsigned char *)malloc(MAP_W * MAP_H)) != NULL)
      bzero((char *)mp_img, MAP_W * MAP_H);
  } else {
    mp_mono = mem_create(mp_w, mp_h, 1);
  }
  if (mp_out == NULL || (SvColor ? mp_img == NULL : mp_mono == NULL)) {
    fprintf(stderr, "micropolis: out of memory for the map\n");
    exit(1);
  }
}


/* Per tick: recompose when the engine says the map changed. */
void
sv_map_update()
{
  int i, must;

  if (!SvReady || mp_pw == NULL)
    return;
  mp_alloc();
  must = mp_dirty || NewMap || NewMapFlags[mp_state];
  if (mp_tool_dirty && SvTicks - mp_last_compose >= 10) {
    must = 1;
    mp_tool_dirty = 0;
  }
  NewMap = 0;
  for (i = 0; i < NMAPS; i++)
    NewMapFlags[i] = 0;
  if (must) {
    if (SvColor)
      draw_compose();
    else
      mo_compose();
    mp_dirty = 0;
    mp_view_dirty = 1;
    mp_last_compose = SvTicks;
  }
  if (mp_view_dirty) {
    build_out();
    mp_blit();
    mp_view_dirty = 0;
  }
}


void
sv_map_invalidate()
{
  mp_dirty = 1;
}


void
sv_map_view_moved()
{
  mp_view_dirty = 1;
}


void
sv_map_tool_used()
{
  mp_tool_dirty = 1;
}


/* Menu or keyboard: switch the overlay (the engine calls back below). */
void
sv_map_set_state(state)
  int state;
{
  if (state < 0 || state >= NMAPS)
    return;
  DoSetMapState(SvMap, state);
}


/* Engine hook from DoSetMapState(): the DUX UISetMapState. */
void
sv_set_map_state(view, state)
  SimView *view;
  int state;
{
  if (view != SvMap || state < 0 || state >= NMAPS)
    return;
  mp_state = state;
  mp_dirty = 1;
  if (SvMapFrame != NULL)
    window_set(SvMapFrame, FRAME_LABEL, mp_titles[state], 0);
  sv_bar_redraw(mp_bar);
}


/* ---- canvas callbacks ------------------------------------------------------ */

static void
mp_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (!SvReady || mp_out == NULL) {
    pw_writebackground(pw, 0, 0, mp_w, mp_h, PIX_SRC);
    return;
  }
  mp_blit();
}


/* A menu of map states, the current one marked with a star. */
typedef struct MapMenu {
  Menu menu;
  int n;
  int state[NMAPS];
  Menu_item item[NMAPS];
  char label[NMAPS][40];
} MapMenu;


static Menu
mm_menu(mm, title, states)
  MapMenu *mm;
  char *title;
  int *states;
{
  int i;

  if (mm->menu == NULL) {
    mm->menu = menu_create(0);
    menu_set(mm->menu, MENU_APPEND_ITEM,
	     menu_create_item(MENU_STRING, title, MENU_VALUE, 0, 0), 0);
    for (i = 0; states[i] >= 0; i++) {
      mm->state[i] = states[i];
      strcpy(mm->label[i], mp_names[states[i]]);
      mm->item[i] = menu_create_item(MENU_STRING, mm->label[i],
				     MENU_VALUE, SV_CMD_MAP + states[i], 0);
      menu_set(mm->menu, MENU_APPEND_ITEM, mm->item[i], 0);
    }
    mm->n = i;
  }
  for (i = 0; i < mm->n; i++) {
    sprintf(mm->label[i], "%s %s", (mm->state[i] == mp_state) ? "*" : " ",
	    mp_names[mm->state[i]]);
    menu_set(mm->item[i], MENU_STRING, mm->label[i], 0);
  }
  return mm->menu;
}


/* The DUX Zones menu. */
Menu
sv_zones_menu()
{
  static MapMenu mm;
  static int states[] = { ALMAP, REMAP, COMAP, INMAP, RDMAP, -1 };

  return mm_menu(&mm, "Zones:", states);
}


/* The DUX Overlays menu. */
Menu
sv_overlays_menu()
{
  static MapMenu mm;
  static int states[] = { PDMAP, RGMAP, LVMAP, CRMAP, PLMAP, TDMAP, PRMAP,
			  FIMAP, POMAP, -1 };

  return mm_menu(&mm, "Overlays:", states);
}


/* Every map, for the right button. */
Menu
sv_map_menu()
{
  static MapMenu mm;
  static int states[] = { ALMAP, REMAP, COMAP, INMAP, RDMAP, PDMAP, RGMAP,
			  LVMAP, CRMAP, PLMAP, TDMAP, PRMAP, FIMAP, POMAP, -1 };

  return mm_menu(&mm, "Map:", states);
}


static void
mp_goto(event)
  Event *event;
{
  int x = event_x(event) / SvMapScale, y = event_y(event) / SvMapScale;

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= WORLD_X) x = WORLD_X - 1;
  if (y >= WORLD_Y) y = WORLD_Y - 1;
  sv_editor_pan_to(x, y);
}


static void
mp_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  if (!SvReady)
    return;
  switch (event_id(event)) {
  case MS_LEFT:
  case MS_MIDDLE:
    if (event_is_down(event)) {
      mp_button = 1;
      mp_goto(event);
    } else {
      mp_button = 0;
    }
    break;
  case LOC_DRAG:
    if (mp_button)
      mp_goto(event);
    break;
  case LOC_WINEXIT:
    mp_button = 0;
    break;
  case MS_RIGHT:
    if (event_is_down(event))
      sv_menu_event(sv_map_menu(), canvas,
		    canvas_window_event(canvas, event));
    break;
  default:
    if (event_is_ascii(event) ||
	(event_id(event) >= KEY_RIGHT(1) && event_id(event) <= KEY_RIGHT(15)))
      sv_editor_key(event);
    break;
  }
}


/* The legend beside the menus (the DUX legend label). */
static void
mp_bar_extra(b)
  SvBar *b;
{
  SvImage *img;

  if (!SvColor) {
    if (mp_state >= PDMAP && mp_state <= POMAP && mp_state != PRMAP)
      mo_legend(b);
    return;
  }
  switch (mp_state) {
  case PDMAP: case TDMAP: case PLMAP: case CRMAP: case LVMAP:
  case FIMAP: case POMAP:
    img = mp_legend[1];
    break;
  case RGMAP:
    img = mp_legend[2];
    break;
  default:
    img = mp_legend[0];
    break;
  }
  if (img != NULL && img->w > 1)
    sv_draw_image(b->buf, b->w - img->w - 6, (b->h - img->h) / 2, img);
}


static void
mp_done(frame)
  Frame frame;
{
  window_set(frame, WIN_SHOW, FALSE, 0);
}


void
sv_map_create()
{
  SvMapScale = SvColor ? 3 : 2;
  mp_w = WORLD_X * SvMapScale;
  mp_h = WORLD_Y * SvMapScale;
  SvMapFrame = window_create(SvFrame, FRAME,
			     FRAME_LABEL, mp_titles[ALMAP],
			     FRAME_SHOW_LABEL, TRUE,
			     FRAME_DONE_PROC, mp_done,
			     WIN_SHOW, FALSE,
			     0);
  mp_bar = sv_bar_create(SvMapFrame, mp_w, mp_items, mp_bar_extra);
  SvMapCanvas = window_create(SvMapFrame, CANVAS,
			      WIN_X, 0,
			      WIN_BELOW, mp_bar->canvas,
			      WIN_WIDTH, mp_w,
			      WIN_HEIGHT, mp_h,
			      CANVAS_RETAINED, FALSE,
			      CANVAS_REPAINT_PROC, mp_repaint,
			      WIN_EVENT_PROC, mp_event,
			      0);
  window_set(SvMapCanvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, WIN_UP_EVENTS,
	       LOC_DRAG, LOC_WINEXIT, 0,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  mp_pw = canvas_pixwin(SvMapCanvas);
  window_fit(SvMapFrame);
}


void
sv_map_load()
{
  if (!SvColor)
    return;			/* the 1 bit map draws its own legend */
  mp_legend[0] = sv_load_image("legendn.xpm");
  mp_legend[1] = sv_load_image("legendmm.xpm");
  mp_legend[2] = sv_load_image("legendpm.xpm");
}


void
sv_map_cms()
{
  sv_bar_cms(mp_bar);
  sv_set_cms(mp_pw);
}


/* ---- engine hooks ----------------------------------------------------------- */

setUpMapProcs()
{
}


drawAll(view)
  SimView *view;
{
  sv_map_invalidate();
}


InvalidateMaps()
{
  mp_dirty = 1;
}


RedrawMaps()
{
  mp_dirty = 1;
}


int
DoUpdateMap(view)
  SimView *view;
{
  mp_dirty = 1;
  return 1;
}
