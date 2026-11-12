/* sv_dialogs.c -- alerts, the notice window, the budget and evaluation
 * windows, and the load / save dialogs.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The notices (scenario descriptions, disasters, milestones, the zone query)
 * are the DUX ones: their texts are read at run time from the "Message"
 * lines of $SIMHOME/res/simcity.tcl.  A notice about a place shows a small
 * live view of it, like the DUX notice window.  The budget window uses
 * native SunView panel sliders, the evaluation is drawn on a canvas in the
 * DUX layout, and loading and saving use small modal frames (window_loop).
 */

#include "sv.h"
#include <dirent.h>

extern char *getcwd();

static void nt_draw();
static void ev_draw();


static char *
dl_strdup(s)
  char *s;
{
  char *d = (char *)malloc((unsigned)strlen(s) + 1);

  strcpy(d, s);
  return d;
}


/* ==== alerts ============================================================= */

#define AL_LINES	14
#define AL_COLS		64

static int
al_box(msg, yes, no)
  char *msg, *yes, *no;
{
  static char lines[AL_LINES][AL_COLS + 1];
  char *ptrs[AL_LINES + 1];
  int n = 0, len, cut, result;
  Event ev;

  if (SvFrame == NULL || SvTicks == 0) {	/* not on the screen yet */
    fprintf(stderr, "micropolis: %s\n", msg);
    return no == NULL;
  }
  while (*msg && n < AL_LINES) {
    len = 0;
    while (msg[len] && msg[len] != '\n' && len < AL_COLS)
      len++;
    cut = len;
    if (msg[len] && msg[len] != '\n') {	/* too long: break at a space */
      while (cut > 0 && msg[cut] != ' ')
	cut--;
      if (cut == 0)
	cut = len;
    }
    strncpy(lines[n], msg, cut);
    lines[n][cut] = 0;
    ptrs[n] = lines[n];
    n++;
    msg += cut;
    while (*msg == ' ')
      msg++;
    if (*msg == '\n')
      msg++;
  }
  ptrs[n] = NULL;

  bzero((char *)&ev, sizeof(ev));
  SvBusy++;
  if (no != NULL)
    result = alert_prompt(SvFrame, &ev,
			  ALERT_MESSAGE_STRINGS_ARRAY_PTR, ptrs,
			  ALERT_BUTTON_YES, yes,
			  ALERT_BUTTON_NO, no,
			  ALERT_POSITION, ALERT_SCREEN_CENTERED,
			  0);
  else
    result = alert_prompt(SvFrame, &ev,
			  ALERT_MESSAGE_STRINGS_ARRAY_PTR, ptrs,
			  ALERT_BUTTON_YES, yes,
			  ALERT_POSITION, ALERT_SCREEN_CENTERED,
			  0);
  SvBusy--;
  if (result == ALERT_FAILED) {
    fprintf(stderr, "micropolis: %s\n", n > 0 ? ptrs[0] : "");
    return no == NULL;
  }
  return result == ALERT_YES;
}


/* An OK box. */
int
sv_alert(msg)
  char *msg;
{
  al_box(msg, "OK", (char *)NULL);
  return 1;
}


/* A Yes / No box; 1 for yes. */
int
sv_confirm(msg)
  char *msg;
{
  return al_box(msg, "Yes", "No");
}


/* A question with the DUX answers ("I guess so.", "No way!"); 1 for yes. */
int
sv_ask(msg, yes, no)
  char *msg, *yes, *no;
{
  return al_box(msg, yes, no);
}


/* ==== notices ============================================================ */

typedef struct Notice {
  int id;
  char *color;				/* "#rrggbb" */
  char *title;
  char *body;
  char *props;				/* Tcl keyed list: view, left, ... */
} Notice;

#define NT_MAX		64

static Notice nt_tab[NT_MAX];
static int nt_count = 0;
static int nt_loaded = 0;

static char tw_buf[8192];


/* Read the next Tcl word of a command at *pp: {braced} (nested; a
 * backslash-newline becomes a space), "quoted" (\n, \t, \", \\ and
 * backslash-newline) or bare.  Returns a malloc'd copy, or NULL at the end
 * of the command (a newline or ';' outside any word). */
static char *
tcl_word(pp)
  char **pp;
{
  char *p = *pp;
  int n = 0, depth;
  int wmax = sizeof(tw_buf) - 2;

  for (;;) {
    if (*p == ' ' || *p == '\t')
      p++;
    else if (p[0] == '\\' && p[1] == '\n')
      p += 2;
    else
      break;
  }
  if (*p == 0 || *p == '\n' || *p == ';') {
    *pp = p;
    return NULL;
  }

  if (*p == '{') {
    depth = 1;
    p++;
    while (*p) {
      if (p[0] == '\\' && p[1] == '\n') {
	p += 2;
	while (*p == ' ' || *p == '\t')
	  p++;
	if (n < wmax)
	  tw_buf[n++] = ' ';
	continue;
      }
      if (p[0] == '\\' && p[1]) {	/* kept as is inside braces */
	if (n < wmax - 1) {
	  tw_buf[n++] = p[0];
	  tw_buf[n++] = p[1];
	}
	p += 2;
	continue;
      }
      if (*p == '{')
	depth++;
      else if (*p == '}' && --depth == 0) {
	p++;
	break;
      }
      if (n < wmax)
	tw_buf[n++] = *p;
      p++;
    }
  } else if (*p == '"') {
    p++;
    while (*p && *p != '"') {
      if (p[0] == '\\' && p[1]) {
	p++;
	if (*p == 'n') {
	  if (n < wmax)
	    tw_buf[n++] = '\n';
	  p++;
	} else if (*p == 't') {
	  if (n < wmax)
	    tw_buf[n++] = '\t';
	  p++;
	} else if (*p == '\n') {
	  p++;
	  while (*p == ' ' || *p == '\t')
	    p++;
	  if (n < wmax)
	    tw_buf[n++] = ' ';
	} else {
	  if (n < wmax)
	    tw_buf[n++] = *p;
	  p++;
	}
	continue;
      }
      if (n < wmax)
	tw_buf[n++] = *p;
      p++;
    }
    if (*p == '"')
      p++;
  } else {
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != ';' &&
	   !(p[0] == '\\' && p[1] == '\n')) {
      if (n < wmax)
	tw_buf[n++] = *p;
      p++;
    }
  }
  tw_buf[n] = 0;
  *pp = p;
  return dl_strdup(tw_buf);
}


/* Collect the "Message id color title body ?props?" commands. */
static void
nt_load()
{
  char path[700], *buf, *p, *w[5];
  int n, i;

  nt_loaded = 1;
  sprintf(path, "%s/simcity.tcl", ResourceDir);
  if ((buf = sv_read_file(path, (int *)NULL)) == NULL) {
    fprintf(stderr, "micropolis: cannot read %s (no notices)\n", path);
    return;
  }
  for (p = buf; *p; ) {
    if (strncmp(p, "Message ", 8) == 0 && isdigit(p[8]) && nt_count < NT_MAX) {
      p += 8;
      for (n = 0; n < 5; n++)
	if ((w[n] = tcl_word(&p)) == NULL)
	  break;
      if (n >= 4) {
	nt_tab[nt_count].id = atoi(w[0]);
	nt_tab[nt_count].color = w[1];
	nt_tab[nt_count].title = w[2];
	nt_tab[nt_count].body = w[3];
	nt_tab[nt_count].props = (n > 4) ? w[4] : dl_strdup("");
	nt_count++;
	free(w[0]);
      } else {
	for (i = 0; i < n; i++)
	  free(w[i]);
      }
    }
    while (*p && *p != '\n')
      p++;
    if (*p)
      p++;
  }
  free(buf);
  sv_log("%d notices from %s\n", (char *)nt_count, path);
}


