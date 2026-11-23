/* sv.h -- Micropolis for SunView: front-end declarations.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The SunView front end replaces the original Tcl/Tk/X11 user interface.
 * The simulation engine (s_*.c, w_*.c) is unchanged apart from K&R syntax
 * and the UI notifications, which now call the sv_* hooks declared here.
 *
 * Old-style (K&R) C only: SunOS 4 /usr/bin/cc has no prototypes.
 */

#include <suntool/sunview.h>
#include <suntool/canvas.h>
#include <suntool/panel.h>
#ifndef SUNOS3
#include <suntool/alert.h>	/* SunOS 3.5 has no alerts */
#endif
#include <suntool/scrollbar.h>
#include <sunwindow/cms.h>

#include "sim.h"


/* ---- colormap ---------------------------------------------------------- */

#define SV_CMS_NAME	"micropolis"
#define SV_CMS_SIZE	128		/* SunView segments are powers of two */
#define SV_BG		0		/* entry 0 is the window background */
#define SV_FG		(SV_CMS_SIZE - 1)	/* last entry is the foreground */

extern int SvColor;			/* 1: 8-bit color, 0: monochrome */
extern int SvCmsDirty;			/* colormap grew: reinstall it */
extern unsigned char SvRed[SV_CMS_SIZE];
extern unsigned char SvGreen[SV_CMS_SIZE];
extern unsigned char SvBlue[SV_CMS_SIZE];
extern unsigned char SvGray[SV_CMS_SIZE];	/* luminance of each entry */
extern unsigned char SvDither[4][4];	/* ordered dither thresholds */

/* UI colors (indices into the colormap) */
extern int SvUiBack;			/* DUX gray #b0b0b0 */
extern int SvUiLight;			/* #d8d8d8 */
extern int SvUiDark;			/* #7f7f7f */
extern int SvUiText;			/* black */

extern void sv_init_colors();
extern int sv_alloc_color();		/* (r, g, b) -> colormap index */
extern void sv_set_cms();		/* (pixwin) install our colormap */
extern void sv_install_cms();		/* on every window that draws */
extern int sv_pix();			/* (index) -> pixel value */


/* ---- images, fonts, drawing --------------------------------------------- */

typedef struct SvImage {
  int w, h;
  Pixrect *pr;			/* depth 8 (color) or 1 (mono) */
  Pixrect *mask;		/* depth 1, 1 = opaque; NULL if no "None" */
} SvImage;

/* sv_load_xpm modes */
#define SV_XPM_MONO	0		/* 1 bit: mono color, else brightness */
#define SV_XPM_COLOR	1		/* 8 bit colormap indices */
#define SV_XPM_BRIGHT	2		/* 1 bit, by the brightest of r, g, b */

extern char *sv_read_file();		/* (path, &len) whole file, 0-ended */
extern SvImage *sv_load_xpm();		/* (path, SV_XPM_*) */
extern SvImage *sv_load_image();	/* (name) from $SIMHOME/images */
extern SvImage *sv_load_image_bright();	/* (name) the same, 1 bit by
					 * SV_XPM_BRIGHT */
extern void sv_free_image();
extern void sv_load_art();		/* tiles and sprites */

extern Pixfont *SvFont;			/* screen.r.12: plain text */
extern Pixfont *SvBoldFont;		/* screen.b.12: DUX "Medium" */
extern Pixfont *SvBigFont;		/* screen.b.14: DUX "Large" */
extern Pixfont *SvHugeFont;		/* screen.b.16: DUX "Big" */
extern void sv_init_fonts();
extern int sv_text_width();		/* (font, str) */
extern int sv_font_ascent();		/* (font) */
extern int sv_font_height();		/* (font) */

extern void sv_fill();			/* (pr, x, y, w, h, color) */
extern void sv_frame_rect();		/* (pr, x, y, w, h, color, thick) */
extern void sv_bevel();			/* (pr, x, y, w, h, raised) */
extern void sv_bevel2();		/* (pr, x, y, w, h, raised) 2 pixels */
extern void sv_button();		/* (pr, x, y, w, h, label) */
extern void sv_line();			/* (pr, x0, y0, x1, y1, color) */
extern void sv_text();			/* (pr, x, y, color, font, str) */
extern void sv_draw_image();		/* (pr, x, y, image) masked */
extern int sv_wrap();			/* (font, str, w, fn, arg) lines */

/* tile art */
extern Pixrect *SvBigTiles;		/* 16 x (16 * TILE_COUNT) */
extern unsigned char *SvSmallTiles;	/* 3x3 per tile, 9 bytes: color index,
					 * or gray 0..255 in monochrome */
