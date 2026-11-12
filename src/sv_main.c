/* sv_main.c -- Micropolis for SunView: start-up, windows, game loop.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * Replaces the original sim.c / w_tk.c driver.  The windows are the DUX
 * ones, each a frame of its own: the base frame is "Micropolis Controls"
 * (sv_head.c); the editor (sv_editor.c), the overall map (sv_map.c), the
 * notices, budget, evaluation and graph windows are its subframes.  An
 * interval timer (50 ms; the Priority menu changes it) drives the
 * simulation: every tick runs SimFrame() (which itself slows down for the
 * lower speeds), moves the sprites, animates the tiles and redraws
 * whatever changed.
 */

#include "sv.h"

/* ---- engine globals that lived in the original sim.c ------------------- */

char *MicropolisVersion = "4.0";
Sim *sim = NULL;
int sim_loops = 0;
int sim_delay = 50;
int sim_skips = 0;
int sim_skip = 0;
int sim_paused = 0;
int sim_paused_speed = 3;
int sim_tty = 0;
int heat_steps = 0;
int heat_flow = -7;
int heat_rule = 0;
int heat_wrap = 3;
struct timeval start_time, now_time, beat_time, last_now_time;
char *CityFileName = NULL;
int Startup = 0;
int StartupGameLevel = 0;
char *StartupName = NULL;
int WireMode = 0;
int MultiPlayerMode = 0;
int SugarMode = 0;
int TilesAnimated = 0;
int DoAnimation = 1;
int DoMessages = 1;
int DoNotices = 1;
char *Displays = NULL;
char *FirstDisplay = NULL;
int ExitReturn = 0;
int tkMustExit = 0;

/* ---- front-end globals -------------------------------------------------- */

Frame SvFrame = NULL;
SimView *SvEditor = NULL;
SimView *SvMap = NULL;
int SvScreenW = 1024, SvScreenH = 768;
char *SvHome = ".";
int SvDebug = 0;
int SvReady = 0;			/* art loaded, engine running */
int SvTicks = 0;			/* 50 ms ticks since start */
int SvBusy = 0;				/* a modal dialog is up */
int SvSoundWanted = 1;

static char SvHomeBuf[600];
static char SvResBuf[640];
static char SvCityNameBuf[128];
static int SvFireBombs = 0, SvFireBombWait = 0;

extern void sim_heat();


/* ---- logging ------------------------------------------------------------ */

void
sv_log(fmt, a, b, c, d)
  char *fmt;
  char *a, *b, *c, *d;
{
  if (!SvDebug)
    return;
  fprintf(stderr, "micropolis: ");
  fprintf(stderr, fmt, a, b, c, d);
  fflush(stderr);
}


/* ---- $SIMHOME ----------------------------------------------------------- */

static int
is_simhome(dir)
  char *dir;
{
  char p[700];
  struct stat st;

  sprintf(p, "%s/images/tiles.xpm", dir);
  return stat(p, &st) == 0;
}


/* Find the DUX SimCity asset tree (images/, res/, cities/). */
static void
env_init()
{
  static char *cands[] = {
    ".", "..", "/usr/local/SimCity", "/usr/SimCity", "/usr/local/lib/SimCity",
    "/usr/games/lib/SimCity", "/opt/SimCity", NULL
  };
  char *s = getenv("SIMHOME");
  int i;

  if (s && *s && is_simhome(s)) {
    strcpy(SvHomeBuf, s);
  } else {
    if (s && *s)
      fprintf(stderr, "micropolis: SIMHOME=%s has no images/tiles.xpm\n", s);
    strcpy(SvHomeBuf, (s && *s) ? s : ".");
    for (i = 0; cands[i] != NULL; i++) {
      if (is_simhome(cands[i])) {
	strcpy(SvHomeBuf, cands[i]);
	break;
      }
    }
  }
  SvHome = SvHomeBuf;
  sprintf(SvResBuf, "%s/res", SvHome);
  HomeDir = SvHome;
  ResourceDir = SvResBuf;
  HostName = "localhost";
}


/* ---- engine bring-up (the original sim_init) ----------------------------- */

