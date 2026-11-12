/* sv_editor.c -- the "Micropolis Editor" window: menu bar, palette, city.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * Laid out like the DUX editor window (res/weditor.tcl): Display and
 * Options menus with the last message beside them, the tool palette on
 * the left (sv_tools.c) and the city view.
 *
 * The editor keeps a tile-aligned back buffer that covers the visible part
 * of the city plus one tile of slack for the sub-tile pan offset.  A cache
 * remembers which tile is in each cell, so a tick only copies the tiles that
 * changed.  Sprites, the tool outline and chalk lines are drawn straight
 * into the buffer; the cells under them are redrawn on the next tick.  Only
 * the cells that changed are copied to the screen.
 */

#include "sv.h"

Canvas SvEditorCanvas = NULL;
Frame SvEditorFrame = NULL;
int SvEditorTools = 1;			/* the palette is shown */
int SvEditorChalk = 1;			/* chalk is drawn */
int SvEditorSkip = 0;			/* ticks to skip between updates */

static SvBar *ed_bar = NULL;
static char ed_msg[300] = "";		/* the label beside the menus */
static int ed_skip_left = 0;

static SvBarItem ed_items[] = {
  { "Display", sv_display_menu },
  { "Options", sv_edopt_menu },
  { NULL, NULL }
};

static Pixwin *ed_pw = NULL;
static Pixrect *ed_buf = NULL;		/* back buffer, tile aligned */
static unsigned char *ed_base;		/* its pixels */
static int ed_lb;			/* its bytes per line */
static int ed_cols = 0, ed_rows = 0;	/* its size in tiles */
static short *ed_cache = NULL;		/* tile drawn in each cell, -1 redraw */
static char *ed_dirty = NULL;		/* cells to copy to the screen */
static int ed_full = 1;			/* copy the whole buffer next time */
static int ed_ww = 0, ed_wh = 0;	/* canvas size */
static int ed_last_tx = -1, ed_last_ty = -1;
static int ed_last_sx = -9999, ed_last_sy = -9999;

#define MAX_OVER 80
typedef struct EdRect {
  short x, y, w, h;
} EdRect;
static EdRect ed_over[MAX_OVER];	/* overlays drawn last tick */
static int ed_nover = 0;
static int ed_over_all = 0;		/* overflowed: redraw everything */

static int ed_mouse_in = 0, ed_mouse_x = 0, ed_mouse_y = 0;
static int ed_button = 0;		/* 1 tool, 2 pan */
static int ed_pan_mx, ed_pan_my, ed_pan_ox, ed_pan_oy;
static int ed_shake = 0;		/* ticks of earthquake left */
static int ed_dx = 0, ed_dy = 0;	/* shake offset */

#define CELL(c, r) ((c) * ed_rows + (r))


/* ---- geometry ----------------------------------------------------------- */

static int
floor16(v)
  int v;
{
  return (v >= 0) ? (v >> 4) : -((15 - v) >> 4);
}


/* Recompute the visible tile range from pan_x/pan_y (the world pixel at the
 * middle of the window).  Like the original DoAdjustPan. */
static void
ed_adjust()
{
  SimView *v = SvEditor;
  int ww2, wh2, tx, ty, tw, th, maxx, maxy;

  if (v == NULL)
    return;
  v->w_width = ed_ww;
  v->w_height = ed_wh;
  v->m_width = ed_cols << 4;
  v->m_height = ed_rows << 4;
  ww2 = ed_ww >> 1;
  wh2 = ed_wh >> 1;

  /* keep the city filling the window when it is bigger than the window */
  maxx = v->i_width - (ed_ww - ww2);
  maxy = v->i_height - (ed_wh - wh2);
  if (maxx < ww2)
    v->pan_x = v->i_width >> 1;
  else if (v->pan_x < ww2)
    v->pan_x = ww2;
  else if (v->pan_x > maxx)
    v->pan_x = maxx;
  if (maxy < wh2)
    v->pan_y = v->i_height >> 1;
  else if (v->pan_y < wh2)
    v->pan_y = wh2;
  else if (v->pan_y > maxy)
    v->pan_y = maxy;

  tx = floor16(v->pan_x - ww2);
  ty = floor16(v->pan_y - wh2);
  if (tx < 0)
    tx = 0;
  if (ty < 0)
    ty = 0;
  tw = (v->pan_x - ww2 + ed_ww + 15) >> 4;
  th = (v->pan_y - wh2 + ed_wh + 15) >> 4;
  if (tw > WORLD_X)
    tw = WORLD_X;
  if (th > WORLD_Y)
    th = WORLD_Y;
  tw -= tx;
  th -= ty;
  if (tw > ed_cols)
    tw = ed_cols;
  if (th > ed_rows)
    th = ed_rows;
  if (tw < 0)
    tw = 0;
  if (th < 0)
    th = 0;

  v->tile_x = tx;
  v->tile_y = ty;
  v->tile_width = tw;
  v->tile_height = th;
  v->screen_x = ww2 - v->pan_x + (tx << 4);
  v->screen_y = wh2 - v->pan_y + (ty << 4);
  v->screen_width = tw << 4;
  v->screen_height = th << 4;

  if (tx != ed_last_tx || ty != ed_last_ty) {
    sv_editor_invalidate();
    ed_last_tx = tx;
    ed_last_ty = ty;
  }
  if (v->screen_x != ed_last_sx || v->screen_y != ed_last_sy) {
    ed_full = 1;
    ed_last_sx = v->screen_x;
    ed_last_sy = v->screen_y;
  }
}


