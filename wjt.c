/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)

#define MAXVAL 1000000000
#define VBUFSIZE 12 /* enough to hold negation of above */
#define VERROR " must be a resonably sized integer"

/* enums */
enum { SchemePrompt, SchemeSlider, SchemeValue, SchemeLast }; /* color schemes */

static char *embed;
static int sw, sh;
static int winx;
static int sx = 0;
static int promptw = 0;
static int lrpad; /* sum of left and right padding */
static int mon = -1, screen;

static char minstr[VBUFSIZE];
static char maxstr[VBUFSIZE];
static char valstr[VBUFSIZE];
static int minw;
static int maxw;
static int valw;
static int valx;
static int valout;
static int val = MAXVAL + 1;

static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

#include "config.h"

static void
quit(int status)
{
	size_t i;

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
	exit(status);
}

static float
lerp(float a0, float a1, float b0, float b1, float x)
{
	return a0 + (a1 - a0) * (x - b0) / (b1 - b0);
}

static void
drawslider(void)
{
	drw_setscheme(drw, scheme[SchemeSlider]);
	drw_rect(drw, 0, 0, sw, sh, 1, 1);
	if (prompt && *prompt) {
		drw_setscheme(drw, scheme[SchemePrompt]);
		drw_text(drw, 0, 0, promptw, sh, lrpad / 2, prompt, 1, 0);
	}
	drw_setscheme(drw, scheme[SchemeValue]);
	drw_rect(drw, sx, 0, valx, sh, 1, 1);
	if (labelval)
		drw_text(drw, sx + (sw - sx) / 2 - valw / 2, 0, valw, sh, lrpad / 2, valstr, 0, 0);
	if (labelexts) {
		drw_setscheme(drw, scheme[SchemeSlider]);
		drw_text(drw, sx, 0, minw, sh, lrpad / 2, minstr, 0, 0);
		drw_text(drw, sw - maxw, 0, maxw, sh, lrpad / 2, maxstr, 0, 0);
	}
	drw_map(drw, win, 0, 0, sw, sh);
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

static void
grabinput(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
	int ownkbd = 0;
	int ownptr = 0;
	int i;

	if (embed)
		return;
	/* try to grab keyboard and pointer, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (!ownkbd)
			ownkbd = (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
			                        GrabModeAsync, CurrentTime) == GrabSuccess);
		if (!ownptr)
			ownptr = (XGrabPointer(dpy, DefaultRootWindow(dpy), True,
			                       ButtonPressMask | ButtonReleaseMask | Button1MotionMask,
			                       GrabModeAsync, GrabModeAsync, None, None,
			                       CurrentTime) == GrabSuccess);
		if (ownkbd && ownptr)
			return;
		nanosleep(&ts, NULL);
	}
	if (!ownkbd)
		die("cannot grab keyboard");
	die("cannot grab pointer");
}

static void
adjustval(int v)
{
	if (v < min)
		v = min;
	else if (v > max)
		v = max;
	valx = (int)lerp(0, sw - 1 - sx, min, max, v);
	if (v != val) {
		val = v;
		snprintf(valstr, VBUFSIZE, "%d", val);
		valw = TEXTW(valstr);
	}
}

static void
updateval(int v)
{
	adjustval(v);
	if (val != valout) {
		valout = val;
		puts(valstr);
		fflush(stdout);
	}
}

static void
xtoval(int x)
{
	float fv;
	int v;

	fv = lerp(min, max, sx, sw - 1, x);
	v = (int)fv;
	if (v > min && v < max) {
		v = (int)(fv / step + copysignf(0.5, fv)) * step;
	}
	updateval(v);
}

static void
printspecial(void)
{
	if (special && *special) {
		puts(special);
		fflush(stdout);
	}
}

static void
keypress(XKeyEvent *ev)
{
	char buf[32];
	KeySym ksym = NoSymbol;
	Status status;

	XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	if (status == XBufferOverflow)
		return;
	if (ev->state & ControlMask) {
		switch(ksym) {
		case XK_a:
			ksym = XK_Home;
			break;
		case XK_b:
			ksym = XK_Left;
			break;
		case XK_c:
		case XK_bracketleft:
			ksym = XK_Escape;
			break;
		case XK_e:
			ksym = XK_End;
			break;
		case XK_f:
			ksym = XK_Right;
			break;
		case XK_p:
			ksym = XK_Down;
			break;
		case XK_n:
			ksym = XK_Up;
			break;
		case XK_j:
		case XK_m:
			ksym = XK_Return;
			break;
		default:
			return;
		}
	}

	switch(ksym) {
	case XK_space:
		printspecial();
		break;
	case XK_KP_Enter:
	case XK_Return:
		quit(0);
	case XK_Escape:
		quit(1);
	case XK_h:
	case XK_minus:
	case XK_Left:
		updateval(val - step);
		break;
	case XK_l:
	case XK_plus:
	case XK_equal:
	case XK_Right:
		updateval(val + step);
		break;
	case XK_j:
	case XK_Down:
	case XK_Page_Down:
		updateval(val - jump);
		break;
	case XK_k:
	case XK_Up:
	case XK_Page_Up:
		updateval(val + jump);
		break;
	case XK_g:
	case XK_Home:
		updateval(min);
		break;
	case XK_G:
	case XK_End:
		updateval(max);
		break;
	/* map number row to deciles */
	case XK_grave: updateval(min); break;
	case XK_1: updateval(min + 1 * (max - min) / 10); break;
	case XK_2: updateval(min + 2 * (max - min) / 10); break;
	case XK_3: updateval(min + 3 * (max - min) / 10); break;
	case XK_4: updateval(min + 4 * (max - min) / 10); break;
	case XK_5: updateval(min + 5 * (max - min) / 10); break;
	case XK_6: updateval(min + 6 * (max - min) / 10); break;
	case XK_7: updateval(min + 7 * (max - min) / 10); break;
	case XK_8: updateval(min + 8 * (max - min) / 10); break;
	case XK_9: updateval(min + 9 * (max - min) / 10); break;
	case XK_0: updateval(max); break;
	default:
		return;
	}
	drawslider();
}

