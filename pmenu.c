#include <ctype.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xinerama.h>
#include <Imlib2.h>

#define PROGNAME "pmenu"
#define ITEMPREV 0
#define ITEMNEXT 1

/* macros */
#define LEN(x)              (sizeof (x) / sizeof (x[0]))
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))

/* color enum */
enum {ColorFG, ColorBG, ColorLast};

/* configuration structure */
struct Config {
	const char *font;
	const char *background_color;
	const char *foreground_color;
	const char *selbackground_color;
	const char *selforeground_color;
	const char *separator_color;
	const char *border_color;
	int border_pixels;
	int separator_pixels;
	unsigned diameter_pixels;
	double separatorbeg;
	double separatorend;
};

/* draw context structure */
struct DC {
	XftColor normal[ColorLast];     /* color of unselected slice */
	XftColor selected[ColorLast];   /* color of selected slice */
	XftColor border;                /* color of border */
	XftColor separator;             /* color of the separator */

	GC gc;                          /* graphics context */

	FcPattern *pattern;
	XftFont **fonts;
	size_t nfonts;
};

/* pie slice structure */
struct Slice {
	char *label;            /* string to be drawed on the slice */
	char *output;           /* string to be outputed when slice is clicked */
	char *file;             /* filename of the icon */
	size_t labellen;        /* strlen(label) */

	int x, y;               /* position of the pointer of the slice */
	int labelx, labely;     /* position of the label */
	int angle1, angle2;     /* angle of the borders of the slice */
	int linexi, lineyi;     /* position of the inner point of the line segment */
	int linexo, lineyo;     /* position of the outer point of the line segment */
	int iconx, icony;       /* position of the icon */

	struct Slice *prev;     /* previous slice */
	struct Slice *next;     /* next slice */
	struct Menu *submenu;   /* submenu spawned by clicking on slice */
	Imlib_Image icon;       /* icon */
};

/* menu structure */
struct Menu {
	struct Menu *parent;    /* parent menu */
	struct Slice *caller;   /* slice that spawned the menu */
	struct Slice *list;     /* list of slices contained by the pie menu */
	struct Slice *selected; /* slice currently selected in the menu */
	unsigned nslices;       /* number of slices */
	int x, y;               /* menu position */
	int halfslice;          /* angle of half a slice of the pie menu */
	unsigned level;         /* menu level relative to root */
	Drawable pixmap;        /* pixmap to draw the menu on */
	XftDraw *draw;          /* drawable to draw the text on*/
	Window win;             /* menu window to map on the screen */
};

/* monitor and cursor geometry structure */
struct Monitor {
	int x, y, w, h;         /* monitor geometry */
	int cursx, cursy;
};

/* geometry of the pie and bitmap that shapes it */
struct Pie {
	GC gc;              /* graphic context of the bitmaps */
	Drawable clip;      /* bitmap shaping the clip region (without borders) */
	Drawable bounding;  /* bitmap shaping the bounding region (with borders)*/

	int fulldiameter;   /* diameter of the pie + 2*border*/
	int diameter;       /* diameter of the pie */
	int radius;         /* radius of the pie */
	int border;         /* border of the pie */

	int innercirclex;
	int innercircley;
	int innercirclediameter;

	double separatorbeg;
	double separatorend;
};

/*
 * Functions declarations
 */

/* initializers, and their helper routine */
static void ealloccolor(const char *s, XftColor *color);
static void parsefonts(const char *s);
static void initmonitor(void);
static void initresources(void);
static void initdc(void);
static void initpie(void);

/* structure builders, and their helper routines */
static struct Slice *allocslice(const char *label, const char *output, char *file);
static struct Menu *allocmenu(struct Menu *parent, struct Slice *list, unsigned level);
static struct Menu *buildmenutree(unsigned level, const char *label, const char *output, char *file);
static struct Menu *parsestdin(void);

/* icon loader */
static Imlib_Image loadicon(const char *file, int size, int *width_ret, int *height_ret);

/* text drawer, and its helper routine */
static FcChar32 getnextutf8char(const char *s, const char **end_ret);
static XftFont *getfontucode(FcChar32 ucode);
static int drawtext(XftDraw *draw, XftColor *color, int x, int y, const char *text);

/* menu and slice setters, and their helper routines */
static void setupslices(struct Menu *menu);
static void setupmenupos(struct Menu *menu);
static void setupmenu(struct Menu *menu);

/* grabbers */
static void grabpointer(void);
static void grabkeyboard(void);

/* getters */
static struct Menu *getmenu(struct Menu *currmenu, Window win);
static struct Slice *getslice(struct Menu *menu, int x, int y);

/* menu drawers and mapper */
static void mapmenu(struct Menu *currmenu);
static void drawslice(struct Menu *menu, struct Slice *slice, XftColor *color);
static void drawmenu(struct Menu *currmenu);

/* cycle through slices */
static struct Slice *slicecycle(struct Menu *currmenu, int direction);

/* main event loop */
static void run(struct Menu *currmenu);

