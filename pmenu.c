#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xinerama.h>
#include <Imlib2.h>

#define SHELL    "sh"
#define CLASS    "PMenu"
#define TTPAD    4              /* padding for the tooltip */
#define TTVERT   30             /* vertical distance from mouse to place tooltip */
#define MAXPATHS 128            /* maximal number of paths to look for icons */
#define ICONPATH "ICONPATH"     /* environment variable name */

/* macros */
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))

/* color enum */
enum {ColorFG, ColorBG, ColorLast};

/* state of command to popen */
enum {NO_CMD = 0, CMD_NOTRUN = 1, CMD_RUN = 2};

/* atoms */
enum {
	NET_WM_WINDOW_TYPE,
	NET_WM_WINDOW_TYPE_TOOLTIP,
	NET_WM_WINDOW_TYPE_POPUP_MENU,
	ATOM_LAST
};

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
	int triangle_width;
	int triangle_height;
	int triangle_distance;
	int execcommand;
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
	int fonth;

	XRenderPictureAttributes pictattr;
};

/* pie slice structure */
struct Slice {
	struct Slice *prev, *next;
	struct Menu *submenu;   /* submenu spawned by clicking on slice */
	struct Menu *parent;

	char *label;            /* string to be drawed on the slice */
	char *output;           /* string to be outputed when slice is clicked */
	char *file;             /* filename of the icon */
	size_t labellen;        /* strlen(label) */
	int iscmd;              /* whether output is actually a command to popen */

	unsigned slicen;
	int x, y;               /* position of the pointer of the slice */
	int labelx, labely;     /* position of the label */
	int iconx, icony;       /* position of the icon */
	double anglea, angleb;  /* angle of the borders of the slice */

	int drawn;              /* whether the pixmap have been drawn */
	Drawable pixmap;        /* pixmap containing the pie menu with the slice selected */
	Picture picture;        /* XRender picture */
	Imlib_Image icon;       /* icon */

	int ttdrawn;            /* whether the pixmap for the tooltip have been drawn */
	int ttw;                /* tooltip width */
	Window tooltip;         /* tooltip that appears when hovering a slice */
	Drawable ttpix;         /* pixmap for the tooltip */
};

/* menu structure */
struct Menu {
	struct Menu *parent;    /* parent menu */
	struct Slice *caller;   /* slice that spawned the menu */
	struct Slice *list;     /* list of slices contained by the pie menu */
	struct Slice *selected; /* slice currently selected in the menu */
	unsigned nslices;       /* number of slices */
	int x, y;               /* menu position */
	double half;            /* angle of half a slice of the pie menu */
	int level;              /* menu level relative to root */

	int drawn;              /* whether the pixmap have been drawn */
	Drawable pixmap;        /* pixmap to draw the menu on */
	Picture picture;        /* XRender picture */
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
	int tooltiph;

	int triangleinner;
	int triangleouter;
	int separatorbeg;
	int separatorend;
	double triangleangle;
	double innerangle;
	double outerangle;

	Picture bg;
	Picture fg;
	Picture selbg;
	Picture selfg;
	Picture separator;
};

/* X stuff */
static Display *dpy;
static Visual *visual;
static Window rootwin;
static Colormap colormap;
static XrmDatabase xdb;
static XRenderPictFormat *xformat;
static char *xrm;
static int screen;
static int depth;
static struct DC dc;
static Atom atoms[ATOM_LAST];
static XClassHint classh;

/* The pie bitmap structure */
static struct Pie pie;

/* flags */
static int rootmodeflag = 0;            /* wheter to run in root mode */
static int nowarpflag = 0;              /* whether to disable pointer warping */
static int passclickflag = 0;           /* whether to pass click to root window */

/* arguments */
static unsigned int button = 0;         /* button to trigger pmenu in root mode */
static unsigned int modifier = 0;       /* modifier to trigger pmenu */

/* icons paths */
static char *iconstring = NULL;         /* string read from getenv */
static char *iconpaths[MAXPATHS];       /* paths to icon directories */
static int niconpaths = 0;              /* number of paths to icon directories */

#include "config.h"

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: pmenu [-ew] [-d diameter] [(-x|-X) [modifier-]button]\n");
	exit(1);
}

/* call strdup checking for error */
static char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

/* call fork checking for error; exit on error */
static pid_t
efork(void)
{
	pid_t pid;

	if ((pid = fork()) < 0)
		err(1, "fork");
	return pid;
}

/* call execlp on sh -c checking for error; exit on error */
static void
eexecsh(const char *cmd)
{
	if (execlp(SHELL, SHELL, "-c", cmd, NULL) == -1) {
		err(1, SHELL);
	}
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	char *type;
	XrmValue xval;

	if (xrm == NULL || xdb == NULL)
		return;
	if (XrmGetResource(xdb, "pmenu.diameterWidth", "*", &type, &xval) == True)
		config.diameter_pixels = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "pmenu.borderWidth", "*", &type, &xval) == True)
		config.border_pixels = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "pmenu.separatorWidth", "*", &type, &xval) == True)
		config.separator_pixels = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "pmenu.background", "*", &type, &xval) == True)
		config.background_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.foreground", "*", &type, &xval) == True)
		config.foreground_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.selbackground", "*", &type, &xval) == True)
		config.selbackground_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.selforeground", "*", &type, &xval) == True)
		config.selforeground_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.separator", "*", &type, &xval) == True)
		config.separator_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.border", "*", &type, &xval) == True)
		config.border_color = xval.addr;
	if (XrmGetResource(xdb, "pmenu.font", "*", &type, &xval) == True)
		config.font = xval.addr;
}

