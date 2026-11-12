/* sv_graph.c -- the history graphs.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The data side (drawMonth, doAllGraphs, ChangeCensus, graphDoer,
 * initGraphs, InitGraphMax) is the original w_graph.c.  The window is the
 * DUX one (res/wgraph.tcl): a pallet on the left with the 10 / 120 years
 * buttons and a toggle for each history, the graph on the right drawn the
 * way the original DoUpdateGraph did, and Dismiss along the bottom.
 */

#include "sv.h"

short NewGraph = 0;
short AllMax;
unsigned char *History10[HISTORIES];
unsigned char *History120[HISTORIES];
int HistoryInitialized = 0;
short Graph10Max, Graph120Max;
int GraphUpdateTime = 100;

char *HistName[] = {
  "Residential", "Commercial", "Industrial",
  "Cash Flow", "Crime", "Pollution"
};

unsigned char HistColor[] = {
  SIMCOLOR_LIGHTGREEN, SIMCOLOR_DARKBLUE, SIMCOLOR_YELLOW,
  SIMCOLOR_DARKGREEN, SIMCOLOR_RED, SIMCOLOR_OLIVE
};


/* ---- data (w_graph.c) ---------------------------------------------------- */

drawMonth(hist, s, scale)
  short *hist;
  unsigned char *s;
  float scale;
{
  register short val;
  register short x;

  for (x = 0; x < 120; x++) {
    val = hist[x] * scale;
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    s[119 - x] = val;
  }
}


doAllGraphs()
{
  float scaleValue;

  AllMax = 0;
  if (ResHisMax > AllMax) AllMax = ResHisMax;
  if (ComHisMax > AllMax) AllMax = ComHisMax;
  if (IndHisMax > AllMax) AllMax = IndHisMax;
  if (AllMax <= 128) AllMax = 0;

  if (AllMax) {
    scaleValue = 128.0 / AllMax;
  } else {
    scaleValue = 1.0;
  }

  drawMonth(ResHis, History10[RES_HIST], scaleValue);
  drawMonth(ComHis, History10[COM_HIST], scaleValue);
  drawMonth(IndHis, History10[IND_HIST], scaleValue);
  drawMonth(MoneyHis, History10[MONEY_HIST], 1.0);
  drawMonth(CrimeHis, History10[CRIME_HIST], 1.0);
  drawMonth(PollutionHis, History10[POLLUTION_HIST], 1.0);

  AllMax = 0;
  if (Res2HisMax > AllMax) AllMax = Res2HisMax;
  if (Com2HisMax > AllMax) AllMax = Com2HisMax;
  if (Ind2HisMax > AllMax) AllMax = Ind2HisMax;
  if (AllMax <= 128) AllMax = 0;

  if (AllMax) {
    scaleValue = 128.0 / AllMax;
  } else {
    scaleValue = 1.0;
  }

  drawMonth(ResHis + 120, History120[RES_HIST], scaleValue);
  drawMonth(ComHis + 120, History120[COM_HIST], scaleValue);
  drawMonth(IndHis + 120, History120[IND_HIST], scaleValue);
  drawMonth(MoneyHis + 120, History120[MONEY_HIST], 1.0);
  drawMonth(CrimeHis + 120, History120[CRIME_HIST], 1.0);
  drawMonth(PollutionHis + 120, History120[POLLUTION_HIST], 1.0);
}


ChangeCensus()
{
  CensusChanged = 1;
}


static int gr_dirty = 1;
int SvGraphStamp = 0;			/* the controls window's graph */

graphDoer()
{
  if (CensusChanged) {
    doAllGraphs();
    NewGraph = 1;
    CensusChanged = 0;
  }

  if (NewGraph) {
    gr_dirty = 1;
    SvGraphStamp++;
    NewGraph = 0;
  }
}


initGraphs()
{
  int i;

  if (!HistoryInitialized) {
    HistoryInitialized = 1;
    for (i = 0; i < HISTORIES; i++) {
      History10[i] = (unsigned char *)calloc(120, 1);
      History120[i] = (unsigned char *)calloc(120, 1);
    }
  }
}