extern SvImage *SvSprites[OBJN][16];	/* sprite frames, [type][frame-1] */
extern int SvSpriteFrames[OBJN];


/* ---- windows ------------------------------------------------------------
 *
 * Like DUX SimCity, every part of the game has a window of its own: the
 * base frame is "Micropolis Controls" (menus, date, funds, graph, demand,
 * message log); the editor, the overall map, the notices, budget,
 * evaluation and graph windows are subframes of it.
 */

#ifndef MAP_W				/* sim.h has them too */
#define MAP_W		(WORLD_X * 3)	/* the overall map, 3x3 per tile */
#define MAP_H		(WORLD_Y * 3)
#endif
#define SV_TOOLS_W	132		/* the DUX tool palette */
#define SV_TOOLS_H	400

extern Frame SvFrame;			/* base frame: Micropolis Controls */
extern Frame SvEditorFrame;		/* Micropolis Editor */
extern Frame SvMapFrame;		/* Micropolis Overall Map */
extern Canvas SvEditorCanvas, SvMapCanvas, SvToolCanvas;
extern SimView *SvEditor;		/* the engine's editor view */
extern SimView *SvMap;			/* the engine's map view */

extern void sv_place();			/* (frame, screen x, y) a subframe */
extern void sv_show_frame();		/* (frame) show and raise */

/* A DUX style menu bar: a canvas with menu buttons, and room on the
 * right for a picture or a label (drawn by the extra proc). */
#define SV_BAR_MAX	8

typedef struct SvBarItem {
  char *label;
  Menu (*menu)();			/* the menu, its marks refreshed */
} SvBarItem;

typedef struct SvBar {
  Canvas canvas;
  Pixwin *pw;
  Pixrect *buf;
  int w, h;
  int n;
  SvBarItem *item;
  int x[SV_BAR_MAX], lw[SV_BAR_MAX];	/* where each button is */
  int end;				/* x after the last button */
  int pressed;				/* button with its menu up, or -1 */
  void (*extra)();			/* (bar): draws the right-hand part */
} SvBar;

extern int SvBarH;			/* menu bar height */
extern SvBar *sv_bar_create();		/* (frame, width, items, extra) */
extern int sv_bar_width();		/* (items) the width they need */
extern void sv_bar_redraw();		/* (bar) */
extern void sv_bar_cms();		/* (bar) */

extern int SvScreenW, SvScreenH;	/* screen size */
extern char *SvHome;			/* $SIMHOME: DUX SimCity asset tree */
extern int SvDebug;			/* -debug: log to stderr */
extern int SvReady;			/* art loaded, engine running */
extern int SvTicks;			/* 50 ms ticks so far */
extern int SvBusy;			/* a modal dialog is up: no ticks */
extern int SvSoundWanted;


/* ---- menu commands (menu item values) ----------------------------------- */

#define SV_CMD_NEW	1		/* + level 0..2 */
#define SV_CMD_LOAD	10
#define SV_CMD_SAVE	11
#define SV_CMD_SAVE_AS	12
#define SV_CMD_ABOUT	13
#define SV_CMD_QUIT	14
#define SV_CMD_SCENARIO	20		/* + scenario 1..8 */
#define SV_CMD_OPTION	40		/* + option 0..6 */
#define SV_CMD_DISASTER	60		/* + disaster 0..5 */
#define SV_CMD_SPEED	70		/* + speed 0..3 */
#define SV_CMD_LEVEL	80		/* + level 0..2 */
#define SV_CMD_BUDGET	90
#define SV_CMD_EVAL	91
#define SV_CMD_GRAPH	92
#define SV_CMD_NOTICE	93
#define SV_CMD_SHOW_MAP	94
#define SV_CMD_SHOW_ED	95
#define SV_CMD_MAP	100		/* + map state */
#define SV_CMD_PRIORITY	120		/* + priority 0..4 */
#define SV_CMD_SKIP	130		/* + editor display rate 0..4 */
#define SV_CMD_EDOPT	140		/* + editor option 0..2 */
#define SV_CMD_TOOL	200		/* + tool state */


/* ---- front-end entry points --------------------------------------------- */

extern void sv_log();			/* (fmt, a, b, c, d) debug log */

/* sv_editor.c */
extern void sv_editor_create();		/* (w, h) the editor frame */
extern void sv_editor_update();		/* per tick: redraw changed tiles */
extern void sv_editor_invalidate();	/* force full redraw */
extern void sv_editor_pan_to();		/* (tile x, tile y) center */
extern void sv_editor_pan_by();		/* (dx, dy) pixels */
extern void sv_editor_key();		/* (event) */
extern void sv_auto_goto();		/* (tile x, tile y) */
extern void sv_editor_message();	/* (str) the label over the editor */
extern void sv_editor_title();		/* city name changed */
extern void sv_editor_cms();
extern void sv_editor_show_tools();	/* (on) the palette panel */
extern int SvEditorTools, SvEditorChalk, SvEditorSkip;

