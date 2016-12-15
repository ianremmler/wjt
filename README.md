## wjt

wjt is a slider widget for the X Window System that allows the user to select
values within a range using the keyboard or mouse.  It presents a bar at the
top or bottom of the display with a slider area including legend labels, and
optionally, a prompt on the left.  When the user adjusts the slider position,
its value is printed to stdout.  wjt grabs keyboard and mouse input while
running.  wjt was inspired by, and its code is based on, dmenu.

### Dependencies

- Xlib
- FreeType
- Fontconfig
- Xinerama (optional)

### Configuration

Change the default values in config.h and rebuild to customize wjt.  Defaults
may be overridden by command line options.

### Installation

Edit config.mk to match your environment.  The default install location is
/usr/local.  To build and install wjt, run:

`> make clean install`

### Running wjt

Running wjt with no options will put a slider at the top of the display with
initial value 0 and a range of 0 to 100.  See the man page for details on
available options.