static void
buttonpress(XButtonPressedEvent *ev)
{
	switch (ev->button) {
	case Button1:
		xtoval(ev->x_root - winx);
		break;
	case Button4:
		updateval(val + (ev->state & ControlMask ? jump : step));
		break;
	case Button5:
		updateval(val - (ev->state & ControlMask ? jump : step));
		break;
	default:
		return;
	}
	drawslider();
}

static void
buttonrelease(XButtonReleasedEvent *ev)
{
	switch (ev->button) {
	case Button1:
		xtoval(ev->x_root - winx);
		break;
	case Button2:
		printspecial();
		return;
	case Button3:
		quit(0);
	default:
		return;
	}
	drawslider();
}

static void
buttonmove(XMotionEvent *ev)
{
	xtoval(ev->x_root - winx);
	drawslider();
}

static void
run(void)
{
	XEvent ev;

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			quit(1);
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, sw, sh);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case ButtonPress:
			buttonpress(&ev.xbutton);
			break;
		case ButtonRelease:
			buttonrelease(&ev.xbutton);
			break;
		case MotionNotify:
			buttonmove(&ev.xmotion);
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
setup(void)
{
	int x, y, i = 0;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"wjt", "wjt"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, j, di, n, area = 0;
#endif

	/* init appearance */
	scheme[SchemeSlider] = drw_scm_create(drw, colors[SchemeSlider], 2);
	scheme[SchemePrompt] = drw_scm_create(drw, colors[SchemePrompt], 2);
	scheme[SchemeValue] = drw_scm_create(drw, colors[SchemeValue], 2);

	/* calculate slider geometry */
	sh = drw->fonts->h + 2;
#ifdef XINERAMA
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]))
					break;

		x = info[i].x_org;
		y = info[i].y_org + (topbar ? 0 : info[i].height - sh);
		sw = info[i].width;
		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);
		x = 0;
		y = topbar ? 0 : wa.height - sh;
		sw = wa.width;
	}
	winx = x;

	if (prompt && *prompt) {
		promptw = TEXTW(prompt);
		sx = promptw + lrpad / 4;
	}

	/* create slider window */
	swa.override_redirect = True;
	swa.background_pixel = scheme[SchemeSlider][ColBg].pixel;
	swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
	                 Button1MotionMask | VisibilityChangeMask;
	win = XCreateWindow(dpy, parentwin, x, y, sw, sh, 0,
	                    CopyFromParent, CopyFromParent, CopyFromParent,
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
	XSetClassHint(dpy, win, &ch);

	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	XMapRaised(dpy, win);
	if (embed) {
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, sw, sh);

	valout = initval;
	adjustval(initval);
	snprintf(minstr, VBUFSIZE, "%d", min);
	snprintf(maxstr, VBUFSIZE, "%d", max);
	minw = TEXTW(minstr);
	maxw = TEXTW(maxstr);

	drawslider();
}

