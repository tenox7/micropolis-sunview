/* sv_stubs.c -- small engine services the Tcl/Tk front end used to provide.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 */

#include "sv.h"


Sim *
MakeNewSim()
{
  return (Sim *)calloc(1, sizeof(Sim));
}


SimView *
MakeNewView()
{
  return (SimView *)calloc(1, sizeof(SimView));
}


/* The Tk version scheduled an idle-time UI update here; the SunView tick
 * redraws whatever changed every 50 ms anyway. */
Kick()
{
}


/* Set by the Tk version while an idle-time update was pending. */
int UpdateDelayed = 0;


/* setSpeed() starts and stops the simulation timer.  Here the 50 ms tick
 * runs all the time (it also redraws the windows) and only calls
 * SimFrame() when SimSpeed is not 0, so there is nothing to do. */
StartMicropolisTimer()
{
}


StopMicropolisTimer()
{
}
