#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <locale.h>
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

#include "defs.h"
#include "ctrlfnt.h"

#define SHELL    "sh"
#define CLASS    "PMenu"
#define NAME     "pmenu"
#define TTPAD    4              /* padding for the tooltip */
#define TTVERT   30             /* vertical distance from mouse to place tooltip */
#define MAXPATHS 128            /* maximal number of paths to look for icons */
#define ICONPATH "ICONPATH"     /* environment variable name */
#define MAX(x,y)            ((x)>(y)?(x):(y))
#define MIN(x,y)            ((x)<(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))

#define ATOMS                                   \
	X(NET_WM_WINDOW_TYPE)                   \
	X(NET_WM_WINDOW_TYPE_TOOLTIP)           \
	X(NET_WM_WINDOW_TYPE_POPUP_MENU)

#define RESOURCES                                                       \
	X(_SELECT_BG,   "Selbackground",        "selbackground")        \
	X(_SELECT_FG,   "Selforeground",        "selforeground")        \
	X(BORDER_CLR,   "BorderColor",          "borderColor")          \
	X(BORDER_WID,   "BorderWidth",          "borderWidth")          \
	X(DIAMETER,     "DiameterWidth",        "diameterWidth")        \
	X(FACE_NAME,    "FaceName",             "faceName")             \
	X(FACE_SIZE,    "FaceSize",             "faceSize")             \
	X(NORMAL_BG,    "Background",           "background")           \
	X(NORMAL_FG,    "Foreground",           "foreground")           \
	X(SELECT_BG,    "ActiveBackground",     "activeBackground")     \
	X(SELECT_FG,    "ActiveForeground",     "activeForeground")     \
	X(SHADOW_BOT,   "BottomShadowColor",    "bottomShadowColor")    \
	X(SHADOW_MID,   "MiddleShadowColor",    "middleShadowColor")    \
	X(SHADOW_TOP,   "TopShadowColor",       "topShadowColor")       \
	X(SHADOW_WID,   "ShadowThickness",      "shadowThickness")

#define DEF_BORDER 2
#define DEF_DIAMETER 200
#define DEF_COLOR_BG     (XRenderColor){ .red = 0x0000, .green = 0x0000, .blue = 0x0000, .alpha = 0xFFFF }
#define DEF_COLOR_FG     (XRenderColor){ .red = 0xFFFF, .green = 0xFFFF, .blue = 0xFFFF, .alpha = 0xFFFF }
#define DEF_COLOR_SELBG  (XRenderColor){ .red = 0x3400, .green = 0x6500, .blue = 0xA400, .alpha = 0xFFFF }
#define DEF_COLOR_SELFG  (XRenderColor){ .red = 0xFFFF, .green = 0xFFFF, .blue = 0xFFFF, .alpha = 0xFFFF }
#define DEF_COLOR_BORDER (XRenderColor){ .red = 0x3100, .green = 0x3100, .blue = 0x3100, .alpha = 0xFFFF }

#define SEPARATOR_BEG 0.14
#define SEPARATOR_END 0.37

enum {
	SCHEME_NORMAL,
	SCHEME_SELECT,
	SCHEME_BORDER,
	SCHEME_LAST,
};

enum {
	COLOR_BG = 0,
	COLOR_FG = 1,

	COLOR_TOP = 0,
	COLOR_BOT = 1,

	COLOR_LAST = 2,
};

enum {
	TRIANGLE_WIDTH = 3,
	TRIANGLE_HEIGHT = 7,
	TRIANGLE_DISTANCE = 6,
	TTBORDER = 1,
};

enum Resource {
#define X(res, s1, s2) res,
	RESOURCES
	NRESOURCES
#undef  X
};

enum Atoms {
#define X(atom) atom,
	ATOMS
	NATOMS
#undef  X
};

/* state of command to popen */
enum {NO_CMD = 0, CMD_NOTRUN = 1, CMD_RUN = 2};

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
	Picture ttpict;         /* pixmap for the tooltip */
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

struct Pie {
	Display *display;
	Visual *visual;
	Window rootwin;
	Colormap colormap;
	XRenderPictFormat *xformat, *alphaformat;
	int screen;
	int depth;
	Atom atoms[NATOMS];
	XClassHint classh;
	Window dummy;
	struct {
		XRenderColor chans;
		Pixmap pix;
		Picture pict;
	} colors[SCHEME_LAST][COLOR_LAST];
	CtrlFontSet *fontset;
	int fonth;

	Pixmap clip;

	Picture gradient;

	struct {
		XrmClass class;
		XrmName name;
	} application, resources[NRESOURCES];

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
};

/* The pie bitmap structure */
static struct Pie pie = { 0 };

/* flags */
static int harddiameter = 0;
static int execcommand = 0;
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

static void
usage(void)
{
	(void)fprintf(stderr, "usage: pmenu [-ew] [-d diameter] [-N name] [(-x|-X) [modifier-]button]\n");
	exit(1);
}