static void
engine_init()
{
  gettimeofday(&start_time, NULL);
  gettimeofday(&beat_time, NULL);
  gettimeofday(&now_time, NULL);
  last_now_time = now_time;

  UserSoundOn = SvSoundWanted;
  MustUpdateOptions = 1;
  HaveLastMessage = 0;
  ScenarioID = 0;
  StartingYear = 1900;
  tileSynch = 0x01;
  sim_skips = sim_skip = 0;
  autoGo = 1;
  CityTax = 7;
  CityTime = 50;
  NoDisasters = 0;
  PunishCnt = 0;
  autoBulldoze = 1;
  autoBudget = 1;
  MesNum = 0;
  LastMesTime = 0;
  flagBlink = 1;
  SimSpeed = 3;
  ChangeEval();
  MessagePort = 0;
  MesX = MesY = 0;
  sim_paused = 0;
  sim_loops = 0;
  InitSimLoad = 2;
  tkMustExit = 0;
  ExitReturn = 0;

  sim = MakeNewSim();

  SvEditor = MakeNewView();
  SvEditor->title = "Editor";
  SvEditor->type = X_Mem_View;
  SvEditor->class = Editor_Class;
  SvEditor->visible = 1;
  SvEditor->tool_state = residentialState;
  SvEditor->tool_state_save = -1;
  SvEditor->tool_x_const = SvEditor->tool_y_const = -1;
  SvEditor->map_state = ALMAP;
  SvEditor->super_user = 0;
  SvEditor->show_me = 1;
  SvEditor->auto_goto = 1;
  SvEditor->auto_speed = 75;
  SvEditor->i_width = WORLD_X << 4;
  SvEditor->i_height = WORLD_Y << 4;
  SvEditor->pan_x = WORLD_X << 3;
  SvEditor->pan_y = WORLD_Y << 3;
  sim->editor = SvEditor;
  sim->editors = 1;

  SvMap = MakeNewView();
  SvMap->title = "Map";
  SvMap->type = X_Mem_View;
  SvMap->class = Map_Class;
  SvMap->visible = 1;
  SvMap->map_state = ALMAP;
  SvMap->show_editors = 1;
  SvMap->i_width = SvMap->w_width = SvMap->m_width = WORLD_X * 3;
  SvMap->i_height = SvMap->w_height = SvMap->m_height = WORLD_Y * 3;
  SvMap->tool_x_const = SvMap->tool_y_const = -1;
  sim->map = SvMap;
  sim->maps = 1;

  InitializeSound();
  initMapArrays();
  initGraphs();
  InitFundingLevel();
  setUpMapProcs();
  StopEarthquake();
  ResetMapState();
  ResetEditorState();
  ClearMap();
  InitWillStuff();
  SetFunds(5000);
  SetGameLevelFunds(StartupGameLevel);
  setSpeed(0);
  setSkips(0);
}


/* ---- windows ------------------------------------------------------------ */

/* Put a frame at screen position (x, y), on the screen.  A subframe's
 * position is relative to the base frame. */
void
sv_place(frame, x, y)
  Frame frame;
  int x, y;
{
  int w = (int)window_get(frame, WIN_WIDTH);
  int h = (int)window_get(frame, WIN_HEIGHT);

  if (x + w > SvScreenW)
    x = SvScreenW - w;
  if (y + h > SvScreenH)
    y = SvScreenH - h;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (frame != SvFrame) {
    x -= (int)window_get(SvFrame, WIN_X);
    y -= (int)window_get(SvFrame, WIN_Y);
  }
  window_set(frame, WIN_X, x, WIN_Y, y, 0);
}


/* Show a subframe on top of the others (a shown one is put on top by
 * showing it again). */
void
sv_show_frame(frame)
  Frame frame;
{
  if (frame == NULL)
    return;
  if ((int)window_get(frame, WIN_SHOW))
    window_set(frame, WIN_SHOW, FALSE, 0);
  window_set(frame, WIN_SHOW, TRUE, 0);
}


static int ui_placed = 0;		/* -Wp: the user placed the controls */

