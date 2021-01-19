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

	int drawn;              /* whether the pixmap have been drawn */
	Drawable pixmap;        /* pixmap containing the pie menu with the slice selected */
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

	int drawn;              /* whether the pixmap have been drawn */
	Drawable pixmap;        /* pixmap to draw the menu on */
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