/* (Re)allocate the back buffer for the current canvas size. */
static void
ed_alloc()
{
  int cols, rows;

  cols = (ed_ww + 15) / 16 + 1;
  rows = (ed_wh + 15) / 16 + 1;
  if (cols > WORLD_X)
    cols = WORLD_X;
  if (rows > WORLD_Y)
    rows = WORLD_Y;
  if (ed_buf != NULL && cols == ed_cols && rows == ed_rows)
    return;
  if (ed_buf != NULL) {
    pr_destroy(ed_buf);
    free((char *)ed_cache);
    free(ed_dirty);
  }
  ed_cols = cols;
  ed_rows = rows;
  ed_buf = mem_create(cols * 16, rows * 16, SvColor ? 8 : 1);
  ed_cache = (short *)malloc((unsigned)(cols * rows) * sizeof(short));
  ed_dirty = (char *)malloc((unsigned)(cols * rows));
  if (ed_buf == NULL || ed_cache == NULL || ed_dirty == NULL) {
    fprintf(stderr, "micropolis: out of memory for the editor\n");
    exit(1);
  }
  ed_base = (unsigned char *)mpr_d(ed_buf)->md_image;
  ed_lb = mpr_d(ed_buf)->md_linebytes;
  ed_last_tx = ed_last_ty = -1;
  ed_nover = 0;
  sv_editor_invalidate();
}


/* Forget every cached tile: the next update redraws the whole editor. */
void
sv_editor_invalidate()
{
  int i;

  if (ed_cache != NULL)
    for (i = ed_cols * ed_rows - 1; i >= 0; i--)
      ed_cache[i] = -1;
  ed_full = 1;
}


void
sv_editor_pan_to(tx, ty)
  int tx, ty;
{
  if (SvEditor == NULL)
    return;
  SvEditor->pan_x = (tx << 4) + 8;
  SvEditor->pan_y = (ty << 4) + 8;
  SvEditor->auto_going = 0;
  ed_adjust();
  sv_map_view_moved();
}


void
sv_editor_pan_by(dx, dy)
  int dx, dy;
{
  if (SvEditor == NULL)
    return;
  SvEditor->pan_x += dx;
  SvEditor->pan_y += dy;
  ed_adjust();
  sv_map_view_moved();
}


/* Smoothly scroll toward (x, y) in tiles, like the original auto goto. */
void
sv_auto_goto(x, y)
  int x, y;
{
  SimView *v = SvEditor;

  if (v == NULL || !autoGo)
    return;
  v->auto_x_goal = (x << 4) + 8;
  v->auto_y_goal = (y << 4) + 8;
  v->auto_goto = 1;
  v->auto_going = 1;
}