/* cleaners */
static void cleanmenu(struct Menu *menu);
static void cleanup(void);

/* show usage */
static void usage(void);

/*
 * Variable declarations
 */

/* X stuff */
static Display *dpy;
static int screen;
static Visual *visual;
static Window rootwin;
static Colormap colormap;
static struct DC dc;
static struct Monitor mon;

/* The pie bitmap structure */
static struct Pie pie;

#include "config.h"

/* pmenu: generate a pie menu from stdin and print selected entry to stdout */
int
main(int argc, char *argv[])
{
	struct Menu *rootmenu;

	argc--;
	argv++;
	if (argc != 0)
		usage();

	/* open connection to server and set X variables */
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	rootwin = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	/* initializers */
	initmonitor();
	initresources();
	initdc();
	initpie();

	/* generate menus and set them up */
	rootmenu = parsestdin();
	if (rootmenu == NULL)
		errx(1, "no menu generated");
	setupmenu(rootmenu);

	/* grab mouse and keyboard */
	grabpointer();
	grabkeyboard();

	/* run event loop */
	run(rootmenu);

	/* freeing stuff */
	cleanmenu(rootmenu);
	cleanup();

	return 0;
}

/* get color from color string */
static void
ealloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "could not allocate color: %s", s);
}

/* parse color string */
static void
parsefonts(const char *s)
{
	const char *p;
	char buf[1024];
	size_t nfont = 0;

	dc.nfonts = 1;
	for (p = s; *p; p++)
		if (*p == ',')
			dc.nfonts++;

	if ((dc.fonts = calloc(dc.nfonts, sizeof *dc.fonts)) == NULL)
		err(1, "calloc");

	p = s;
	while (*p != '\0') {
		size_t i;

		i = 0;
		while (isspace(*p))
			p++;
		while (i < sizeof buf && *p != '\0' && *p != ',')
			buf[i++] = *p++;
		if (i >= sizeof buf)
			errx(1, "font name too long");
		if (*p == ',')
			p++;
		buf[i] = '\0';
		if (nfont == 0)
			if ((dc.pattern = FcNameParse((FcChar8 *)buf)) == NULL)
				errx(1, "the first font in the cache must be loaded from a font string");
		if ((dc.fonts[nfont++] = XftFontOpenName(dpy, screen, buf)) == NULL)
			errx(1, "could not load font");
	}
}

/* query monitor information and cursor position */
static void
initmonitor(void)
{
	XineramaScreenInfo *info = NULL;
	Window dw;          /* dummy variable */
	int di;             /* dummy variable */
	unsigned du;        /* dummy variable */
	int nmons;
	int i;

	XQueryPointer(dpy, rootwin, &dw, &dw, &mon.cursx, &mon.cursy, &di, &di, &du);

	mon.x = mon.y = 0;
	mon.w = DisplayWidth(dpy, screen);
	mon.h = DisplayHeight(dpy, screen);

	if ((info = XineramaQueryScreens(dpy, &nmons)) != NULL) {
		int selmon = 0;

		for (i = 0; i < nmons; i++) {
			if (BETWEEN(mon.cursx, info[i].x_org, info[i].x_org + info[i].width) &&
			    BETWEEN(mon.cursy, info[i].y_org, info[i].y_org + info[i].height)) {
				selmon = i;
				break;
			}
		}

		mon.x = info[selmon].x_org;
		mon.y = info[selmon].y_org;
		mon.w = info[selmon].width;
		mon.h = info[selmon].height;

		XFree(info);
	}
}