/* comefrom: InitWillStuff */
InitGraphMax()
{
  register x;

  ResHisMax = 0;
  ComHisMax = 0;
  IndHisMax = 0;
  for (x = 118; x >= 0; x--) {
    if (ResHis[x] > ResHisMax) ResHisMax = ResHis[x];
    if (ComHis[x] > ComHisMax) ComHisMax = ComHis[x];
    if (IndHis[x] > IndHisMax) IndHisMax = IndHis[x];
    if (ResHis[x] < 0) ResHis[x] = 0;
    if (ComHis[x] < 0) ComHis[x] = 0;
    if (IndHis[x] < 0) IndHis[x] = 0;
  }
  Graph10Max = ResHisMax;
  if (ComHisMax > Graph10Max) Graph10Max = ComHisMax;
  if (IndHisMax > Graph10Max) Graph10Max = IndHisMax;

  Res2HisMax = 0;
  Com2HisMax = 0;
  Ind2HisMax = 0;
  for (x = 238; x >= 120; x--) {
    if (ResHis[x] > Res2HisMax) Res2HisMax = ResHis[x];
    if (ComHis[x] > Com2HisMax) Com2HisMax = ComHis[x];
    if (IndHis[x] > Ind2HisMax) Ind2HisMax = IndHis[x];
    if (ResHis[x] < 0) ResHis[x] = 0;
    if (ComHis[x] < 0) ComHis[x] = 0;
    if (IndHis[x] < 0) IndHis[x] = 0;
  }
  Graph120Max = Res2HisMax;
  if (Com2HisMax > Graph120Max) Graph120Max = Com2HisMax;
  if (Ind2HisMax > Graph120Max) Graph120Max = Ind2HisMax;
}


/* ---- the window ---------------------------------------------------------- */

#define GR_PW		80		/* the pallet */
#define GR_GW		560		/* the graph */
#define GR_W		(GR_PW + GR_GW)
#define GR_H		240
#define GR_BH		24		/* the Dismiss button */
#define BORDER		5

static Frame gr_frame = NULL;
static Canvas gr_canvas = NULL;
static Pixwin *gr_pw = NULL;
static Pixrect *gr_buf = NULL;
static int gr_shown = 0;
static int gr_mask = ALL_HISTORIES;
static int gr_range = 10;

/* The pallet buttons, laid out as the DUX packs them: the histories in
 * two columns (0..5, the HistName order), then the ranges on top. */
#define GR_BUTTONS	(HISTORIES + 2)
#define GR_YEAR10	HISTORIES
#define GR_YEAR120	(HISTORIES + 1)

static struct {
  char *image;
  int x, y;
} gr_pallet[GR_BUTTONS] = {
  { "grres",   6,  64 }, { "grcom",   6, 100 }, { "grind",   6, 136 },
  { "grmony", 42,  64 }, { "grcrim", 42, 100 }, { "grpoll", 42, 136 },
  { "gr10",    5,   6 }, { "gr120",   5,  32 }
};
static SvImage *gr_img[GR_BUTTONS][2];	/* off, on ("hi") */

/* monochrome: dash lengths in segments (0 solid) and line widths */
static int gr_dash[HISTORIES] = { 0, 3, 1, 0, 2, 5 };
static int gr_thick[HISTORIES] = { 3, 2, 2, 1, 2, 2 };


static void
gr_label(pr, x, y, i)
  Pixrect *pr;
  int x, y, i;
{
  int top = y - sv_font_ascent(SvBoldFont);

  if (SvColor) {
    sv_text(pr, x - 1, top, HistColor[i], SvBoldFont, HistName[i]);
    sv_text(pr, x, top - 1, HistColor[i], SvBoldFont, HistName[i]);
  }
  sv_text(pr, x, top, SIMCOLOR_BLACK, SvBoldFont, HistName[i]);
}


/* The original DoUpdateGraph, drawn with pixrect vectors into pr (w x h):
 * the histories in mask over range (10 or 120) years.  The labels only
 * fit a big graph, like the original's. */