/* sv_map.c */
extern void sv_map_create();		/* the map frame */
extern void sv_map_update();		/* per tick */
extern void sv_map_invalidate();
extern void sv_map_set_state();		/* (state) */
extern int sv_map_state();
extern char *sv_map_state_name();	/* (state) */
extern Menu sv_map_menu();		/* every map state */
extern Menu sv_zones_menu();
extern Menu sv_overlays_menu();
extern void sv_map_view_moved();
extern void sv_map_tool_used();
extern void sv_map_cms();
extern void sv_map_load();		/* the legends */

/* sv_tools.c */
extern void sv_tools_create();		/* (frame, below) the palette */
extern void sv_tools_load();
extern void sv_tools_redraw();
extern void sv_tools_update();		/* per tick */
extern void sv_select_tool();		/* (state) */

/* sv_head.c */
extern void sv_head_create();		/* the base frame's windows */
extern void sv_head_load();		/* its pictures */
extern void sv_head_update();		/* per tick */
extern void sv_head_redraw();
extern void sv_head_cms();
extern void sv_set_message();		/* (str) log it, label the editor */
extern void sv_log_message();		/* (str, tag) log only */
extern void sv_head_shutdown();
#define SV_TAG_STATUS	0		/* the DUX text tags */
#define SV_TAG_ALERT	1

/* sv_menu.c */
extern Menu sv_simcity_menu(), sv_options_menu(), sv_disasters_menu();
extern Menu sv_time_menu(), sv_priority_menu(), sv_windows_menu();
extern Menu sv_display_menu(), sv_edopt_menu(), sv_tool_menu();
extern void sv_do_command();		/* (menu value) */
extern void sv_menu_event();		/* (menu, window, event) show, do */
extern int SvPriority;

/* sv_dialogs.c */
extern int sv_alert();			/* (message) OK box */
extern int sv_confirm();		/* (message) Yes/No box */
extern int sv_ask();			/* (message, yes, no) 1 for yes */
extern void sv_notice_show();		/* (DUX message id, parms) */
extern void sv_notice_again();
extern void sv_about();
extern void sv_budget_show();
extern void sv_budget_changed();
extern void sv_budget_reset();
extern void sv_eval_show();
extern void sv_file_load();
extern void sv_file_save_as();
extern void sv_dialogs_cms();
extern void sv_dialogs_update();	/* per tick */

/* sv_graph.c */
extern void sv_graph_load();		/* the pallet pictures */
extern void sv_graph_show();
extern void sv_graph_update();		/* per tick */
extern void sv_graph_cms();
extern void sv_graph_reset();
extern void sv_graph_draw();		/* (pr, w, h, range, mask, bg) */
extern int SvGraphStamp;		/* bumped when the histories change */

/* sv_main.c */
extern void sv_new_city();		/* (level) */
extern int sv_load_city();		/* (path) */
extern void sv_load_scenario();		/* (number) */
extern void sv_save_city();
extern void sv_save_city_as();
extern void sv_toggle_pause();
extern void sv_set_sim_speed();		/* (0..3) */
extern void sv_set_priority();		/* (0..4) */
extern void sv_quit();
extern void sv_quit_now();

/* sv_sound.c */
extern void sv_init_sound();
extern void sv_shutdown_sound();
extern void sv_play_sound();		/* (channel, id, repeat) */
extern void sv_play_sound_speed();	/* (name, percent) */
extern void sv_stop_sound();		/* (id) */
extern void sv_stop_all_sounds();
extern void sv_sound_update();		/* per tick */


/* ---- engine globals and functions the front end uses ------------------- */

extern int DoAnimation, DoMessages, DoNotices;
extern int sim_skips, sim_skip;
extern int heat_wrap;
extern char *MicropolisVersion;
extern char LastMessage[];
extern short HaveLastMessage;
extern char *cityClassStr[];
extern char *cityLevelStr[];
extern char *probStr[];
extern short CityScore, deltaCityScore;
extern QUAD CostOf[];
extern float roadPercent, policePercent, firePercent;
extern QUAD roadValue, policeValue, fireValue;
extern QUAD roadMaxValue, policeMaxValue, fireMaxValue;
extern char *dateStr[];
extern char *HistName[];
extern unsigned char HistColor[];
extern void sim_heat();