static Notice *
nt_find(id)
  int id;
{
  int i;

  if (!nt_loaded)
    nt_load();
  for (i = 0; i < nt_count; i++)
    if (nt_tab[i].id == id)
      return &nt_tab[i];
  return NULL;
}


/* ---- the notice window --------------------------------------------------- */

#define NT_W		440
#define NT_PAD		8
#define NT_VT		9		/* the little view: 9 x 9 tiles */
#define NT_VW		(NT_VT * 16)

static Frame nt_frame = NULL;
static Canvas nt_canvas = NULL;
static Pixwin *nt_pw = NULL;
static Pixrect *nt_buf = NULL;
static int nt_h = 0;			/* canvas height */
static int nt_shown = 0;
static int nt_have = 0;			/* something to show again */
static int nt_band = 30;		/* the title */
static int nt_bh = 24;			/* the Dismiss button */

static char nt_title[200];
static char nt_text[4096];
static int nt_color = 0;
static SvImage *nt_pic[3];		/* left, middle, right pictures */
static int nt_vx = -1, nt_vy = -1;	/* center tile of the view; -1 none */
static int nt_follow = 0;		/* sprite type the view follows */
static int nt_tx, nt_ty, nt_lh;		/* text position for nt_line */
static int nt_view_x, nt_view_y;	/* where the view is drawn */


/* Body text with %s replaced by parms, tabs expanded, ends trimmed. */
static void
nt_format(body, parms, nparms)
  char *body;
  char **parms;
  int nparms;
{
  char *o = nt_text, *e = nt_text + sizeof(nt_text) - 1, *s;
  int col = 0, k = 0;

  while (*body == '\n' || *body == ' ')
    body++;
  for (; *body && o < e; body++) {
    if (body[0] == '%' && body[1] == 's') {
      s = (parms != NULL && k < nparms && parms[k] != NULL) ? parms[k] : "";
      k++;
      while (*s && o < e) {
	*o++ = *s++;
	col++;
      }
      body++;
    } else if (body[0] == '%' && body[1] == '%') {
      *o++ = '%';
      col++;
      body++;
    } else if (*body == '\t') {
      do {
	*o++ = ' ';
	col++;
      } while ((col & 7) && o < e);
    } else {
      *o++ = *body;
      col = (*body == '\n') ? 0 : col + 1;
    }
  }
  while (o > nt_text && (o[-1] == '\n' || o[-1] == ' '))
    o--;
  *o = 0;
}


static int
nt_parse_color(s)
  char *s;
{
  int r, g, b;

  if (s == NULL || s[0] != '#' || strlen(s) != 7 ||
      sscanf(s + 1, "%2x%2x%2x", &r, &g, &b) != 3)
    return SvUiLight;
  return sv_alloc_color(r, g, b);
}


/* "{middle {@images/key2city.xpm}}" -> the image */
static SvImage *
nt_prop_image(props, key)
  char *props, *key;
{
  char name[64], *p;
  int n = strlen(key), i;

  for (p = props; *p; p++) {
    if (strncmp(p, key, n) == 0 && strncmp(p + n, " {@images/", 10) == 0) {
      p += n + 10;
      for (i = 0; p[i] && p[i] != '}' && i < (int)sizeof(name) - 1; i++)
	name[i] = p[i];
      name[i] = 0;
      return sv_load_image(name);
    }
  }
  return NULL;
}


/* Where a notice is about, as the DUX "view" properties say. */
static void
nt_where(id, xp, yp, followp)
  int id;
  int *xp, *yp, *followp;
{
  int x = -1, y = -1, f = 0;

  switch (id) {
  case 10:
    x = PolMaxX; y = PolMaxY;
    break;
  case 11:
    x = CrimeMaxX; y = CrimeMaxY;
    break;
  case 12:
    x = TrafMaxX; y = TrafMaxY;
    break;
  case 20: case 24: case 25: case 26: case 27: case 30:
    x = CrashX; y = CrashY;
    break;
  case 21:
    f = GOD;
    break;
  case 22:
    f = TOR;
    break;
  case 41:
    f = COP;
    break;
  case 23: case 35: case 36: case 37: case 38: case 39:
    x = CCx; y = CCy;
    break;
  case 42:
    x = FloodX; y = FloodY;
    break;
  case 43:
    x = MeltX; y = MeltY;
    break;
  }
  if (x >= 0 && (x != 0 || y != 0)) {
    *xp = x;
    *yp = y;
  }
  *followp = f;
}


static void
nt_follow_update()
{
  SimSprite *s;

  if (!nt_follow)
    return;
  if ((s = GetSprite(nt_follow)) != NULL && s->frame != 0) {
    nt_vx = (s->x + s->x_hot) >> 4;
    nt_vy = (s->y + s->y_hot) >> 4;
  }
}


static void
nt_line(line, n, arg)
  char *line;
  int n;
  char *arg;
{
  sv_text(nt_buf, nt_tx, nt_ty + n * nt_lh, SIMCOLOR_BLACK, SvBigFont, line);
}


/* The little live view: tiles and sprites around (nt_vx, nt_vy). */
static void
nt_draw_view()
{
  Pixrect *sub;
  SimSprite *s;
  SvImage *img;
  int cx, cy, col, row, tile, x, y, x0, y0;

  x0 = nt_view_x;
  y0 = nt_view_y;
  sv_bevel2(nt_buf, x0 - 2, y0 - 2, NT_VW + 4, NT_VW + 4, 0);
  cx = nt_vx - NT_VT / 2;
  cy = nt_vy - NT_VT / 2;
  if (cx > WORLD_X - NT_VT)
    cx = WORLD_X - NT_VT;
  if (cy > WORLD_Y - NT_VT)
    cy = WORLD_Y - NT_VT;
  if (cx < 0)
    cx = 0;
  if (cy < 0)
    cy = 0;
  if (SvBigTiles == NULL) {
    sv_fill(nt_buf, x0, y0, NT_VW, NT_VW, SIMCOLOR_BROWN);
    return;
  }
  for (row = 0; row < NT_VT; row++)
    for (col = 0; col < NT_VT; col++) {
      tile = Map[cx + col][cy + row] & LOMASK;
      if (tile >= TILE_COUNT)
	tile = 0;
      pr_rop(nt_buf, x0 + (col << 4), y0 + (row << 4), 16, 16, PIX_SRC,
	     SvBigTiles, 0, tile << 4);
    }
  sub = pr_region(nt_buf, x0, y0, NT_VW, NT_VW);
  if (sub == NULL)
    return;
  for (s = sim->sprite; s != NULL; s = s->next) {
    if (s->frame <= 0 || s->type <= 0 || s->type >= OBJN ||
	s->frame > SvSpriteFrames[s->type])
      continue;
    img = SvSprites[s->type][s->frame - 1];
    if (img == NULL)
      continue;
    x = s->x + s->x_offset - (cx << 4);
    y = s->y + s->y_offset - (cy << 4);
    if (x + img->w <= 0 || y + img->h <= 0 || x >= NT_VW || y >= NT_VW)
      continue;
    sv_draw_image(sub, x, y, img);
  }
  pr_destroy(sub);
}