/* read xrdb for configuration options */
static void
initresources(void)
{
	char *xrm;
	long n;
	char *type;
	XrmDatabase xdb;
	XrmValue xval;

	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) == NULL)
		return;

	xdb = XrmGetStringDatabase(xrm);

	if (XrmGetResource(xdb, "pmenu.diameterWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.diameter_pixels = n;
	if (XrmGetResource(xdb, "pmenu.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.border_pixels = n;
	if (XrmGetResource(xdb, "pmenu.separatorWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.separator_pixels = n;
	if (XrmGetResource(xdb, "pmenu.background", "*", &type, &xval) == True)
		config.background_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.foreground", "*", &type, &xval) == True)
		config.foreground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.selbackground", "*", &type, &xval) == True)
		config.selbackground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.selforeground", "*", &type, &xval) == True)
		config.selforeground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.separator", "*", &type, &xval) == True)
		config.separator_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.border", "*", &type, &xval) == True)
		config.border_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "pmenu.font", "*", &type, &xval) == True)
		config.font = strdup(xval.addr);

	XrmDestroyDatabase(xdb);
}

/* init draw context */
static void
initdc(void)
{
	XGCValues values;
	unsigned long valuemask;

	/* get color pixels */
	ealloccolor(config.background_color,    &dc.normal[ColorBG]);
	ealloccolor(config.foreground_color,    &dc.normal[ColorFG]);
	ealloccolor(config.selbackground_color, &dc.selected[ColorBG]);
	ealloccolor(config.selforeground_color, &dc.selected[ColorFG]);
	ealloccolor(config.separator_color,     &dc.separator);
	ealloccolor(config.border_color,        &dc.border);

	/* parse fonts */
	parsefonts(config.font);

	/* create common GC */
	values.arc_mode = ArcPieSlice;
	values.line_width = config.separator_pixels;
	valuemask = GCLineWidth | GCArcMode;
	dc.gc = XCreateGC(dpy, rootwin, valuemask, &values);
}

/* setup pie */
static void
initpie(void)
{
	XGCValues values;
	unsigned long valuemask;

	/* set pie geometry */
	pie.border = config.border_pixels;
	pie.diameter = config.diameter_pixels;
	pie.radius = (pie.diameter + 1) / 2;
	pie.fulldiameter = pie.diameter + (pie.border * 2);

	/* set the separator beginning and end */
	pie.separatorbeg = config.separatorbeg;
	pie.separatorend = config.separatorend;

	/* set the inner circle position */
	pie.innercircley = pie.innercirclex = pie.radius - pie.radius * pie.separatorbeg;
	pie.innercirclediameter = (pie.radius * pie.separatorbeg) * 2;

	/* Create a simple bitmap mask (depth = 1) */
	pie.clip = XCreatePixmap(dpy, rootwin, pie.diameter, pie.diameter, 1);
	pie.bounding = XCreatePixmap(dpy, rootwin, pie.fulldiameter, pie.fulldiameter, 1);

	/* Create the mask GC */
	values.background = 1;
	values.arc_mode = ArcPieSlice;
	valuemask = GCBackground | GCArcMode;
	pie.gc = XCreateGC(dpy, pie.clip, valuemask, &values);

	/* clear the bitmap */
	XSetForeground(dpy, pie.gc, 0);
	XFillRectangle(dpy, pie.clip, pie.gc, 0, 0, pie.diameter, pie.diameter);
	XFillRectangle(dpy, pie.bounding, pie.gc, 0, 0, pie.fulldiameter, pie.fulldiameter);

	/* create round shape */
	XSetForeground(dpy, pie.gc, 1);
	XFillArc(dpy, pie.clip, pie.gc, 0, 0,
	         pie.diameter, pie.diameter, 0, 360*64);
	XFillArc(dpy, pie.bounding, pie.gc, 0, 0,
	         pie.fulldiameter, pie.fulldiameter, 0, 360*64);
}

/* allocate an slice */
static struct Slice *
allocslice(const char *label, const char *output, char *file)
{
	struct Slice *slice;

	if ((slice = malloc(sizeof *slice)) == NULL)
		err(1, "malloc");

	if (label == NULL) {
		slice->label = NULL;
	} else {
		if ((slice->label = strdup(label)) == NULL)
			err(1, "strdup");
	}

	if (label == output) {
		slice->output = slice->label;
	} else {
		if ((slice->output = strdup(output)) == NULL)
			err(1, "strdup");
	}

	if (file == NULL) {
		slice->file = NULL;
	} else {
		if ((slice->file = strdup(file)) == NULL)
			err(1, "strdup");
	}

	slice->y = 0;
	if (slice->label == NULL)
		slice->labellen = 0;
	else
		slice->labellen = strlen(slice->label);
	slice->next = NULL;
	slice->submenu = NULL;
	slice->icon = NULL;

	return slice;
}

/* allocate a menu */
static struct Menu *
allocmenu(struct Menu *parent, struct Slice *list, unsigned level)
{
	XSetWindowAttributes swa;
	struct Menu *menu;

	if ((menu = malloc(sizeof *menu)) == NULL)
		err(1, "malloc");
	menu->parent = parent;
	menu->list = list;
	menu->caller = NULL;
	menu->selected = NULL;
	menu->nslices = 0;
	menu->x = 0;    /* calculated by setupmenu() */
	menu->y = 0;    /* calculated by setupmenu() */
	menu->level = level;

	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.save_under = True;  /* pop-up windows should save_under*/
	swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask
	               | PointerMotionMask | EnterWindowMask | LeaveWindowMask;
	menu->win = XCreateWindow(dpy, rootwin, 0, 0, 1, 1, 0,
	                          CopyFromParent, CopyFromParent, CopyFromParent,
	                          CWOverrideRedirect | CWBackPixel |
	                          CWBorderPixel | CWEventMask | CWSaveUnder,
	                          &swa);

	XShapeCombineMask(dpy, menu->win, ShapeClip, 0, 0, pie.clip, ShapeSet);
	XShapeCombineMask(dpy, menu->win, ShapeBounding, -pie.border, -pie.border, pie.bounding, ShapeSet);

	return menu;
}

/* build the menu tree */
static struct Menu *
buildmenutree(unsigned level, const char *label, const char *output, char *file)
{
	static struct Menu *prevmenu = NULL;    /* menu the previous slice was added to */
	static struct Menu *rootmenu = NULL;    /* menu to be returned */
	struct Slice *currslice = NULL;           /* slice currently being read */
	struct Slice *slice;                      /* dummy slice for loops */
	struct Menu *menu;                      /* dummy menu for loops */
	unsigned i;

	/* create the slice */
	currslice = allocslice(label, output, file);

	/* put the slice in the menu tree */
	if (prevmenu == NULL) {                 /* there is no menu yet */
		menu = allocmenu(NULL, currslice, level);
		rootmenu = menu;
		prevmenu = menu;
		currslice->prev = NULL;
	} else if (level < prevmenu->level) {   /* slice is continuation of a parent menu */
		/* go up the menu tree until find the menu this slice continues */
		for (menu = prevmenu, i = level;
			  menu != NULL && i != prevmenu->level;
			  menu = menu->parent, i++)
			;
		if (menu == NULL)
			errx(1, "improper indentation detected");

		/* find last slice in the new menu */
		for (slice = menu->list; slice->next != NULL; slice = slice->next)
			;

		prevmenu = menu;
		slice->next = currslice;
		currslice->prev = slice;
	} else if (level == prevmenu->level) {  /* slice is a continuation of current menu */
		/* find last slice in the previous menu */
		for (slice = prevmenu->list; slice->next != NULL; slice = slice->next)
			;

		slice->next = currslice;
		currslice->prev = slice;
	} else if (level > prevmenu->level) {   /* slice begins a new menu */
		menu = allocmenu(prevmenu, currslice, level);

		/* find last slice in the previous menu */
		for (slice = prevmenu->list; slice->next != NULL; slice = slice->next)
			;

		prevmenu = menu;
		menu->caller = slice;
		slice->submenu = menu;
		currslice->prev = NULL;
	}

	prevmenu->nslices++;

	return rootmenu;
}

/* create menus and slices from the stdin */
static struct Menu *
parsestdin(void)
{
	struct Menu *rootmenu;
	char *s, buf[BUFSIZ];
	char *file, *label, *output;
	unsigned level = 0;

	rootmenu = NULL;

	while (fgets(buf, BUFSIZ, stdin) != NULL) {
		/* get the indentation level */
		level = strspn(buf, "\t");

		/* get the label */
		s = level + buf;
		label = strtok(s, "\t\n");

		if (label == NULL)
			errx(1, "empty item");

		/* get the filename */
		file = NULL;
		if (label != NULL && strncmp(label, "IMG:", 4) == 0) {
			file = label + 4;
			label = NULL;
		}

		/* get the output */
		output = strtok(NULL, "\n");
		if (output == NULL) {
			output = label;
		} else {
			while (*output == '\t')
				output++;
		}

		rootmenu = buildmenutree(level, label, output, file);
	}

	return rootmenu;
}

/* load image from file and scale it to size; return the image and its size */
static Imlib_Image
loadicon(const char *file, int size, int *width_ret, int *height_ret)
{
	Imlib_Image icon;
	Imlib_Load_Error errcode;
	const char *errstr;
	int width;
	int height;

	icon = imlib_load_image_with_error_return(file, &errcode);
	if (*file == '\0') {
		errx(1, "could not load icon (file name is blank)");
	} else if (icon == NULL) {
		switch (errcode) {
		case IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST:
			errstr = "file does not exist";
			break;
		case IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY:
			errstr = "file is directory";
			break;
		case IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ:
		case IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE:
			errstr = "permission denied";
			break;
		case IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT:
			errstr = "unknown file format";
			break;
		case IMLIB_LOAD_ERROR_PATH_TOO_LONG:
			errstr = "path too long";
			break;
		case IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT:
		case IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY:
		case IMLIB_LOAD_ERROR_PATH_POINTS_OUTSIDE_ADDRESS_SPACE:
			errstr = "improper path";
			break;
		case IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS:
			errstr = "too many symbolic links";
			break;
		case IMLIB_LOAD_ERROR_OUT_OF_MEMORY:
			errstr = "out of memory";
			break;
		case IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS:
			errstr = "out of file descriptors";
			break;
		default:
			errstr = "unknown error";
			break;
		}
		errx(1, "could not load icon (%s): %s", errstr, file);
	}

	imlib_context_set_image(icon);

	width = imlib_image_get_width();
	height = imlib_image_get_height();

	if (width == MAX(width, height)) {
		*width_ret = size;
		*height_ret = (height * size) / width;
	} else {
		*width_ret = (width * size) / height;
		*height_ret = size;
	}

	icon = imlib_create_cropped_scaled_image(0, 0, width, height,
	                                         *width_ret, *height_ret);

	return icon;
}

/* get next utf8 char from s return its codepoint and set next_ret to pointer to end of character */
static FcChar32
getnextutf8char(const char *s, const char **next_ret)
{
	static const unsigned char utfbyte[] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
	static const unsigned char utfmask[] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
	static const FcChar32 utfmin[] = {0, 0x00,  0x80,  0x800,  0x10000};
	static const FcChar32 utfmax[] = {0, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
	/* 0xFFFD is the replacement character, used to represent unknown characters */
	static const FcChar32 unknown = 0xFFFD;
	FcChar32 ucode;         /* FcChar32 type holds 32 bits */
	size_t usize = 0;       /* n' of bytes of the utf8 character */
	size_t i;

	*next_ret = s+1;

	/* get code of first byte of utf8 character */
	for (i = 0; i < sizeof utfmask; i++) {
		if (((unsigned char)*s & utfmask[i]) == utfbyte[i]) {
			usize = i;
			ucode = (unsigned char)*s & ~utfmask[i];
			break;
		}
	}

	/* if first byte is a continuation byte or is not allowed, return unknown */
	if (i == sizeof utfmask || usize == 0)
		return unknown;

	/* check the other usize-1 bytes */
	s++;
	for (i = 1; i < usize; i++) {
		*next_ret = s+1;
		/* if byte is nul or is not a continuation byte, return unknown */
		if (*s == '\0' || ((unsigned char)*s & utfmask[0]) != utfbyte[0])
			return unknown;
		/* 6 is the number of relevant bits in the continuation byte */
		ucode = (ucode << 6) | ((unsigned char)*s & ~utfmask[0]);
		s++;
	}

	/* check if ucode is invalid or in utf-16 surrogate halves */
	if (!BETWEEN(ucode, utfmin[usize], utfmax[usize])
	    || BETWEEN (ucode, 0xD800, 0xDFFF))
		return unknown;

	return ucode;
}

/* get which font contains a given code point */
static XftFont *
getfontucode(FcChar32 ucode)
{
	FcCharSet *fccharset = NULL;
	FcPattern *fcpattern = NULL;
	FcPattern *match = NULL;
	XftFont *retfont = NULL;
	XftResult result;
	size_t i;

	for (i = 0; i < dc.nfonts; i++)
		if (XftCharExists(dpy, dc.fonts[i], ucode) == FcTrue)
			return dc.fonts[i];

	/* create a charset containing our code point */
	fccharset = FcCharSetCreate();
	FcCharSetAddChar(fccharset, ucode);

	/* create a pattern akin to the dc.pattern but containing our charset */
	if (fccharset) {
		fcpattern = FcPatternDuplicate(dc.pattern);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	}

	/* find pattern matching fcpattern */
	if (fcpattern) {
		FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
		FcDefaultSubstitute(fcpattern);
		match = XftFontMatch(dpy, screen, fcpattern, &result);
	}

	/* if found a pattern, open its font */
	if (match) {
		retfont = XftFontOpenPattern(dpy, match);
		if (retfont && XftCharExists(dpy, retfont, ucode) == FcTrue) {
			if ((dc.fonts = realloc(dc.fonts, dc.nfonts+1)) == NULL)
				err(1, "realloc");
			dc.fonts[dc.nfonts] = retfont;
			return dc.fonts[dc.nfonts++];
		} else {
			XftFontClose(dpy, retfont);
		}
	}

	/* in case no fount was found, return the first one */
	return dc.fonts[0];
}

/* draw text into XftDraw */
static int
drawtext(XftDraw *draw, XftColor *color, int x, int y, const char *text)
{
	int textwidth = 0;

	while (*text) {
		XftFont *currfont;
		XGlyphInfo ext;
		FcChar32 ucode;
		const char *next;
		size_t len;

		ucode = getnextutf8char(text, &next);
		currfont = getfontucode(ucode);

		len = next - text;
		XftTextExtentsUtf8(dpy, currfont, (XftChar8 *)text, len, &ext);
		textwidth += ext.xOff;

		if (draw) {
			int texty;

			texty = y + (currfont->ascent - currfont->descent)/2;
			XftDrawStringUtf8(draw, color, currfont, x, texty, (XftChar8 *)text, len);
			x += ext.xOff;
		}

		text = next;
	}

	return textwidth;
}

/* setup position of and content of menu's slices */
static void
setupslices(struct Menu *menu)
{
	struct Slice *slice;
	double anglerad;    /* angle in radians */
	unsigned n = 0;
	int angle = 0;
	int textwidth;

	menu->halfslice = (360 * 64) / (menu->nslices * 2);
	for (slice = menu->list; slice != NULL; slice = slice->next) {
		n++;

		slice->angle1 = angle - menu->halfslice;
		if (slice->angle1 < 0)
			slice->angle1 += 360 * 64;
		slice->angle2 = menu->halfslice * 2;

		/* get length of slice->label rendered in the font */
		textwidth = (slice->label) ? drawtext(NULL, NULL, 0, 0, slice->label): 0;

		/* anglerad is now the angle in radians of the middle of the slice */
		anglerad = (angle * M_PI) / (180 * 64);

		/* get position of slice's label */
		slice->labelx = pie.radius + ((pie.radius*2)/3 * cos(anglerad)) - (textwidth / 2);
		slice->labely = pie.radius - ((pie.radius*2)/3 * sin(anglerad));

		/* get position of submenu */
		slice->x = pie.radius + (pie.diameter * (cos(anglerad) * 0.9));
		slice->y = pie.radius - (pie.diameter * (sin(anglerad) * 0.9));

		/* create icon */
		if (slice->file != NULL) {
			int maxiconsize = (pie.radius + 1) / 2;
			double sliceanglerad;   /* inner angle of a slice */
			int iconw, iconh;       /* icon width and height */
			int iconsize;           /* requested icon size */
			int xdiff, ydiff;

			sliceanglerad = (slice->angle2 * M_PI) / (180 * 64);

			xdiff = pie.radius * 0.5 - (pie.radius * (cos(sliceanglerad) * 0.5));
			ydiff = pie.radius * (sin(sliceanglerad) * 0.5);

			iconsize = sqrt(xdiff * xdiff + ydiff * ydiff);
			iconsize = MIN(maxiconsize, iconsize);

			slice->icon = loadicon(slice->file, iconsize, &iconw, &iconh);

			slice->iconx = pie.radius + (pie.radius * (cos(anglerad) * 0.6)) - iconw / 2;
			slice->icony = pie.radius - (pie.radius * (sin(anglerad) * 0.6)) - iconh / 2;
		}

		/* anglerad is now the angle in radians of angle1 */
		anglerad = (slice->angle1 * M_PI) / (180 * 64);
		
		/* set position of the line segment separating slices */
		slice->linexi = pie.radius + pie.radius * (cos(anglerad) * pie.separatorbeg);
		slice->lineyi = pie.radius + pie.radius * (sin(anglerad) * pie.separatorbeg);
		slice->linexo = pie.radius + pie.radius * (cos(anglerad) * pie.separatorend);
		slice->lineyo = pie.radius + pie.radius * (sin(anglerad) * pie.separatorend);
		if (abs(slice->linexo - slice->linexi) <= 2)
			slice->linexo = slice->linexi;

		/* set position of the icon */

		angle = (360 * 64 * n) / menu->nslices;
	}
}

/* setup the position of a menu */
static void
setupmenupos(struct Menu *menu)
{
	Window w1;  /* dummy variable */
	int x, y;   /* position of the center of the menu */

	if (menu->parent == NULL) {
		x = mon.cursx;
		y = mon.cursy;
	} else {
		Bool ret;
		ret = XTranslateCoordinates(dpy, menu->parent->win, rootwin,
		                            menu->caller->x, menu->caller->y,
		                            &x, &y, &w1);

		if (ret == False)
			errx(EXIT_FAILURE, "menus are on different screens");
	}

	menu->x = mon.x;
	menu->y = mon.y;

	if (x - mon.x >= pie.radius) {
		if (mon.x + mon.w - x >= pie.radius)
			menu->x = x - pie.radius - pie.border;
		else if (mon.x + mon.w >= pie.fulldiameter)
			menu->x = mon.x + mon.w - pie.fulldiameter;
	}

	if (y - mon.y >= pie.radius) {
		if (mon.y + mon.h - y >= pie.radius)
			menu->y = y - pie.radius - pie.border;
		else if (mon.y + mon.h >= pie.fulldiameter)
			menu->y = mon.y + mon.h - pie.fulldiameter;
	}
}

/* recursivelly setup menu configuration and its pixmap */
static void
setupmenu(struct Menu *menu)
{
	struct Slice *slice;
	XWindowChanges changes;
	XSizeHints sizeh;
	XClassHint classh = {PROGNAME, PROGNAME};

	/* setup slices of the menu */
	setupslices(menu);

	/* setup position of menus */
	setupmenupos(menu);

	/* update menu geometry */
	changes.border_width = pie.border;
	changes.height = pie.diameter;
	changes.width = pie.diameter;
	changes.x = menu->x;
	changes.y = menu->y;
	XConfigureWindow(dpy, menu->win, CWBorderWidth | CWWidth | CWHeight | CWX | CWY, &changes);

	/* set window manager hints */
	sizeh.flags = USPosition | PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = pie.diameter;
	sizeh.min_height = sizeh.max_height = pie.diameter;
	XSetWMProperties(dpy, menu->win, NULL, NULL, NULL, 0, &sizeh, NULL, &classh);

	/* create pixmap and XftDraw */
	menu->pixmap = XCreatePixmap(dpy, menu->win, pie.diameter, pie.diameter,
	                             DefaultDepth(dpy, screen));
	menu->draw = XftDrawCreate(dpy, menu->pixmap, visual, colormap);

	/* calculate positions of submenus */
	for (slice = menu->list; slice != NULL; slice = slice->next) {
		if (slice->submenu != NULL)
			setupmenu(slice->submenu);
	}
}

/* try to grab pointer, we may have to wait for another process to ungrab */
static void
grabpointer(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabPointer(dpy, rootwin, True, ButtonPressMask,
		                 GrabModeAsync, GrabModeAsync, None,
		                 None, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	errx(1, "could not grab pointer");
}

/* try to grab keyboard, we may have to wait for another process to ungrab */
static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, rootwin, True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	errx(1, "could not grab keyboard");
}

/* get menu of given window */
static struct Menu *
getmenu(struct Menu *currmenu, Window win)
{
	struct Menu *menu;

	for (menu = currmenu; menu != NULL; menu = menu->parent)
		if (menu->win == win)
			return menu;

	return NULL;
}

/* get slice of given menu and position */
static struct Slice *
getslice(struct Menu *menu, int x, int y)
{
	struct Slice *slice;
	double phi;
	int angle;
	int r;

	if (menu == NULL)
		return NULL;

	x -= pie.radius;
	y -= pie.radius;
	y = -y;

	/* if the cursor is in the middle circle, it is in no slice */
	r = sqrt(x * x + y * y);
	if (r <= pie.radius * pie.separatorbeg)
		return NULL;

	phi = atan2(y, x);
	if (y < 0)
		phi += 2 * M_PI;
	angle = ((phi * 180 * 64) / M_PI);

	if (angle < menu->halfslice)
		return menu->list;
	for (slice = menu->list; slice != NULL; slice = slice->next)
		if (angle >= slice->angle1 && angle < slice->angle1 + slice->angle2)
			return slice;

	return NULL;
}

/* umap previous menus and map current menu and its parents */
static void
mapmenu(struct Menu *currmenu)
{
	static struct Menu *prevmenu = NULL;
	struct Menu *menu, *menu_;
	struct Menu *lcamenu;   /* lowest common ancestor menu */
	unsigned minlevel;      /* level of the closest to root menu */
	unsigned maxlevel;      /* level of the closest to root menu */

	/* do not remap current menu if it wasn't updated*/
	if (prevmenu == currmenu)
		return;

	/* if this is the first time mapping, skip calculations */
	if (prevmenu == NULL) {
		XMapWindow(dpy, currmenu->win);
		prevmenu = currmenu;
		return;
	}

	/* find lowest common ancestor menu */
	minlevel = MIN(currmenu->level, prevmenu->level);
	maxlevel = MAX(currmenu->level, prevmenu->level);
	if (currmenu->level == maxlevel) {
		menu = currmenu;
		menu_ = prevmenu;
	} else {
		menu = prevmenu;
		menu_ = currmenu;
	}
	while (menu->level > minlevel)
		menu = menu->parent;
	while (menu != menu_) {
		menu = menu->parent;
		menu_ = menu_->parent;
	}
	lcamenu = menu;

	/* unmap menus from currmenu (inclusive) until lcamenu (exclusive) */
	for (menu = prevmenu; menu != lcamenu; menu = menu->parent) {
		menu->selected = NULL;
		XUnmapWindow(dpy, menu->win);
	}

	/* map menus from currmenu (inclusive) until lcamenu (exclusive) */
	for (menu = currmenu; menu != lcamenu; menu = menu->parent)
		XMapWindow(dpy, menu->win);

	prevmenu = currmenu;
}

/* draw regular slice */
static void
drawslice(struct Menu *menu, struct Slice *slice, XftColor *color)
{
	if (slice->file != NULL) {      /* if there is an icon, draw it */
		imlib_context_set_drawable(menu->pixmap);
		imlib_context_set_image(slice->icon);
		imlib_render_image_on_drawable(slice->iconx, slice->icony);
	} else {                        /* otherwise, draw the label */
		XSetForeground(dpy, dc.gc, color[ColorFG].pixel);
		drawtext(menu->draw, &color[ColorFG], slice->labelx,
		         slice->labely, slice->label);
	}

	/* draw separator */
	XSetForeground(dpy, dc.gc, dc.separator.pixel);
	XDrawLine(dpy, menu->pixmap, dc.gc,
	          slice->linexi, slice->lineyi,
	          slice->linexo, slice->lineyo);
}

/* draw slices of the current menu and of its ancestors */
static void
drawmenu(struct Menu *currmenu)
{
	struct Menu *menu;
	struct Slice *slice;
	XftColor *color;

	for (menu = currmenu; menu != NULL; menu = menu->parent) {

		/* draw slice background */
		for (slice = menu->list; slice != NULL; slice = slice->next) {
			if (slice == menu->selected)
				color = dc.selected;
			else
				color = dc.normal;

			XSetForeground(dpy, dc.gc, color[ColorBG].pixel);
			XFillArc(dpy, menu->pixmap, dc.gc, 0, 0,
			         pie.diameter, pie.diameter,
			         slice->angle1, slice->angle2);
		}

		/* draw slice foreground */
		for (slice = menu->list; slice != NULL; slice = slice->next) {
			if (slice == menu->selected)
				color = dc.selected;
			else
				color = dc.normal;

			drawslice(menu, slice, color);
		}

		/* draw inner circle */
		XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
		XFillArc(dpy, menu->pixmap, dc.gc,
		         pie.innercirclex, pie.innercircley,
		         pie.innercirclediameter, pie.innercirclediameter,
		         0, 360 * 64);

		XCopyArea(dpy, menu->pixmap, menu->win, dc.gc, 0, 0,
			      pie.diameter, pie.diameter, 0, 0);
	}
}

/* cycle through the slices; non-zero direction is next, zero is prev */
static struct Slice *
slicecycle(struct Menu *currmenu, int direction)
{
	struct Slice *slice;
	struct Slice *lastslice;

	slice = NULL;

	if (direction == ITEMNEXT) {
		if (currmenu->selected == NULL)
			slice = currmenu->list;
		else if (currmenu->selected->next != NULL)
			slice = currmenu->selected->next;

		if (slice == NULL)
			slice = currmenu->list;
	} else {
		for (lastslice = currmenu->list;
		     lastslice != NULL && lastslice->next != NULL;
		     lastslice = lastslice->next)
			;

		if (currmenu->selected == NULL)
			slice = lastslice;
		else if (currmenu->selected->prev != NULL)
			slice = currmenu->selected->prev;

		if (slice == NULL)
			slice = lastslice;
	}

	return slice;
}

/* run event loop */
static void
run(struct Menu *currmenu)
{
	struct Menu *rootmenu;
	struct Menu *menu;
	struct Slice *slice;
	KeySym ksym;
	XEvent ev;

	rootmenu = currmenu;

	mapmenu(currmenu);

	XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);

	while (!XNextEvent(dpy, &ev)) {
		switch(ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				drawmenu(currmenu);
			break;
		case EnterNotify:
			menu = getmenu(currmenu, ev.xcrossing.window);
			if (menu == NULL)
				break;
			mapmenu(currmenu);
			XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
			drawmenu(currmenu);
			break;
		case LeaveNotify:
			menu = getmenu(currmenu, ev.xcrossing.window);
			if (menu == NULL)
				break;
			if (menu != rootmenu && menu == currmenu) {
				currmenu = currmenu->parent;
				mapmenu(currmenu);
			}
			currmenu->selected = NULL;
			drawmenu(currmenu);
			break;
		case MotionNotify:
			menu = getmenu(currmenu, ev.xbutton.window);
			slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
			if (menu == NULL || slice == NULL)
				menu->selected = NULL;
			else
				menu->selected = slice;
			drawmenu(currmenu);
			break;
		case ButtonRelease:
			menu = getmenu(currmenu, ev.xbutton.window);
			slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
			if (menu == NULL || slice == NULL)
				return;
selectslice:
			if (slice->submenu != NULL) {
				currmenu = slice->submenu;
			} else {
				printf("%s\n", slice->output);
				return;
			}
			mapmenu(currmenu);
			currmenu->selected = currmenu->list;
			drawmenu(currmenu);
			XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
			break;
		case ButtonPress:
			menu = getmenu(currmenu, ev.xbutton.window);
			if (menu == NULL)
				return;
			break;
		case KeyPress:
			ksym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

			/* esc closes pmenu when current menu is the root menu */
			if (ksym == XK_Escape && currmenu->parent == NULL)
				return;

			/* Shift-Tab = ISO_Left_Tab */
			if (ksym == XK_Tab && (ev.xkey.state & ShiftMask))
				ksym = XK_ISO_Left_Tab;

			/* cycle through menu */
			slice = NULL;
			if (ksym == XK_Tab) {
				slice = slicecycle(currmenu, ITEMPREV);
			} else if (ksym == XK_ISO_Left_Tab) {
				slice = slicecycle(currmenu, ITEMNEXT);
			} else if ((ksym == XK_Return) &&
			           currmenu->selected != NULL) {
				slice = currmenu->selected;
				goto selectslice;
			} else if ((ksym == XK_Escape) &&
			           currmenu->parent != NULL) {
				slice = currmenu->parent->selected;
				currmenu = currmenu->parent;
				mapmenu(currmenu);
			} else
				break;
			currmenu->selected = slice;
			drawmenu(currmenu);
			break;
		case ConfigureNotify:
			menu = getmenu(currmenu, ev.xconfigure.window);
			if (menu == NULL)
				break;
			menu->x = ev.xconfigure.x;
			menu->y = ev.xconfigure.y;
			break;
		}
	}
}

/* recursivelly free pixmaps and destroy windows */
static void
cleanmenu(struct Menu *menu)
{
	struct Slice *slice;
	struct Slice *tmp;

	slice = menu->list;
	while (slice != NULL) {
		if (slice->submenu != NULL)
			cleanmenu(slice->submenu);
		tmp = slice;
		if (tmp->label != tmp->output)
			free(tmp->label);
		free(tmp->output);
		if (tmp->file != NULL) {
			free(tmp->file);
			if (tmp->icon != NULL) {
				imlib_context_set_image(tmp->icon);
				imlib_free_image();
			}
		}
		slice = slice->next;
		free(tmp);
	}

	XFreePixmap(dpy, menu->pixmap);
	XftDrawDestroy(menu->draw);
	XDestroyWindow(dpy, menu->win);
	free(menu);
}

/* cleanup and exit */
static void
cleanup(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);

	XftColorFree(dpy, visual, colormap, &dc.normal[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.separator);
	XftColorFree(dpy, visual, colormap, &dc.border);

	XFreeGC(dpy, dc.gc);
	XCloseDisplay(dpy);
}

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: pmenu\n");
	exit(1);
}
