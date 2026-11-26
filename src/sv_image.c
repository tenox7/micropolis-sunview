/* sv_image.c -- colormap, XPM loading, fonts and drawing helpers.
 *
 * Micropolis, Unix Version.  Copyright (C) 1989 - 2007 Electronic Arts Inc.
 * Licensed under the GNU GPL v3 or later; see the LICENSE file for the full
 * text and the additional EA terms.  No trademark or publicity rights granted.
 *
 * All drawing goes into memory pixrects that hold colormap indices relative
 * to our own colormap segment (SunView adds the segment offset when a pixwin
 * writes them to the screen).  In monochrome mode the pixrects are 1 bit
 * deep.  Images then use the mono ("m") colors that DUX drew into many of
 * its XPM files, else each color goes to black or white by its brightness,
 * as libXpm does on a 1 bit X screen.  The tile art comes from tilesbw.xpm.
 */

#include "sv.h"

int SvColor = 1;
unsigned char SvRed[SV_CMS_SIZE];
unsigned char SvGreen[SV_CMS_SIZE];
unsigned char SvBlue[SV_CMS_SIZE];
unsigned char SvGray[SV_CMS_SIZE];
static int SvNColors = 0;
int SvCmsDirty = 0;			/* entries added after install */

int SvUiBack, SvUiLight, SvUiDark, SvUiText;

Pixfont *SvFont = NULL;
Pixfont *SvBoldFont = NULL;
Pixfont *SvBigFont = NULL;
Pixfont *SvHugeFont = NULL;

Pixrect *SvBigTiles = NULL;
unsigned char *SvSmallTiles = NULL;
SvImage *SvSprites[OBJN][16];
int SvSpriteFrames[OBJN];

/* the engine's 16 colors, in SIMCOLOR_* order (from the original w_x.c) */
static unsigned char sim_rgb[16][3] = {
  { 255, 255, 255 },	/* WHITE */
  { 255, 255,   0 },	/* YELLOW */
  { 255, 127,   0 },	/* ORANGE */
  { 255,   0,   0 },	/* RED */
  { 191,   0,   0 },	/* DARKRED */
  {   0,   0, 230 },	/* DARKBLUE */
  { 102, 102, 230 },	/* LIGHTBLUE */
  { 204,  76,  76 },	/* BROWN */
  {   0, 230,   0 },	/* LIGHTGREEN */
  {   0, 127,   0 },	/* DARKGREEN */
  { 153, 127,  76 },	/* OLIVE */
  { 204, 127, 102 },	/* LIGHTBROWN */
  { 191, 191, 191 },	/* LIGHTGRAY */
  { 127, 127, 127 },	/* MEDIUMGRAY */
  {  63,  63,  63 },	/* DARKGRAY */
  {   0,   0,   0 }	/* BLACK */
};


static void
set_entry(i, r, g, b)
  int i, r, g, b;
{
  SvRed[i] = r;
  SvGreen[i] = g;
  SvBlue[i] = b;
  SvGray[i] = (r * 30 + g * 59 + b * 11) / 100;
}


/* Entry 0 is the window background and the last entry the foreground, as
 * SunView expects; SIMCOLOR_WHITE happens to be 0 so the two agree. */
void
sv_init_colors()
{
  int i;

  for (i = 0; i < SV_CMS_SIZE; i++)
    set_entry(i, 0, 0, 0);
  for (i = 0; i < 16; i++)
    set_entry(i, sim_rgb[i][0], sim_rgb[i][1], sim_rgb[i][2]);
  SvNColors = 16;
  set_entry(SV_FG, 0, 0, 0);

  SvUiBack = sv_alloc_color(176, 176, 176);	/* DUX window gray */
  SvUiLight = sv_alloc_color(216, 216, 216);
  SvUiDark = SIMCOLOR_MEDIUMGRAY;
  SvUiText = SIMCOLOR_BLACK;
}


/* Find or allocate a colormap entry; nearest match when the map is full. */
int
sv_alloc_color(r, g, b)
  int r, g, b;
{
  int i, best, d, bestd, dr, dg, db;

  for (i = 0; i < SvNColors; i++)
    if (SvRed[i] == r && SvGreen[i] == g && SvBlue[i] == b)
      return i;
  if (r == 0 && g == 0 && b == 0)
    return SV_FG;
  if (SvNColors < SV_FG) {
    set_entry(SvNColors, r, g, b);
    SvCmsDirty = 1;
    return SvNColors++;
  }
  best = 0;
  bestd = 0x7fffffff;
  for (i = 0; i < SvNColors; i++) {
    dr = SvRed[i] - r;
    dg = SvGreen[i] - g;
    db = SvBlue[i] - b;
    d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
    if (d < bestd) {
      bestd = d;
      best = i;
    }
  }
  return best;
}