void
sv_graph_draw(pr, w, h, range, mask, bg)
  Pixrect *pr;
  int w, h, range, mask, bg;
{
  unsigned char **hist;
  int i, j, k, x, y, px, py, c, thick;
  int year = (CityTime / 48) + StartingYear;
  int month = (CityTime / 4) % 12;
  int do_top_labels = 0;
  int do_right_labels = 0;
  int top_label_height = 30;
  int right_label_width;
  int tx, ty, xs[121], ys[121];
  float sx, sy;
  char buf[32];

  hist = (range == 10) ? History10 : History120;
  sv_fill(pr, 0, 0, w, h, SvColor ? bg : SIMCOLOR_WHITE);
  if (hist[0] == NULL)
    return;

  /* room for the widest name (the original's 65 fit its smaller font) */
  right_label_width = 0;
  for (i = 0; i < HISTORIES; i++)
    if ((k = sv_text_width(SvBoldFont, HistName[i])) > right_label_width)
      right_label_width = k;
  right_label_width += 8;

  tx = BORDER;
  ty = BORDER;
  if ((w -= (2 * BORDER)) < 1) w = 1;
  if ((h -= (2 * BORDER)) < 1) h = 1;

  if (w > (4 * right_label_width)) {
    w -= right_label_width;
    do_right_labels = 1;
  }
  if (do_right_labels && (h > (3 * top_label_height))) {
    ty += top_label_height;
    h -= top_label_height;
    do_top_labels = 1;
  }

  sx = ((float)w) / 120.0;
  sy = ((float)h) / 256.0;

  for (i = 0; i < HISTORIES; i++, mask >>= 1, hist++) {
    if (!(mask & 1))
      continue;
    for (j = 0; j < 120; j++) {
      x = tx + (j * sx);
      y = ty + ((int)(h - (((float)(*hist)[j]) * sy)));
      xs[j] = x;
      ys[j] = y;
    }
    x = tx + (j * sx);
    xs[j] = x;
    ys[j] = y;

    c = SvColor ? HistColor[i] : SIMCOLOR_BLACK;
    thick = SvColor ? 3 : gr_thick[i];
    for (j = 0; j < 120; j++) {
      if (!SvColor && gr_dash[i] && ((j / gr_dash[i]) & 1))
	continue;
      for (k = 0; k < thick; k++) {
	px = k - thick / 2;
	sv_line(pr, xs[j], ys[j] + px, xs[j + 1], ys[j + 1] + px, c);
      }
    }
    if (do_right_labels)
      gr_label(pr, x + 5, y + 5, i);
  }

  sv_line(pr, tx, ty - 1, tx + w, ty - 1, SIMCOLOR_BLACK);
  sv_line(pr, tx, ty + h, tx + w, ty + h, SIMCOLOR_BLACK);

  if (range == 10) {
    for (x = 120 - month; x >= 0; x -= 12) {
      px = tx + (x * sx);
      sv_line(pr, px, ty - 1, px, ty + h, SIMCOLOR_BLACK);
      if (do_top_labels) {
	sprintf(buf, "%d", year--);
	px = tx + (x * sx) + 2;
	py = ty - ((year & 1) ? 4 : 20);
	sv_text(pr, px, py - sv_font_ascent(SvFont), SIMCOLOR_BLACK,
		SvFont, buf);
      }
    }
  } else {
    int past;

    sx /= 10;
    past = 10 * (year % 10);
    year /= 10;
    for (x = 1200 - past; x >= 0; x -= 120) {
      px = tx + (x * sx);
      sv_line(pr, px, ty - 1, px, ty + h, SIMCOLOR_BLACK);
      if (do_top_labels) {
	sprintf(buf, "%d0", year--);
	px = tx + (x * sx) + 2;
	py = ty - ((year & 1) ? 4 : 20);
	sv_text(pr, px, py - sv_font_ascent(SvFont), SIMCOLOR_BLACK,
		SvFont, buf);
      }
    }
  }
}


/* Pictures for the pallet buttons.  The history pictures have no mono
 * colors and are dark drawings on a blue sky, so 1 bit takes them by the
 * brightest channel (the range buttons have DUX mono colors). */
void
sv_graph_load()
{
  int i;
  char name[64];

  for (i = 0; i < GR_BUTTONS; i++) {
    sprintf(name, "%s.xpm", gr_pallet[i].image);
    gr_img[i][0] = (i < HISTORIES) ? sv_load_image_bright(name) :
      sv_load_image(name);
    sprintf(name, "%shi.xpm", gr_pallet[i].image);
    gr_img[i][1] = (i < HISTORIES) ? sv_load_image_bright(name) :
      sv_load_image(name);
  }
}


static int
gr_button_on(i)
  int i;
{
  if (i < HISTORIES)
    return (gr_mask >> i) & 1;
  return (i == GR_YEAR10) == (gr_range == 10);
}


static void
gr_draw()
{
  Pixrect *sub;
  SvImage *img;
  int i;

  if (gr_buf == NULL)
    gr_buf = mem_create(GR_W, GR_H + GR_BH, SvColor ? 8 : 1);

  sv_fill(gr_buf, 0, 0, GR_PW, GR_H, SvUiBack);
  sv_bevel2(gr_buf, 0, 0, GR_PW, GR_H, 1);
  for (i = 0; i < GR_BUTTONS; i++) {
    img = gr_img[i][gr_button_on(i)];
    if (img == NULL)
      img = gr_img[i][0];
    if (img == NULL)
      continue;
    sv_draw_image(gr_buf, gr_pallet[i].x, gr_pallet[i].y, img);
    /* 1 bit: a history that is off is inverted, white when on and black
     * when off like the DUX mono range buttons */
    if (!SvColor && i < HISTORIES && !gr_button_on(i))
      pr_rop(gr_buf, gr_pallet[i].x, gr_pallet[i].y, img->w, img->h,
	     PIX_NOT(PIX_DST), (Pixrect *)NULL, 0, 0);
  }

  sv_bevel2(gr_buf, GR_PW, 0, GR_GW, GR_H, 1);
  sub = pr_region(gr_buf, GR_PW + 2, 2, GR_GW - 4, GR_H - 4);
  sv_graph_draw(sub, GR_GW - 4, GR_H - 4, gr_range, gr_mask,
		SIMCOLOR_LIGHTGRAY);
  pr_destroy(sub);

  sv_button(gr_buf, 0, GR_H, GR_W, GR_BH, "Dismiss");
}