/* Height of the canvas for the current notice. */
static int
nt_measure()
{
  int x, lines, content, pich, i;

  nt_band = sv_font_height(SvHugeFont) + 14;
  nt_bh = sv_font_height(SvBigFont) + 12;
  x = NT_PAD + ((nt_vx >= 0) ? NT_VW + 4 + NT_PAD : 0);
  nt_lh = sv_font_height(SvBigFont) + 1;
  lines = sv_wrap(SvBigFont, nt_text, NT_W - x - 2 * NT_PAD,
		  (void (*)())NULL, (char *)NULL);
  content = lines * nt_lh;
  pich = 0;
  for (i = 0; i < 3; i++)
    if (nt_pic[i] != NULL && nt_pic[i]->h > pich)
      pich = nt_pic[i]->h;
  if (pich)
    content += NT_PAD + pich;
  if (nt_vx >= 0 && content < NT_VW + 4)
    content = NT_VW + 4;
  content = nt_band + NT_PAD + content + NT_PAD + nt_bh;
  return (content < 120) ? 120 : content;
}


/* Laid out like the DUX notice: the title on a band of the notice's
 * color, the view on the left, the text and its pictures on the right,
 * the Dismiss button along the bottom. */
static void
nt_draw()
{
  int w, x, i, tw, bot;
  SvImage *img;

  if (nt_buf == NULL || nt_buf->pr_height != nt_h) {
    if (nt_buf != NULL)
      pr_destroy(nt_buf);
    nt_buf = mem_create(NT_W, nt_h, SvColor ? 8 : 1);
  }
  sv_fill(nt_buf, 0, 0, NT_W, nt_h, SvUiBack);
  sv_bevel2(nt_buf, 0, 0, NT_W, nt_h - nt_bh, 1);

  sv_fill(nt_buf, 4, 4, NT_W - 8, nt_band - 8,
	  SvColor ? nt_color : SIMCOLOR_WHITE);
  sv_bevel2(nt_buf, 4, 4, NT_W - 8, nt_band - 8, 1);
  w = sv_text_width(SvHugeFont, nt_title);
  sv_text(nt_buf, (NT_W - w) / 2, (nt_band - sv_font_height(SvHugeFont)) / 2,
	  SIMCOLOR_BLACK, SvHugeFont, nt_title);

  x = NT_PAD;
  nt_view_x = NT_PAD + 2;
  nt_view_y = nt_band + NT_PAD + 2;
  if (nt_vx >= 0) {
    nt_draw_view();
    x += NT_VW + 4 + NT_PAD;
  }
  nt_tx = x;
  nt_ty = nt_band + NT_PAD;
  sv_wrap(SvBigFont, nt_text, NT_W - x - 2 * NT_PAD, nt_line, (char *)NULL);

  bot = nt_h - nt_bh - NT_PAD;
  for (i = 0; i < 3; i++) {
    if ((img = nt_pic[i]) == NULL)
      continue;
    if (i == 0)
      tw = x;
    else if (i == 1)
      tw = x + (NT_W - NT_PAD - x - img->w) / 2;
    else
      tw = NT_W - NT_PAD - img->w;
    sv_draw_image(nt_buf, tw, bot - img->h, img);
  }

  sv_button(nt_buf, 0, nt_h - nt_bh, NT_W, nt_bh, "Dismiss");
}


static void
nt_blit()
{
  if (nt_buf != NULL && nt_pw != NULL)
    pw_rop(nt_pw, 0, 0, NT_W, nt_h, PIX_SRC, nt_buf, 0, 0);
}


static void
nt_done(frame)
  Frame frame;
{
  window_set(frame, WIN_SHOW, FALSE, 0);
  nt_shown = 0;
}


static void
nt_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (nt_buf != NULL)
    pw_rop(pw, 0, 0, NT_W, nt_h, PIX_SRC, nt_buf, 0, 0);
  else
    pw_writebackground(pw, 0, 0, NT_W, 200, PIX_SRC);
}


/* Dismiss hides the notice; the view (the DUX ComeToMe) brings the
 * editor there. */
static void
nt_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int x = event_x(event), y = event_y(event), id = event_id(event);

  if (!event_is_down(event) ||
      (id != MS_LEFT && id != MS_MIDDLE && id != MS_RIGHT))
    return;
  if (y >= nt_h - nt_bh) {
    nt_done(nt_frame);
  } else if (nt_vx >= 0 && x >= nt_view_x && x < nt_view_x + NT_VW &&
	     y >= nt_view_y && y < nt_view_y + NT_VW) {
    nt_follow_update();
    sv_editor_pan_to(nt_vx, nt_vy);
    sv_show_frame(SvEditorFrame);
  }
}


static void
nt_create()
{
  nt_h = nt_measure();
  nt_frame = window_create(SvFrame, FRAME,
			   FRAME_LABEL, "Micropolis Notice",
			   FRAME_SHOW_LABEL, TRUE,
			   FRAME_DONE_PROC, nt_done,
			   WIN_SHOW, FALSE,
			   0);
  nt_canvas = window_create(nt_frame, CANVAS,
			    WIN_X, 0,
			    WIN_Y, 0,
			    WIN_WIDTH, NT_W,
			    WIN_HEIGHT, nt_h,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, nt_repaint,
			    WIN_EVENT_PROC, nt_event,
			    0);
  window_set(nt_canvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     0);
  nt_pw = canvas_pixwin(nt_canvas);
  sv_set_cms(nt_pw);
  window_fit(nt_frame);
}


/* Show nt_title / nt_text with the given look, in the DUX place: the
 * bottom left corner of the screen. */
static void
nt_show(color, props, vx, vy, follow)
  char *color, *props;
  int vx, vy, follow;
{
  static char *keys[3] = { "left", "middle", "right" };
  int i, h, fh, x, y;

  nt_color = nt_parse_color(color);
  for (i = 0; i < 3; i++) {
    if (nt_pic[i] != NULL)
      sv_free_image(nt_pic[i]);
    nt_pic[i] = (props != NULL) ? nt_prop_image(props, keys[i]) : NULL;
  }
  nt_vx = vx;
  nt_vy = vy;
  nt_follow = follow;
  nt_follow_update();

  if (nt_frame == NULL) {
    nt_create();
  } else {
    /* grow or shrink the frame by hand: a second window_fit() of this
     * frame made it a few pixels wide */
    h = nt_measure();
    if (h != nt_h) {
      fh = (int)window_get(nt_frame, WIN_HEIGHT);
      x = (int)window_get(nt_frame, WIN_X) + (int)window_get(SvFrame, WIN_X);
      y = (int)window_get(nt_frame, WIN_Y) + (int)window_get(SvFrame, WIN_Y);
      window_set(nt_canvas, WIN_HEIGHT, h, 0);
      window_set(nt_frame, WIN_HEIGHT, fh + h - nt_h, 0);
      /* the bottom edge stays put, as with the DUX notice, so a taller
       * notice grows up instead of off the bottom of the screen */
      sv_place(nt_frame, x, y - (h - nt_h));
      nt_h = h;
    }
  }
  nt_draw();
  if (!nt_shown)
    sv_place(nt_frame, 5, SvScreenH - (int)window_get(nt_frame, WIN_HEIGHT)
	     - 5);
  sv_show_frame(nt_frame);
  nt_shown = 1;
  nt_have = 1;
  nt_blit();
}