static void
usage(void)
{
	fputs("usage: wjt [-v] [-b] [-lv] [-le] [-m monnum] [-w winid] [-p prompt]\n"
	      "           [-f font] [-pb color] [-pf color] [-sb color] [-sf color]\n"
	      "           [-vb color] [-vf color] [-l lower] [-u upper] [-s step]\n"
	      "           [-j jump] [-x value] [-z special]\n", stderr);
	exit(1);
}

static int
valarg(char *arg, int *ok)
{
	long x;
	char *p;

	x = strtol(arg, &p, 0);
	if (ok) {
		*ok = (p != arg && labs(x) <= MAXVAL);
	}
	return x;
}

int
main(int argc, char *argv[])
{
	XWindowAttributes wa;
	int i;
	int ok;

	for (i = 1; i < argc; i++) {
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {
			puts("wjt-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) /* invert bar vertical screen location */
			topbar = !topbar;
		else if (!strcmp(argv[i], "-lv")) /* invert whether to display value label */
			labelval = !labelval;
		else if (!strcmp(argv[i], "-le")) /* invert whether to display extent labels */
			labelexts = !labelexts;
		else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-m")) /* monitor number */
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-w")) /* embedding window id */
			embed = argv[++i];
		else if (!strcmp(argv[i], "-p")) /* adds prompt to left of slider */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-f")) /* font or font set */
			fonts[0] = argv[++i];
		else if (!strcmp(argv[i], "-pb")) /* prompt background color */
			colors[SchemePrompt][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-pf")) /* prompt foreground color */
			colors[SchemePrompt][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb")) /* slider background color */
			colors[SchemeSlider][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf")) /* slider foreground color */
			colors[SchemeSlider][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-vb")) /* value background color */
			colors[SchemeValue][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-vf")) /* value foreground color */
			colors[SchemeValue][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-l")) { /* lower bound */
			min = valarg(argv[++i], &ok);
			if (!ok)
				die("lower bound"VERROR);
		} else if (!strcmp(argv[i], "-u")) { /* upper bound */
			max = valarg(argv[++i], &ok);
			if (!ok)
				die("upper bound"VERROR);
		} else if (!strcmp(argv[i], "-j")) { /* jump */
			jump = valarg(argv[++i], &ok);
			if (!ok)
				die("jump"VERROR);
		} else if (!strcmp(argv[i], "-s")) { /* step */
			step = valarg(argv[++i], &ok);
			if (!ok)
				die("step"VERROR);
		} else if (!strcmp(argv[i], "-x")) { /* initial value */
			initval = valarg(argv[++i], &ok);
			if (!ok)
				die("initial value"VERROR);
		} else if (!strcmp(argv[i], "-z")) /* special text */
			special = argv[++i];
		else
			usage();
	}

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);
	drw = drw_create(dpy, screen, root, wa.width, wa.height);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

	if (max <= min)
		die("upper bound must be greater than lower bound");
	if (step < 1)
		die("step must be positive");
	if (jump <= step)
		die("jump must not be less than step");

	grabinput();
	setup();
	run();

	return 1; /* unreachable */
}
