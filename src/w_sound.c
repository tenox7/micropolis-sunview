#include "sim.h"


/* Sound routines */


int SoundInitialized = 0;
short Dozing;


InitializeSound()
{
  SoundInitialized = 1;

  if (!UserSoundOn) return;

  sv_init_sound();
}


ShutDownSound()
{
  if (SoundInitialized) {
    SoundInitialized = 0;
    sv_shutdown_sound();
  }
}


MakeSound(channel, id)
  char *channel;
  char *id;
{
  if (!UserSoundOn) return;
  if (!SoundInitialized) InitializeSound();

  sv_play_sound(channel, id, 0);
}


MakeSoundOn(view, channel, id)
  SimView *view;
  char *channel;
  char *id;
{
  /* SunView port: one audio device, so per-view sounds just play. */
  MakeSound(channel, id);
}


StartBulldozer()
{
  if (!UserSoundOn) return;
  if (!SoundInitialized) InitializeSound();
  if (!Dozing) {
    DoStartSound("edit", "1");
    Dozing = 1;
  }
}


StopBulldozer()
{
  if ((!UserSoundOn) || (!SoundInitialized)) return;
  DoStopSound("1");
  Dozing = 0;
}


/* comefrom: doKeyEvent */
SoundOff()
{
  if (!SoundInitialized) InitializeSound();
  sv_stop_all_sounds();
  Dozing = 0;
}


DoStartSound(channel, id)
  char *channel;
  char *id;
{
  sv_play_sound(channel, id, 1);
}


DoStopSound(id)
  char *id;
{
  sv_stop_sound(id);
}