static void
nt_notice_at(id, parms, nparms, x, y, follow)
  int id;
  char **parms;
  int nparms, x, y, follow;
{
  Notice *n;

  if ((n = nt_find(id)) == NULL) {
    sv_log("no notice %d\n", (char *)id);
    return;
  }
  strncpy(nt_title, n->title, sizeof(nt_title) - 1);
  nt_format(n->body, parms, nparms);
  nt_show(n->color, n->props, x, y, follow);
}


/* A notice by its DUX message number (scenario texts, win, lose). */
void
sv_notice_show(id, parms)
  int id;
  char **parms;
{
  nt_notice_at(id, parms, parms ? 6 : 0, -1, -1, 0);
}


/* Engine hook: a picture message (disaster, milestone, ...). */
void
sv_show_picture(id)
  int id;
{
  int x = -1, y = -1, follow = 0;

  if (!DoNotices)
    return;
  nt_where(id, &x, &y, &follow);
  if (x < 0 && (MesX || MesY)) {	/* a followed sprite overrides it */
    x = MesX;
    y = MesY;
  }
  nt_notice_at(id, (char **)NULL, 0, x, y, follow);
}


/* Engine hook: the query tool's result (DUX message 9). */
void
sv_show_zone_status(str, s0, s1, s2, s3, s4, x, y)
  char *str, *s0, *s1, *s2, *s3, *s4;
  int x, y;
{
  char *parms[6];

  parms[0] = str;
  parms[1] = s0;
  parms[2] = s1;
  parms[3] = s2;
  parms[4] = s3;
  parms[5] = s4;
  nt_notice_at(9, parms, 6, x, y, 0);
}


void
sv_notice_again()
{
  if (!nt_have) {
    sv_alert("There have been no notices yet.");
    return;
  }
  nt_follow_update();
  nt_draw();
  sv_show_frame(nt_frame);
  nt_shown = 1;
  nt_blit();
}


void
sv_about()
{
  strcpy(nt_title, "About Micropolis");
  sprintf(nt_text,
	  "Micropolis for SunView, version %s\n\
Copyright (C) 1989 - 2007 Electronic Arts Inc.\n\
\n\
Free software under the GNU General Public License, version 3 or later, \
with additional terms from Electronic Arts; see the LICENSE file.  No \
trademark or publicity rights are granted.\n\
\n\
Original game design by Will Wright.  Unix version and Tcl/Tk interface \
by Don Hopkins.  This version has a native SunView interface for \
SunOS 4.\n\
\n\
Pictures, sounds and notices are read from the DUX files in:\n\
%s",
	  MicropolisVersion, SvHome);
  nt_show("#ffd700", (char *)NULL, -1, -1, 0);
}


static void
nt_update()
{
  if (!nt_shown || nt_vx < 0 || nt_buf == NULL || (SvTicks % 10) != 0)
    return;
  nt_follow_update();
  nt_draw_view();
  pw_rop(nt_pw, nt_view_x - 2, nt_view_y - 2, NT_VW + 4, NT_VW + 4, PIX_SRC,
	 nt_buf, nt_view_x - 2, nt_view_y - 2);
}


/* ==== budget ============================================================= */

#define BD_PCT(f)	((int)((f) * 100.0 + 0.5))
#define BD_SLIDER_W	200

static Frame bd_frame = NULL;
static Panel bd_panel = NULL;
static Panel_item bd_val[4];		/* collected, flow, previous, current */
static Panel_item bd_tax = NULL, bd_taxlabel = NULL;
static Panel_item bd_fund[3], bd_fundlabel[3];	/* road, fire, police */
static char bd_cache[4 + 1 + 3][100];	/* what the labels show now */
static int bd_shown = 0;
static int bd_saved = 0;
static int bd_old_tax, bd_old_pct[3];


static void
bd_label(item, cache, str)
  Panel_item item;
  char *cache, *str;
{
  if (item == NULL || strcmp(cache, str) == 0)
    return;
  strncpy(cache, str, 99);
  cache[99] = 0;
  panel_set(item, PANEL_LABEL_STRING, cache, 0);
}


static void
bd_slider(item, v)
  Panel_item item;
  int v;
{
  if (item != NULL && (int)panel_get_value(item) != v)
    panel_set_value(item, v);
}


static void
bd_set_fund(which, pct)
  int which, pct;
{
  switch (which) {
  case 0:
    roadPercent = pct / 100.0;
    RoadSpend = (roadMaxValue * pct) / 100;
    break;
  case 1:
    firePercent = pct / 100.0;
    FireSpend = (fireMaxValue * pct) / 100;
    break;
  case 2:
    policePercent = pct / 100.0;
    PoliceSpend = (policeMaxValue * pct) / 100;
    break;
  }
  UpdateFundEffects();
}


static void
bd_slide(item, value, event)
  Panel_item item;
  int value;
  Event *event;
{
  int which = (int)panel_get(item, PANEL_CLIENT_DATA);

  if (which == 3) {
    CityTax = value;
    drawBudgetWindow();
  } else {
    bd_set_fund(which, value);
  }
  Kick();
}


static int
bd_changed()
{
  return CityTax != bd_old_tax || BD_PCT(roadPercent) != bd_old_pct[0] ||
	 BD_PCT(firePercent) != bd_old_pct[1] ||
	 BD_PCT(policePercent) != bd_old_pct[2];
}


static void
bd_reset()
{
  int i;

  CityTax = bd_old_tax;
  for (i = 0; i < 3; i++)
    bd_set_fund(i, bd_old_pct[i]);
  drawBudgetWindow();
  drawCurrPercents();
  sv_set_message("The budget was reset.");
}


static void
bd_continue(say)
  int say;
{
  if (say)
    sv_set_message(bd_changed() ? "The budget was changed."
				: "The budget wasn't changed.");
  bd_saved = 0;
  if (bd_frame != NULL)
    window_set(bd_frame, WIN_SHOW, FALSE, 0);
  bd_shown = 0;
  Resume();
  sv_head_redraw();
}


static void
bd_done(frame)
  Frame frame;
{
  bd_continue(1);
}


static void
bd_button(item, event)
  Panel_item item;
  Event *event;
{
  switch ((int)panel_get(item, PANEL_CLIENT_DATA)) {
  case 0:
    bd_continue(1);
    break;
  case 1:
    bd_reset();
    break;
  case 2:
    bd_reset();
    bd_continue(0);
    break;
  }
}