static void
ui_create(argc, argv)
  int *argc;
  char **argv;
{
  Rect *r;
  int i, x, w, h;

  for (i = 1; i < *argc; i++)
    if (strcmp(argv[i], "-Wp") == 0 || strcmp(argv[i], "-position") == 0)
      ui_placed = 1;

  SvFrame = window_create((Window)NULL, FRAME,
			  FRAME_LABEL, "Micropolis Controls",
			  FRAME_ARGC_PTR_ARGV, argc, argv,
			  WIN_ERROR_MSG,
			  "micropolis: must be run from SunView (suntools)",
			  0);
  if (SvFrame == NULL)
    exit(1);

  r = (Rect *)window_get(SvFrame, WIN_SCREEN_RECT);
  if (r != NULL && r->r_width > 0) {
    SvScreenW = r->r_width;
    SvScreenH = r->r_height;
  }
  sv_head_create();
  if (!ui_placed)
    window_set(SvFrame, WIN_X, 5, WIN_Y, 5, 0);

  /* the editor fills the screen to the right of the controls, like the
   * DUX one beside the head window */
  x = (int)window_get(SvFrame, WIN_WIDTH) + 15;
  w = SvScreenW - x - 5;
  h = (SvScreenH * 2) / 3;
  if (w < 400)
    w = 400;
  if (h < 400)
    h = 400;
  sv_map_create();
  sv_editor_create(w, h);
}


/* First tick, the controls on the screen: show the map below them and
 * the editor beside them. */
static void
ui_show()
{
  int bx = (int)window_get(SvFrame, WIN_X);
  int by = (int)window_get(SvFrame, WIN_Y);
  int bw = (int)window_get(SvFrame, WIN_WIDTH);
  int bh = (int)window_get(SvFrame, WIN_HEIGHT);

  sv_place(SvMapFrame, bx, by + bh + 5);
  sv_place(SvEditorFrame, bx + bw + 5, by);
  window_set(SvMapFrame, WIN_SHOW, TRUE, 0);
  window_set(SvEditorFrame, WIN_SHOW, TRUE, 0);
  sv_log("controls %dx%d+%d+%d\n", (char *)bw, (char *)bh, (char *)bx,
	 (char *)by);
  sv_log("map at %d,%d editor at %d,%d (relative)\n",
	 (char *)window_get(SvMapFrame, WIN_X),
	 (char *)window_get(SvMapFrame, WIN_Y),
	 (char *)window_get(SvEditorFrame, WIN_X),
	 (char *)window_get(SvEditorFrame, WIN_Y));
}


/* Decide color or mono from the editor's pixwin, load the art, install the
 * colormap on every canvas. */
static void
art_init(force_mono)
  int force_mono;
{
  Pixwin *pw = canvas_pixwin(SvEditorCanvas);

  SvColor = (!force_mono && pw->pw_pixrect->pr_depth >= 8);
  sv_init_colors();
  sv_load_art();
  sv_tools_load();
  sv_head_load();
  sv_map_load();
  sv_graph_load();
  sv_install_cms();
}


void
sv_install_cms()
{
  SvCmsDirty = 0;
  sv_head_cms();
  sv_map_cms();
  sv_editor_cms();
  sv_dialogs_cms();
}


/* ---- the tick ----------------------------------------------------------- */

static Notify_value
sv_tick(client, which)
  Notify_client client;
  int which;
{
  int j;

  if (!SvReady || SvBusy)
    return NOTIFY_DONE;
  SvTicks++;
  if (SvTicks == 1)
    ui_show();

  gettimeofday(&now_time, NULL);
  flagBlink = (now_time.tv_usec < 500000) ? 1 : -1;

  if (SimSpeed) {
    if (heat_steps) {
      for (j = 0; j < heat_steps; j++)
	sim_heat();
      MoveObjects();
      NewMap = 1;
    } else {
      SimFrame();
      MoveObjects();
      if (DoAnimation)
	animateTiles();
    }
    sim_loops++;
  }

  if (SvFireBombs > 0 && --SvFireBombWait <= 0) {
    FireBomb();
    SvFireBombs--;
    SvFireBombWait = 6;
  }

  sv_editor_update();
  sv_map_update();
  DoUpdateHeads();
  graphDoer();
  UpdateBudgetWindow();
  scoreDoer();
  sv_tools_update();
  sv_head_update();
  sv_dialogs_update();
  sv_sound_update();
  if (SvCmsDirty)
    sv_install_cms();

  if (tkMustExit)
    sv_quit_now();
  return NOTIFY_DONE;
}