static void
gr_blit()
{
  if (gr_buf != NULL && gr_pw != NULL)
    pw_rop(gr_pw, 0, 0, GR_W, GR_H + GR_BH, PIX_SRC, gr_buf, 0, 0);
}


static void
gr_repaint(canvas, pw, area)
  Canvas canvas;
  Pixwin *pw;
  Rectlist *area;
{
  if (gr_buf != NULL)
    gr_blit();
  else
    pw_writebackground(pw, 0, 0, GR_W, GR_H + GR_BH, PIX_SRC);
}


static void
gr_done(frame)
  Frame frame;
{
  window_set(frame, WIN_SHOW, FALSE, 0);
  gr_shown = 0;
}


/* The DUX GraphPallet (toggle a history), GraphYearPallet (pick the
 * range) and Dismiss. */
static void
gr_event(canvas, event, arg)
  Canvas canvas;
  Event *event;
  caddr_t arg;
{
  int i, x = event_x(event), y = event_y(event), id = event_id(event);
  SvImage *img;

  if (!event_is_down(event) ||
      (id != MS_LEFT && id != MS_MIDDLE && id != MS_RIGHT))
    return;
  if (y >= GR_H) {
    gr_done(gr_frame);
    return;
  }
  for (i = 0; i < GR_BUTTONS; i++) {
    img = gr_img[i][0];
    if (img != NULL && x >= gr_pallet[i].x && y >= gr_pallet[i].y &&
	x < gr_pallet[i].x + img->w && y < gr_pallet[i].y + img->h) {
      if (i < HISTORIES)
	gr_mask ^= 1 << i;
      else
	gr_range = (i == GR_YEAR10) ? 10 : 120;
      gr_draw();
      gr_blit();
      return;
    }
  }
}


static void
gr_create()
{
  gr_frame = window_create(SvFrame, FRAME,
			   FRAME_LABEL, "Micropolis Graph",
			   FRAME_SHOW_LABEL, TRUE,
			   FRAME_DONE_PROC, gr_done,
			   WIN_SHOW, FALSE,
			   0);
  gr_canvas = window_create(gr_frame, CANVAS,
			    WIN_X, 0,
			    WIN_Y, 0,
			    WIN_WIDTH, GR_W,
			    WIN_HEIGHT, GR_H + GR_BH,
			    CANVAS_RETAINED, FALSE,
			    CANVAS_REPAINT_PROC, gr_repaint,
			    WIN_EVENT_PROC, gr_event,
			    0);
  window_set(gr_canvas,
	     WIN_CONSUME_PICK_EVENTS,
	       WIN_NO_EVENTS, WIN_MOUSE_BUTTONS, 0,
	     0);
  gr_pw = canvas_pixwin(gr_canvas);
  sv_set_cms(gr_pw);
  window_fit(gr_frame);
  /* the DUX place: below the editor, at the bottom of the screen */
  sv_place(gr_frame, 420, SvScreenH - (int)window_get(gr_frame, WIN_HEIGHT)
	   - 5);
}


/* A new game: all histories, ten years (the DUX InitGraph). */
void
sv_graph_reset()
{
  gr_mask = ALL_HISTORIES;
  gr_range = 10;
  gr_dirty = 1;
}


/* Windows menu: Graph. */
void
sv_graph_show()
{
  if (gr_frame == NULL)
    gr_create();
  doAllGraphs();
  gr_draw();
  gr_dirty = 0;
  sv_show_frame(gr_frame);
  gr_shown = 1;
  gr_blit();
}


/* Per tick. */
void
sv_graph_update()
{
  if (gr_shown && gr_dirty) {
    gr_draw();
    gr_blit();
    gr_dirty = 0;
  }
}


void
sv_graph_cms()
{
  if (gr_pw != NULL)
    sv_set_cms(gr_pw);
}
