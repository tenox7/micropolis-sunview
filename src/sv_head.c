/* sv_head.c -- the "Micropolis Controls" window, the base frame.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * Laid out like the DUX head window (res/whead.tcl): the menu bar; the date
 * and the funds; a ten year graph of the residential, commercial and
 * industrial populations; the demand gauge with the head picture (click
 * the picture to pause, the gauge to hear the demands); and the message
 * log, which scrolls.
 */

#include "sv.h"

#define HD_W		416		/* at least; the DUX window is 400 wide */
#define HD_INFO_H	69
#define HD_LOG_H	112
#define DM_W		80		/* the demand canvas */
#define DM_H		55
#define LG_PAD		4

static SvBarItem hd_items[] = {
  { "Micropolis", sv_simcity_menu },
  { "Options", sv_options_menu },
  { "Disasters", sv_disasters_menu },
  { "Time", sv_time_menu },
  { "Priority", sv_priority_menu },
  { "Windows", sv_windows_menu },
  { NULL, NULL }
};

static SvBar *hd_bar = NULL;
static int hd_dirty = 1;		/* redraw the bar and the info */


/* ==== date, funds, graph, demand ========================================= */

static Canvas in_canvas = NULL;
static Pixwin *in_pw = NULL;
static Pixrect *in_buf = NULL;
static int in_w = HD_W, in_h = HD_INFO_H;
static int in_dirty = 1;
static SvImage *in_gauge = NULL, *in_go = NULL, *in_stop = NULL;
static int in_rgb_r, in_rgb_c, in_rgb_i;
static char in_funds[80] = "";
static char in_date[80] = "";
static int in_r = 0, in_c = 0, in_i = 0;
static int in_running = 0;
static int in_stamp = -1;		/* graph drawn for this SvGraphStamp */
static int in_dx = 0, in_dy = 0;	/* the demand canvas */

/* SayDemands: three notes, 200 ms apart */
static int sd_at[3], sd_speed[3], sd_n = 0;
static char *sd_note[3] = { "O", "A", "E" };


void
sv_head_load()
{
  in_gauge = sv_load_image("demandg.xpm");
  in_go = sv_load_image("simcityg.xpm");
  in_stop = sv_load_image("simcitys.xpm");
  in_rgb_r = sv_alloc_color(0, 255, 0);
  in_rgb_c = sv_alloc_color(0, 0, 255);
  in_rgb_i = SIMCOLOR_YELLOW;
}


/* One R, C or I bar: Tk "coords x0 y0 x1 y1" with a black outline. */
static void
in_bar(x0, v, c)
  int x0, v, c;
{
  int y0, y1, t;

  if (v == 0)
    return;
  y0 = (v <= 0) ? 32 : 24;		/* from the DUX UISetDemand */
  y1 = y0 - v;
  if (y1 < y0) {
    t = y0;
    y0 = y1;
    y1 = t;
  }
  sv_fill(in_buf, in_dx + x0, in_dy + y0, 7, y1 - y0 + 1, SIMCOLOR_BLACK);
  if (SvColor && y1 - y0 > 1)
    sv_fill(in_buf, in_dx + x0 + 1, in_dy + y0 + 1, 5, y1 - y0 - 1, c);
}


static void
in_draw()
{
  int x0, gx, gw, fh;
  Pixrect *sub;
  SvImage *pic;

  if (in_w <= 0 || in_h <= 0)
    return;
  if (in_buf == NULL || in_buf->pr_width != in_w ||
      in_buf->pr_height != in_h) {
    if (in_buf != NULL)
      pr_destroy(in_buf);
    in_buf = mem_create(in_w, in_h, SvColor ? 8 : 1);
  }
  sv_fill(in_buf, 0, 0, in_w, in_h, SvUiBack);
  sv_bevel2(in_buf, 0, 0, in_w, in_h, 1);

  /* date and funds, 17 characters wide like the DUX labels */
  fh = sv_font_height(SvBoldFont);
  sv_text(in_buf, 9, 12, SvUiText, SvBoldFont, in_date);
  sv_text(in_buf, 9, 12 + fh + 8, SvUiText, SvBoldFont, in_funds);
  x0 = 9 + 17 * sv_text_width(SvBoldFont, "n");

  /* the sunken frame with the graph and the demand gauge */
  sv_bevel2(in_buf, x0, 5, in_w - 5 - x0, DM_H + 4, 0);
  in_dx = in_w - 7 - DM_W;
  in_dy = 7;
  gx = x0 + 2;
  gw = in_dx - gx;
  if (gw > 8) {
    sub = pr_region(in_buf, gx, 7, gw, DM_H);
    if (sub != NULL) {
      sv_graph_draw(sub, gw, DM_H, 10, 7, SIMCOLOR_LIGHTGRAY);
      pr_destroy(sub);
    }
  }
  sv_fill(in_buf, in_dx, in_dy, DM_W, DM_H,
	  SvColor ? SIMCOLOR_LIGHTGRAY : SIMCOLOR_WHITE);
  if (in_gauge != NULL)
    sv_draw_image(in_buf, in_dx, in_dy + 4, in_gauge);
  in_bar(8, in_r, in_rgb_r);
  in_bar(17, in_c, in_rgb_c);
  in_bar(26, in_i, in_rgb_i);
  pic = in_running ? in_go : in_stop;
  if (pic == NULL)
    pic = in_stop;
  if (pic != NULL)
    sv_draw_image(in_buf, in_dx + 41, in_dy + 4, pic);
}