static Window
createwindow(int width, int height, long eventmask)
{
	return XCreateWindow(
		pie.display,
		pie.rootwin,
		0, 0,
		width,
		height,
		0,
		pie.depth,
		InputOutput,
		pie.visual,
		CWBackPixel | CWEventMask | CWColormap |
		CWBorderPixel | CWOverrideRedirect | CWSaveUnder,
		&(XSetWindowAttributes){
			.border_pixel = 0,
			.background_pixel = 0,
			.colormap = pie.colormap,
			.event_mask = eventmask,
			.save_under = True,     /* pop-up windows should save_under */
			.override_redirect = True,
		}
	);
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

	if (strncasecmp(s, "Mod", 3) == 0)
		s += 3;
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
	double l;
	int ch;
	char *endp, *s, *t;

	pie.classh.res_class = CLASS;
	pie.classh.res_name = getenv("RESOURCES_NAME");
	if (pie.classh.res_name == NULL && argv[0] != NULL && argv[0][0] != '\0') {
		if ((pie.classh.res_name = strrchr(argv[0], '/')) != NULL) {
			pie.classh.res_name++;
		} else {
			pie.classh.res_name = argv[0];
		}
	}
	if (pie.classh.res_name == NULL)
		pie.classh.res_name = NAME;
	parseiconpaths(getenv(ICONPATH));
	while ((ch = getopt(argc, argv, "d:eN:wx:X:P:r:m:p")) != -1) {
		switch (ch) {
		case 'd':
			l = strtol(optarg, &endp, 10);
			if (optarg[0] != '\0' && *endp == '\0' && l > 0 && l <= 100) {
				harddiameter = 1;
				pie.diameter = l;
			}
			break;
		case 'e':
			execcommand = !execcommand;
			break;
		case 'N':
			pie.classh.res_name = optarg;
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
	XSizeHints sizeh;
	struct Menu *menu;

	menu = emalloc(sizeof *menu);

	/* create menu window */
	menu->win = createwindow(
		pie.fulldiameter, pie.fulldiameter,
		KeyPressMask | ButtonPressMask |
		ButtonReleaseMask | PointerMotionMask | LeaveWindowMask
	);

	/* Set window type */
	XChangeProperty(pie.display, menu->win, pie.atoms[NET_WM_WINDOW_TYPE], XA_ATOM, 32,
	                PropModeReplace, (unsigned char *)&pie.atoms[NET_WM_WINDOW_TYPE_POPUP_MENU], 1);

	XShapeCombineMask(pie.display, menu->win, ShapeClip, 0, 0, pie.clip, ShapeSet);
	XShapeCombineMask(pie.display, menu->win, ShapeBounding, 0, 0, pie.clip, ShapeSet);

	/* set window manager hints */
	sizeh.flags = USPosition | PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = pie.fulldiameter;
	sizeh.min_height = sizeh.max_height = pie.fulldiameter;
	XSetWMProperties(pie.display, menu->win, NULL, NULL, NULL, 0, &sizeh, NULL, &pie.classh);

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
	menu->pixmap = XCreatePixmap(
		pie.display,
		menu->win,
		pie.fulldiameter,
		pie.fulldiameter,
		pie.depth
	);
	menu->picture = XRenderCreatePicture(
		pie.display,
		menu->pixmap,
		pie.xformat,
		0,
		NULL
	);
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

/* setup position of and content of menu's slices */
/* recursivelly setup menu configuration and its pixmap */
static void
setslices(struct Menu *menu)
{
	struct Slice *slice;
	double a = 0.0;
	unsigned n = 0;
	int textwidth;
	int w, h;

	menu->half = M_PI / menu->nslices;
	for (slice = menu->list; slice; slice = slice->next) {
		slice->parent = menu;
		slice->slicen = n++;

		slice->anglea = a - menu->half;
		slice->angleb = a + menu->half;

		/* get length of slice->label rendered in the font */
		if (slice->label != NULL)
			textwidth = ctrlfnt_width(pie.fontset, slice->label, strlen(slice->label));
		else
			textwidth = 0;

		/* get position of slice's label */
		slice->labelx = pie.radius + ((pie.radius*2)/3 * cos(a)) - (textwidth / 2);
		slice->labely = pie.radius - ((pie.radius*2)/3 * sin(a)) - (pie.fonth / 2);

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
		slice->pixmap = XCreatePixmap(
			pie.display,
			menu->win,
			pie.fulldiameter,
			pie.fulldiameter,
			pie.depth
		);
		slice->picture = XRenderCreatePicture(
			pie.display,
			slice->pixmap,
			pie.xformat,
			0,
			NULL
		);
		slice->drawn = 0;

		/* create tooltip */
		slice->ttdrawn = 0;
		if (textwidth > 0) {
			slice->ttw = textwidth + 2 * TTPAD;

			w = slice->ttw + TTBORDER * 2;
			h = pie.tooltiph + TTBORDER * 2;
			slice->tooltip = createwindow(w, h, 0);
			slice->ttpix = XCreatePixmap(
				pie.display,
				slice->tooltip,
				w, h,
				pie.depth
			);
			slice->ttpict = XRenderCreatePicture(
				pie.display,
				slice->ttpix,
				pie.xformat,
				0, NULL
			);
			XChangeProperty(
				pie.display,
				slice->tooltip,
				pie.atoms[NET_WM_WINDOW_TYPE],
				XA_ATOM, 32,
				PropModeReplace,
				(unsigned char *)&pie.atoms[NET_WM_WINDOW_TYPE_TOOLTIP],
				1
			);

		} else {
			slice->ttw = 0;
			slice->tooltip = None;
			slice->ttpix = None;
			slice->ttpict = None;
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

	XQueryPointer(pie.display, pie.rootwin, &dw, &dw, &mon->cursx, &mon->cursy, &di, &di, &du);

	mon->x = mon->y = 0;
	mon->w = DisplayWidth(pie.display, pie.screen);
	mon->h = DisplayHeight(pie.display, pie.screen);

	if ((info = XineramaQueryScreens(pie.display, &nmons)) != NULL) {
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
		if (XGrabPointer(pie.display, pie.rootwin, True, ButtonPressMask,
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
		if (XGrabKeyboard(pie.display, pie.rootwin, True, GrabModeAsync,
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
		ret = XTranslateCoordinates(pie.display, menu->parent->win, pie.rootwin,
		                            menu->caller->x, menu->caller->y,
		                            &x, &y, &w1);
		if (ret == False)
			errx(EXIT_FAILURE, "menus are on different screens");
	}
	menu->x = mon->x;
	menu->y = mon->y;
	if (x - mon->x >= pie.radius + pie.border) {
		if (mon->x + mon->w - x >= pie.radius + pie.border)
			menu->x = x - pie.radius - pie.border;
		else if (mon->x + mon->w >= pie.fulldiameter)
			menu->x = mon->x + mon->w - pie.fulldiameter;
	}
	if (y - mon->y >= pie.radius + pie.border) {
		if (mon->y + mon->h - y >= pie.radius + pie.border)
			menu->y = y - pie.radius - pie.border;
		else if (mon->y + mon->h >= pie.fulldiameter)
			menu->y = mon->y + mon->h - pie.fulldiameter;
	}
	changes.x = menu->x;
	changes.y = menu->y;
	XConfigureWindow(pie.display, menu->win, CWX | CWY, &changes);
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

	x -= pie.border + pie.radius;
	y -= pie.border + pie.radius;
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
	XMoveWindow(pie.display, slice->tooltip, x, y);
	XMapRaised(pie.display, slice->tooltip);
}

/* unmap tooltip if mapped, set mapped to zero */
static void
unmaptooltip(struct Slice *slice)
{
	if (slice == NULL || slice->icon == NULL || slice->label == NULL)
		return;
	XUnmapWindow(pie.display, slice->tooltip);
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
		XMapRaised(pie.display, currmenu->win);
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
		XUnmapWindow(pie.display, menu->win);
	}

	/* map menus from currmenu (inclusive) until lcamenu (exclusive) */
	for (menu = currmenu; menu != lcamenu; menu = menu->parent)
		XMapRaised(pie.display, menu->win);

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
		XUnmapWindow(pie.display, menu->win);
	}
}

/* draw background of selected slice */
static void
drawslice(Picture picture, Picture color, int nslices, int slicen, int separatorbeg)
{
	XPointDouble *p;
	int i, outer, inner, npoints;
	double h, a, b;

	/* determine number of segments to draw */
	h = hypot(pie.radius, pie.radius)/2;
	outer = ((2 * M_PI) / (nslices * acos(h/(h+1.0)))) + 0.5;
	outer = (outer < 3) ? 3 : outer;
	h = hypot(separatorbeg, separatorbeg)/2;
	inner = ((2 * M_PI) / (nslices * acos(h/(h+1.0)))) + 0.5;
	inner = (inner < 3) ? 3 : inner;
	npoints = inner + outer + 2;
	p = emalloc(npoints * sizeof *p);

	b = ((2 * M_PI) / nslices) * slicen;

	/* outer points */
	a = ((2 * M_PI) / (nslices * outer));
	for (i = 0; i <= outer; i++) {
		p[i].x = pie.border + pie.radius + pie.radius * cos((i - (outer / 2.0)) * a - b);
		p[i].y = pie.border + pie.radius + pie.radius * sin((i - (outer / 2.0)) * a - b);
	}

	/* inner points */
	a = ((2 * M_PI) / (nslices * inner));
	for (i = 0; i <= inner; i++) {
		p[i + outer + 1].x = pie.border + pie.radius + separatorbeg * cos(((inner - i) - (inner / 2.0)) * a - b);
		p[i + outer + 1].y = pie.border + pie.radius + separatorbeg * sin(((inner - i) - (inner / 2.0)) * a - b);
	}
	
	XRenderCompositeDoublePoly(
		pie.display,
		PictOpOver,
		color,
		picture,
		pie.alphaformat,
		0, 0, 0, 0, p, npoints, 0
	);
	free(p);
}

/* draw separator before slice */
static void
drawseparator(Picture picture, struct Menu *menu, struct Slice *slice)
{
	XPointDouble p[4];
	double a;

	a = -((M_PI + 2 * M_PI * slice->slicen) / menu->nslices);
	p[0].x = pie.border + pie.radius + pie.separatorbeg * cos(a - pie.innerangle);
	p[0].y = pie.border + pie.radius + pie.separatorbeg * sin(a - pie.innerangle);
	p[1].x = pie.border + pie.radius + pie.separatorbeg * cos(a + pie.innerangle);
	p[1].y = pie.border + pie.radius + pie.separatorbeg * sin(a + pie.innerangle);
	p[2].x = pie.border + pie.radius + pie.separatorend * cos(a + pie.outerangle);
	p[2].y = pie.border + pie.radius + pie.separatorend * sin(a + pie.outerangle);
	p[3].x = pie.border + pie.radius + pie.separatorend * cos(a - pie.outerangle);
	p[3].y = pie.border + pie.radius + pie.separatorend * sin(a - pie.outerangle);
	XRenderCompositeDoublePoly(
		pie.display,
		PictOpOver,
		pie.colors[SCHEME_NORMAL][COLOR_FG].pict,
		picture,
		pie.alphaformat,
		0, 0, 0, 0, p, 4, 0
	);
}

/* draw triangle for slice with submenu */
static void
drawtriangle(Picture source, Picture picture, struct Menu *menu, struct Slice *slice)
{
	XPointDouble p[3];
	double a;

	a = - (((2 * M_PI) / menu->nslices) * slice->slicen);
	p[0].x = pie.border + pie.radius + pie.triangleinner * cos(a - pie.triangleangle);
	p[0].y = pie.border + pie.radius + pie.triangleinner * sin(a - pie.triangleangle);
	p[1].x = pie.border + pie.radius + pie.triangleouter * cos(a);
	p[1].y = pie.border + pie.radius + pie.triangleouter * sin(a);
	p[2].x = pie.border + pie.radius + pie.triangleinner * cos(a + pie.triangleangle);
	p[2].y = pie.border + pie.radius + pie.triangleinner * sin(a + pie.triangleangle);
	XRenderCompositeDoublePoly(
		pie.display,
		PictOpOver,
		source,
		picture,
		pie.alphaformat,
		0, 0, 0, 0, p, 3, 0
	);
}

/* draw regular slice */
static void
drawmenu(struct Menu *menu, struct Slice *selected)
{
	struct Slice *slice;
	Drawable pixmap;
	Picture picture;
	Picture source;
	Picture fg;

	if (selected) {
		pixmap = selected->pixmap;
		picture = selected->picture;
		fg = pie.colors[SCHEME_SELECT][COLOR_FG].pict;
		selected->drawn = 1;
	} else {
		pixmap = menu->pixmap;
		picture = menu->picture;
		fg = pie.colors[SCHEME_NORMAL][COLOR_FG].pict;
		menu->drawn = 1;
	}

	XRenderComposite(
		pie.display,
		PictOpSrc,
		pie.gradient,
		None,
		picture,
		0, 0,
		0, 0,
		0, 0,
		pie.fulldiameter,
		pie.fulldiameter
	);
	drawslice(
		picture,
		pie.colors[SCHEME_NORMAL][COLOR_BG].pict,
		1,
		0,
		0
	);
	if (selected != NULL) {
		drawslice(
			picture,
			pie.colors[SCHEME_SELECT][COLOR_BG].pict,
			menu->nslices,
			selected->slicen,
			pie.separatorbeg
		);
	}

	/* draw slice foreground */
	for (slice = menu->list; slice; slice = slice->next) {
		if (slice == selected) {
			source = pie.colors[SCHEME_SELECT][COLOR_FG].pict;
		} else {
			source = pie.colors[SCHEME_NORMAL][COLOR_FG].pict;
		}

		if (slice->icon != NULL) {      /* if there is an icon, draw it */
			imlib_context_set_drawable(pixmap);
			imlib_context_set_image(slice->icon);
			imlib_render_image_on_drawable(slice->iconx, slice->icony);
		} else {                        /* otherwise, draw the label */
			ctrlfnt_draw(
				pie.fontset,
				picture,
				fg,
				(XRectangle){
					.x = slice->labelx,
					.y = slice->labely,
					.width = pie.radius,
					.height = pie.fonth,
				},
				slice->label,
				strlen(slice->label)
			);
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

	XRenderFillRectangle(
		pie.display,
		PictOpSrc,
		slice->ttpict,
		&pie.colors[SCHEME_NORMAL][COLOR_FG].chans,
		0, 0,
		slice->ttw + TTBORDER * 2,
		pie.tooltiph + TTBORDER * 2
	);
	XRenderFillRectangle(
		pie.display,
		PictOpSrc,
		slice->ttpict,
		&pie.colors[SCHEME_NORMAL][COLOR_BG].chans,
		TTBORDER, TTBORDER,
		slice->ttw,
		pie.tooltiph
	);
	ctrlfnt_draw(
		pie.fontset,
		slice->ttpict,
		pie.colors[SCHEME_NORMAL][COLOR_FG].pict,
		(XRectangle){
			.x = TTPAD + TTBORDER,
			.y = TTPAD + TTBORDER,
			.width = slice->ttw,
			.height = pie.fonth,
		},
		slice->label,
		strlen(slice->label)
	);
	XSetWindowBackgroundPixmap(pie.display, slice->tooltip, slice->ttpix);
	XClearWindow(pie.display, slice->tooltip);
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
		XSetWindowBackgroundPixmap(pie.display, menu->win, pixmap);
		XClearWindow(pie.display, menu->win);
	}
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

	if (menu == NULL)
		return;
	slice = menu->list;
	while (slice != NULL) {
		if (slice->submenu != NULL)
			cleanmenu(slice->submenu);
		tmp = slice;
		if (tmp->label != tmp->output)
			free(tmp->label);
		free(tmp->output);
		XFreePixmap(pie.display, slice->pixmap);
		if (slice->tooltip != None)
			XDestroyWindow(pie.display, slice->tooltip);
		if (slice->ttpix != None)
			XFreePixmap(pie.display, slice->ttpix);
		if (slice->ttpict != None)
			XRenderFreePicture(pie.display, slice->ttpict);
		if (tmp->file != NULL)
			free(tmp->file);
		if (tmp->icon != NULL) {
			imlib_context_set_image(tmp->icon);
			imlib_free_image();
		}
		slice = slice->next;
		free(tmp);
	}

	XFreePixmap(pie.display, menu->pixmap);
	XDestroyWindow(pie.display, menu->win);
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
	XUngrabPointer(pie.display, CurrentTime);
	XUngrabKeyboard(pie.display, CurrentTime);
}

/* create tooltip */
static void
tooltip(struct Menu *currmenu, XEvent *ev)
{
	struct Menu *menu = NULL;
	struct Slice *slice = NULL;

	while (!XNextEvent(pie.display, ev)) {
		switch (ev->type) {
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
	if (execcommand) {
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

static void
warppointer(struct Menu *currmenu)
{
	XWarpPointer(
		pie.display,
		None,
		currmenu->win,
		0, 0, 0, 0,
		pie.radius + pie.border,
		pie.radius + pie.border
	);
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
	while (XPending(pie.display) || (nready = poll(pfd, 1, timeout)) != -1) {
		if (nready == 0 && currmenu != NULL && currmenu->selected != NULL) {
			if (!currmenu->selected->ttdrawn)
				drawtooltip(currmenu->selected);
			maptooltip(mon, currmenu->selected, ttx, tty);
			tooltip(currmenu, &ev);
			unmaptooltip(currmenu->selected);
		} else {
			XNextEvent(pie.display, &ev);
		}
		switch (ev.type) {
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
				warppointer(currmenu);
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
			ksym = XkbKeycodeToKeysym(pie.display, ev.xkey.keycode, 0, 0);

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
		XFlush(pie.display);
	}
	if (nready == -1)
		err(1, "poll");
done:
	unmapmenu(currmenu);
	ungrab();
	cleangenmenu(rootmenu);
}

static char *
getresource(XrmDatabase xdb, enum Resource resource)
{
	XrmQuark name[] = {
		pie.application.name,
		pie.resources[resource].name,
		NULLQUARK,
	};
	XrmQuark class[] = {
		pie.application.class,
		pie.resources[resource].class,
		NULLQUARK,
	};
	XrmRepresentation tmp;
	XrmValue xval;

	if (XrmQGetResource(xdb, name, class, &tmp, &xval))
		return xval.addr;
	return NULL;
}

static void
setcolor(int scheme, int colornum, const char *colorname)
{
	XColor color;

	if (colorname == NULL)
		return;
	if (!XParseColor(pie.display, pie.colormap, colorname, &color)) {
		warnx("%s: unknown color name", colorname);
		return;
	}
	pie.colors[scheme][colornum].chans = (XRenderColor){
		.red   = FLAG(color.flags, DoRed)   ? color.red   : 0x0000,
		.green = FLAG(color.flags, DoGreen) ? color.green : 0x0000,
		.blue  = FLAG(color.flags, DoBlue)  ? color.blue  : 0x0000,
		.alpha = 0xFFFF,
	};
	XRenderFillRectangle(
		pie.display,
		PictOpSrc,
		pie.colors[scheme][colornum].pict,
		&pie.colors[scheme][colornum].chans,
		0, 0, 1, 1
	);
}

static void
setfont(const char *facename, double facesize)
{
	CtrlFontSet *fontset;

	if (facename == NULL)
		facename = "xft:";
	fontset = ctrlfnt_open(
		pie.display,
		pie.screen,
		pie.visual,
		pie.colormap,
		facename,
		facesize
	);
	if (fontset == NULL)
		return;
	pie.fontset = fontset;
	pie.fonth = ctrlfnt_height(fontset);
}

static XrmDatabase
loadxdb(const char *str)
{
	return XrmGetStringDatabase(str);
}

static void
loadresources(const char *str)
{

	XrmDatabase xdb;
	char *value;
	enum Resource resource;
	char *endp;
	char *fontname = NULL;
	long l;
	double d, d0, d1;
	double fontsize = 0.0;
	int changefont = FALSE;

	if (str == NULL)
		return;
	if ((xdb = loadxdb(str)) == NULL)
		return;
	pie.border = DEF_BORDER;
	if (!harddiameter)
		pie.diameter = DEF_DIAMETER;
	for (resource = 0; resource < NRESOURCES; resource++) {
		value = getresource(xdb, resource);
		if (value == NULL)
			continue;
		switch (resource) {
		case SHADOW_BOT:
			setcolor(SCHEME_BORDER, COLOR_BOT, value);
			break;
		case BORDER_CLR:
			setcolor(SCHEME_BORDER, COLOR_TOP, value);
			setcolor(SCHEME_BORDER, COLOR_BOT, value);
			break;
		case SHADOW_TOP:
			setcolor(SCHEME_BORDER, COLOR_TOP, value);
			break;
		case SHADOW_WID:
		case BORDER_WID:
			l = strtol(value, &endp, 10);
			if (value[0] != '\0' && *endp == '\0' && l > 0 && l <= 100)
				pie.border = l;
			break;
		case DIAMETER:
			if (harddiameter)
				break;
			l = strtol(value, &endp, 10);
			if (value[0] != '\0' && *endp == '\0' && l > 0 && l <= 100)
				pie.diameter = l;
			break;
		case FACE_NAME:
			fontname = value;
			changefont = TRUE;
			break;
		case FACE_SIZE:
			d = strtod(value, &endp);
			if (value[0] != '\0' && *endp == '\0' && d > 0.0 && d <= 100.0) {
				fontsize = d;
				changefont = TRUE;
			}
			break;
		case NORMAL_BG:
		case SHADOW_MID:
			setcolor(SCHEME_NORMAL, COLOR_BG, value);
			break;
		case NORMAL_FG:
			setcolor(SCHEME_NORMAL, COLOR_FG, value);
			break;
		case SELECT_BG:
		case _SELECT_BG:
			setcolor(SCHEME_SELECT, COLOR_BG, value);
			break;
		case SELECT_FG:
		case _SELECT_FG:
			setcolor(SCHEME_SELECT, COLOR_FG, value);
			break;
		default:
			break;
		}
	}

	pie.radius = (pie.diameter + 1) / 2;
	pie.fulldiameter = pie.diameter + (pie.border * 2);

	if (changefont)
		setfont(fontname, fontsize);

	/* create gradient picture */
	if (pie.gradient != None)
		XRenderFreePicture(pie.display, pie.gradient);
	d = pie.fulldiameter * pie.fulldiameter * 2;
	d = sqrt(d);
	d0 = d - pie.fulldiameter;
	d0 /= 2;
	d0 /= d;
	d1 = d - pie.fulldiameter;
	d1 /= 2;
	d1 += pie.fulldiameter;
	d1 /= d;
	pie.gradient = XRenderCreateLinearGradient(
		pie.display,
		&(XLinearGradient){
			.p1 = (XPointFixed){
				.x = XDoubleToFixed(0.0),
				.y = XDoubleToFixed(0.0),
			},
			.p2 = (XPointFixed){
				.x = XDoubleToFixed((double)pie.fulldiameter),
				.y = XDoubleToFixed((double)pie.fulldiameter),
			},
		},
		(XFixed[]){
			[0] = XDoubleToFixed(0.0),
			[1] = XDoubleToFixed(d0),
			[2] = XDoubleToFixed(d1),
			[3] = XDoubleToFixed(1.0),
		},
		(XRenderColor[]){
			[0] = pie.colors[SCHEME_BORDER][COLOR_TOP].chans,
			[1] = pie.colors[SCHEME_BORDER][COLOR_TOP].chans,
			[2] = pie.colors[SCHEME_BORDER][COLOR_BOT].chans,
			[3] = pie.colors[SCHEME_BORDER][COLOR_BOT].chans,
		},
		4
	);

	XrmDestroyDatabase(xdb);
}

static int
initxconn(void)
{
	static char *atomnames[NATOMS] = {
#define X(atom) [atom] = #atom,
		ATOMS
#undef X
	};

	ctrlfnt_init();
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("could not set locale");
	if ((pie.display = XOpenDisplay(NULL)) == NULL) {
		warnx("could not connect to X server");
		return RETURN_FAILURE;
	}
	if (!XInternAtoms(pie.display, atomnames, NATOMS, False, pie.atoms)) {
		warnx("could not intern X pie.atoms");
		return RETURN_FAILURE;
	}
	pie.screen = DefaultScreen(pie.display);
	pie.rootwin = RootWindow(pie.display, pie.screen);
	return RETURN_SUCCESS;
}

static int
initvisual(void)
{
	XVisualInfo vinfo;
	Colormap cmap;
	int success;

	success = XMatchVisualInfo(
		pie.display,
		pie.screen,
		32,             /* preferred depth */
		TrueColor,
		&vinfo
	);
	cmap = success ? XCreateColormap(
		pie.display,
		pie.rootwin,
		vinfo.visual,
		AllocNone
	) : None;
	if (success && cmap != None) {
		pie.colormap = cmap;
		pie.visual = vinfo.visual;
		pie.depth = vinfo.depth;
	} else {
		pie.colormap = DefaultColormap(pie.display, pie.screen);
		pie.visual = DefaultVisual(pie.display, pie.screen);
		pie.depth = DefaultDepth(pie.display, pie.screen);
	}
	pie.xformat = XRenderFindVisualFormat(pie.display, pie.visual);
	if (pie.xformat == NULL)
		goto error;
	pie.alphaformat = XRenderFindStandardFormat(pie.display, PictStandardA8);
	if (pie.alphaformat == NULL)
		goto error;
	pie.dummy = createwindow(1, 1, 0);
	if (pie.dummy == None) {
		warnx("could not find XRender visual format");
		return RETURN_FAILURE;
	}
	return RETURN_SUCCESS;
error:
	warnx("could not find XRender visual format");
	return RETURN_FAILURE;
}

static int
initresources(void)
{
	static struct {
		const char *class, *name;
	} resourceids[NRESOURCES] = {
#define X(res, s1, s2) [res] = { .class = s1, .name = s2, },
		RESOURCES
#undef  X
	};
	int i;

	XrmInitialize();
	pie.application.class = XrmPermStringToQuark(pie.classh.res_class);
	pie.application.name = XrmPermStringToQuark(pie.classh.res_name);
	for (i = 0; i < NRESOURCES; i++) {
		pie.resources[i].class = XrmPermStringToQuark(resourceids[i].class);
		pie.resources[i].name = XrmPermStringToQuark(resourceids[i].name);
	}
	return RETURN_SUCCESS;
}

static int
inittheme(void)
{
	int i, j;

	pie.colors[SCHEME_NORMAL][COLOR_BG].chans = DEF_COLOR_BG;
	pie.colors[SCHEME_NORMAL][COLOR_FG].chans = DEF_COLOR_FG;
	pie.colors[SCHEME_SELECT][COLOR_BG].chans = DEF_COLOR_SELBG;
	pie.colors[SCHEME_SELECT][COLOR_FG].chans = DEF_COLOR_SELFG;
	pie.colors[SCHEME_BORDER][COLOR_TOP].chans = DEF_COLOR_BORDER;
	pie.colors[SCHEME_BORDER][COLOR_BOT].chans = DEF_COLOR_BORDER;
	for (i = 0; i < SCHEME_LAST; i++) {
		for (j = 0; j < COLOR_LAST; j++) {
			pie.colors[i][j].pix = XCreatePixmap(
				pie.display,
				pie.dummy,
				1, 1,
				pie.depth
			);
			if (pie.colors[i][j].pix == None) {
				warnx("could not create pixmap");
				return RETURN_FAILURE;
			}
			pie.colors[i][j].pict = XRenderCreatePicture(
				pie.display,
				pie.colors[i][j].pix,
				pie.xformat,
				CPRepeat,
				&(XRenderPictureAttributes){
					.repeat = RepeatNormal,
				}
			);
			if (pie.colors[i][j].pict == None) {
				warnx("could not create pixmap");
				return RETURN_FAILURE;
			}
			XRenderFillRectangle(
				pie.display,
				PictOpSrc,
				pie.colors[i][j].pict,
				&pie.colors[i][j].chans,
				0, 0, 1, 1
			);
		}
	}
	loadresources(XResourceManagerString(pie.display));
	if (pie.fontset == NULL)
		setfont(NULL, 0.0);
	if (pie.fontset == NULL) {
		warnx("could not load any font");
		return RETURN_FAILURE;
	}
	return RETURN_SUCCESS;
}

static int
initpie(void)
{
	GC gc;

	/* set tooltip geometry */
	pie.tooltiph = pie.fonth + 2 * TTPAD;

	/* set the geometry of the triangle for submenus */
	pie.triangleouter = pie.radius - TRIANGLE_DISTANCE;
	pie.triangleinner = pie.radius - TRIANGLE_DISTANCE - TRIANGLE_WIDTH;
	pie.triangleangle = ((double)TRIANGLE_HEIGHT / 2.0) / (double)pie.triangleinner;

	/* set the separator beginning and end */
	pie.separatorbeg = pie.radius * SEPARATOR_BEG;
	pie.separatorend = pie.radius * SEPARATOR_END;
	pie.innerangle = atan(1.0 / (2.0 * pie.separatorbeg));
	pie.outerangle = atan(1.0 / (2.0 * pie.separatorend));

	/* create bitmap mask (depth = 1) */
	pie.clip = XCreatePixmap(
		pie.display,
		pie.dummy,
		pie.fulldiameter,
		pie.fulldiameter,
		1
	);
	if (pie.clip == None) {
		warnx("could not create bitmap");
		return RETURN_FAILURE;
	}

	/* Create the mask GC */
	gc = XCreateGC(
		pie.display,
		pie.clip,
		GCBackground | GCArcMode,
		&(XGCValues){
			.background = 1,
			.arc_mode = ArcPieSlice,
		}
	);
	if (gc == NULL) {
		warnx("could not create graphics context");
		return RETURN_FAILURE;
	}

	/* clear the bitmap */
	XSetForeground(pie.display, gc, 0);
	XFillRectangle(pie.display, pie.clip, gc, 0, 0, pie.fulldiameter, pie.fulldiameter);

	/* create round shape */
	XSetForeground(pie.display, gc, 1);
	XFillArc(pie.display, pie.clip, gc, 0, 0,
	         pie.fulldiameter, pie.fulldiameter, 0, 360*64);
	XFreeGC(pie.display, gc);
	return RETURN_SUCCESS;
}

static void
cleanup(void)
{
	int i, j;

	if (pie.fontset != NULL)
		ctrlfnt_free(pie.fontset);
	for (i = 0; i < SCHEME_LAST; i++) {
		for (j = 0; j < COLOR_LAST; j++) {
			if (pie.colors[i][j].pict != None) {
				XRenderFreePicture(
					pie.display,
					pie.colors[i][j].pict
				);
			}
			if (pie.colors[i][j].pix != None) {
				XFreePixmap(
					pie.display,
					pie.colors[i][j].pix
				);
			}
		}
	}
	if (pie.clip != None)
		XFreePixmap(pie.display, pie.clip);
	if (pie.gradient != None)
		XRenderFreePicture(pie.display, pie.gradient);
	if (pie.colormap != None)
		XFreeColormap(pie.display, pie.colormap);
	if (pie.dummy != None)
		XDestroyWindow(pie.display, pie.dummy);
	if (pie.display != NULL)
		XCloseDisplay(pie.display);
	ctrlfnt_term();
}

int
main(int argc, char *argv[])
{
	int (*initsteps[])(void) = {
		initxconn,
		initvisual,
		initresources,
		inittheme,
		initpie,
	};
	struct pollfd pfd;
	struct Menu *rootmenu = NULL;
	struct Monitor mon;
	XEvent ev;
	size_t i;
	int exitval = EXIT_FAILURE;

	/* get configuration */
	getoptions(argc, argv);

	for (i = 0; i < LEN(initsteps); i++)
		if ((*initsteps[i])() == RETURN_FAILURE)
			goto error;

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(pie.display);
	imlib_context_set_visual(pie.visual);
	imlib_context_set_colormap(pie.colormap);

	/* if running in root mode, get button presses from root window */
	if (rootmodeflag)
		XGrabButton(pie.display, button, AnyModifier, pie.rootwin, False, ButtonPressMask, GrabModeSync, GrabModeSync, None, None);

	/* generate menus and set them up */
	rootmenu = parse(stdin, 0);
	if (rootmenu == NULL)
		errx(1, "no menu generated");
	setslices(rootmenu);

	pfd.fd = XConnectionNumber(pie.display);
	pfd.events = POLLIN;
	do {
		if (rootmodeflag)
			XNextEvent(pie.display, &ev);
		if (!rootmodeflag ||
		    (ev.type == ButtonPress &&
		     (modifier == AnyModifier ||
		      (modifier && ev.xbutton.state == modifier) ||
		      (ev.xbutton.subwindow == None)))) {
			if (rootmodeflag && passclickflag) {
				XAllowEvents(pie.display, ReplayPointer, CurrentTime);
			}
			getmonitor(&mon);
			grabpointer();
			grabkeyboard();
			placemenu(&mon, rootmenu);
			mapmenu(rootmenu, NULL);
			warppointer(rootmenu);
			XFlush(pie.display);
			run(&pfd, &mon, rootmenu);
		} else {
			XAllowEvents(pie.display, ReplayPointer, CurrentTime);
		}
	} while (rootmodeflag);
	exitval = EXIT_SUCCESS;

error:
	free(iconstring);
	if (rootmenu != NULL)
		cleanmenu(rootmenu);
	cleanup();

	return exitval;
}
