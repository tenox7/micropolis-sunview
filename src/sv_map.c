/* sv_map.c -- the "Micropolis Overall Map" window and its overlays.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * Laid out like the DUX map window (res/wmap.tcl): Zones and Overlays
 * menus with the overlay legend, and the map, 3 pixels per tile.  The map
 * is composed as colormap indices (the drawing rules come from the
 * original g_map.c and g_smmaps.c), then copied to an 8 bit pixrect.  In
 * monochrome mode it is composed as gray levels from the DUX gray tiles,
 * with the overlays in the black and white patterns of the DUX legend
 * images, then ordered-dithered to a 1 bit pixrect.  The editor's visible
 * area is drawn on top as a rectangle.  Click or drag to move the editor;
 * the right button has every map in one menu.
 */

#include "sv.h"

Canvas SvMapCanvas = NULL;
Frame SvMapFrame = NULL;

static SvBar *mp_bar = NULL;
static SvImage *mp_legend[3];		/* none, low-high, minus-plus */

static SvBarItem mp_items[] = {
  { "Zones", sv_zones_menu },
  { "Overlays", sv_overlays_menu },
  { NULL, NULL }
};

static Pixwin *mp_pw = NULL;
static Pixrect *mp_out = NULL;		/* map + view rectangle, screen depth */
static unsigned char *mp_img = NULL;	/* composed map: colormap indices,
					 * gray levels in monochrome */
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

/* Monochrome overlay patterns, 4x4, one row per (y & 3), bit 3 is x & 3
 * == 0, 1 = black.  They are the mono colors of the DUX legend images
 * (legendmm.xpm, legendpm.xpm), so the map matches its legend: like the
 * original valGrayMap, low values are dark and high values white. */
static unsigned char mono_pat[5][4] = {
  { 0x0, 0x0, 0x0, 0x0 },		/* white */
  { 0x4, 0xa, 0x1, 0xa },		/* light, 3/8 black */
  { 0xa, 0x5, 0xa, 0x5 },		/* half */
  { 0xe, 0x5, 0xb, 0x5 },		/* dark, 5/8 black */
  { 0xf, 0xf, 0xf, 0xf }		/* black */
};
static short val_pat[9] = {
  -1, 3, 2, 1, 0,			/* low .. very high */
  1, 0, 3, 4				/* plus, very plus, minus, very minus */
};

/* monochrome power map levels (g_smmaps.c) */
#define GRAY_POWERED	255
#define GRAY_UNPOWERED	0
#define GRAY_CONDUCTIVE	127

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


/* ---- composing ------------------------------------------------------------ */

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
  int x, y, t, powered, unpowered, conductive;
  unsigned short *mp;

  if (SvColor) {
    powered = SIMCOLOR_RED;
    unpowered = SIMCOLOR_LIGHTBLUE;
    conductive = SIMCOLOR_LIGHTGRAY;
  } else {
    powered = GRAY_POWERED;
    unpowered = GRAY_UNPOWERED;
    conductive = GRAY_CONDUCTIVE;
  }
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
	fill3x3(x, y, (tile & PWRBIT) ? powered : unpowered);
      } else if (tile & CONDBIT) {
	fill3x3(x, y, conductive);
      } else {
	put3x3(x, y, &SvSmallTiles[0]);
      }
    }
  }
}


/* An overlay square: stippled color, or in monochrome the legend's
 * pattern as pure black (0) and white (255), which the dither keeps. */