static void
ed_auto_goto()
{
  SimView *v = SvEditor;
  int dx, dy, speed, ox, oy;
  double dist, sloth, dir;

  if (v->follow != NULL) {
    sv_editor_pan_by(v->follow->x + v->follow->x_hot - v->pan_x,
		     v->follow->y + v->follow->y_hot - v->pan_y);
    return;
  }
  if (!v->auto_going || ed_button != 0)
    return;
  speed = v->auto_speed;
  sloth = (v->auto_going < 5) ? ((double)v->auto_going) / 5.0 : 1.0;
  dx = v->auto_x_goal - v->pan_x;
  dy = v->auto_y_goal - v->pan_y;
  dist = sqrt((double)(dx * dx + dy * dy));
  if (dist < speed * sloth) {
    v->auto_going = 0;
    sv_editor_pan_by(dx, dy);
  } else {
    ox = v->pan_x;
    oy = v->pan_y;
    dir = atan2((double)dy, (double)dx);
    sv_editor_pan_by((int)(cos(dir) * speed * sloth + 0.5),
		     (int)(sin(dir) * speed * sloth + 0.5));
    v->auto_going++;
    if (v->pan_x == ox && v->pan_y == oy)
      v->auto_going = 0;		/* stuck at the city edge */
  }
}


/* ---- drawing into the buffer -------------------------------------------- */

static void
ed_put_tile(col, row, tile)
  int col, row, tile;
{
  register unsigned char *d;
  register int i, lb = ed_lb;

  if (SvColor) {
    register unsigned int *s, *dd;

    s = (unsigned int *)((unsigned char *)mpr_d(SvBigTiles)->md_image +
			 (tile << 8));
    d = ed_base + (row << 4) * lb + (col << 4);
    for (i = 16; i > 0; i--) {
      dd = (unsigned int *)d;
      dd[0] = s[0];
      dd[1] = s[1];
      dd[2] = s[2];
      dd[3] = s[3];
      s += 4;
      d += lb;
    }
  } else {
    register unsigned short *s;

    s = (unsigned short *)((unsigned char *)mpr_d(SvBigTiles)->md_image +
			   (tile << 5));
    d = ed_base + (row << 4) * lb + (col << 1);
    for (i = 16; i > 0; i--) {
      *(unsigned short *)d = *s++;
      d += lb;
    }
  }
}


/* Mark the cells under a buffer rectangle dirty (and optionally redraw). */
static void
ed_mark(x, y, w, h, redraw)
  int x, y, w, h, redraw;
{
  int c0, c1, r0, r1, c, r;

  if (w <= 0 || h <= 0)
    return;
  c0 = floor16(x);
  r0 = floor16(y);
  c1 = floor16(x + w - 1);
  r1 = floor16(y + h - 1);
  if (c0 < 0)
    c0 = 0;
  if (r0 < 0)
    r0 = 0;
  if (c1 >= ed_cols)
    c1 = ed_cols - 1;
  if (r1 >= ed_rows)
    r1 = ed_rows - 1;
  for (c = c0; c <= c1; c++)
    for (r = r0; r <= r1; r++) {
      ed_dirty[CELL(c, r)] = 1;
      if (redraw)
	ed_cache[CELL(c, r)] = -1;
    }
}


static void
ed_add_over(x, y, w, h)
  int x, y, w, h;
{
  ed_mark(x, y, w, h, 0);
  if (ed_nover < MAX_OVER) {
    ed_over[ed_nover].x = x;
    ed_over[ed_nover].y = y;
    ed_over[ed_nover].w = w;
    ed_over[ed_nover].h = h;
    ed_nover++;
  } else {
    ed_over_all = 1;
  }
}


static void
ed_draw_tiles()
{
  SimView *v = SvEditor;
  register unsigned short tile;
  register int t, row;
  unsigned short *mp;
  short *cp;
  char *dp;
  int col, blink = (flagBlink <= 0);

  for (col = 0; col < v->tile_width; col++) {
    mp = (unsigned short *)&Map[v->tile_x + col][v->tile_y];
    cp = &ed_cache[CELL(col, 0)];
    dp = &ed_dirty[CELL(col, 0)];
    for (row = 0; row < v->tile_height; row++) {
      tile = mp[row];
      if ((tile & LOMASK) >= TILE_COUNT)
	tile -= TILE_COUNT;
      if (blink && (tile & ZONEBIT) && !(tile & PWRBIT))
	t = LIGHTNINGBOLT;
      else
	t = tile & LOMASK;
      if (t != cp[row]) {
	cp[row] = t;
	ed_put_tile(col, row, t);
	dp[row] = 1;
      }
    }
  }
}