static void
in_blit()
{
  if (in_buf != NULL)
    pw_rop(in_pw, 0, 0, in_w, in_h, PIX_SRC, in_buf, 0, 0);
}


static void
in_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (!SvReady) {
    pw_writebackground(pw, 0, 0, in_w, in_h, PIX_SRC);
    return;
  }
  in_draw();
  in_blit();
}


static void
in_resize(canvas, w, h)
  Canvas canvas;
  int w, h;
{
  in_w = w;
  in_h = h;
  in_dirty = 1;
}


/* The DUX SayDemands: the three notes, pitched by the demands. */
static void
say_demands()
{
  int i, v;

  for (i = 0; i < 3; i++) {
    v = (i == 0) ? in_r : (i == 1) ? in_c : in_i;
    sd_speed[i] = 100 + v * 3;
    sd_at[i] = SvTicks + i * 4;
  }
  sd_n = 3;
}


static void
in_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int x = event_x(event) - in_dx, y = event_y(event) - in_dy;
  int id = event_id(event);

  if (!SvReady)
    return;
  if ((id == MS_LEFT || id == MS_MIDDLE || id == MS_RIGHT) &&
      event_is_down(event)) {
    if (x >= 41 && x < 41 + 37 && y >= 4 && y < 4 + 47)
      sv_toggle_pause();
    else if (x >= 0 && x < 40 && y >= 4 && y < 4 + 47)
      say_demands();
    return;
  }
  if (event_is_ascii(event) ||
      (id >= KEY_RIGHT(1) && id <= KEY_RIGHT(15)))
    sv_editor_key(event);
}


/* ==== the message log ==================================================== */

#define LM_MAX		120		/* messages kept */

static Canvas lg_canvas = NULL;
static Scrollbar lg_sb = NULL;
static Pixwin *lg_pw = NULL;
static char *lm_text[LM_MAX];
static char lm_tag[LM_MAX];
static short lm_lines[LM_MAX];		/* lines at the current width */
static int lm_n = 0;
static int lg_w = 0;			/* width the lines are wrapped to */
static int lg_vw = 0, lg_vh = 0;	/* the visible part */
static int lg_lh = 14;			/* line height */
static int lg_total = 0;		/* content height */
static int lg_ch = 0;			/* canvas height */

/* for lg_line */
static int lg_y, lg_top, lg_bot, lg_color;


static void
lg_line(line, n, arg)
  char *line;
  int n;
  char *arg;
{
  int y = lg_y + n * lg_lh;

  if (y + lg_lh > lg_top && y < lg_bot)
    sv_text((Pixrect *)arg, LG_PAD, y - lg_top, lg_color, SvBoldFont, line);
}


static int
lg_color_of(tag)
  int tag;
{
  if (tag == SV_TAG_ALERT && SvColor)
    return SIMCOLOR_RED;
  return SvUiText;
}


/* Draw canvas rows top..bot (canvas coordinates). */
static void
lg_paint(top, bot)
  int top, bot;
{
  Pixrect *pr;
  int i, y, w;

  if (lg_pw == NULL || bot <= top)
    return;
  w = (lg_vw > 0) ? lg_vw : lg_w;
  if (w <= 0)
    return;
  pr = mem_create(w, bot - top, SvColor ? 8 : 1);
  if (pr == NULL)
    return;
  sv_fill(pr, 0, 0, w, bot - top, SvColor ? SvUiLight : SIMCOLOR_WHITE);
  lg_top = top;
  lg_bot = bot;
  y = LG_PAD;
  for (i = 0; i < lm_n; i++) {
    if (y < bot && y + lm_lines[i] * lg_lh > top) {
      lg_y = y;
      lg_color = lg_color_of(lm_tag[i]);
      sv_wrap(SvBoldFont, lm_text[i], lg_w - 2 * LG_PAD, lg_line, (char *)pr);
    }
    y += lm_lines[i] * lg_lh;
  }
  pw_rop(lg_pw, 0, top, w, bot - top, PIX_SRC, pr, 0, 0);
  pr_destroy(pr);
}