static void
start_timer()
{
  struct itimerval itv;

  itv.it_interval.tv_sec = sim_delay / 1000;
  itv.it_interval.tv_usec = (sim_delay % 1000) * 1000;
  itv.it_value = itv.it_interval;
  notify_set_itimer_func((Notify_client)SvFrame, sv_tick, ITIMER_REAL,
			 &itv, (struct itimerval *)NULL);
}


/* The Priority menu: milliseconds between ticks. */
void
sv_set_priority(delay)
  int delay;
{
  if (delay < 10)
    delay = 10;
  sim_delay = delay;
  if (SvReady)
    start_timer();
}


/* ---- game control (menus and the engine's UI hooks) -------------------- */

static void
everything_changed()
{
  sv_editor_invalidate();
  sv_map_invalidate();
  UpdateHeads();
  UpdateBudget();
  ChangeCensus();
  ChangeEval();
  MustUpdateOptions = 1;
  sv_tools_redraw();
  sv_head_redraw();
  sv_editor_title();
}


/* Generate a new random city at the given level (0 easy .. 2 hard). */
void
sv_new_city(level)
  int level;
{
  ClearMes();
  sv_stop_all_sounds();
  GenerateNewCity();
  strcpy(SvCityNameBuf, "Micropolis");
  setCityName(SvCityNameBuf);
  SetGameLevelFunds(level);
  sim_paused = 0;
  setSpeed(3);
  everything_changed();
}


void
sv_did_generate_new_city()
{
  sv_editor_pan_to(WORLD_X / 2, WORLD_Y / 2);
  everything_changed();
}


/* Load a .cty file; returns 1 on success. */
int
sv_load_city(path)
  char *path;
{
  char *copy;

  copy = (char *)malloc((unsigned)strlen(path) + 1);
  strcpy(copy, path);			/* LoadCity edits its argument */
  sv_stop_all_sounds();
  if (!LoadCity(copy)) {
    free(copy);
    return 0;
  }
  free(copy);
  DoSimInit();
  sim_paused = 0;
  setSpeed(3);
  Kick();
  everything_changed();
  return 1;
}


void
sv_did_load_city()
{
  sv_editor_pan_to(WORLD_X / 2, WORLD_Y / 2);
  everything_changed();
}


void
sv_didnt_load_city(msg)
  char *msg;
{
  sv_log_message(msg, SV_TAG_ALERT);
  sv_alert(msg);
}


void
sv_load_scenario(n)
  int n;
{
  sv_stop_all_sounds();
  ClearMes();
  sim_paused = 0;
  LoadScenario(n);
  sim_paused = 0;
  setSpeed(3);
  sv_head_redraw();
}


void
sv_did_load_scenario()
{
  sv_editor_pan_to(WORLD_X / 2, WORLD_Y / 2);
  everything_changed();
  sv_notice_show(ScenarioID, (char **)NULL);
}


void
sv_save_city()
{
  SaveCity();				/* asks for a name if it has none */
}


void
sv_save_city_as()
{
  sv_file_save_as();
}


void
sv_did_save_city()
{
  char msg[700];

  sprintf(msg, "Saved the city in \"%.600s\".",
	  CityFileName ? CityFileName : "?");
  sv_log_message(msg, SV_TAG_STATUS);
  sv_head_redraw();
}


void
sv_didnt_save_city(msg)
  char *msg;
{
  sv_log_message(msg, SV_TAG_ALERT);
  sv_alert(msg);
}


/* GameStarted() hooks: the Tcl version showed its start screen here. */
void
sv_play_new_city()
{
  sv_new_city(StartupGameLevel);
}


void
sv_really_start_game()
{
  sv_new_city(StartupGameLevel);
}


void
sv_start_load()
{
  DoSimInit();
  setSpeed(3);
  everything_changed();
}


void
sv_start_scenario(n)
  int n;
{
  sv_load_scenario(n);
}


/* Engine hook DoNewGame (from InitWillStuff, after a city was generated or
 * loaded): like the DUX UINewGame, reset the windows for a new game.  The
 * callers set the speed again afterwards. */