static void
ed_draw_ink()
{
  Ink *ink;
  int i, x, y, lx, ly, ox, oy, c;

  ox = SvEditor->tile_x << 4;
  oy = SvEditor->tile_y << 4;
  for (ink = sim->overlay; ink != NULL; ink = ink->next) {
    if (ink->right < ox || ink->left > ox + (ed_cols << 4) ||
	ink->bottom < oy || ink->top > oy + (ed_rows << 4))
      continue;
    c = SvColor ? ink->color : SIMCOLOR_BLACK;
    x = ink->x - ox;
    y = ink->y - oy;
    if (ink->length == 1) {
      sv_fill(ed_buf, x - 1, y - 1, 3, 3, c);
      continue;
    }
    for (i = 1; i < ink->length; i++) {
      lx = x;
      ly = y;
      x += ink->points[i].x;
      y += ink->points[i].y;
      sv_line(ed_buf, lx, ly, x, y, c);
      sv_line(ed_buf, lx + 1, ly, x + 1, y, c);
      sv_line(ed_buf, lx, ly + 1, x, y + 1, c);
    }
  }
}


static void
ed_draw_sprites()
{
  SimSprite *s;
  SvImage *img;
  int x, y, ox, oy;

  ox = SvEditor->tile_x << 4;
  oy = SvEditor->tile_y << 4;
  for (s = sim->sprite; s != NULL; s = s->next) {
    if (s->frame <= 0 || s->type <= 0 || s->type >= OBJN ||
	s->frame > SvSpriteFrames[s->type])
      continue;
    img = SvSprites[s->type][s->frame - 1];
    if (img == NULL)
      continue;
    x = s->x + s->x_offset - ox;
    y = s->y + s->y_offset - oy;
    if (x + img->w <= 0 || y + img->h <= 0 ||
	x >= (ed_cols << 4) || y >= (ed_rows << 4))
      continue;
    sv_draw_image(ed_buf, x, y, img);
    ed_add_over(x, y, img->w, img->h);
  }
}


/* The tool outline under the pointer, like the original DrawCursor. */
static void
ed_draw_cursor()
{
  SimView *v = SvEditor;
  int state, size, tx, ty, x, y, w, c1, c2;

  if (!ed_mouse_in || ed_button == 2)
    return;
  state = v->tool_state;
  if (state < firstState || state > lastState)
    return;
  size = toolSize[state];
  tx = floor16(v->pan_x - (ed_ww >> 1) + ed_mouse_x);
  ty = floor16(v->pan_y - (ed_wh >> 1) + ed_mouse_y);
  if (size == 0) {
    /* chalk and eraser: a small box at the pointer */
    x = ed_mouse_x - v->screen_x;
    y = ed_mouse_y - v->screen_y;
    sv_frame_rect(ed_buf, x - 3, y - 3, 7, 7, SIMCOLOR_BLACK, 1);
    sv_frame_rect(ed_buf, x - 2, y - 2, 5, 5, SIMCOLOR_WHITE, 1);
    ed_add_over(x - 3, y - 3, 7, 7);
    return;
  }
  tx -= toolOffset[state];
  ty -= toolOffset[state];
  x = (tx - v->tile_x) << 4;
  y = (ty - v->tile_y) << 4;
  w = size << 4;
  c1 = toolColors[state] & 0xff;
  c2 = (toolColors[state] >> 8) & 0xff;
  if (!SvColor) {
    sv_frame_rect(ed_buf, x, y, w, w, SIMCOLOR_BLACK, 1);
    sv_frame_rect(ed_buf, x + 1, y + 1, w - 2, w - 2, SIMCOLOR_WHITE, 1);
    sv_frame_rect(ed_buf, x + 2, y + 2, w - 4, w - 4, SIMCOLOR_BLACK, 1);
  } else {
    sv_frame_rect(ed_buf, x, y, w, w, SIMCOLOR_BLACK, 1);
    sv_frame_rect(ed_buf, x + 1, y + 1, w - 2, w - 2, c1, 2);
    sv_frame_rect(ed_buf, x + 3, y + 3, w - 6, w - 6, c2, 1);
  }
  ed_add_over(x, y, w, w);
}


/* ---- copying to the screen ---------------------------------------------- */