static void
lg_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  int top = area->rl_bound.r_top;
  int bot = top + area->rl_bound.r_height;

  if (!SvReady) {
    pw_writebackground(pw, area->rl_bound.r_left, top,
		       area->rl_bound.r_width, area->rl_bound.r_height,
		       PIX_SRC);
    return;
  }
  lg_paint(top, bot);
}


/* Size the canvas to the text and show the end of it. */
static void
lg_scroll_end()
{
  int ch, start;

  ch = lg_total;
  if (ch < lg_vh)
    ch = lg_vh;
  if (ch != lg_ch) {
    lg_ch = ch;
    window_set(lg_canvas, CANVAS_HEIGHT, ch, 0);
  }
  start = ch - lg_vh;
  if (start < 0)
    start = 0;
  if (lg_sb != NULL)
    scrollbar_scroll_to(lg_sb, (long)start);
}


/* Wrap every message again (the width changed or messages were dropped). */
static void
lg_relayout()
{
  int i;

  lg_total = 2 * LG_PAD;
  for (i = 0; i < lm_n; i++) {
    lm_lines[i] = sv_wrap(SvBoldFont, lm_text[i], lg_w - 2 * LG_PAD,
			  (void (*)())NULL, (char *)NULL);
    lg_total += lm_lines[i] * lg_lh;
  }
}


/* Notice a change of the visible size (the frame was resized). */
static void
lg_check_size()
{
  int vw, vh, t;

  if (lg_canvas == NULL)
    return;
  vw = (int)window_get(lg_canvas, WIN_WIDTH);
  if (lg_sb != NULL) {
    t = (int)scrollbar_get(lg_sb, SCROLL_THICKNESS);
    vw -= t;
  }
  vh = (int)window_get(lg_canvas, WIN_HEIGHT);
  if (vw < 40)
    vw = 40;
  if (vw == lg_vw && vh == lg_vh)
    return;
  lg_vw = vw;
  lg_vh = vh;
  if (lg_w != vw) {
    lg_w = vw;
    window_set(lg_canvas, CANVAS_WIDTH, vw, 0);
    lg_relayout();
  }
  lg_ch = -1;
  lg_scroll_end();
  lg_paint(0, lg_ch);
}


/* Add a line to the log (the DUX appendWithTag). */
void
sv_log_message(str, tag)
  char *str;
  int tag;
{
  int i, drop, oldtotal;
  char *s;

  if (str == NULL || *str == 0)
    return;
  if (lm_n >= LM_MAX) {
    drop = LM_MAX / 5;
    for (i = 0; i < drop; i++)
      free(lm_text[i]);
    for (i = drop; i < lm_n; i++) {
      lm_text[i - drop] = lm_text[i];
      lm_tag[i - drop] = lm_tag[i];
      lm_lines[i - drop] = lm_lines[i];
    }
    lm_n -= drop;
    lg_relayout();
    drop = 1;				/* everything moved */
  } else {
    drop = 0;
  }
  s = (char *)malloc((unsigned)strlen(str) + 1);
  if (s == NULL)
    return;
  strcpy(s, str);
  i = lm_n++;
  lm_text[i] = s;
  lm_tag[i] = tag;
  lm_lines[i] = (lg_w > 0) ?
    sv_wrap(SvBoldFont, s, lg_w - 2 * LG_PAD, (void (*)())NULL,
	    (char *)NULL) : 1;
  oldtotal = lg_total;
  if (lg_total == 0)
    lg_total = 2 * LG_PAD;
  lg_total += lm_lines[i] * lg_lh;
  if (!SvReady || lg_canvas == NULL || lg_w <= 0)
    return;
  lg_scroll_end();
  if (drop || oldtotal < LG_PAD)
    lg_paint(0, lg_ch);
  else
    lg_paint(oldtotal - LG_PAD, lg_total);
}


/* Engine hook (SetMessageField) and the DUX UISetMessage: the log gets the
 * message, the editor shows it over the city. */
void
sv_set_message(str)
  char *str;
{
  if (!DoMessages)
    return;
  sv_log_message(str, SV_TAG_STATUS);
  sv_editor_message(str);
}


static void
lg_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int id = event_id(event);

  if (event_is_ascii(event) ||
      (id >= KEY_RIGHT(1) && id <= KEY_RIGHT(15)))
    sv_editor_key(event);
}


/* ==== the window ========================================================= */

/* The screen's depth, from the info canvas: 1 on a black and white
 * screen. */
int
sv_head_depth()
{
  return in_pw != NULL ? in_pw->pw_pixrect->pr_depth : 1;
}