static void
bd_create()
{
  static char *names[4] = {
    "Taxes Collected:", "Cash Flow:", "Previous Funds:", "Current Funds:"
  };
  static char *funds[3] = { "Road Fund:", "Fire Fund:", "Police Fund:" };
  Pixfont *pf;
  Panel_item it;
  Rect *r;
  int cw, ch, i, x, vx;

#define BX(c)	((c) * cw + 10)
#define BY(n)	((n) * (ch + 12) + 10)

  bd_frame = window_create(SvFrame, FRAME,
			   FRAME_LABEL, "Micropolis Budget",
			   FRAME_SHOW_LABEL, TRUE,
			   FRAME_DONE_PROC, bd_done,
			   WIN_SHOW, FALSE,
			   0);
  bd_panel = window_create(bd_frame, PANEL, 0);
  pf = (Pixfont *)window_get(bd_panel, WIN_FONT);
  cw = pf->pf_defaultsize.x;
  ch = pf->pf_defaultsize.y;
  vx = BX(18);

  panel_create_item(bd_panel, PANEL_MESSAGE,
		    PANEL_ITEM_X, BX(0), PANEL_ITEM_Y, BY(0),
		    PANEL_LABEL_STRING,
		      "The simulation waits while you set the budget.",
		    0);
  for (i = 0; i < 4; i++) {
    panel_create_item(bd_panel, PANEL_MESSAGE,
		      PANEL_ITEM_X, BX(0), PANEL_ITEM_Y, BY(i + 1),
		      PANEL_LABEL_STRING, names[i],
		      PANEL_LABEL_BOLD, TRUE,
		      0);
    bd_val[i] = panel_create_item(bd_panel, PANEL_MESSAGE,
				  PANEL_ITEM_X, vx, PANEL_ITEM_Y, BY(i + 1),
				  PANEL_LABEL_STRING, "$0",
				  0);
    strcpy(bd_cache[i], "$0");
  }

  bd_tax = panel_create_item(bd_panel, PANEL_SLIDER,
			     PANEL_ITEM_X, BX(0), PANEL_ITEM_Y, BY(6),
			     PANEL_LABEL_STRING, "Tax Rate:",
			     PANEL_LABEL_BOLD, TRUE,
			     PANEL_VALUE_X, vx,
			     PANEL_MIN_VALUE, 0,
			     PANEL_MAX_VALUE, 20,
			     PANEL_VALUE, CityTax,
			     PANEL_SLIDER_WIDTH, BD_SLIDER_W,
			     PANEL_SHOW_VALUE, FALSE,
			     PANEL_SHOW_RANGE, FALSE,
			     PANEL_NOTIFY_LEVEL, PANEL_ALL,
			     PANEL_NOTIFY_PROC, bd_slide,
			     PANEL_CLIENT_DATA, 3,
			     0);
  bd_taxlabel = panel_create_item(bd_panel, PANEL_MESSAGE,
				  PANEL_ITEM_X, vx + BD_SLIDER_W + 3 * cw,
				  PANEL_ITEM_Y, BY(6),
				  PANEL_LABEL_STRING, "0%",
				  0);
  strcpy(bd_cache[4], "0%");

  for (i = 0; i < 3; i++) {
    bd_fund[i] = panel_create_item(bd_panel, PANEL_SLIDER,
				   PANEL_ITEM_X, BX(0),
				   PANEL_ITEM_Y, BY(7 + i),
				   PANEL_LABEL_STRING, funds[i],
				   PANEL_LABEL_BOLD, TRUE,
				   PANEL_VALUE_X, vx,
				   PANEL_MIN_VALUE, 0,
				   PANEL_MAX_VALUE, 100,
				   PANEL_VALUE, 100,
				   PANEL_SLIDER_WIDTH, BD_SLIDER_W,
				   PANEL_SHOW_VALUE, FALSE,
				   PANEL_SHOW_RANGE, FALSE,
				   PANEL_NOTIFY_LEVEL, PANEL_ALL,
				   PANEL_NOTIFY_PROC, bd_slide,
				   PANEL_CLIENT_DATA, i,
				   0);
    bd_fundlabel[i] = panel_create_item(bd_panel, PANEL_MESSAGE,
					PANEL_ITEM_X,
					  vx + BD_SLIDER_W + 3 * cw,
					PANEL_ITEM_Y, BY(7 + i),
					PANEL_LABEL_STRING,
					  "100% of $0 = $0",
					0);
    strcpy(bd_cache[5 + i], "100% of $0 = $0");
  }

  x = BX(0);
  it = panel_create_item(bd_panel, PANEL_BUTTON,
			 PANEL_ITEM_X, x, PANEL_ITEM_Y, BY(11),
			 PANEL_LABEL_IMAGE,
			   panel_button_image(bd_panel,
					      "Continue With These Figures", 0,
					      (Pixfont *)0),
			 PANEL_NOTIFY_PROC, bd_button,
			 PANEL_CLIENT_DATA, 0,
			 0);
  r = (Rect *)panel_get(it, PANEL_ITEM_RECT);
  x += r->r_width + 2 * cw;
  it = panel_create_item(bd_panel, PANEL_BUTTON,
			 PANEL_ITEM_X, x, PANEL_ITEM_Y, BY(11),
			 PANEL_LABEL_IMAGE,
			   panel_button_image(bd_panel,
					      "Reset to Original Figures", 0,
					      (Pixfont *)0),
			 PANEL_NOTIFY_PROC, bd_button,
			 PANEL_CLIENT_DATA, 1,
			 0);
  r = (Rect *)panel_get(it, PANEL_ITEM_RECT);
  x += r->r_width + 2 * cw;
  panel_create_item(bd_panel, PANEL_BUTTON,
		    PANEL_ITEM_X, x, PANEL_ITEM_Y, BY(11),
		    PANEL_LABEL_IMAGE,
		      panel_button_image(bd_panel,
					 "Cancel Changes and Continue", 0,
					 (Pixfont *)0),
		    PANEL_NOTIFY_PROC, bd_button,
		    PANEL_CLIENT_DATA, 2,
		    0);
  window_fit(bd_panel);
  window_fit(bd_frame);
  sv_place(bd_frame, 5, 5);		/* the DUX place, over the controls */
#undef BX
#undef BY
}


/* Engine hook: SetBudget (w_budget.c). */
void
sv_set_budget(flow, previous, current, collected, tax)
  char *flow, *previous, *current, *collected;
  int tax;
{
  char buf[32];

  if (bd_frame == NULL)
    return;
  bd_label(bd_val[0], bd_cache[0], collected);
  bd_label(bd_val[1], bd_cache[1], flow);
  bd_label(bd_val[2], bd_cache[2], previous);
  bd_label(bd_val[3], bd_cache[3], current);
  sprintf(buf, "%d%%", tax);
  bd_label(bd_taxlabel, bd_cache[4], buf);
  bd_slider(bd_tax, tax);
}


static void
bd_fund_show(i, got, want, pct)
  int i;
  char *got, *want;
  int pct;
{
  char buf[100];

  sprintf(buf, "%d%% of %s = %s", pct, want, got);
  bd_label(bd_fundlabel[i], bd_cache[5 + i], buf);
  bd_slider(bd_fund[i], pct);
}