static void
ed_blit_all()
{
  SimView *v = SvEditor;
  int x0 = v->screen_x + ed_dx, y0 = v->screen_y + ed_dy;
  int x1 = x0 + v->screen_width, y1 = y0 + v->screen_height;
  int bg = SvColor ? SIMCOLOR_BLACK : 1;

  /* outside the city (only when the window is bigger than the city) */
  if (y0 > 0)
    pw_rop(ed_pw, 0, 0, ed_ww, y0, PIX_SRC | PIX_COLOR(bg), (Pixrect *)NULL,
	   0, 0);
  if (y1 < ed_wh)
    pw_rop(ed_pw, 0, y1, ed_ww, ed_wh - y1, PIX_SRC | PIX_COLOR(bg),
	   (Pixrect *)NULL, 0, 0);
  if (x0 > 0)
    pw_rop(ed_pw, 0, y0, x0, y1 - y0, PIX_SRC | PIX_COLOR(bg),
	   (Pixrect *)NULL, 0, 0);
  if (x1 < ed_ww)
    pw_rop(ed_pw, x1, y0, ed_ww - x1, y1 - y0, PIX_SRC | PIX_COLOR(bg),
	   (Pixrect *)NULL, 0, 0);
  pw_rop(ed_pw, x0, y0, v->screen_width, v->screen_height, PIX_SRC,
	 ed_buf, 0, 0);
}


/* Copy the dirty cells, one run of cells per row (small gaps merged). */
static void
ed_blit_dirty()
{
  SimView *v = SvEditor;
  int row, col, start, gap, last;

  for (row = 0; row < v->tile_height; row++) {
    start = -1;
    last = -1;
    for (col = 0; col <= v->tile_width; col++) {
      if (col < v->tile_width && ed_dirty[CELL(col, row)]) {
	ed_dirty[CELL(col, row)] = 0;
	if (start < 0)
	  start = col;
	last = col;
	continue;
      }
      if (start < 0)
	continue;
      gap = col - last;
      if (col < v->tile_width && gap <= 2)
	continue;			/* bridge a small gap */
      pw_rop(ed_pw, v->screen_x + (start << 4) + ed_dx,
	     v->screen_y + (row << 4) + ed_dy,
	     (last - start + 1) << 4, 16, PIX_SRC,
	     ed_buf, start << 4, row << 4);
      start = -1;
    }
  }
}


static void
ed_clear_dirty()
{
  int i;

  for (i = ed_cols * ed_rows - 1; i >= 0; i--)
    ed_dirty[i] = 0;
}


/* Follow auto goto, update the buffer, copy changes. */
static void
ed_update_now()
{
  SimView *v = SvEditor;
  int i;
  Rect r;

  if (!SvReady || ed_pw == NULL || v == NULL || ed_ww <= 0 || ed_wh <= 0)
    return;
  if (ed_buf == NULL) {
    ed_alloc();
    ed_adjust();
  }
  ed_auto_goto();

  if (ed_shake > 0) {
    ed_shake--;
    if (ed_shake > 0) {
      ed_dx = Rand(16) - 8;
      ed_dy = Rand(16) - 8;
    } else {
      ed_dx = ed_dy = 0;
    }
    ed_full = 1;
  }

  /* cells under last tick's sprites and outlines get redrawn */
  if (ed_over_all) {
    sv_editor_invalidate();
    ed_over_all = 0;
  } else {
    for (i = 0; i < ed_nover; i++)
      ed_mark(ed_over[i].x, ed_over[i].y, ed_over[i].w, ed_over[i].h, 1);
  }
  ed_nover = 0;

  ed_draw_tiles();
  if (sim->overlay != NULL && SvEditorChalk)
    ed_draw_ink();
  ed_draw_sprites();
  ed_draw_cursor();

  r.r_left = 0;
  r.r_top = 0;
  r.r_width = ed_ww;
  r.r_height = ed_wh;
  pw_lock(ed_pw, &r);
  if (ed_full) {
    ed_blit_all();
    ed_clear_dirty();
    ed_full = 0;
  } else {
    ed_blit_dirty();
  }
  pw_unlock(ed_pw);
}


/* Per tick, or less often as the Display menu says. */
void
sv_editor_update()
{
  if (SvEditorSkip > 0) {
    if (ed_skip_left > 0) {
      ed_skip_left--;
      return;
    }
    ed_skip_left = SvEditorSkip;
  }
  ed_update_now();
}


/* ---- canvas callbacks ---------------------------------------------------- */

