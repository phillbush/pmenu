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
#include "pmenu.h"

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
static struct Monitor mon;

/* The pie bitmap structure */
static struct Pie pie;

/* flags */
static int rflag = 0;           /* whether to run in root mode */
static int pflag = 0;           /* whether to pass click to root window */
static unsigned int button;     /* button to trigger pmenu in root mode */

#include "config.h"

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: pmenu [-p] [-r button]\n");
	exit(1);
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

/* get options */
static void
getoptions(int *argc, char ***argv)
{
	int ch;

	while ((ch = getopt(*argc, *argv, "pr:")) != -1) {
		switch (ch) {
		case 'p':
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			switch (*optarg) {
			case '1':
				button = Button1;
				break;
			case '2':
				button = Button2;
				break;
			case '3':
				button = Button3;
				break;
			case '4':
				button = Button4;
				break;
			case '5': 
				button = Button5;
				break;
			default:
				button = Button3;
				break;
			}
			break;
		default:
			usage();
			break;
		}
	}
	*argc -= optind;
	*argv += optind;
	if (*argc > 0)
		usage();
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
	Pixmap pbg, pselbg, separator;
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
	pselbg = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	separator = XCreatePixmap(dpy, rootwin, 1, 1, depth);
	pie.bg = XRenderCreatePicture(dpy, pbg, xformat, CPRepeat, &dc.pictattr);
	pie.selbg = XRenderCreatePicture(dpy, pselbg, xformat, CPRepeat, &dc.pictattr);
	pie.separator = XRenderCreatePicture(dpy, separator, xformat, CPRepeat, &dc.pictattr);
	XRenderFillRectangle(dpy, PictOpOver, pie.bg, &dc.normal[ColorBG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.selbg, &dc.selected[ColorBG].color, 0, 0, 1, 1);
	XRenderFillRectangle(dpy, PictOpOver, pie.separator, &dc.separator.color, 0, 0, 1, 1);
	XFreePixmap(dpy, pbg);
	XFreePixmap(dpy, pselbg);
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

/* call strdup checking for error */
static char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
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
	slice->label = label ? estrdup(label) : NULL;
	slice->output = (label == output) ? slice->label : estrdup(output);
	slice->file = file ? estrdup(file) : NULL;
	slice->y = 0;
	slice->labellen = (slice->label) ? strlen(slice->label) : 0;
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
	XClassHint classh = {PROGNAME, PROGNAME};
	XSizeHints sizeh;
	struct Menu *menu;

	menu = emalloc(sizeof *menu);

	/* create menu window */
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.save_under = True;  /* pop-up windows should save_under*/
	swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask
	               | PointerMotionMask | EnterWindowMask | LeaveWindowMask;
	menu->win = XCreateWindow(dpy, rootwin, 0, 0, pie.diameter, pie.diameter, pie.border,
	                          CopyFromParent, CopyFromParent, CopyFromParent,
	                          CWOverrideRedirect | CWBackPixel |
	                          CWBorderPixel | CWEventMask | CWSaveUnder,
	                          &swa);

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
	menu->x = 0;    /* calculated by setupmenu() */
	menu->y = 0;    /* calculated by setupmenu() */
	menu->level = level;

	/* create pixmap and picture */
	menu->pixmap = XCreatePixmap(dpy, menu->win, pie.diameter, pie.diameter, depth);
	menu->picture = XRenderCreatePicture(dpy, menu->pixmap, xformat, CPPolyEdge | CPRepeat, &dc.pictattr);
	menu->drawn = 0;

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
	struct Slice *slice;
	double a = 0.0;
	unsigned n = 0;
	int textwidth;

	menu->half = M_PI / menu->nslices;
	for (slice = menu->list; slice; slice = slice->next) {
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

			xdiff = pie.radius * 0.5 - (pie.radius * (cos(menu->half) * 0.75));
			ydiff = pie.radius * (sin(menu->half) * 0.75);

			iconsize = sqrt(xdiff * xdiff + ydiff * ydiff);
			iconsize = MIN(maxiconsize, iconsize);

			slice->icon = loadicon(slice->file, iconsize, &iconw, &iconh);

			slice->iconx = pie.radius + (pie.radius * (cos(a) * 0.6)) - iconw / 2;
			slice->icony = pie.radius - (pie.radius * (sin(a) * 0.6)) - iconh / 2;
		}

		/* create and draw pixmap */
		slice->pixmap = XCreatePixmap(dpy, menu->win, pie.diameter, pie.diameter, depth);
		slice->picture = XRenderCreatePicture(dpy, slice->pixmap, xformat, CPPolyEdge | CPRepeat, &dc.pictattr);
		slice->drawn = 0;

		/* call recursivelly */
		if (slice->submenu != NULL) {
			setslices(slice->submenu);
		}

		a += menu->half * 2;
	}
}