/* Engine hook: SetBudgetValues (w_budget.c). */
void
sv_set_budget_values(roadGot, roadWant, roadPct, policeGot, policeWant,
		     policePct, fireGot, fireWant, firePct)
  char *roadGot, *roadWant;
  int roadPct;
  char *policeGot, *policeWant;
  int policePct;
  char *fireGot, *fireWant;
  int firePct;
{
  if (bd_frame == NULL)
    return;
  bd_fund_show(0, roadGot, roadWant, roadPct);
  bd_fund_show(1, fireGot, fireWant, firePct);
  bd_fund_show(2, policeGot, policeWant, policePct);
}


/* Engine hook: UpdateBudget; the next UpdateBudgetWindow redraws. */
void
sv_update_budget()
{
}


/* Funds changed (sv_set_funds): the budget shows them too. */
void
sv_budget_changed()
{
  if (bd_shown)
    drawBudgetWindow();
}


/* Engine hook: ShowBudgetWindowAndStartWaiting; the engine pauses after
 * this returns and Continue resumes. */
void
sv_show_budget_and_wait()
{
  if (bd_frame == NULL)
    bd_create();
  if (!bd_saved) {
    bd_old_tax = CityTax;
    bd_old_pct[0] = BD_PCT(roadPercent);
    bd_old_pct[1] = BD_PCT(firePercent);
    bd_old_pct[2] = BD_PCT(policePercent);
    bd_saved = 1;
  }
  drawBudgetWindow();
  drawCurrPercents();
  UpdateBudgetWindow();
  sv_show_frame(bd_frame);
  bd_shown = 1;
  sv_set_message("Pausing to set the budget ...");
}


/* A new game: forget the budget being edited (the DUX OldBudget 0). */
void
sv_budget_reset()
{
  bd_saved = 0;
  if (bd_shown && bd_frame != NULL)
    window_set(bd_frame, WIN_SHOW, FALSE, 0);
  bd_shown = 0;
}


/* Windows menu: Budget. */
void
sv_budget_show()
{
  DoBudgetFromMenu();
  sv_head_redraw();
}


/* ==== evaluation ========================================================= */

#define EV_W		560
#define EV_BH		24		/* the Dismiss button */
#define EV_H		(250 + EV_BH)

static Frame ev_frame = NULL;
static Canvas ev_canvas = NULL;
static Pixwin *ev_pw = NULL;
static Pixrect *ev_buf = NULL;
static int ev_shown = 0;
static int ev_dirty = 1;
static char ev_s[18][80];		/* the SetEvaluation strings, in order */

#define EV_CHANGED	0
#define EV_SCORE	1
#define EV_PS		2		/* 2..5 problem names */
#define EV_PV		6		/* 6..9 problem votes */
#define EV_POP		10
#define EV_DELTA	11
#define EV_ASSESSED	12
#define EV_CLASS	13
#define EV_LEVEL	14
#define EV_YES		15
#define EV_NO		16
#define EV_TITLE	17


/* Engine hook: SetEvaluation (w_eval.c). */
void
sv_set_evaluation(changed, score, ps0, ps1, ps2, ps3, pv0, pv1, pv2, pv3,
		  pop, delta, assessed, cityclass, citylevel, goodyes, goodno,
		  title)
  char *changed, *score, *ps0, *ps1, *ps2, *ps3, *pv0, *pv1, *pv2, *pv3;
  char *pop, *delta, *assessed, *cityclass, *citylevel, *goodyes, *goodno;
  char *title;
{
  char *v[18], cls[60], msg[120];
  int i;

  v[0] = changed; v[1] = score;
  v[2] = ps0; v[3] = ps1; v[4] = ps2; v[5] = ps3;
  v[6] = pv0; v[7] = pv1; v[8] = pv2; v[9] = pv3;
  v[10] = pop; v[11] = delta; v[12] = assessed;
  v[13] = cityclass; v[14] = citylevel;
  v[15] = goodyes; v[16] = goodno; v[17] = title;
  for (i = 0; i < 18; i++) {
    strncpy(ev_s[i], v[i] ? v[i] : "", sizeof(ev_s[i]) - 1);
    ev_s[i][sizeof(ev_s[i]) - 1] = 0;
  }
  ev_dirty = 1;

  /* the DUX UISetEvaluation: "Score 323, capital population 91940." */
  strncpy(cls, cityclass ? cityclass : "", sizeof(cls) - 1);
  cls[sizeof(cls) - 1] = 0;
  for (i = 0; cls[i]; i++)
    if (isupper(cls[i]))
      cls[i] = tolower(cls[i]);
  sprintf(msg, "Score %.20s, %.40s population %.20s.", score ? score : "",
	  cls, pop ? pop : "");
  sv_set_message(msg);
}


/* "123" -> "+123"; negative numbers keep their sign */
static char *
ev_signed(s)
  char *s;
{
  static char buf[2][40];
  static int k = 0;

  k ^= 1;
  if (isdigit(s[0]) && atoi(s) > 0)
    sprintf(buf[k], "+%s", s);
  else
    strcpy(buf[k], s);
  return buf[k];
}


static void
ev_row(x, y, w, label, value)
  int x, y, w;
  char *label, *value;
{
  sv_text(ev_buf, x, y, SIMCOLOR_BLACK, SvBoldFont, label);
  sv_text(ev_buf, x + w - sv_text_width(SvFont, value), y, SIMCOLOR_BLACK,
	  SvFont, value);
}


