#include "sim.h"


/* Stubs */


QUAD TotalFunds;
short PunishCnt;
short autoBulldoze, autoBudget;
QUAD LastMesTime;
short GameLevel;
short InitSimLoad;
short ScenarioID;
short SimSpeed;
short SimMetaSpeed;
short UserSoundOn;
char *CityName;
short NoDisasters;
short MesNum;
short EvalChanged;
short flagBlink;


Spend(dollars)
  int dollars;
{
  SetFunds(TotalFunds - dollars);
}


SetFunds(dollars)
  int dollars;
{
  TotalFunds = dollars;
  UpdateFunds();
}


/* Mac */

QUAD TickCount()
{
  struct timeval time;

  gettimeofday(&time, 0);

  return (QUAD)((time.tv_sec / 60) + (time.tv_usec * 1000000 / 60));
}


Ptr
NewPtr(size)
int size;
{
  return ((Ptr)calloc(size, sizeof(Byte)));
}


/* w_hlhandlers.c */

GameStarted()
{
  InvalidateMaps();
  InvalidateEditors();
  gettimeofday(&start_time, NULL);

  switch (Startup) {
  case -2: /* Load a city */
    if (LoadCity(StartupName)) {
      DoStartLoad();
      StartupName = NULL;
      break;
    }
    StartupName = NULL;
  case -1:
    if (StartupName != NULL) {
      setCityName(StartupName);
      StartupName = NULL;
    } else {
      setCityName("NowHere");
    }
    DoPlayNewCity();
    break;
  case 0:
    DoReallyStartGame();
    break;
  default: /* scenario number */
    DoStartScenario(Startup);
    break;
  }
}


DoPlayNewCity()
{
  sv_play_new_city();
}


DoReallyStartGame()
{
  sv_really_start_game();
}


DoStartLoad()
{
  sv_start_load();
}


DoStartScenario(scenario)
  int scenario;
{
  sv_start_scenario(scenario);
}


DropFireBombs()
{
  sv_drop_fire_bombs();
}


InitGame()
{
  sim_skips = sim_skip = sim_paused = sim_paused_speed = heat_steps = 0;
  setSpeed(0);
}


ReallyQuit()
{
  sim_exit(0); /* Just sets tkMustExit and ExitReturn */
}