/* query monitor information and cursor position */
static void
getmonitor(void)
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
placemenu(struct Menu *menu)
{
	struct Slice *slice;
	XWindowChanges changes;
	Window w1;  /* dummy variable */
	int x, y;   /* position of the center of the menu */
	Bool ret;

	if (menu->parent == NULL) {
		x = mon.cursx;
		y = mon.cursy;
	} else {
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
	changes.x = menu->x;
	changes.y = menu->y;
	XConfigureWindow(dpy, menu->win, CWX | CWY, &changes);
	for (slice = menu->list; slice != NULL; slice = slice->next) {
		if (slice->submenu != NULL) {
			placemenu(slice->submenu);
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

/* umap previous menus and map current menu and its parents */
static struct Menu *
mapmenu(struct Menu *currmenu, struct Menu *prevmenu)
{
	struct Menu *menu, *menu_;
	struct Menu *lcamenu;   /* lowest common ancestor menu */
	unsigned minlevel;      /* level of the closest to root menu */
	unsigned maxlevel;      /* level of the closest to root menu */

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

/* umap urrent menu and its parents */
static void
unmapmenu(struct Menu *currmenu)
{
	struct Menu *menu;

	/* unmap menus from currmenu (inclusive) until lcamenu (exclusive) */
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
	int i, n;
	double hd, a, b, c;

	if (slice == NULL)
		return;

	/* determine number of segments to draw */
	hd = hypot(pie.radius, pie.radius)/2;
	n = ((2 * M_PI) / (menu->nslices * acos(hd/(hd+1.0)))) + 0.5;

	/* angles */
	a = ((2 * M_PI) / (menu->nslices * n));
	b = ((2 * M_PI) / menu->nslices);
	c = b * slice->slicen;

	p = emalloc((n + 2) * sizeof *p);
	p[0].x = pie.radius;
	p[0].y = pie.radius;
	for (i = 0; i < n + 1; i++) {
		p[i+1].x = pie.radius + (pie.radius + 1) * cos((i - (n / 2.0)) * a - c);
		p[i+1].y = pie.radius + (pie.radius + 1) * sin((i - (n / 2.0)) * a - c);
	}
	
	XRenderCompositeDoublePoly(dpy, PictOpOver, pie.selbg, slice->picture,
	                           XRenderFindStandardFormat(dpy, PictStandardA8),
	                           0, 0, 0, 0, p, n + 2, 0);

	free(p);
}

/* draw circle */
static void
drawcircle(Picture picture, int radius)
{
	XPointDouble *p;
	int i, n;
	double hd, a;

	/* determine number of segments to draw */
	hd = hypot(radius, radius)/2;
	n = ((2 * M_PI) / (acos(hd/(hd+1.0)))) + 0.5;

	/* angles */
	a = ((2 * M_PI) / n);

	p = emalloc((n + 2) * sizeof *p);
	p[0].x = pie.radius;
	p[0].y = pie.radius;
	for (i = 0; i < n + 1; i++) {
		p[i+1].x = pie.radius + (radius + 1) * cos(i * a);
		p[i+1].y = pie.radius + (radius + 1) * sin(i * a);
	}
	
	XRenderCompositeDoublePoly(dpy, PictOpOver, pie.bg, picture,
	                           XRenderFindStandardFormat(dpy, PictStandardA8),
	                           0, 0, 0, 0, p, n + 2, 0);

	free(p);
}

/* draw separator before slice */
static void
drawseparator(Picture picture, struct Menu *menu, struct Slice *slice)
{
	XPointDouble p[4];
	double a;

	a = (M_PI / menu->nslices) + ((2 * M_PI) / menu->nslices) * slice->slicen;
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

/* draw regular slice */
static void
drawmenu(struct Menu *menu, struct Slice *selected)
{
	struct Slice *slice;
	XftColor *color;
	XftDraw *draw;
	Drawable pixmap;
	Picture picture;

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
	drawslice(menu, selected);

	/* draw slice foreground */
	for (slice = menu->list; slice; slice = slice->next) {
		if (slice == selected)
			color = dc.selected;
		else
			color = dc.normal;

		if (slice->file) {      /* if there is an icon, draw it */
			imlib_context_set_drawable(pixmap);
			imlib_context_set_image(slice->icon);
			imlib_render_image_on_drawable(slice->iconx, slice->icony);
		} else {                /* otherwise, draw the label */
			draw = XftDrawCreate(dpy, pixmap, visual, colormap);
			XSetForeground(dpy, dc.gc, color[ColorFG].pixel);
			drawtext(draw, &color[ColorFG], slice->labelx,
			         slice->labely, slice->label);
			XftDrawDestroy(draw);
		}

		/* draw separator */
		drawseparator(picture, menu, slice);
	}

	/* draw inner circle */
	drawcircle(picture, pie.separatorbeg);
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
		XCopyArea(dpy, pixmap, menu->win, dc.gc, 0, 0,
			      pie.diameter, pie.diameter, 0, 0);
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

/* ungrab pointer and keyboard */
static void
ungrab(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
}

/* run event loop */
static void
run(struct Menu *rootmenu)
{
	struct Menu *currmenu;
	struct Menu *prevmenu;
	struct Menu *menu = NULL;
	struct Slice *slice = NULL;
	KeySym ksym;
	XEvent ev;
	int mapped = 0;

	if (!rflag) {
		getmonitor();
		prevmenu = NULL;
		currmenu = rootmenu;
		grabpointer();
		grabkeyboard();
		placemenu(currmenu);
		prevmenu = mapmenu(currmenu, prevmenu);
		XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
	}
	do {
		while (!XNextEvent(dpy, &ev)) {
			if (rflag && !mapped && ev.type == ButtonPress) {
				if (ev.xbutton.subwindow == None) {
					if (pflag)
						XAllowEvents(dpy, ReplayPointer, CurrentTime);
					mapped = 1;
					getmonitor();
					prevmenu = NULL;
					currmenu = rootmenu;
					grabpointer();
					grabkeyboard();
					placemenu(currmenu);
					prevmenu = mapmenu(currmenu, prevmenu);
					XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
				} else {
					XAllowEvents(dpy, ReplayPointer, CurrentTime);
				}
				continue;
			}
			switch (ev.type) {
			case Expose:
				if (ev.xexpose.count == 0)
					copymenu(currmenu);
				break;
			case EnterNotify:
				menu = getmenu(currmenu, ev.xcrossing.window);
				if (menu == NULL)
					break;
				prevmenu = mapmenu(currmenu, prevmenu);
				XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
				copymenu(currmenu);
				break;
			case LeaveNotify:
				menu = getmenu(currmenu, ev.xcrossing.window);
				if (menu == NULL)
					break;
				if (menu != rootmenu && menu == currmenu) {
					currmenu = currmenu->parent;
					prevmenu = mapmenu(currmenu, prevmenu);
				}
				currmenu->selected = NULL;
				copymenu(currmenu);
				break;
			case MotionNotify:
				menu = getmenu(currmenu, ev.xbutton.window);
				slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
				if (menu == NULL)
					break;
				else if (slice == NULL)
					menu->selected = NULL;
				else
					menu->selected = slice;
				copymenu(currmenu);
				break;
			case ButtonRelease:
				menu = getmenu(currmenu, ev.xbutton.window);
				slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
				if (menu == NULL || slice == NULL)
					break;
	selectslice:
				if (slice->submenu) {
					currmenu = slice->submenu;
				} else {
					printf("%s\n", slice->output);
					fflush(stdout);
					goto done;
				}
				prevmenu = mapmenu(currmenu, prevmenu);
				currmenu->selected = currmenu->list;
				copymenu(currmenu);
				XWarpPointer(dpy, None, currmenu->win, 0, 0, 0, 0, pie.radius, pie.radius);
				break;
			case ButtonPress:
				menu = getmenu(currmenu, ev.xbutton.window);
				slice = getslice(menu, ev.xbutton.x, ev.xbutton.y);
				if (menu == NULL || slice == NULL)
					goto done;
				break;
			case KeyPress:
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
		}
	done:
		mapped = 0;
		unmapmenu(currmenu);
		ungrab();
		XFlush(dpy);
	} while (rflag);
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
	XDestroyWindow(dpy, menu->win);
	free(menu);
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
	struct Menu *rootmenu;

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
	getoptions(&argc, &argv);

	/* imlib2 stuff */
	imlib_set_cache_size(2048 * 1024);
	imlib_context_set_dither(1);
	imlib_context_set_display(dpy);
	imlib_context_set_visual(visual);
	imlib_context_set_colormap(colormap);

	/* initializers */
	initdc();
	initpie();

	/* if running in root mode, get button presses from root window */
	if (rflag)
		XGrabButton(dpy, button, 0, rootwin, False, ButtonPressMask, GrabModeSync, GrabModeSync, None, None);

	/* generate menus and set them up */
	rootmenu = parsestdin();
	if (rootmenu == NULL)
		errx(1, "no menu generated");
	setslices(rootmenu);

	/* run event loop */
	run(rootmenu);

	/* freeing stuff */
	cleanmenu(rootmenu);
	cleandc();
	XCloseDisplay(dpy);

	return 0;
}