void
sv_head_create()
{
  int w = sv_bar_width(hd_items);	/* wide enough for the whole bar */

  if (w < HD_W)
    w = HD_W;
  hd_bar = sv_bar_create(SvFrame, w, hd_items, (void (*)())NULL);

  in_canvas = window_create(SvFrame, CANVAS,
			    WIN_X, 0,
			    WIN_BELOW, hd_bar->canvas,
			    WIN_WIDTH, w,
			    WIN_HEIGHT, HD_INFO_H,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, in_repaint,
			    CANVAS_RESIZE_PROC, in_resize,
			    WIN_EVENT_PROC, in_event,
			    0);
  window_set(in_canvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  in_pw = canvas_pixwin(in_canvas);
  in_w = (int)window_get(in_canvas, CANVAS_WIDTH);
  in_h = (int)window_get(in_canvas, CANVAS_HEIGHT);

  lg_lh = sv_font_height(SvBoldFont) + 1;
  lg_sb = scrollbar_create(SCROLL_LINE_HEIGHT, lg_lh, 0);
  lg_canvas = window_create(SvFrame, CANVAS,
			    WIN_X, 0,
			    WIN_BELOW, in_canvas,
			    WIN_WIDTH, w,
			    WIN_HEIGHT, HD_LOG_H,
			    WIN_VERTICAL_SCROLLBAR, lg_sb,
			    CANVAS_AUTO_SHRINK, FALSE,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, lg_repaint,
			    WIN_EVENT_PROC, lg_event,
			    0);
  window_set(lg_canvas,
	     WIN_CONSUME_KBD_EVENTS,
	       WIN_ASCII_EVENTS, WIN_UP_ASCII_EVENTS, WIN_RIGHT_KEYS,
	       WIN_UP_EVENTS, 0,
	     0);
  lg_pw = canvas_pixwin(lg_canvas);

  window_fit(SvFrame);
  /* from now on the log takes up any new room */
  window_set(hd_bar->canvas, WIN_WIDTH, WIN_EXTEND_TO_EDGE, 0);
  window_set(in_canvas, WIN_WIDTH, WIN_EXTEND_TO_EDGE, 0);
  window_set(lg_canvas, WIN_WIDTH, WIN_EXTEND_TO_EDGE,
	     WIN_HEIGHT, WIN_EXTEND_TO_EDGE, 0);
}


void
sv_head_cms()
{
  sv_bar_cms(hd_bar);
  sv_set_cms(in_pw);
  sv_set_cms(lg_pw);
}


void
sv_head_redraw()
{
  hd_dirty = 1;
}


/* Per tick. */
void
sv_head_update()
{
  int i;

  if (!SvReady || in_pw == NULL)
    return;
  if (lg_w == 0 || (SvTicks % 10) == 0)
    lg_check_size();
  if (SvGraphStamp != in_stamp) {
    in_stamp = SvGraphStamp;
    in_dirty = 1;
  }
  if (hd_dirty) {
    sv_bar_redraw(hd_bar);
    hd_dirty = 0;
    in_dirty = 1;
  }
  if (in_dirty) {
    in_draw();
    in_blit();
    in_dirty = 0;
  }
  for (i = 0; i < sd_n; i++)
    if (sd_at[i] >= 0 && SvTicks >= sd_at[i]) {
      sv_play_sound_speed(sd_note[i], sd_speed[i]);
      sd_at[i] = -1;
    }
}


void
sv_head_shutdown()
{
  int i;

  for (i = 0; i < lm_n; i++)
    free(lm_text[i]);
  lm_n = 0;
}


/* ==== engine hooks (w_update.c, w_util.c) ================================ */

void
sv_set_funds(str)
  char *str;
{
  strncpy(in_funds, str, sizeof(in_funds) - 1);
  in_dirty = 1;
  sv_budget_changed();
}


void
sv_set_date(str, m, y)
  char *str;
  int m, y;
{
  strncpy(in_date, str, sizeof(in_date) - 1);
  in_dirty = 1;
}


void
sv_set_demand(r, c, i)
  int r, c, i;
{
  if (r != in_r || c != in_c || i != in_i) {
    in_r = r;
    in_c = c;
    in_i = i;
    in_dirty = 1;
  }
}


/* The DUX UISetSpeed and UIUpdateRunning. */
void
sv_set_speed(speed)
  int speed;
{
  static char *said[4] = {
    "Time pauses.", "Time flows slow.", "Time flows medium.",
    "Time flows fast."
  };
  static int last = -1;

  in_running = (speed != 0);
  in_dirty = 1;
  if (speed >= 0 && speed <= 3 && speed != last && SvTicks > 0)
    sv_set_message(said[speed]);
  last = speed;
}


void
sv_set_game_level(level)
  int level;
{
  hd_dirty = 1;
}


void
sv_set_city_name(name)
  char *name;
{
  sv_editor_title();
}


void
sv_set_options(options)
  int options;
{
}
