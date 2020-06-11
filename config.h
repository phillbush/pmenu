/* font */
static const char *font = "monospace:size=9";    /* for regular items */

/* colors */
static const char *background_color = "#000000";
static const char *foreground_color = "#FFFFFF";
static const char *selbackground_color = "#3465a4";
static const char *selforeground_color = "#FFFFFF";
static const char *separator_color = "#555753";
static const char *border_color = "#3465a4";

/* sizes in pixels */
static int width_pixels = 130;  /* minimum width of a menu */
static int padding_pixels = 4;  /* padding around label in a item */
static int border_pixels = 1;   /* menu border */
static int separator_pixels = 1; /* line between items */
static unsigned diameter_pixels = 200;

/* sizes from 0 to 1 */
static double separatorbeg = 0.14;  /* beginning of the separator */
static double separatorend = 0.37;  /* end of the separator */