void
sv_new_game()
{
  sv_budget_reset();
  InitGame();
  EraseOverlay();
  sv_graph_reset();
  sv_editor_invalidate();
  sv_map_invalidate();
}


void
sv_drop_fire_bombs()
{
  FireBomb();
  SvFireBombs = 3;
  SvFireBombWait = 6;
}


void
sv_win_game()
{
  sv_notice_show(100, (char **)NULL);
}


void
sv_lose_game()
{
  sv_notice_show(200, (char **)NULL);
  Pause();
}


void
sv_popup_message(msg)
  char *msg;
{
  sv_alert(msg);
}


void
sv_toggle_pause()
{
  if (sim_paused)
    Resume();
  else
    Pause();
  sv_head_redraw();
}


void
sv_set_sim_speed(n)
  int n;
{
  if (n == 0) {
    Pause();
  } else {
    if (sim_paused)
      Resume();
    setSpeed(n);
  }
  sv_head_redraw();
}


void
sv_quit()
{
  if (sv_ask("Do you want to quit playing Micropolis?", "I quit!",
	     "Keep playing."))
    sv_quit_now();
}


void
sv_quit_now()
{
  SvReady = 0;
  ShutDownSound();
  window_set(SvFrame, FRAME_NO_CONFIRM, TRUE, 0);
  window_destroy(SvFrame);
}


/* ---- engine hooks for the run loop ------------------------------------- */

sim_exit(val)
  int val;
{
  tkMustExit = 1;
  ExitReturn = val;
}


sim_really_exit(val)
  int val;
{
  ShutDownSound();
  exit(val);
}


/* ---- main --------------------------------------------------------------- */

static void
usage()
{
  fprintf(stderr,
	  "usage: micropolis [-l level] [-s scenario] [-seed n] [-mono]\n");
  fprintf(stderr,
	  "                  [-nosound] [-debug] [city.cty]\n");
  fprintf(stderr,
	  "  level: 0 easy, 1 medium, 2 hard   scenario: 1..8\n");
  fprintf(stderr,
	  "  -mono: black and white (1 bit) art, the default on 1 bit screens\n");
  fprintf(stderr,
	  "  SIMHOME must point at the SimCity directory (images/, res/).\n");
  exit(2);
}


main(argc, argv)
  int argc;
  char **argv;
{
  char *loadfile = NULL;
  int scenario = 0, seed = -1, force_mono = 0, i;

  if (getenv("SV_DEBUG"))
    SvDebug = 1;

  env_init();
  sv_init_fonts();			/* the windows are sized by them */
  ui_create(&argc, argv);		/* eats the SunView -W options */

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
      StartupGameLevel = atoi(argv[++i]);
      if (StartupGameLevel < 0 || StartupGameLevel > 2)
	usage();
    } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      scenario = atoi(argv[++i]);
      if (scenario < 1 || scenario > 8)
	usage();
    } else if (strcmp(argv[i], "-seed") == 0 && i + 1 < argc) {
      seed = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-mono") == 0) {
      force_mono = 1;
    } else if (strcmp(argv[i], "-nosound") == 0) {
      SvSoundWanted = 0;
    } else if (strcmp(argv[i], "-debug") == 0) {
      SvDebug = 1;
    } else if (argv[i][0] == '-' || loadfile != NULL) {
      usage();
    } else {
      loadfile = argv[i];
    }
  }
  sv_log("SIMHOME %s\n", SvHome);

  art_init(force_mono);
  engine_init();
  SvReady = 1;

  if (loadfile != NULL) {
    if (!sv_load_city(loadfile)) {
      fprintf(stderr, "micropolis: cannot load city %s\n", loadfile);
      sv_new_city(StartupGameLevel);
    }
  } else if (scenario > 0) {
    sv_load_scenario(scenario);
  } else {
    if (seed >= 0) {
      GenerateSomeCity(seed);
      strcpy(SvCityNameBuf, "Micropolis");
      setCityName(SvCityNameBuf);
      SetGameLevelFunds(StartupGameLevel);
      sim_paused = 0;
      setSpeed(3);
      everything_changed();
    } else {
      sv_new_city(StartupGameLevel);
    }
  }

  start_timer();
  window_main_loop(SvFrame);

  ShutDownSound();
  exit(ExitReturn);
}