static void
ev_draw()
{
  int lh, x, y, y0, bw, i, w;
  char buf[100];

  if (ev_buf == NULL)
    ev_buf = mem_create(EV_W, EV_H, SvColor ? 8 : 1);
  sv_fill(ev_buf, 0, 0, EV_W, EV_H, SvColor ? SvUiBack : SIMCOLOR_WHITE);
  lh = sv_font_height(SvFont) + 4;

  w = sv_text_width(SvBigFont, ev_s[EV_TITLE]);
  sv_text(ev_buf, (EV_W - w) / 2, 8, SIMCOLOR_BLACK, SvBigFont,
	  ev_s[EV_TITLE]);
  y0 = 8 + sv_font_height(SvBigFont) + 10;
  bw = EV_W / 2 - 14;
  sv_bevel(ev_buf, 8, y0, bw, EV_H - EV_BH - y0 - 8, 0);
  sv_bevel(ev_buf, EV_W / 2 + 6, y0, bw, EV_H - EV_BH - y0 - 8, 0);

  /* the DUX Dismiss button along the bottom */
  sv_button(ev_buf, 0, EV_H - EV_BH, EV_W, EV_BH, "Dismiss");

  /* left: public opinion */
  x = 20;
  y = y0 + 8;
  w = sv_text_width(SvBoldFont, "Public Opinion");
  sv_text(ev_buf, 8 + (bw - w) / 2, y, SIMCOLOR_BLACK, SvBoldFont,
	  "Public Opinion");
  y += lh + lh / 2;
  sv_text(ev_buf, x, y, SIMCOLOR_BLACK, SvFont,
	  "Is the mayor doing a good job?");
  y += lh;
  sprintf(buf, "%s", ev_s[EV_YES]);
  ev_row(x + 20, y, 90, "YES", buf);
  ev_row(x + 140, y, 80, "NO", ev_s[EV_NO]);
  y += lh + lh / 2;
  sv_text(ev_buf, x, y, SIMCOLOR_BLACK, SvFont,
	  "What are the worst problems?");
  y += lh;
  for (i = 0; i < 4; i++) {
    sv_text(ev_buf, x + 20, y, SIMCOLOR_BLACK, (i == 0) ? SvBoldFont : SvFont,
	    ev_s[EV_PS + i]);
    w = sv_text_width(SvFont, ev_s[EV_PV + i]);
    sv_text(ev_buf, x + 220 - w, y, SIMCOLOR_BLACK, SvFont, ev_s[EV_PV + i]);
    y += lh;
  }

  /* right: statistics and score */
  x = EV_W / 2 + 18;
  w = bw - 24;
  y = y0 + 8;
  i = sv_text_width(SvBoldFont, "Statistics");
  sv_text(ev_buf, EV_W / 2 + 6 + (bw - i) / 2, y, SIMCOLOR_BLACK, SvBoldFont,
	  "Statistics");
  y += lh + lh / 2;
  ev_row(x, y, w, "Population:", ev_s[EV_POP]);
  y += lh;
  ev_row(x, y, w, "Net Migration:", ev_signed(ev_s[EV_DELTA]));
  y += lh;
  ev_row(x, y, w, "Assessed Value:", ev_s[EV_ASSESSED]);
  y += lh;
  ev_row(x, y, w, "Category:", ev_s[EV_CLASS]);
  y += lh;
  ev_row(x, y, w, "Game Level:", ev_s[EV_LEVEL]);
  y += lh + lh / 2;
  sv_text(ev_buf, x, y, SIMCOLOR_BLACK, SvBoldFont,
	  "Overall City Score (0 - 1000)");
  y += lh;
  ev_row(x, y, w, "Current Score:", ev_s[EV_SCORE]);
  y += lh;
  ev_row(x, y, w, "Annual Change:", ev_signed(ev_s[EV_CHANGED]));
}


static void
ev_blit()
{
  if (ev_buf != NULL && ev_pw != NULL)
    pw_rop(ev_pw, 0, 0, EV_W, EV_H, PIX_SRC, ev_buf, 0, 0);
}


static void
ev_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (ev_buf != NULL)
    ev_blit();
  else
    pw_writebackground(pw, 0, 0, EV_W, EV_H, PIX_SRC);
}


static void
ev_done(frame)
  Frame frame;
{
  window_set(frame, WIN_SHOW, FALSE, 0);
  ev_shown = 0;
}


/* A click on the Dismiss button hides the window. */
static void
ev_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int id = event_id(event);

  if (event_is_down(event) &&
      (id == MS_LEFT || id == MS_MIDDLE || id == MS_RIGHT) &&
      event_y(event) >= EV_H - EV_BH)
    ev_done(ev_frame);
}


static void
ev_create()
{
  ev_frame = window_create(SvFrame, FRAME,
			   FRAME_LABEL, "Micropolis Evaluation",
			   FRAME_SHOW_LABEL, TRUE,
			   FRAME_DONE_PROC, ev_done,
			   WIN_SHOW, FALSE,
			   0);
  ev_canvas = window_create(ev_frame, CANVAS,
			    WIN_WIDTH, EV_W,
			    WIN_HEIGHT, EV_H,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, ev_repaint,
			    WIN_EVENT_PROC, ev_event,
			    0);
  window_set(ev_canvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     0);
  ev_pw = canvas_pixwin(ev_canvas);
  sv_set_cms(ev_pw);
  window_fit(ev_frame);
  /* the DUX place: the bottom left corner */
  sv_place(ev_frame, 5, SvScreenH - (int)window_get(ev_frame, WIN_HEIGHT) - 5);
}


/* Windows menu: Evaluation. */
void
sv_eval_show()
{
  if (ev_frame == NULL)
    ev_create();
  doScoreCard();
  ev_draw();
  ev_dirty = 0;
  sv_show_frame(ev_frame);
  ev_shown = 1;
  ev_blit();
}


static void
ev_update()
{
  if (ev_shown && ev_dirty) {
    ev_draw();
    ev_blit();
    ev_dirty = 0;
  }
}


/* ==== load and save ====================================================== */

#define FL_MAX		64

static char *fl_path[FL_MAX];		/* the city buttons' files */
static int fl_n = 0;
static Panel_item fl_text = NULL;


static int
fl_is_city(name)
  char *name;
{
  int n = strlen(name);

  return n > 4 && strcmp(name + n - 4, ".cty") == 0;
}


/* Add the .cty files of a directory, sorted by name. */
static void
fl_scan_dir(dir)
  char *dir;
{
  DIR *d;
  struct dirent *e;
  char path[700], *t;
  int first = fl_n, i, j;

  if ((d = opendir(dir)) == NULL)
    return;
  while ((e = readdir(d)) != NULL && fl_n < FL_MAX) {
    if (!fl_is_city(e->d_name))
      continue;
    if (strcmp(dir, ".") == 0)
      strcpy(path, e->d_name);
    else
      sprintf(path, "%s/%s", dir, e->d_name);
    fl_path[fl_n++] = dl_strdup(path);
  }
  closedir(d);
  for (i = first; i < fl_n; i++)
    for (j = i + 1; j < fl_n; j++)
      if (strcmp(fl_path[j], fl_path[i]) < 0) {
	t = fl_path[i];
	fl_path[i] = fl_path[j];
	fl_path[j] = t;
      }
}


static void
fl_free()
{
  while (fl_n > 0)
    free(fl_path[--fl_n]);
}


/* The button label for a path: file name without ".cty". */
static char *
fl_label(path)
  char *path;
{
  static char buf[64];
  char *s = path, *p;
  int n;

  for (p = path; *p; p++)
    if (*p == '/')
      s = p + 1;
  n = strlen(s) - 4;
  if (n > (int)sizeof(buf) - 1)
    n = sizeof(buf) - 1;
  if (n < 0)
    n = 0;
  strncpy(buf, s, n);
  buf[n] = 0;
  return buf;
}


static void
fl_button(item, event)
  Panel_item item;
  Event *event;
{
  window_return((caddr_t)panel_get(item, PANEL_CLIENT_DATA));
}


static Panel_setting
fl_text_notify(item, event)
  Panel_item item;
  Event *event;
{
  if (event_id(event) == '\r' || event_id(event) == '\n') {
    window_return((caddr_t)-1);
    return PANEL_NONE;
  }
  return panel_text_notify(item, event);
}


/* Center a modal (base) frame on the screen. */
static void
fl_center(f)
  Frame f;
{
  int w = (int)window_get(f, WIN_WIDTH);
  int h = (int)window_get(f, WIN_HEIGHT);

  window_set(f, WIN_X, (SvScreenW - w) / 2, WIN_Y, (SvScreenH - h) / 3, 0);
}


