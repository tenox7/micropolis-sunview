/* sv_sound.c -- sound effects through /dev/audio.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * The DUX sound files ($SIMHOME/res/*.au) are 8 kHz mu-law, which is what
 * the SPARCstation audio device plays natively.  One sound plays at a time;
 * a new sound cuts off the old one.  The device is set non-blocking and
 * fed a little ahead from the 50 ms tick, so the game never waits for it.
 * "-speed N" (percent) from the original Tcl sound server is done by
 * resampling when the sound is loaded.
 */

#include "sv.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef sun
#include <sys/stropts.h>
#endif

typedef struct SvSound {
  struct SvSound *next;
  char name[40];
  int speed;
  unsigned char *data;
  int len;
} SvSound;

static SvSound *snd_cache = NULL;
static int snd_fd = -1;
static int snd_last_open = -1000;	/* tick of the last open attempt */
static SvSound *snd_cur = NULL;
static int snd_pos = 0;
static int snd_repeat = 0;
static char snd_cur_id[40];

#define SND_AHEAD 2048			/* bytes queued per tick, 1/4 s */

static struct {
  char *name, *file;
} snd_files[] = {
  { "HeavyTraffic", "traffic" },
  { "HonkHonk-Low", "honk-low" },
  { "HonkHonk-Med", "honk-med" },
  { "HonkHonk-High", "honk-hi" },
  { "Explosion-High", "expl-hi" },
  { "Explosion-Low", "expl-low" },
  { "QuackQuack", "quack" },
  { NULL, NULL }
};


static void
snd_open()
{
  char *dev = getenv("AUDIODEV");
  int on = 1;

  if (snd_fd >= 0 || SvTicks - snd_last_open < 100)
    return;
  snd_last_open = SvTicks;
  snd_fd = open(dev ? dev : "/dev/audio", O_WRONLY | O_NDELAY);
  /* O_NDELAY in open() only keeps a busy device from blocking the open;
   * the writes need FIONBIO.  Without it a full queue (a stalled device)
   * blocks write(), and a signal restarts the write: the game hangs. */
  if (snd_fd >= 0)
    ioctl(snd_fd, FIONBIO, &on);
  sv_log("audio device %s: fd %d\n", dev ? dev : "/dev/audio",
	 (char *)snd_fd);
}


static void
snd_flush()
{
#ifdef I_FLUSH
  if (snd_fd >= 0)
    ioctl(snd_fd, I_FLUSH, FLUSHW);
#endif
}


static unsigned int
get32(p)
  unsigned char *p;
{
  return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
	 ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}


/* Load (and cache) a sound by its Tcl name, resampled to speed percent. */
static SvSound *
snd_load(name, speed)
  char *name;
  int speed;
{
  SvSound *s;
  char path[700], file[64], *buf;
  unsigned char *raw;
  int i, len, off, n;

  for (s = snd_cache; s != NULL; s = s->next)
    if (s->speed == speed && strcmp(s->name, name) == 0)
      return s->data ? s : NULL;

  s = (SvSound *)calloc(1, sizeof(SvSound));
  strncpy(s->name, name, sizeof(s->name) - 1);
  s->speed = speed;
  s->next = snd_cache;
  snd_cache = s;

  file[0] = 0;
  for (i = 0; snd_files[i].name != NULL; i++)
    if (strcmp(snd_files[i].name, name) == 0)
      strcpy(file, snd_files[i].file);
  if (!file[0]) {
    for (i = 0; name[i] && i < (int)sizeof(file) - 1; i++)
      file[i] = isupper(name[i]) ? tolower(name[i]) : name[i];
    file[i] = 0;
  }
  sprintf(path, "%s/%s.au", ResourceDir, file);
  if ((buf = sv_read_file(path, &len)) == NULL) {
    sv_log("no sound %s\n", path);
    return NULL;
  }
  raw = (unsigned char *)buf;
  off = 0;
  if (len >= 24 && raw[0] == '.' && raw[1] == 's' && raw[2] == 'n' &&
      raw[3] == 'd') {
    off = (int)get32(raw + 4);
    if (off < 24 || off > len)
      off = 24;
  }
  len -= off;
  if (speed <= 0)
    speed = 100;
  n = (int)(((long)len * 100) / speed);
  if (n <= 0) {
    free(buf);
    return NULL;
  }
  s->data = (unsigned char *)malloc((unsigned)n);
  for (i = 0; i < n; i++)
    s->data[i] = raw[off + (int)(((long)i * speed) / 100)];
  s->len = n;
  free(buf);
  return s;
}


static void
snd_start(name, speed, repeat)
  char *name;
  int speed, repeat;
{
  SvSound *s;

  if (!UserSoundOn)
    return;
  snd_open();
  if (snd_fd < 0)
    return;
  if ((s = snd_load(name, speed)) == NULL)
    return;
  if (snd_cur != NULL)
    snd_flush();
  snd_cur = s;
  snd_pos = 0;
  snd_repeat = repeat;
  strncpy(snd_cur_id, name, sizeof(snd_cur_id) - 1);
  sv_sound_update();
}


/* Per tick: keep the device fed. */
void
sv_sound_update()
{
  int n, left;

  if (snd_cur == NULL || snd_fd < 0)
    return;
  left = SND_AHEAD;
  while (left > 0) {
    n = snd_cur->len - snd_pos;
    if (n <= 0) {
      if (snd_repeat) {
	snd_pos = 0;
	continue;
      }
      snd_cur = NULL;
      return;
    }
    if (n > left)
      n = left;
    n = write(snd_fd, (char *)snd_cur->data + snd_pos, n);
    if (n <= 0)
      return;			/* device full (EWOULDBLOCK): next tick */
    snd_pos += n;
    left -= n;
  }
}


/* ---- engine hooks (w_sound.c) -------------------------------------------- */

void
sv_init_sound()
{
  snd_open();
}


void
sv_shutdown_sound()
{
  sv_stop_all_sounds();
  snd_flush();			/* close() would wait for the queued sound */
  if (snd_fd >= 0)
    close(snd_fd);
  snd_fd = -1;
}


/* Find "-speed N" in a sound id (no strstr in every SunOS libc). */
static int
id_speed(id)
  char *id;
{
  for (; *id; id++)
    if (strncmp(id, "-speed ", 7) == 0 && isdigit(id[7]))
      return atoi(id + 7);
  return 100;
}


/* id is a Tcl sound name, maybe followed by options ("-speed 140"). */
void
sv_play_sound(channel, id, repeat)
  char *channel, *id;
  int repeat;
{
  char name[40];
  int i, speed;

  for (i = 0; id[i] && id[i] != ' ' && i < (int)sizeof(name) - 1; i++)
    name[i] = id[i];
  name[i] = 0;
  if (!name[0] || isdigit(name[0]))
    return;
  speed = id_speed(id);
  if (speed < 25 || speed > 400)
    speed = 100;
  snd_start(name, speed, repeat);
}


void
sv_play_sound_speed(name, speed)
  char *name;
  int speed;
{
  snd_start(name, speed, 0);
}


void
sv_stop_sound(id)
  char *id;
{
  if (snd_cur != NULL && strcmp(snd_cur_id, id) == 0) {
    snd_cur = NULL;
    snd_flush();
  }
}


void
sv_stop_all_sounds()
{
  if (snd_cur != NULL) {
    snd_cur = NULL;
    snd_flush();
  }
}
