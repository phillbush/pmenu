<p align="center">
  <img src="https://user-images.githubusercontent.com/63266536/89110619-10034f00-d43c-11ea-92a6-275eb50ef881.png", title="demo"/>
</p>

# πmenu

πmenu is a pie menu utility for X.
πmenu receives a menu specification in stdin, shows a menu for the user
to select one of the options, and outputs the option selected to stdout.

πmenu comes with the following features:

* πmenu reads something in and prints something out, the UNIX way.
* Submenus (some pie-menu slices can spawn another menu).
* Icons (pie-menu slices can contain icon image).
* X resources support (you don't need to recompile πmenu for configuring it).

Check out my other project, [xclickroot](https://github.com/phillbush/xclickroot) for an application that can
spawn πmenu by right clicking on the root window (i.e. on the desktop).

## Files

The files are:
* ./README:     This file.
* ./Makefile:   The makefile.
* ./config.h:   The hardcoded default configuration for πmenu.
* ./config.mk:  The settings for the makefile.
* ./pmenu.1:    The manual file (man page) for πmenu.
* ./pmenu.c:    The source code of πmenu.
* ./pmenu.sh:   A sample script illustrating how to use πmenu.


## Installation

First, edit ./config.mk to match your local setup.

In order to build πmenu you need the Imlib2, Xlib and Xft header files.
The default configuration for πmenu is specified in the file config.h,
you can edit it, but most configuration can be changed at runtime via
X resources.  Enter the following command to build πmenu.  This command
creates the binary file ./pmenu.

	make

By default, πmenu is installed into the /usr/local prefix.  Enter the
following command to install πmenu (if necessary as root).  This command
installs the binary file ./pmenu into the ${PREFIX}/bin/ directory, and
the manual file ./pmenu.1 into ${MANPREFIX}/man1/ directory.

	make install


## Running πmenu

πmenu receives as input a menu specification where each line is a menu
entry.  Each line can be indented with tabs to represent nested menus.
Each line is made out of a label and a command separated by any number
of tabs.

See the script ./pmenu.sh for an example of how to use πmenu to draw a
simple pie menu.

Read the manual for more information on running πmenu.