static void
ed_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (!SvReady || ed_buf == NULL) {
    pw_writebackground(pw, 0, 0, ed_ww, ed_wh, PIX_SRC);
    return;
  }
  ed_full = 1;
  ed_update_now();
}


static void
ed_resize(canvas, w, h)
  Canvas canvas;
  int w, h;
{
  ed_ww = w;
  ed_wh = h;
  if (SvReady && SvEditor != NULL) {
    ed_alloc();
    ed_adjust();
    sv_map_view_moved();
  }
}


static void
ed_tool(event, kind)
  Event *event;
  int kind;
{
  int x = event_x(event), y = event_y(event);

  if (kind == 0)
    ToolDown(SvEditor, x, y);
  else if (kind == 1)
    ToolDrag(SvEditor, x, y);
  else
    ToolUp(SvEditor, x, y);
  sv_map_tool_used();
}


/* Keys: arrows pan, 0-3 speed, space pause, the rest to w_keys.c. */
void
sv_editor_key(event)
  Event *event;
{
  int id = event_id(event), step;

  step = event_shift_is_down(event) ? 160 : 48;
  if (event_is_up(event)) {
    if (event_is_ascii(event))
      doKeyUp(SvEditor, id);
    return;
  }
  if (id == KEY_RIGHT(8)) {
    sv_editor_pan_by(0, -step);
  } else if (id == KEY_RIGHT(14)) {
    sv_editor_pan_by(0, step);
  } else if (id == KEY_RIGHT(10)) {
    sv_editor_pan_by(-step, 0);
  } else if (id == KEY_RIGHT(12)) {
    sv_editor_pan_by(step, 0);
  } else if (id == KEY_RIGHT(11)) {
    sv_editor_pan_to(WORLD_X / 2, WORLD_Y / 2);
  } else if (event_is_ascii(event)) {
    if (id >= '0' && id <= '3')
      sv_set_sim_speed(id - '0');
    else if (id == ' ')
      sv_toggle_pause();
    else
      doKeyDown(SvEditor, id);
  }
}


static void
ed_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int id = event_id(event);

  if (!SvReady || SvEditor == NULL)
    return;
  ed_mouse_x = event_x(event);
  ed_mouse_y = event_y(event);

  switch (id) {
  case LOC_WINENTER:
    ed_mouse_in = 1;
    break;
  case LOC_WINEXIT:
    ed_mouse_in = 0;
    break;
  case LOC_MOVE:
    ed_mouse_in = 1;
    break;
  case MS_LEFT:
    if (event_is_down(event)) {
      ed_button = 1;
      SvEditor->auto_going = 0;
      ed_tool(event, 0);
    } else if (ed_button == 1) {
      ed_tool(event, 2);
      ed_button = 0;
    }
    break;
  case MS_MIDDLE:
    if (event_is_down(event)) {
      ed_button = 2;
      SvEditor->auto_going = 0;
      ed_pan_mx = ed_mouse_x;
      ed_pan_my = ed_mouse_y;
      ed_pan_ox = SvEditor->pan_x;
      ed_pan_oy = SvEditor->pan_y;
    } else {
      ed_button = 0;
    }
    break;
  case LOC_DRAG:
    ed_mouse_in = 1;
    if (ed_button == 1) {
      ed_tool(event, 1);
    } else if (ed_button == 2) {
      sv_editor_pan_by(ed_pan_ox - (ed_mouse_x - ed_pan_mx) - SvEditor->pan_x,
		       ed_pan_oy - (ed_mouse_y - ed_pan_my) - SvEditor->pan_y);
    }
    break;
  case MS_RIGHT:
    if (event_is_down(event)) {
      ed_button = 0;
      ed_mouse_in = 0;			/* no outline while the menu is up */
      sv_menu_event(sv_tool_menu(), canvas,
		    canvas_window_event(canvas, event));
    }
    break;
  default:
    sv_editor_key(event);
    break;
  }
}


/* The message label: the rest of the bar, raised, in the Large font. */
static void
ed_bar_extra(b)
  SvBar *b;
{
  Pixrect *sub;
  int x = b->end, w = b->w - b->end - 3, h = b->h - 4, tw;

  if (w < 8)
    return;
  sv_bevel2(b->buf, x, 2, w, h, 1);
  if (ed_msg[0] == 0 || (sub = pr_region(b->buf, x + 3, 4, w - 6, h - 4)) ==
      NULL)
    return;
  tw = sv_text_width(SvBigFont, ed_msg);
  sv_text(sub, (tw < w - 6) ? (w - 6 - tw) / 2 : 0,
	  (h - 4 - sv_font_height(SvBigFont)) / 2, SvUiText, SvBigFont, ed_msg);
  pr_destroy(sub);
}