static Panel_item
fl_add_button(p, label, x, y, width, value)
  Panel p;
  char *label;
  int x, y, width, value;
{
  return panel_create_item(p, PANEL_BUTTON,
			   PANEL_ITEM_X, x, PANEL_ITEM_Y, y,
			   PANEL_LABEL_IMAGE,
			     panel_button_image(p, label, width, (Pixfont *)0),
			   PANEL_NOTIFY_PROC, fl_button,
			   PANEL_CLIENT_DATA, value,
			   0);
}


/* Run a modal file frame; returns the button's value. */
static int
fl_run(f)
  Frame f;
{
  int r;

  fl_center(f);
  SvBusy++;
  r = (int)window_loop(f);
  SvBusy--;
  return r;
}


static void
fl_destroy(f)
  Frame f;
{
  window_set(f, FRAME_NO_CONFIRM, TRUE, 0);
  window_destroy(f);
  fl_text = NULL;
}


/* File menu: Load City.  A button for each city found, or type a name. */
void
sv_file_load()
{
  Frame f;
  Panel p;
  Pixfont *pf;
  Panel_item it;
  Rect *r;
  char dir[700], *path = NULL, *v;
  int cw, ch, i, x, y, rh, res, cols = 5, bw = 12;

  fl_free();
  fl_scan_dir(".");
  sprintf(dir, "%s/cities", SvHome);
  fl_scan_dir(dir);

  f = window_create((Window)NULL, FRAME,
		    FRAME_LABEL, "File Chooser",
		    FRAME_SHOW_LABEL, TRUE,
		    0);
  if (f == NULL)
    return;
  p = window_create(f, PANEL, 0);
  pf = (Pixfont *)window_get(p, WIN_FONT);
  cw = pf->pf_defaultsize.x;
  ch = pf->pf_defaultsize.y;
  rh = ch + 14;

  panel_create_item(p, PANEL_MESSAGE,
		    PANEL_ITEM_X, 10, PANEL_ITEM_Y, 10,
		    PANEL_LABEL_STRING,
		    fl_n ? "Choose a City to Load:"
			 : "Type the name of a city file and press Load:",
		    0);
  y = 10 + rh;
  for (i = 0; i < fl_n; i++) {
    x = 10 + (i % cols) * (bw + 3) * cw;
    fl_add_button(p, fl_label(fl_path[i]), x, y + (i / cols) * rh, bw, i + 1);
  }
  if (fl_n)
    y += ((fl_n + cols - 1) / cols) * rh + rh / 2;

  fl_text = panel_create_item(p, PANEL_TEXT,
			      PANEL_ITEM_X, 10, PANEL_ITEM_Y, y,
			      PANEL_LABEL_STRING, "File:",
			      PANEL_LABEL_BOLD, TRUE,
			      PANEL_VALUE_DISPLAY_LENGTH, 50,
			      PANEL_VALUE_STORED_LENGTH, 600,
			      PANEL_VALUE, CityFileName ? CityFileName : "",
			      PANEL_NOTIFY_LEVEL, PANEL_SPECIFIED,
			      PANEL_NOTIFY_STRING, "\r\n",
			      PANEL_NOTIFY_PROC, fl_text_notify,
			      0);
  y += rh + rh / 2;
  it = fl_add_button(p, "Load", 10, y, 0, -1);
  r = (Rect *)panel_get(it, PANEL_ITEM_RECT);
  fl_add_button(p, "Cancel", 10 + r->r_width + 2 * cw, y, 0, 0);
  window_fit(p);
  window_fit(f);

  res = fl_run(f);
  if (res == -1) {
    v = (char *)panel_get_value(fl_text);
    if (v != NULL && *v)
      path = dl_strdup(v);
  } else if (res > 0 && res <= fl_n) {
    path = dl_strdup(fl_path[res - 1]);
  }
  fl_destroy(f);
  fl_free();

  if (path != NULL) {
    sv_load_city(path);			/* complains itself on failure */
    free(path);
  }
}


/* File menu: Save City As (and Save City for an unnamed city). */
void
sv_file_save_as()
{
  Frame f;
  Panel p;
  Panel_item it;
  Rect *r;
  Pixfont *pf;
  char name[700], *v, *s;
  int cw, ch, y, res, n;

  if (CityFileName != NULL) {
    strncpy(name, CityFileName, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
  } else {
    sprintf(name, "%.60s.cty", (CityName && *CityName) ? CityName : "city");
    for (s = name; *s; s++)
      if (*s == ' ' || *s == '/')
	*s = '_';
  }

  f = window_create((Window)NULL, FRAME,
		    FRAME_LABEL, "File Chooser",
		    FRAME_SHOW_LABEL, TRUE,
		    0);
  if (f == NULL)
    return;
  p = window_create(f, PANEL, 0);
  pf = (Pixfont *)window_get(p, WIN_FONT);
  cw = pf->pf_defaultsize.x;
  ch = pf->pf_defaultsize.y;

  panel_create_item(p, PANEL_MESSAGE,
		    PANEL_ITEM_X, 10, PANEL_ITEM_Y, 10,
		    PANEL_LABEL_STRING, "Choose a File to Save the City:",
		    0);
  y = 10 + ch + 14;
  fl_text = panel_create_item(p, PANEL_TEXT,
			      PANEL_ITEM_X, 10, PANEL_ITEM_Y, y,
			      PANEL_LABEL_STRING, "File:",
			      PANEL_LABEL_BOLD, TRUE,
			      PANEL_VALUE_DISPLAY_LENGTH, 50,
			      PANEL_VALUE_STORED_LENGTH, 600,
			      PANEL_VALUE, name,
			      PANEL_NOTIFY_LEVEL, PANEL_SPECIFIED,
			      PANEL_NOTIFY_STRING, "\r\n",
			      PANEL_NOTIFY_PROC, fl_text_notify,
			      0);
  y += ch + 20;
  it = fl_add_button(p, "Save", 10, y, 0, -1);
  r = (Rect *)panel_get(it, PANEL_ITEM_RECT);
  fl_add_button(p, "Cancel", 10 + r->r_width + 2 * cw, y, 0, 0);
  window_fit(p);
  window_fit(f);

  res = fl_run(f);
  name[0] = 0;
  if (res == -1) {
    v = (char *)panel_get_value(fl_text);
    if (v != NULL) {
      strncpy(name, v, sizeof(name) - 8);
      name[sizeof(name) - 8] = 0;
    }
  }
  fl_destroy(f);

  /* trim blanks */
  for (s = name; *s == ' '; s++)
    ;
  if (s != name)
    strcpy(name, s);
  n = strlen(name);
  while (n > 0 && name[n - 1] == ' ')
    name[--n] = 0;
  if (n == 0)
    return;
  if (!fl_is_city(name))
    strcat(name, ".cty");
  s = dl_strdup(name);			/* SaveCityAs edits its argument */
  SaveCityAs(s);
  free(s);
}


/* ==== shared ============================================================= */

/* Reinstall the colormap on the dialog canvases (it grew). */
void
sv_dialogs_cms()
{
  if (nt_pw != NULL)
    sv_set_cms(nt_pw);
  if (ev_pw != NULL)
    sv_set_cms(ev_pw);
  sv_graph_cms();
}


/* Per tick. */
void
sv_dialogs_update()
{
  nt_update();
  ev_update();
  sv_graph_update();
}
