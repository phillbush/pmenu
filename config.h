static struct Config config = {
	/* font, separate different fonts with comma */
	.font = "monospace:size=9,DejaVuSansMono:size=9",

	/* colors */
	.background_color = "#000000",
	.foreground_color = "#FFFFFF",
	.selbackground_color = "#3465a4",
	.selforeground_color = "#FFFFFF",
	.separator_color = "#555753",
	.border_color = "#555753",

	/* sizes in pixels */
	.border_pixels = 2,     /* menu border */
	.separator_pixels = 1,  /* line between items */
	.diameter_pixels = 200,

	/* the values below cannot be set via X resources */

	/* sizes between 0 and 1 */
	.separatorbeg = 0.14,  /* beginning of the separator */
	.separatorend = 0.37,  /* end of the separator */

	/*
	 * The variables below cannot be set by X resources.
	 * Their values must be less than .height_pixels.
	 */

	/* geometry of the right-pointing isoceles triangle for submenus */
	.triangle_width = 3,
	.triangle_height = 7,
	.triangle_distance = 6  /* distance from the border of the menu */
};