void
sv_editor_message(str)
  char *str;
{
  strncpy(ed_msg, str, sizeof(ed_msg) - 1);
  sv_bar_redraw(ed_bar);
}


/* As the DUX UISetCityName: "Micropolis Editor on <name>". */
void
sv_editor_title()
{
  static char label[160];

  if (SvEditorFrame == NULL)
    return;
  sprintf(label, "Micropolis Editor on %.120s", CityName ? CityName : "");
  window_set(SvEditorFrame, FRAME_LABEL, label, 0);
}


void
sv_editor_cms()
{
  sv_bar_cms(ed_bar);
  sv_set_cms(ed_pw);
  sv_set_cms(canvas_pixwin(SvToolCanvas));
}


/* Options / Pallet Panel: show or hide the palette. */
void
sv_editor_show_tools(on)
  int on;
{
  if (on == SvEditorTools || SvToolCanvas == NULL)
    return;
  SvEditorTools = on;
  if (on) {
    window_set(SvToolCanvas, WIN_SHOW, TRUE, 0);
    window_set(SvEditorCanvas, WIN_RIGHT_OF, SvToolCanvas,
	       WIN_WIDTH, WIN_EXTEND_TO_EDGE, 0);
  } else {
    window_set(SvToolCanvas, WIN_SHOW, FALSE, 0);
    window_set(SvEditorCanvas, WIN_X, 0, WIN_WIDTH, WIN_EXTEND_TO_EDGE, 0);
  }
  sv_tools_redraw();
  sv_editor_invalidate();
}


static void
ed_done(frame)
  Frame frame;
{
  window_set(frame, WIN_SHOW, FALSE, 0);
}