/* set button global variable */
static void
setbutton(char *s)
{
	size_t len;

	if ((len = strlen(s)) < 1)
		return;
	button = strtoul(s, NULL, 10);
}

/* set modifier global variable */
static void
setmodifier(char *s)
{
	size_t len;

	if ((len = strlen(s)) < 1)
		return;
	switch (s[0]) {
	case '0': modifier = AnyModifier; break;
	case '1': modifier = Mod1Mask; break;
	case '2': modifier = Mod2Mask; break;
	case '3': modifier = Mod3Mask; break;
	case '4': modifier = Mod4Mask; break;
	case '5': modifier = Mod5Mask; break;
	default:
		if (strcasecmp(s, "Alt") == 0) {
			modifier = Mod1Mask;
		} else if (strcasecmp(s, "Super") == 0) {
			modifier = Mod4Mask;
		}
		break;
	}
}

/* parse icon path string */
static void
parseiconpaths(char *s)
{
	if (s == NULL)
		return;
	free(iconstring);
	iconstring = estrdup(s);
	niconpaths = 0;
	for (s = strtok(iconstring, ":"); s != NULL; s = strtok(NULL, ":")) {
		if (niconpaths < MAXPATHS) {
			iconpaths[niconpaths++] = s;
		}
	}
}

/* get options */
static void
getoptions(int argc, char **argv)
{
	int ch;
	char *s, *t;

	classh.res_class = CLASS;
	classh.res_name = argv[0];
	if ((s = strrchr(argv[0], '/')) != NULL)
		classh.res_name = s + 1;
	parseiconpaths(getenv(ICONPATH));
	while ((ch = getopt(argc, argv, "d:ewx:X:P:r:m:p")) != -1) {
		switch (ch) {
		case 'd':
			config.diameter_pixels = strtoul(optarg, NULL, 10);
			break;
		case 'e':
			config.execcommand = !config.execcommand;
			break;
		case 'w':
			nowarpflag = 1;
			break;
		case 'X':
			passclickflag = 1;
			/* PASSTHROUGH */
		case 'x':
			rootmodeflag = 1;
			s = optarg;
			setmodifier(s);
			if ((t = strchr(s, '-')) == NULL)
				return;
			*(t++) = '\0';
			setbutton(t);
			break;

		/* the options below are deprecated and may be removed in the future */
		case 'P':
			parseiconpaths(optarg);
			break;
		case 'p':
			passclickflag = 1;
			break;
		case 'r':
			rootmodeflag = 1;
			setbutton(optarg);
			break;
		case 'm':
			setmodifier(optarg);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0) {
		usage();
	}
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

/* init draw context */
static void
initdc(void)
{
	XGCValues values;
	Pixmap pbg, pfg, pselbg, pselfg, separator;
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

	/* create color source Pictures */
	dc.pictattr.repeat = 1;
	dc.pictattr.poly_edge = PolyEdgeSmooth;
	pbg = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	pfg = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	pselbg = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	pselfg = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	separator = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	pie.bg = XRenderCreatePicture(dpy, pbg, xformat, CPRepeat, &dc.pictattr);
	pie.fg = XRenderCreatePicture(dpy, pfg, xformat, CPRepeat, &dc.pictattr);
	pie.selbg = XRenderCreatePicture(dpy, pselbg, xformat, CPRepeat, &dc.pictattr);
	pie.selfg = XRenderCreatePicture(dpy, pselfg, xformat, CPRepeat, &dc.pictattr);
	pie.separator = XRenderCreatePicture(dpy, separator, xformat, CPRepeat, &dc.pictattr);
	XRenderFillRectangle(dpy, PictOpOver, pie.bg, &dc.normal[ColorBG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.fg, &dc.normal[ColorFG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.selbg, &dc.selected[ColorBG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.selfg, &dc.selected[ColorFG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.separator, &dc.separator.color, 0, 0, 1, 1);
	XFreePixmap(dpy, pbg);
	XFreePixmap(dpy, pfg);
	XFreePixmap(dpy, pselbg);
	XFreePixmap(dpy, pselfg);
	XFreePixmap(dpy, separator);
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
	pie.tooltiph = dc.fonts[0]->height + 2 * TTPAD;

	/* set the geometry of the triangle for submenus */
	pie.triangleouter = pie.radius - config.triangle_distance;
	pie.triangleinner = pie.radius - config.triangle_distance - config.triangle_width;
	pie.triangleangle = ((double)config.triangle_height / 2.0) / (double)pie.triangleinner;

	/* set the separator beginning and end */
	pie.separatorbeg = pie.radius * config.separatorbeg;
	pie.separatorend = pie.radius * config.separatorend;
	pie.innerangle = atan(config.separator_pixels / (2.0 * pie.separatorbeg));
	pie.outerangle = atan(config.separator_pixels / (2.0 * pie.separatorend));

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

/* intern atoms */
static void
initatoms(void)
{
	char *atomnames[ATOM_LAST] = {
		[NET_WM_WINDOW_TYPE] = "_NET_WM_WINDOW_TYPE",
		[NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
		[NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
	};

	XInternAtoms(dpy, atomnames, ATOM_LAST, False, atoms);
}

/* call malloc checking for error */
static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

/* allocate an slice */
static struct Slice *
allocslice(const char *label, const char *output, char *file)
{
	struct Slice *slice;

	slice = emalloc(sizeof *slice);
	slice->label = (label != NULL) ? estrdup(label) : NULL;
	slice->file = (file != NULL) ? estrdup(file) : NULL;
	slice->labellen = (slice->label != NULL) ? strlen(slice->label) : 0;
	slice->y = 0;
	slice->next = NULL;
	slice->submenu = NULL;
	slice->icon = NULL;
	if (output && *output == '$') {
		output++;
		while (isspace(*output))
			output++;
		slice->output = estrdup(output);
		slice->iscmd = CMD_NOTRUN;
	} else {
		slice->output = (label == output) ? slice->label : estrdup(output);
		slice->iscmd = NO_CMD;
	}
	return slice;
}

/* allocate a menu */
static struct Menu *
allocmenu(struct Menu *parent, struct Slice *list, int level)
{
	XSetWindowAttributes swa;
	XSizeHints sizeh;
	struct Menu *menu;

	menu = emalloc(sizeof *menu);

	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.save_under = True;  /* pop-up windows should save_under*/
	swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask;
	menu->win = XCreateWindow(dpy, rootwin, 0, 0, pie.diameter, pie.diameter, pie.border,
	                          CopyFromParent, CopyFromParent, CopyFromParent,
	                          CWOverrideRedirect | CWBackPixel |
	                          CWBorderPixel | CWEventMask | CWSaveUnder,
	                          &swa);

	/* Set window type */
	XChangeProperty(dpy, menu->win, atoms[NET_WM_WINDOW_TYPE], XA_ATOM, 32,
	                PropModeReplace, (unsigned char *)&atoms[NET_WM_WINDOW_TYPE_POPUP_MENU], 1);

	XShapeCombineMask(dpy, menu->win, ShapeClip, 0, 0, pie.clip, ShapeSet);
	XShapeCombineMask(dpy, menu->win, ShapeBounding, -pie.border, -pie.border, pie.bounding, ShapeSet);

	/* set window manager hints */
	sizeh.flags = USPosition | PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = pie.diameter;
	sizeh.min_height = sizeh.max_height = pie.diameter;
	XSetWMProperties(dpy, menu->win, NULL, NULL, NULL, 0, &sizeh, NULL, &classh);

	/* set menu variables */
	menu->parent = parent;
	menu->list = list;
	menu->caller = NULL;
	menu->selected = NULL;
	menu->nslices = 0;
	menu->x = 0;
	menu->y = 0;
	menu->level = level;

	/* create pixmap and picture */
	menu->pixmap = XCreatePixmap(dpy, menu->win, pie.diameter, pie.diameter, depth);
	menu->picture = XRenderCreatePicture(dpy, menu->pixmap, xformat, CPPolyEdge | CPRepeat, &dc.pictattr);
	menu->drawn = 0;

	return menu;
}

/* build the menu tree */
static struct Menu *
buildmenutree(struct Menu *rootmenu, int level, const char *label, const char *output, char *file)
{
	static struct Menu *prevmenu;           /* menu the previous slice was added to */
	struct Slice *currslice = NULL;         /* slice currently being read */
	struct Slice *slice;                    /* dummy slice for loops */
	struct Menu *menu;                      /* dummy menu for loops */
	int i;

	if (rootmenu == NULL)
		prevmenu = NULL;

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
parse(FILE *fp, int initlevel)
{
	struct Menu *rootmenu;
	char *s, buf[BUFSIZ];
	char *file, *label, *output;
	int level;

	rootmenu = NULL;
	while (fgets(buf, BUFSIZ, fp) != NULL) {
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
			label = strtok(NULL, "\t\n");
		}

		/* get the output */
		output = strtok(NULL, "\n");
		if (output == NULL) {
			output = label;
		} else {
			while (*output == '\t')
				output++;
		}

		rootmenu = buildmenutree(rootmenu, initlevel + level, label, output, file);
	}

	return rootmenu;
}

/* check if path is absolute or relative to current directory */
static int
isabsolute(const char *s)
{
	return s[0] == '/' || (s[0] == '.' && (s[1] == '/' || (s[1] == '.' && s[2] == '/')));
}

/* load image from file and scale it to size; return the image and its size */
static Imlib_Image
loadicon(const char *file, int size, int *width_ret, int *height_ret)
{
	Imlib_Image icon;
	Imlib_Load_Error errcode;
	char path[PATH_MAX];
	const char *errstr;
	int width;
	int height;
	int i;

	if (*file == '\0') {
		warnx("could not load icon (file name is blank)");
		return NULL;
	}
	if (isabsolute(file))
		icon = imlib_load_image_with_error_return(file, &errcode);
	else {
		for (i = 0; i < niconpaths; i++) {
			snprintf(path, sizeof(path), "%s/%s", iconpaths[i], file);
			icon = imlib_load_image_with_error_return(path, &errcode);
			if (icon != NULL || errcode != IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST) {
				break;
			}
		}
	}
	if (icon == NULL) {
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
		warnx("could not load icon (%s): %s", errstr, file);
		return NULL;
	}

	imlib_context_set_image(icon);

	width = imlib_image_get_width();
	height = imlib_image_get_height();

	if (width > height) {
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
/* recursivelly setup menu configuration and its pixmap */
static void
setslices(struct Menu *menu)
{
	XSetWindowAttributes swa;
	struct Slice *slice;
	double a = 0.0;
	unsigned n = 0;
	int textwidth;

	menu->half = M_PI / menu->nslices;
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.normal[ColorFG].pixel;
	swa.save_under = True;  /* pop-up windows should save_under*/
	swa.event_mask = ExposureMask;
	for (slice = menu->list; slice; slice = slice->next) {
		slice->parent = menu;
		slice->slicen = n++;

		slice->anglea = a - menu->half;
		slice->angleb = a + menu->half;

		/* get length of slice->label rendered in the font */
		textwidth = (slice->label) ? drawtext(NULL, NULL, 0, 0, slice->label): 0;

		/* get position of slice's label */
		slice->labelx = pie.radius + ((pie.radius*2)/3 * cos(a)) - (textwidth / 2);
		slice->labely = pie.radius - ((pie.radius*2)/3 * sin(a));

		/* get position of submenu */
		slice->x = pie.radius + (pie.diameter * (cos(a) * 0.9));
		slice->y = pie.radius - (pie.diameter * (sin(a) * 0.9));

		/* create icon */
		if (slice->file != NULL) {
			int maxiconsize = (pie.radius + 1) / 2;
			int iconw, iconh;       /* icon width and height */
			int iconsize;           /* requested icon size */
			int xdiff, ydiff;

			xdiff = pie.radius * 0.5 - (pie.radius * (cos(menu->half) * 0.8));
			ydiff = pie.radius * (sin(menu->half) * 0.8);

			iconsize = sqrt(xdiff * xdiff + ydiff * ydiff);
			iconsize = MIN(maxiconsize, iconsize);

			if ((slice->icon = loadicon(slice->file, iconsize, &iconw, &iconh)) != NULL) {
				slice->iconx = pie.radius + (pie.radius * (cos(a) * 0.6)) - iconw / 2;
				slice->icony = pie.radius - (pie.radius * (sin(a) * 0.6)) - iconh / 2;
			}

			free(slice->file);
			slice->file = NULL;
		}

		/* create pixmap */
		slice->pixmap = XCreatePixmap(dpy, menu->win, pie.diameter, pie.diameter, depth);
		slice->picture = XRenderCreatePicture(dpy, slice->pixmap, xformat, CPPolyEdge | CPRepeat, &dc.pictattr);
		slice->drawn = 0;

		/* create tooltip */
		slice->ttdrawn = 0;
		if (textwidth > 0) {
			slice->ttw = textwidth + 2 * TTPAD;
			slice->tooltip = XCreateWindow(dpy, rootwin, 0, 0, slice->ttw, pie.tooltiph, 1,
			                               CopyFromParent, CopyFromParent, CopyFromParent,
			                               CWOverrideRedirect | CWBackPixel | CWEventMask | CWSaveUnder | CWBorderPixel,
			                               &swa);
			slice->ttpix = XCreatePixmap(dpy, slice->tooltip, slice->ttw, pie.tooltiph, depth);
			XChangeProperty(dpy, slice->tooltip, atoms[NET_WM_WINDOW_TYPE], XA_ATOM, 32,
			                PropModeReplace, (unsigned char *)&atoms[NET_WM_WINDOW_TYPE_TOOLTIP], 1);

		} else {
			slice->ttw = 0;
			slice->tooltip = None;
			slice->ttpix = None;
		}

		/* call recursivelly */
		if (slice->submenu != NULL) {
			setslices(slice->submenu);
		}

		a += menu->half * 2;
	}
}

/* query monitor information and cursor position */
static void
getmonitor(struct Monitor *mon)
{
	XineramaScreenInfo *info = NULL;
	Window dw;          /* dummy variable */
	int di;             /* dummy variable */
	unsigned du;        /* dummy variable */
	int nmons;
	int i;

	XQueryPointer(dpy, rootwin, &dw, &dw, &mon->cursx, &mon->cursy, &di, &di, &du);

	mon->x = mon->y = 0;
	mon->w = DisplayWidth(dpy, screen);
	mon->h = DisplayHeight(dpy, screen);

	if ((info = XineramaQueryScreens(dpy, &nmons)) != NULL) {
		int selmon = 0;

		for (i = 0; i < nmons; i++) {
			if (BETWEEN(mon->cursx, info[i].x_org, info[i].x_org + info[i].width) &&
			    BETWEEN(mon->cursy, info[i].y_org, info[i].y_org + info[i].height)) {
				selmon = i;
				break;
			}
		}

		mon->x = info[selmon].x_org;
		mon->y = info[selmon].y_org;
		mon->w = info[selmon].width;
		mon->h = info[selmon].height;

		XFree(info);
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

/* setup the position of a menu */
static void
placemenu(struct Monitor *mon, struct Menu *menu)
{
	struct Slice *slice;
	XWindowChanges changes;
	Window w1;  /* dummy variable */
	int x, y;   /* position of the center of the menu */
	Bool ret;

	if (menu->parent == NULL) {
		x = mon->cursx;
		y = mon->cursy;
	} else {
		ret = XTranslateCoordinates(dpy, menu->parent->win, rootwin,
		                            menu->caller->x, menu->caller->y,
		                            &x, &y, &w1);
		if (ret == False)
			errx(EXIT_FAILURE, "menus are on different screens");
	}
	menu->x = mon->x;
	menu->y = mon->y;
	if (x - mon->x >= pie.radius) {
		if (mon->x + mon->w - x >= pie.radius)
			menu->x = x - pie.radius - pie.border;
		else if (mon->x + mon->w >= pie.fulldiameter)
			menu->x = mon->x + mon->w - pie.fulldiameter;
	}
	if (y - mon->y >= pie.radius) {
		if (mon->y + mon->h - y >= pie.radius)
			menu->y = y - pie.radius - pie.border;
		else if (mon->y + mon->h >= pie.fulldiameter)
			menu->y = mon->y + mon->h - pie.fulldiameter;
	}
	changes.x = menu->x;
	changes.y = menu->y;
	XConfigureWindow(dpy, menu->win, CWX | CWY, &changes);
	for (slice = menu->list; slice != NULL; slice = slice->next) {
		if (slice->submenu != NULL) {
			placemenu(mon, slice->submenu);
		}
	}
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
	double angle;
	int r;

	if (menu == NULL)
		return NULL;

	x -= pie.radius;
	y -= pie.radius;
	y = -y;

	/* if the cursor is in the middle circle, it is in no slice */
	r = sqrt(x * x + y * y);
	if (r <= pie.separatorbeg)
		return NULL;

	angle = atan2(y, x);
	if (angle < 0.0) {
		if (angle > -menu->half)
			return menu->list;
		angle = (2 * M_PI) + angle;
	}
	for (slice = menu->list; slice; slice = slice->next)
		if (angle >= slice->anglea && angle < slice->angleb)
			return slice;

	return NULL;
}

/* map tooltip and place it on given position */
static void
maptooltip(struct Monitor *mon, struct Slice *slice, int x, int y)
{
	y += TTVERT;
	if (slice->icon == NULL || slice->label == NULL)
		return;
	if (y + pie.tooltiph + 2 > mon->y + mon->h)
		y = mon->y + mon->h - pie.tooltiph - 2;
	if (x + slice->ttw + 2 > mon->x + mon->w)
		x = mon->x + mon->w - slice->ttw - 2;
	XMoveWindow(dpy, slice->tooltip, x, y);
	XMapRaised(dpy, slice->tooltip);
}

/* unmap tooltip if mapped, set mapped to zero */
static void
unmaptooltip(struct Slice *slice)
{
	if (slice == NULL || slice->icon == NULL || slice->label == NULL)
		return;
	XUnmapWindow(dpy, slice->tooltip);
}

/* unmap previous menus; map current menu and its parents */
static struct Menu *
mapmenu(struct Menu *currmenu, struct Menu *prevmenu)
{
	struct Menu *menu, *menu_;
	struct Menu *lcamenu;   /* lowest common ancestor menu */
	int minlevel;           /* level of the closest to root menu */
	int maxlevel;           /* level of the closest to root menu */

	/* do not remap current menu if it wasn't updated*/
	if (prevmenu == currmenu)
		goto done;

	/* if this is the first time mapping, skip calculations */
	if (prevmenu == NULL) {
		XMapRaised(dpy, currmenu->win);
		goto done;
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
		XMapRaised(dpy, menu->win);

done:
	return currmenu;
}

/* unmap current menu and its parents */
static void
unmapmenu(struct Menu *currmenu)
{
	struct Menu *menu;

	for (menu = currmenu; menu; menu = menu->parent) {
		menu->selected = NULL;
		XUnmapWindow(dpy, menu->win);
	}
}

/* draw background of selected slice */
static void
drawslice(struct Menu *menu, struct Slice *slice)
{
	XPointDouble *p;
	int i, outer, inner, npoints;
	double h, a, b;

	/* determine number of segments to draw */
	h = hypot(pie.radius, pie.radius)/2;
	outer = ((2 * M_PI) / (menu->nslices * acos(h/(h+1.0)))) + 0.5;
	outer = (outer < 3) ? 3 : outer;
	h = hypot(pie.separatorbeg, pie.separatorbeg)/2;
	inner = ((2 * M_PI) / (menu->nslices * acos(h/(h+1.0)))) + 0.5;
	inner = (inner < 3) ? 3 : inner;
	npoints = inner + outer + 2;
	p = emalloc(npoints * sizeof *p);

	b = ((2 * M_PI) / menu->nslices) * slice->slicen;

	/* outer points */
	a = ((2 * M_PI) / (menu->nslices * outer));
	for (i = 0; i <= outer; i++) {
		p[i].x = pie.radius + (pie.radius + 1) * cos((i - (outer / 2.0)) * a - b);
		p[i].y = pie.radius + (pie.radius + 1) * sin((i - (outer / 2.0)) * a - b);
	}

	/* inner points */
	a = ((2 * M_PI) / (menu->nslices * inner));
	for (i = 0; i <= inner; i++) {
		p[i + outer + 1].x = pie.radius + pie.separatorbeg * cos(((inner - i) - (inner / 2.0)) * a - b);
		p[i + outer + 1].y = pie.radius + pie.separatorbeg * sin(((inner - i) - (inner / 2.0)) * a - b);
	}
	
	XRenderCompositeDoublePoly(dpy, PictOpOver, pie.selbg, slice->picture,
	                           XRenderFindStandardFormat(dpy, PictStandardA8),
	                           0, 0, 0, 0, p, npoints, 0);

	free(p);
}

/* draw separator before slice */
static void
drawseparator(Picture picture, struct Menu *menu, struct Slice *slice)
{
	XPointDouble p[4];
	double a;

	a = -((M_PI + 2 * M_PI * slice->slicen) / menu->nslices);
	p[0].x = pie.radius + pie.separatorbeg * cos(a - pie.innerangle);
	p[0].y = pie.radius + pie.separatorbeg * sin(a - pie.innerangle);
	p[1].x = pie.radius + pie.separatorbeg * cos(a + pie.innerangle);
	p[1].y = pie.radius + pie.separatorbeg * sin(a + pie.innerangle);
	p[2].x = pie.radius + pie.separatorend * cos(a + pie.outerangle);
	p[2].y = pie.radius + pie.separatorend * sin(a + pie.outerangle);
	p[3].x = pie.radius + pie.separatorend * cos(a - pie.outerangle);
	p[3].y = pie.radius + pie.separatorend * sin(a - pie.outerangle);
	XRenderCompositeDoublePoly(dpy, PictOpOver, pie.separator, picture,
	                           XRenderFindStandardFormat(dpy, PictStandardA8),
	                           0, 0, 0, 0, p, 4, 0);
}

/* draw triangle for slice with submenu */
static void
drawtriangle(Picture source, Picture picture, struct Menu *menu, struct Slice *slice)
{
	XPointDouble p[3];
	double a;

	a = - (((2 * M_PI) / menu->nslices) * slice->slicen);
	p[0].x = pie.radius + pie.triangleinner * cos(a - pie.triangleangle);
	p[0].y = pie.radius + pie.triangleinner * sin(a - pie.triangleangle);
	p[1].x = pie.radius + pie.triangleouter * cos(a);
	p[1].y = pie.radius + pie.triangleouter * sin(a);
	p[2].x = pie.radius + pie.triangleinner * cos(a + pie.triangleangle);
	p[2].y = pie.radius + pie.triangleinner * sin(a + pie.triangleangle);
	XRenderCompositeDoublePoly(dpy, PictOpOver, source, picture,
	                           XRenderFindStandardFormat(dpy, PictStandardA8),
	                           0, 0, 0, 0, p, 3, 0);
}

/* draw regular slice */
static void
drawmenu(struct Menu *menu, struct Slice *selected)
{
	struct Slice *slice;
	XftColor *color;
	XftDraw *draw;
	Drawable pixmap;
	Picture picture;
	Picture source;

	if (selected) {
		pixmap = selected->pixmap;
		picture = selected->picture;
		selected->drawn = 1;
	} else {
		pixmap = menu->pixmap;
		picture = menu->picture;
		menu->drawn = 1;
	}

	/* draw background */
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, pixmap, dc.gc, 0, 0, pie.diameter, pie.diameter);
	if (selected)
		drawslice(menu, selected);

	/* draw slice foreground */
	for (slice = menu->list; slice; slice = slice->next) {
		if (slice == selected) {
			color = dc.selected;
			source = pie.selfg;
		} else {
			color = dc.normal;
			source = pie.fg;
		}

		if (slice->icon != NULL) {      /* if there is an icon, draw it */
			imlib_context_set_drawable(pixmap);
			imlib_context_set_image(slice->icon);
			imlib_render_image_on_drawable(slice->iconx, slice->icony);
		} else {                        /* otherwise, draw the label */
			draw = XftDrawCreate(dpy, pixmap, visual, colormap);
			XSetForeground(dpy, dc.gc, color[ColorFG].pixel);
			drawtext(draw, &color[ColorFG], slice->labelx, slice->labely, slice->label);
			XftDrawDestroy(draw);
		}

		/* draw separator */
		drawseparator(picture, menu, slice);

		/* draw triangle */
		if (slice->submenu || slice->iscmd) {
			drawtriangle(source, picture, menu, slice);
		}
	}
}

/* draw tooltip of slice */
static void
drawtooltip(struct Slice *slice)
{
	XftDraw *draw;

	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, slice->ttpix, dc.gc, 0, 0, slice->ttw, pie.tooltiph);
	draw = XftDrawCreate(dpy, slice->ttpix, visual, colormap);
	XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
	drawtext(draw, &dc.normal[ColorFG], TTPAD, pie.tooltiph / 2, slice->label);
	XftDrawDestroy(draw);
	slice->ttdrawn = 1;
}

/* draw slices of the current menu and of its ancestors */
static void
copymenu(struct Menu *currmenu)
{
	struct Menu *menu;
	Drawable pixmap;

	for (menu = currmenu; menu != NULL; menu = menu->parent) {
		if (menu->selected) {
			pixmap = menu->selected->pixmap;
			if (!menu->selected->drawn)
				drawmenu(menu, menu->selected);
		} else {
			pixmap = menu->pixmap;
			if (!menu->drawn)
				drawmenu(menu, NULL);
		}
		XCopyArea(dpy, pixmap, menu->win, dc.gc, 0, 0, pie.diameter, pie.diameter, 0, 0);
	}
}

/* draw slice's tooltip */
static void
copytooltip(struct Slice *slice)
{
	if (slice->icon == NULL || slice->label == NULL)
		return;
	if (!slice->ttdrawn)
		drawtooltip(slice);
	XCopyArea(dpy, slice->ttpix, slice->tooltip, dc.gc, 0, 0, slice->ttw, pie.tooltiph, 0, 0);
}

/* cycle through the slices; non-zero direction is next, zero is prev */
static struct Slice *
slicecycle(struct Menu *currmenu, int clockwise)
{
	struct Slice *slice;
	struct Slice *lastslice;

	slice = NULL;
	if (clockwise) {
		for (lastslice = currmenu->list;
		     lastslice != NULL && lastslice->next != NULL;
		     lastslice = lastslice->next)
			;
		if (currmenu->selected == NULL)
			slice = currmenu->list;
		else if (currmenu->selected->prev != NULL)
			slice = currmenu->selected->prev;
		if (slice == NULL)
			slice = lastslice;
	} else {
		if (currmenu->selected == NULL)
			slice = currmenu->list;
		else if (currmenu->selected->next != NULL)
			slice = currmenu->selected->next;
		if (slice == NULL)
			slice = currmenu->list;
	}
	return slice;
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
		XFreePixmap(dpy, slice->pixmap);
		if (slice->tooltip != None)
			XDestroyWindow(dpy, slice->tooltip);
		if (slice->ttpix != None)
			XFreePixmap(dpy, slice->ttpix);
		if (tmp->file != NULL)
			free(tmp->file);
		if (tmp->icon != NULL) {
			imlib_context_set_image(tmp->icon);
			imlib_free_image();
		}
		slice = slice->next;
		free(tmp);
	}

	XFreePixmap(dpy, menu->pixmap);
	XDestroyWindow(dpy, menu->win);
	free(menu);
}

/* clear menus generated via genmenu */
static void
cleangenmenu(struct Menu *menu)
{
	struct Slice *slice;

	for (slice = menu->list; slice; slice = slice->next) {
		if (slice->submenu != NULL)
			cleangenmenu(slice->submenu);
		if (slice->iscmd == CMD_RUN) {
			cleanmenu(slice->submenu);
			slice->iscmd = CMD_NOTRUN;
			slice->submenu = NULL;
		}
	}
}

/* run command of slice to generate a submenu */
static struct Menu *
genmenu(struct Monitor *mon, struct Menu *menu, struct Slice *slice)
{
	FILE *fp;

	if ((fp = popen(slice->output, "r")) == NULL) {
		warnx("could not run: %s", slice->output);
		return NULL;
	}
	if ((slice->submenu = parse(fp, menu->level + 1)) == NULL)
		return NULL;
	pclose(fp);
	slice->submenu->parent = menu;
	slice->submenu->caller = slice;
	slice->iscmd = CMD_RUN;
	setslices(slice->submenu);
	placemenu(mon, slice->submenu);
	if (slice->submenu->list == NULL) {
		cleanmenu(slice->submenu);
		return NULL;
	}
	return slice->submenu;
}

/* ungrab pointer and keyboard */
static void
ungrab(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
}

/* create tooltip */
static void
tooltip(struct Menu *currmenu, XEvent *ev)
{
	struct Menu *menu = NULL;
	struct Slice *slice = NULL;

	while (!XNextEvent(dpy, ev)) {
		switch (ev->type) {
		case Expose:
			if (ev->xexpose.count == 0) {
				if (ev->xexpose.window == currmenu->selected->tooltip) {
					copytooltip(currmenu->selected);
				} else if (ev->xexpose.window == currmenu->win) {
					copymenu(currmenu);
				}
			}
			break;
		case MotionNotify:
			menu = getmenu(currmenu, ev->xmotion.window);
			slice = getslice(menu, ev->xmotion.x, ev->xmotion.y);
			if ((menu != NULL && menu != currmenu) || slice != currmenu->selected) {
				/* motion off selected slice */
				return;
			}
			break;
		case LeaveNotify:
		case ButtonRelease:
		case ButtonPress:
		case KeyPress:
		case ConfigureNotify:
			return;
		}
	}
}

/* item was entered, print its output or run it */
static void
enteritem(struct Slice *slice)
{
	if (config.execcommand) {
		if (efork() == 0) {
			if (efork() == 0) {
				eexecsh(slice->output);
				exit(1);
			}
			exit(1);
		}
		wait(NULL);
	} else {
		printf("%s\n", slice->output);
		fflush(stdout);
	}
}

/* run event loop */
static void
run(struct pollfd *pfd, struct Monitor *mon, struct Menu *rootmenu)
{
	struct Menu *currmenu;
	struct Menu *prevmenu;
	struct Menu *menu = NULL;
	struct Slice *slice = NULL;
	KeySym ksym;
	XEvent ev;
	int timeout;
	int nready;
	int ttx, tty;

	nready = 3;
	timeout = -1;
	ttx = tty = 0;
	prevmenu = currmenu = rootmenu;
	while (XPending(dpy) || (nready = poll(pfd, 1, timeout)) != -1) {
		if (nready == 0 && currmenu != NULL && currmenu->selected != NULL) {
			maptooltip(mon, currmenu->selected, ttx, tty);
			tooltip(currmenu, &ev);
			unmaptooltip(currmenu->selected);
		} else {
			XNextEvent(dpy, &ev);
		}
		switch (ev.type) {
		case Expose:
			if (currmenu != NULL && ev.xexpose.count == 0)
				copymenu(currmenu);
			break;
		case MotionNotify:
			timeout = -1;
			menu = getmenu(currmenu, ev.xmotion.window);
			slice = getslice(menu, ev.xmotion.x, ev.xmotion.y);
			if (menu == NULL)
				break;
			if (nowarpflag) {
				menu->selected = slice;
			} else if (currmenu != rootmenu && menu != currmenu) {
				/* motion off a non-root menu */
				currmenu = currmenu->parent;
				prevmenu = mapmenu(currmenu, prevmenu);
				currmenu->selected = NULL;
				copymenu(currmenu);
			}
			if (menu == currmenu) {
				/* motion inside a menu */
				currmenu->selected = slice;
				timeout = 1000;
				ttx = ev.xmotion.x_root;
				tty = ev.xmotion.y_root;
			}
			copymenu(currmenu);
			break;
		case ButtonRelease:
			timeout = -1;
			if (ev.xbutton.button != Button1 && ev.xbutton.button != Button3)
				break;
			menu = getmenu(currmenu, ev.xbutton.window);
			slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
			if (menu == NULL || slice == NULL)
				break;
selectslice:
			if (slice->submenu) {
				currmenu = slice->submenu;
			} else if (slice->iscmd == CMD_NOTRUN) {
				if ((menu = genmenu(mon, menu, slice)) != NULL) {
					currmenu = menu;
				}
			} else {
				enteritem(slice);
				goto done;
			}
			prevmenu = mapmenu(currmenu, prevmenu);
			currmenu->selected = NULL;
			copymenu(currmenu);
			if (!nowarpflag)
				XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
			break;
		case LeaveNotify:
			if (!(ev.xcrossing.state & (Button1Mask | Button3Mask)))
				break;
			menu = getmenu(currmenu, ev.xcrossing.window);
			slice = getslice(menu, ev.xcrossing.x, ev.xcrossing.y);
			if (menu == NULL || slice == NULL)
				break;
			if (slice == currmenu->selected &&
			    (slice->submenu != NULL || slice->iscmd == CMD_NOTRUN))
				goto selectslice;
			break;
		case ButtonPress:
			timeout = -1;
			if (ev.xbutton.button != Button1 && ev.xbutton.button != Button3)
				break;
			menu = getmenu(currmenu, ev.xbutton.window);
			slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
			if (menu == NULL || slice == NULL)
				goto done;
			break;
		case KeyPress:
			timeout = -1;
			ksym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);

			/* esc closes pmenu when current menu is the root menu */
			if (ksym == XK_Escape && currmenu->parent == NULL)
				goto done;

			/* Shift-Tab = ISO_Left_Tab */
			if (ksym == XK_Tab && (ev.xkey.state & ShiftMask))
				ksym = XK_ISO_Left_Tab;

			/* cycle through menu */
			slice = NULL;
			if (ksym == XK_Tab) {
				slice = slicecycle(currmenu, 1);
			} else if (ksym == XK_ISO_Left_Tab) {
				slice = slicecycle(currmenu, 0);
			} else if ((ksym == XK_Return) &&
			           currmenu->selected != NULL) {
				slice = currmenu->selected;
				goto selectslice;
			} else if ((ksym == XK_Escape) &&
			           currmenu->parent != NULL) {
				slice = currmenu->parent->selected;
				currmenu = currmenu->parent;
				prevmenu = mapmenu(currmenu, prevmenu);
			} else
				break;
			currmenu->selected = slice;
			copymenu(currmenu);
			break;
		case ConfigureNotify:
			menu = getmenu(currmenu, ev.xconfigure.window);
			if (menu == NULL)
				break;
			menu->x = ev.xconfigure.x;
			menu->y = ev.xconfigure.y;
			break;
		}
		XFlush(dpy);
	}
	if (nready == -1)
		err(1, "poll");
done:
	unmapmenu(currmenu);
	ungrab();
	cleangenmenu(rootmenu);
}

/* free pictures */
static void
cleanpictures(void)
{
	XRenderFreePicture(dpy, pie.bg);
	XRenderFreePicture(dpy, pie.fg);
	XRenderFreePicture(dpy, pie.selbg);
	XRenderFreePicture(dpy, pie.selfg);
	XRenderFreePicture(dpy, pie.separator);
}

/* cleanup drawing context */
static void
cleandc(void)
{
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.separator);
	XftColorFree(dpy, visual, colormap, &dc.border);
	XFreeGC(dpy, dc.gc);
}

/* pmenu: generate a pie menu from stdin and print selected entry to stdout */
int
main(int argc, char *argv[])
{
	struct pollfd pfd;
	struct Menu *rootmenu;
	struct Monitor mon;
	XEvent ev;

	/* open connection to server and set X variables */
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	rootwin = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	xformat = XRenderFindVisualFormat(dpy, visual);
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);

	/* get configuration */
	getresources();
	getoptions(argc, argv);

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	/* initializers */
	initdc();
	initpie();
	initatoms();

	/* if running in root mode, get button presses from root window */
	if (rootmodeflag)
		XGrabButton(dpy, button, AnyModifier, rootwin, False, ButtonPressMask, GrabModeSync, GrabModeSync, None, None);

	/* generate menus and set them up */
	rootmenu = parse(stdin, 0);
	if (rootmenu == NULL)
		errx(1, "no menu generated");
	setslices(rootmenu);

	pfd.fd = XConnectionNumber(dpy);
	pfd.events = POLLIN;
	do {
		if (rootmodeflag)
			XNextEvent(dpy, &ev);
		if (!rootmodeflag ||
		    (ev.type == ButtonPress &&
		     (modifier == AnyModifier ||
		      (modifier && ev.xbutton.state == modifier) ||
		      (ev.xbutton.subwindow == None)))) {
			if (rootmodeflag && passclickflag) {
				XAllowEvents(dpy, ReplayPointer, CurrentTime);
			}
			getmonitor(&mon);
			grabpointer();
			grabkeyboard();
			placemenu(&mon, rootmenu);
			mapmenu(rootmenu, NULL);
			XWarpPointer(dpy, None, rootmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
			XFlush(dpy);
			run(&pfd, &mon, rootmenu);
		} else {
			XAllowEvents(dpy, ReplayPointer, CurrentTime);
		}
	} while (rootmodeflag);

	/* free stuff */
	free(iconstring);
	cleanmenu(rootmenu);
	cleanpictures();
	cleandc();
	XCloseDisplay(dpy);

	return 0;
}