/* Install our colormap segment on a pixwin (color mode only). */
void
sv_set_cms(pw)
  Pixwin *pw;
{
  if (!SvColor || pw == NULL)
    return;
  pw_setcmsname(pw, SV_CMS_NAME);
  pw_putcolormap(pw, 0, SV_CMS_SIZE, SvRed, SvGreen, SvBlue);
}


/* Pixel value to store for colormap index c (1 bit: black = 1). */
int
sv_pix(c)
  int c;
{
  if (SvColor)
    return c;
  return (SvGray[c & (SV_CMS_SIZE - 1)] < 128) ? 1 : 0;
}


/* ---- files ------------------------------------------------------------- */

char *
sv_read_file(path, lenp)
  char *path;
  int *lenp;
{
  FILE *fp;
  char *buf;
  long len;

  if ((fp = fopen(path, "r")) == NULL)
    return NULL;
  fseek(fp, 0L, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0L, SEEK_SET);
  if (len < 0 || (buf = (char *)malloc((unsigned)len + 1)) == NULL) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (int)len, fp) != (int)len) {
    free(buf);
    fclose(fp);
    return NULL;
  }
  buf[len] = 0;
  fclose(fp);
  if (lenp)
    *lenp = (int)len;
  return buf;
}


/* ---- XPM --------------------------------------------------------------- */

static struct {
  char *name;
  unsigned char r, g, b;
} named_colors[] = {
  { "black", 0, 0, 0 },
  { "white", 255, 255, 255 },
  { "red", 255, 0, 0 },
  { "green", 0, 255, 0 },
  { "blue", 0, 0, 255 },
  { "yellow", 255, 255, 0 },
  { "cyan", 0, 255, 255 },
  { "magenta", 255, 0, 255 },
  { "orange", 255, 165, 0 },
  { "brown", 165, 42, 42 },
  { "gray", 190, 190, 190 },
  { "grey", 190, 190, 190 },
  { NULL, 0, 0, 0 }
};


static int
hexval(c)
  int c;
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}