/* The editor frame, w x h, to the right of the controls. */
void
sv_editor_create(w, h)
  int w, h;
{
  SvEditorFrame = window_create(SvFrame, FRAME,
				FRAME_LABEL, "Micropolis Editor",
				FRAME_SHOW_LABEL, TRUE,
				FRAME_DONE_PROC, ed_done,
				WIN_WIDTH, w,
				WIN_HEIGHT, h,
				WIN_SHOW, FALSE,
				0);
  ed_bar = sv_bar_create(SvEditorFrame, WIN_EXTEND_TO_EDGE, ed_items,
			 ed_bar_extra);
  sv_tools_create(SvEditorFrame, ed_bar->canvas);
  SvEditorCanvas = window_create(SvEditorFrame, CANVAS,
				 WIN_RIGHT_OF, SvToolCanvas,
				 WIN_BELOW, ed_bar->canvas,
				 WIN_WIDTH, WIN_EXTEND_TO_EDGE,
				 WIN_HEIGHT, WIN_EXTEND_TO_EDGE,
				 CANVAS_RETAINED, FALSE,
				 CANVAS_REPAINT_PROC, ed_repaint,
				 CANVAS_RESIZE_PROC, ed_resize,
				 WIN_EVENT_PROC, ed_event,
				 0);
  window_set(SvEditorCanvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, WIN_UP_EVENTS,
	       LOC_MOVE, LOC_DRAG, LOC_WINENTER, LOC_WINEXIT, 0,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  ed_pw = canvas_pixwin(SvEditorCanvas);
  ed_ww = (int)window_get(SvEditorCanvas, CANVAS_WIDTH);
  ed_wh = (int)window_get(SvEditorCanvas, CANVAS_HEIGHT);
}


/* ---- engine hooks: coordinates, redraw, earthquake, chalk ---------------- */

ViewToTileCoords(view, x, y, outx, outy)
  SimView *view;
  int x, y;
  int *outx, *outy;
{
  int px, py;

  ViewToPixelCoords(view, x, y, &px, &py);
  *outx = px >> 4;
  *outy = py >> 4;
}


/* Window pixel -> city pixel (the original's clamping included). */
ViewToPixelCoords(view, x, y, outx, outy)
  SimView *view;
  int x, y;
  int *outx, *outy;
{
  if (view->class == Map_Class) {
    x = (x * 16) / 3;
    y = (y * 16) / 3;
  } else {
    x = view->pan_x - ((view->w_width >> 1) - x);
    y = view->pan_y - ((view->w_height >> 1) - y);
  }
  if (x < 0)
    x = 0;
  if (x >= (WORLD_X << 4))
    x = (WORLD_X << 4) - 1;
  if (y < 0)
    y = 0;
  if (y >= (WORLD_Y << 4))
    y = (WORLD_Y << 4) - 1;
  if (view->class == Editor_Class) {
    if (x < (view->tile_x << 4))
      x = view->tile_x << 4;
    if (x >= ((view->tile_x + view->tile_width) << 4))
      x = ((view->tile_x + view->tile_width) << 4) - 1;
    if (y < (view->tile_y << 4))
      y = view->tile_y << 4;
    if (y >= ((view->tile_y + view->tile_height) << 4))
      y = ((view->tile_y + view->tile_height) << 4) - 1;
  }
  if (view->tool_x_const != -1)
    x = (view->tool_x_const << 4) + 8;
  if (view->tool_y_const != -1)
    y = (view->tool_y_const << 4) + 8;
  *outx = x;
  *outy = y;
}


EventuallyRedrawView(view)
  SimView *view;
{
  if (view == NULL)
    return;
  if (view->class == Editor_Class)
    sv_editor_invalidate();
  else
    sv_map_invalidate();
}


CancelRedrawView(view)
  SimView *view;
{
}


InvalidateEditors()
{
  /* the per-tick cache compare finds the changed tiles by itself */
}


RedrawEditors()
{
  sv_editor_invalidate();
}


DoEarthQuake()
{
  MakeSound("city", "Explosion-Low");
  ShakeNow++;
  ed_shake = 60;			/* 3 seconds */
}


StopEarthquake()
{
  ShakeNow = 0;
  if (ed_shake > 1)
    ed_shake = 1;
}


#define POINT_BATCH 32

Ink *
NewInk()
{
  Ink *ink;

  ink = (Ink *)calloc(1, sizeof(Ink));
  ink->maxlength = POINT_BATCH;
  ink->points = (SimPoint *)malloc(POINT_BATCH * sizeof(SimPoint));
  ink->length = 0;
  return ink;
}


FreeInk(ink)
  Ink *ink;
{
  if (ink == NULL)
    return;
  free((char *)ink->points);
  free((char *)ink);
}


StartInk(ink, x, y)
  Ink *ink;
  int x, y;
{
  ink->length = 1;
  ink->left = ink->right = ink->last_x = ink->points[0].x = x;
  ink->top = ink->bottom = ink->last_y = ink->points[0].y = y;
  if (SvEditor != NULL)
    ed_mark(x - (SvEditor->tile_x << 4) - 2, y - (SvEditor->tile_y << 4) - 2,
	    5, 5, 0);
}


AddInk(ink, x, y)
  Ink *ink;
  int x, y;
{
  int dx = x - ink->last_x, dy = y - ink->last_y;
  int l, t, r, b;

  if (dx == 0 && dy == 0)
    return;
  if (ink->length >= ink->maxlength) {
    ink->maxlength += POINT_BATCH;
    ink->points = (SimPoint *)realloc((char *)ink->points,
			      (unsigned)ink->maxlength * sizeof(SimPoint));
  }
  ink->points[ink->length].x = dx;
  ink->points[ink->length].y = dy;
  ink->length++;
  if (x < ink->left) ink->left = x;
  if (x > ink->right) ink->right = x;
  if (y < ink->top) ink->top = y;
  if (y > ink->bottom) ink->bottom = y;

  l = (x < ink->last_x) ? x : ink->last_x;
  r = (x < ink->last_x) ? ink->last_x : x;
  t = (y < ink->last_y) ? y : ink->last_y;
  b = (y < ink->last_y) ? ink->last_y : y;
  if (SvEditor != NULL)
    ed_mark(l - (SvEditor->tile_x << 4) - 2, t - (SvEditor->tile_y << 4) - 2,
	    r - l + 5, b - t + 5, 0);
  ink->last_x = x;
  ink->last_y = y;
}


EraseOverlay()
{
  Ink *ink;

  while (sim->overlay != NULL) {
    ink = sim->overlay;
    sim->overlay = ink->next;
    FreeInk(ink);
  }
  sv_editor_invalidate();
}