static void
maybe_rect(val, x, y, w, h)
  int val, x, y, w, h;
{
  register unsigned char *d;
  register int i, j, c;
  unsigned char *pat;

  if (val == VAL_NONE)
    return;
  if (x + w > MAP_W)
    w = MAP_W - x;
  if (y + h > MAP_H)
    h = MAP_H - y;
  if (w <= 0 || h <= 0)
    return;
  if (SvColor) {
    c = val_color[val];
    for (j = 0; j < h; j++) {
      d = mp_img + (y + j) * MAP_W + x;
      for (i = (x + y + j) & 1; i < w; i += 2)
	d[i] = c;
    }
  } else {
    pat = mono_pat[val_pat[val]];
    for (j = 0; j < h; j++) {
      d = mp_img + (y + j) * MAP_W + x;
      c = pat[(y + j) & 3];
      for (i = 0; i < w; i++)
	d[i] = (c & (8 >> ((x + i) & 3))) ? 0 : 255;
    }
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


/* Composed map -> the output pixrect, plus the editor rectangle: black
 * and white (DUX: white, yellow, black) so it shows on any map. */
static void
build_out()
{
  struct mpr_data *md = mpr_d(mp_out);
  unsigned char *d, *s;
  int x, y, lb = md->md_linebytes, bits;
  unsigned char *dith;
  SimView *v = SvEditor;
  int rx, ry, rw, rh;

  d = (unsigned char *)md->md_image;
  s = mp_img;
  if (SvColor) {
    for (y = 0; y < MAP_H; y++) {
      bcopy((char *)s, (char *)d, MAP_W);
      s += MAP_W;
      d += lb;
    }
  } else {
    for (y = 0; y < MAP_H; y++) {
      dith = SvDither[y & 3];
      for (x = 0; x < MAP_W; x += 8) {
	bits = 0;
	if (s[x]     < dith[0]) bits |= 0x80;
	if (s[x + 1] < dith[1]) bits |= 0x40;
	if (s[x + 2] < dith[2]) bits |= 0x20;
	if (s[x + 3] < dith[3]) bits |= 0x10;
	if (s[x + 4] < dith[0]) bits |= 0x08;
	if (s[x + 5] < dith[1]) bits |= 0x04;
	if (s[x + 6] < dith[2]) bits |= 0x02;
	if (s[x + 7] < dith[3]) bits |= 0x01;
	d[x >> 3] = bits;
      }
      s += MAP_W;
      d += lb;
    }
  }

  if (v != NULL && v->w_width > 0) {
    rx = ((v->pan_x - (v->w_width >> 1)) * 3) >> 4;
    ry = ((v->pan_y - (v->w_height >> 1)) * 3) >> 4;
    rw = (v->w_width * 3) >> 4;
    rh = (v->w_height * 3) >> 4;
    if (!SvColor)
      sv_frame_rect(mp_out, rx - 2, ry - 2, rw + 4, rh + 4, SIMCOLOR_WHITE, 1);
    sv_frame_rect(mp_out, rx - 1, ry - 1, rw + 2, rh + 2, SIMCOLOR_BLACK, 1);
    sv_frame_rect(mp_out, rx, ry, rw, rh, SIMCOLOR_WHITE, 1);
  }
}


static void
mp_blit()
{
  pw_rop(mp_pw, 0, 0, MAP_W, MAP_H, PIX_SRC, mp_out, 0, 0);
}


static void
mp_alloc()
{
  if (mp_img != NULL)
    return;
  mp_img = (unsigned char *)malloc(MAP_W * MAP_H + 8);	/* +8: dither reads */
  mp_out = mem_create(MAP_W, MAP_H, SvColor ? 8 : 1);
  if (mp_img == NULL || mp_out == NULL) {
    fprintf(stderr, "micropolis: out of memory for the map\n");
    exit(1);
  }
  bzero((char *)mp_img, MAP_W * MAP_H + 8);
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
    draw_compose();
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
    pw_writebackground(pw, 0, 0, MAP_W, MAP_H, PIX_SRC);
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
  int x = event_x(event) / 3, y = event_y(event) / 3;

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
  SvMapFrame = window_create(SvFrame, FRAME,
			     FRAME_LABEL, mp_titles[ALMAP],
			     FRAME_SHOW_LABEL, TRUE,
			     FRAME_DONE_PROC, mp_done,
			     WIN_SHOW, FALSE,
			     0);
  mp_bar = sv_bar_create(SvMapFrame, MAP_W, mp_items, mp_bar_extra);
  SvMapCanvas = window_create(SvMapFrame, CANVAS,
			      WIN_X, 0,
			      WIN_BELOW, mp_bar->canvas,
			      WIN_WIDTH, MAP_W,
			      WIN_HEIGHT, MAP_H,
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