static int
lower_eq(a, b)
  char *a, *b;
{
  while (*a && *b) {
    if ((isupper(*a) ? tolower(*a) : *a) != (isupper(*b) ? tolower(*b) : *b))
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}


/* Parse one color value.  Returns 0 for "None" (transparent), 1 otherwise. */
static int
parse_color(val, rgb)
  char *val;
  int *rgb;
{
  int n, per, i, c, v;

  if (lower_eq(val, "none"))
    return 0;
  if (*val == '#') {
    val++;
    for (n = 0; hexval(val[n]) >= 0; n++) ;
    per = n / 3;
    if (per < 1) {
      rgb[0] = rgb[1] = rgb[2] = 0;
      return 1;
    }
    for (c = 0; c < 3; c++) {
      v = 0;
      for (i = 0; i < per; i++)
	v = (v << 4) | hexval(val[c * per + i]);
      if (per == 1)
	v *= 17;
      else if (per > 2)
	v >>= 4 * (per - 2);
      rgb[c] = v;
    }
    return 1;
  }
  for (i = 0; named_colors[i].name != NULL; i++) {
    if (lower_eq(val, named_colors[i].name)) {
      rgb[0] = named_colors[i].r;
      rgb[1] = named_colors[i].g;
      rgb[2] = named_colors[i].b;
      return 1;
    }
  }
  rgb[0] = rgb[1] = rgb[2] = 128;
  return 1;
}


/* Split s into whitespace separated words (in place). */
static int
split_words(s, w, nmax)
  char *s;
  char **w;
  int nmax;
{
  int n = 0;

  while (*s) {
    while (*s == ' ' || *s == '\t')
      s++;
    if (!*s)
      break;
    if (n < nmax)
      w[n++] = s;
    while (*s && *s != ' ' && *s != '\t')
      s++;
    if (*s)
      *s++ = 0;
  }
  return n;
}


/* Color line after the pixel characters: "c #rrggbb", "s name c None" ...
 * The key search order is libXpm's for a color or a 1 bit screen. */
static int
xpm_line_color(s, rgb, color)
  char *s;
  int *rgb;
  int color;
{
  char *w[16];
  int n, i, k;
  static char *ckeys[] = { "c", "g", "g4", "m" };
  static char *mkeys[] = { "m", "g4", "g", "c" };
  char **keys = color ? ckeys : mkeys;

  n = split_words(s, w, 16);
  for (k = 0; k < 4; k++)
    for (i = 0; i + 1 < n; i++)
      if (strcmp(w[i], keys[k]) == 0)
	return parse_color(w[i + 1], rgb);
  rgb[0] = rgb[1] = rgb[2] = 0;
  return 1;
}


/* Load an XPM file into a memory pixrect (and a 1 bit mask if it has
 * transparent pixels).  SV_XPM_COLOR: 8 bit colormap indices.  Otherwise
 * 1 bit (1 = black): the mono color, or black below half brightness;
 * SV_XPM_BRIGHT takes the brightest of r, g and b as the brightness, which
 * keeps the dark drawings on a blue sky. */
SvImage *
sv_load_xpm(path, mode)
  char *path;
  int mode;
{
  char *buf, *p, **strs, *row;
  int color = (mode == SV_XPM_COLOR);
  int len, nq, ns, w, h, nc, cpp, i, x, y, rgb[3], idx, ki, v;
  int lut[256];
  char *keys2;
  int *cidx;
  SvImage *img;
  unsigned char *pix, *mpix;
  int plb, mlb, opaque, hasmask;
  struct mpr_data *md;

  if ((buf = sv_read_file(path, &len)) == NULL)
    return NULL;

  for (nq = 0, p = buf; *p; p++)
    if (*p == '"')
      nq++;
  strs = (char **)malloc((unsigned)(nq / 2 + 1) * sizeof(char *));
  if (strs == NULL) {
    free(buf);
    return NULL;
  }

  ns = 0;
  p = buf;
  while (*p) {
    if (p[0] == '/' && p[1] == '*') {
      p += 2;
      while (*p && !(p[0] == '*' && p[1] == '/'))
	p++;
      if (*p)
	p += 2;
    } else if (*p == '"') {
      strs[ns++] = ++p;
      while (*p && *p != '"')
	p++;
      if (*p)
	*p++ = 0;
    } else {
      p++;
    }
  }

  if (ns < 1 || sscanf(strs[0], "%d %d %d %d", &w, &h, &nc, &cpp) != 4 ||
      w <= 0 || h <= 0 || nc <= 0 || cpp < 1 || cpp > 2 ||
      ns < 1 + nc + h) {
    fprintf(stderr, "micropolis: bad XPM file %s\n", path);
    free((char *)strs);
    free(buf);
    return NULL;
  }

  cidx = (int *)malloc((unsigned)nc * sizeof(int));
  keys2 = (char *)malloc((unsigned)nc * 2);
  for (i = 0; i < 256; i++)
    lut[i] = 0;
  hasmask = 0;
  for (i = 0; i < nc; i++) {
    char *cl = strs[1 + i];
    int k0, k1;

    k0 = (unsigned char)cl[0];
    k1 = (cpp > 1 && cl[0]) ? (unsigned char)cl[1] : 0;
    keys2[i * 2] = k0;
    keys2[i * 2 + 1] = k1;
    if ((int)strlen(cl) < cpp) {
      cidx[i] = 0;
      continue;
    }
    if (xpm_line_color(cl + cpp, rgb, color)) {
      if (color) {
	cidx[i] = sv_alloc_color(rgb[0], rgb[1], rgb[2]);
      } else {
	if (mode == SV_XPM_BRIGHT) {
	  v = rgb[0] > rgb[1] ? rgb[0] : rgb[1];
	  if (rgb[2] > v)
	    v = rgb[2];
	} else {
	  v = (rgb[0] * 30 + rgb[1] * 59 + rgb[2] * 11) / 100;
	}
	cidx[i] = (v < 128);
      }
    } else {
      cidx[i] = -1;
      hasmask = 1;
    }
    if (cpp == 1)
      lut[k0] = i;
  }

  img = (SvImage *)malloc(sizeof(SvImage));
  img->w = w;
  img->h = h;
  img->pr = mem_create(w, h, color ? 8 : 1);
  img->mask = hasmask ? mem_create(w, h, 1) : NULL;
  if (img->pr == NULL || (hasmask && img->mask == NULL)) {
    fprintf(stderr, "micropolis: out of memory loading %s\n", path);
    exit(1);
  }

  md = mpr_d(img->pr);
  pix = (unsigned char *)md->md_image;
  plb = md->md_linebytes;
  bzero((char *)pix, plb * h);
  mpix = NULL;
  mlb = 0;
  if (hasmask) {
    md = mpr_d(img->mask);
    mpix = (unsigned char *)md->md_image;
    mlb = md->md_linebytes;
    bzero((char *)mpix, mlb * h);
  }

  for (y = 0; y < h; y++) {
    row = strs[1 + nc + y];
    for (x = 0; x < w; x++) {
      if (!row[0] || (cpp > 1 && !row[1]))
	break;
      if (cpp == 1) {
	ki = lut[(unsigned char)row[0]];
      } else {
	for (ki = 0; ki < nc; ki++)
	  if (keys2[ki * 2] == row[0] && keys2[ki * 2 + 1] == row[1])
	    break;
	if (ki >= nc)
	  ki = 0;
      }
      row += cpp;
      idx = cidx[ki];
      opaque = (idx >= 0);
      if (!opaque)
	idx = 0;
      if (color)
	pix[y * plb + x] = idx;
      else if (idx)
	pix[y * plb + (x >> 3)] |= 0x80 >> (x & 7);
      if (hasmask && opaque)
	mpix[y * mlb + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }

  free((char *)cidx);
  free(keys2);
  free((char *)strs);
  free(buf);
  return img;
}


static SvImage *
load_image(name, mode)
  char *name;
  int mode;
{
  char path[700];
  SvImage *img;

  sprintf(path, "%s/images/%s", SvHome, name);
  img = sv_load_xpm(path, mode);
  if (img == NULL)
    sv_log("cannot load %s\n", path);
  return img;
}


/* An image from $SIMHOME/images in the display's format. */
SvImage *
sv_load_image(name)
  char *name;
{
  return load_image(name, SvColor ? SV_XPM_COLOR : SV_XPM_MONO);
}


/* The same, but 1 bit by the brightest color channel. */
SvImage *
sv_load_image_bright(name)
  char *name;
{
  return load_image(name, SvColor ? SV_XPM_COLOR : SV_XPM_BRIGHT);
}


void
sv_free_image(img)
  SvImage *img;
{
  if (img == NULL)
    return;
  if (img->pr)
    pr_destroy(img->pr);
  if (img->mask)
    pr_destroy(img->mask);
  free((char *)img);
}


/* ---- game art ---------------------------------------------------------- */

/* 1 bit sprites: black the white pixels at the sprite's edge, so that a
 * light sprite still shows on white land. */
static void
outline_sprite(img)
  SvImage *img;
{
  struct mpr_data *md;
  unsigned char *pix, *mpix, *out;
  int plb, mlb, x, y, edge;

#define SP_BIT(b, lb, x, y) ((b)[(y) * (lb) + ((x) >> 3)] & (0x80 >> ((x) & 7)))
  if (img == NULL || img->mask == NULL)
    return;
  md = mpr_d(img->pr);
  pix = (unsigned char *)md->md_image;
  plb = md->md_linebytes;
  md = mpr_d(img->mask);
  mpix = (unsigned char *)md->md_image;
  mlb = md->md_linebytes;
  if ((out = (unsigned char *)malloc((unsigned)(plb * img->h))) == NULL)
    return;
  bcopy((char *)pix, (char *)out, plb * img->h);
  for (y = 0; y < img->h; y++)
    for (x = 0; x < img->w; x++) {
      if (!SP_BIT(mpix, mlb, x, y) || SP_BIT(pix, plb, x, y))
	continue;
      edge = x == 0 || y == 0 || x == img->w - 1 || y == img->h - 1 ||
	!SP_BIT(mpix, mlb, x - 1, y) || !SP_BIT(mpix, mlb, x + 1, y) ||
	!SP_BIT(mpix, mlb, x, y - 1) || !SP_BIT(mpix, mlb, x, y + 1);
      if (edge)
	out[y * plb + (x >> 3)] |= 0x80 >> (x & 7);
    }
  bcopy((char *)out, (char *)pix, plb * img->h);
  free((char *)out);
#undef SP_BIT
}


/* Big tiles (16x16), small map tiles (3x3) and the sprite frames.  The
 * small tiles hold colormap indices; the 1 bit map needs none. */
void
sv_load_art()
{
  SvImage *img;
  struct mpr_data *md;
  unsigned char *p;
  int t, x, y, lb, type, frame;
  char name[64];

  img = sv_load_image(SvColor ? "tiles.xpm" : "tilesbw.xpm");
  if (img == NULL || img->w != 16 || img->h < 16 * TILE_COUNT) {
    fprintf(stderr,
	    "micropolis: cannot load the tile art from %s/images.\n", SvHome);
    fprintf(stderr, "Set SIMHOME to the SimCity directory.\n");
    exit(1);
  }
  SvBigTiles = img->pr;
  if (img->mask)
    pr_destroy(img->mask);
  free((char *)img);

  if (SvColor) {
    SvSmallTiles = (unsigned char *)malloc(TILE_COUNT * 9);
    img = load_image("tilessm.xpm", SV_XPM_COLOR);	/* indices */
    if (img == NULL || img->h < 3 * TILE_COUNT) {
      for (t = 0; t < TILE_COUNT * 9; t++)
	SvSmallTiles[t] = SIMCOLOR_LIGHTBROWN;
    } else {
      md = mpr_d(img->pr);
      p = (unsigned char *)md->md_image;
      lb = md->md_linebytes;
      for (t = 0; t < TILE_COUNT; t++)
	for (y = 0; y < 3; y++)
	  for (x = 0; x < 3; x++)
	    SvSmallTiles[t * 9 + y * 3 + x] = p[(t * 3 + y) * lb + x];
      sv_free_image(img);
    }
  }

  for (type = 0; type < OBJN; type++) {
    SvSpriteFrames[type] = 0;
    for (frame = 0; frame < 16; frame++)
      SvSprites[type][frame] = NULL;
  }
  for (type = 1; type < OBJN; type++) {
    for (frame = 0; frame < 16; frame++) {
      sprintf(name, "obj%d-%d.xpm", type, frame);
      if ((SvSprites[type][frame] = sv_load_image(name)) == NULL)
	break;
      if (!SvColor)
	outline_sprite(SvSprites[type][frame]);
      SvSpriteFrames[type] = frame + 1;
    }
  }
}


/* ---- fonts ------------------------------------------------------------- */

static Pixfont *
try_font(name)
  char *name;
{
  char path[256];

  sprintf(path, "/usr/lib/fonts/fixedwidthfonts/%s", name);
  return pf_open(path);
}


void
sv_init_fonts()
{
  SvFont = try_font("screen.r.12");
  if (SvFont == NULL)
    SvFont = pf_default();
  SvBoldFont = try_font("screen.b.12");
  if (SvBoldFont == NULL)
    SvBoldFont = SvFont;
  SvBigFont = try_font("screen.b.14");
  if (SvBigFont == NULL)
    SvBigFont = SvBoldFont;
  SvHugeFont = try_font("screen.b.16");
  if (SvHugeFont == NULL)
    SvHugeFont = SvBigFont;
}


int
sv_text_width(font, s)
  Pixfont *font;
  char *s;
{
  int w = 0;

  while (*s)
    w += font->pf_char[(unsigned char)*s++].pc_adv.x;
  return w;
}


int
sv_font_ascent(font)
  Pixfont *font;
{
  int a = -font->pf_char['A'].pc_home.y;

  if (a <= 0)
    a = (font->pf_defaultsize.y * 3) / 4;
  return a;
}


int
sv_font_height(font)
  Pixfont *font;
{
  return font->pf_defaultsize.y;
}


/* ---- drawing into memory pixrects -------------------------------------- */

void
sv_fill(pr, x, y, w, h, c)
  Pixrect *pr;
  int x, y, w, h, c;
{
  if (w <= 0 || h <= 0)
    return;
  pr_rop(pr, x, y, w, h, PIX_SRC | PIX_COLOR(sv_pix(c)), (Pixrect *)NULL,
	 0, 0);
}


void
sv_frame_rect(pr, x, y, w, h, c, t)
  Pixrect *pr;
  int x, y, w, h, c, t;
{
  sv_fill(pr, x, y, w, t, c);
  sv_fill(pr, x, y + h - t, w, t, c);
  sv_fill(pr, x, y + t, t, h - 2 * t, c);
  sv_fill(pr, x + w - t, y + t, t, h - 2 * t, c);
}


/* 3D-ish bevel like the DUX Tk widgets */
void
sv_bevel(pr, x, y, w, h, raised)
  Pixrect *pr;
  int x, y, w, h, raised;
{
  int tl = raised ? SvUiLight : SvUiDark;
  int br = raised ? SvUiDark : SvUiLight;

  if (!SvColor) {
    sv_frame_rect(pr, x, y, w, h, SIMCOLOR_BLACK, 1);
    return;
  }
  sv_fill(pr, x, y, w, 1, tl);
  sv_fill(pr, x, y, 1, h, tl);
  sv_fill(pr, x, y + h - 1, w, 1, br);
  sv_fill(pr, x + w - 1, y, 1, h, br);
}


/* A Tk border of 2 pixels (one black line in monochrome). */
void
sv_bevel2(pr, x, y, w, h, raised)
  Pixrect *pr;
  int x, y, w, h, raised;
{
  sv_bevel(pr, x, y, w, h, raised);
  if (SvColor)
    sv_bevel(pr, x + 1, y + 1, w - 2, h - 2, raised);
}


/* A DUX button: a raised 2 pixel border with the label centered in the
 * Large font. */
void
sv_button(pr, x, y, w, h, label)
  Pixrect *pr;
  int x, y, w, h;
  char *label;
{
  sv_fill(pr, x, y, w, h, SvUiBack);
  sv_bevel2(pr, x, y, w, h, 1);
  sv_text(pr, x + (w - sv_text_width(SvBigFont, label)) / 2,
	  y + (h - sv_font_height(SvBigFont)) / 2, SvUiText, SvBigFont, label);
}


void
sv_line(pr, x0, y0, x1, y1, c)
  Pixrect *pr;
  int x0, y0, x1, y1, c;
{
  pr_vector(pr, x0, y0, x1, y1, PIX_SRC, sv_pix(c));
}


/* Text with its top-left corner at (x, y); transparent background. */
void
sv_text(pr, x, y, c, font, s)
  Pixrect *pr;
  int x, y, c;
  Pixfont *font;
  char *s;
{
  if (!*s)
    return;
  pr_ttext(pr, x, y + sv_font_ascent(font), PIX_SRC | PIX_COLOR(sv_pix(c)),
	   font, s);
}


/* Draw an image, through its mask if it has one. */
void
sv_draw_image(pr, x, y, img)
  Pixrect *pr;
  int x, y;
  SvImage *img;
{
  if (img == NULL)
    return;
  if (img->mask == NULL) {
    pr_rop(pr, x, y, img->w, img->h, PIX_SRC, img->pr, 0, 0);
    return;
  }
#ifdef NO_PR_STENCIL
  /* clear the opaque area, then OR the image in (transparent pixels are 0) */
  pr_rop(pr, x, y, img->w, img->h, PIX_NOT(PIX_SRC) & PIX_DST, img->mask,
	 0, 0);
  pr_rop(pr, x, y, img->w, img->h, PIX_SRC | PIX_DST, img->pr, 0, 0);
#else
  pr_stencil(pr, x, y, img->w, img->h, PIX_SRC, img->mask, 0, 0,
	     img->pr, 0, 0);
#endif
}


/* Word-wrap s into lines no wider than w; calls back with each line. */
int
sv_wrap(font, s, w, fn, arg)
  Pixfont *font;
  char *s;
  int w;
  void (*fn)();
  char *arg;
{
  char line[512];
  int n, lines = 0, lastsp, i;

  while (*s) {
    n = 0;
    lastsp = -1;
    while (s[n] && s[n] != '\n' && n < (int)sizeof(line) - 1) {
      line[n] = s[n];
      line[n + 1] = 0;
      if (s[n] == ' ')
	lastsp = n;
      if (sv_text_width(font, line) > w && n > 0) {
	if (lastsp > 0)
	  n = lastsp;
	break;
      }
      n++;
    }
    for (i = 0; i < n; i++)
      line[i] = s[i];
    line[n] = 0;
    if (fn)
      (*fn)(line, lines, arg);
    lines++;
    s += n;
    if (*s == '\n')
      s++;
    else
      while (*s == ' ')			/* no blanks at the start of a line */
	s++;
  }
  return lines;
}
