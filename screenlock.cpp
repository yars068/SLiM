/* SLiM - Simple Login Manager
   Copyright (C) 1997, 1998 Per Liden
   Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
   Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   Screen locking code adapted from 'lock' by Elliott Hughes,
   distributed under the terms of the GNU GPL v2 or later.
*/

#include	<string.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<sys/types.h>
#include	<pwd.h>
#ifdef HAVE_SHADOW
  #include <shadow.h>
  #include <fcntl.h>
  #include <sys/vt.h>
#endif

#include	<stdio.h>
#include	<signal.h>

#include	<X11/X.h>
#include	<X11/Xlib.h>
#include	<X11/Xutil.h>
#include	<X11/keysym.h>

#include "screenlock.h"

ScreenLocker::ScreenLocker() {
	chars = 0;
	drawtitle = 1;
}


void ScreenLocker::Lock() {
	XEvent ev;
	
	/* Open a connection to the X server. */
	dpy = XOpenDisplay("");
	int scr = DefaultScreen(dpy);
	if (dpy == 0)
		exit(1);
	
	/* Set up signal handlers. */
	signal(SIGTERM, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	
	/* Get font. */
 	font = XftFontOpenName(dpy, scr,font_name);
	initScreens();
	
	/* Grab the keyboard. */
	XGrabKeyboard(dpy, screens[0].window, False,
		GrabModeAsync, GrabModeAsync, CurrentTime);
#ifdef todovtlock
	{
		int fd = open("/dev/console", O_RDWR);
		if (fd != -1) {
			ioctl(fd, VT_LOCKSWITCH, 0);
//			close(fd);
		}
		fprintf(stderr, "/dev/console = %i\n", fd);
	}
#endif
	
	/* Make sure all our communication to the server got through. */
	XSync(dpy, False);
	
	/* The main event loop. */
	for (;;) {
		XNextEvent(dpy, &ev);
		dispatch(&ev);
	}
}

int ScreenLocker::CheckPassword(char * p) {
    char *encrypted, *correct;
	struct passwd * pw;
	char * logname;
	
	if (p == 0)
		return 0;

	logname = getenv("LOGNAME");
	if (logname == 0)
		logname = getenv("USER");
	if (logname == 0)
		logname = getlogin();
	pw = getpwnam(logname);
    endpwent();

	if (pw == 0 || pw->pw_passwd == 0)
		return 0;
#ifdef HAVE_SHADOW
    struct spwd *sp = getspnam(logname);    
    endspent();
    if(sp)
		correct = sp->sp_pwdp;
    else
#endif
	correct = pw->pw_passwd;
	encrypted = crypt(p, correct);
	memset(p, 0, strlen (p));
	if (encrypted == 0)
		return 0;
	return 1;
	/* return !strcmp(correct, encrypted); */
}

void
ScreenLocker::initScreens(void) {
	int screen;
	
	/* Find out how many screens we've got, and allocate space for their info. */
	screen_count = ScreenCount(dpy);
	screens = (ScreenInfo *) malloc(sizeof(ScreenInfo) * screen_count);
	
	/* Go through the screens one-by-one, initialising them. */
	for (screen = 0; screen < screen_count; screen++)
		initScreen(screen);
}

void
ScreenLocker::initScreen(int screen) {
	XGCValues gv;
	XSetWindowAttributes attr;
	Cursor no_cursor;
	XColor colour;
	
	/* Find the screen's dimensions. */
	screens[screen].width = DisplayWidth(dpy, screen);
	screens[screen].height = DisplayHeight(dpy, screen);

	/* Get the pixel values of the only two colours we use. */
	screens[screen].black = BlackPixel(dpy, screen);
	screens[screen].white = WhitePixel(dpy, screen);

	/* Create the locking window. */
	attr.override_redirect = True;
	attr.background_pixel = screens[screen].black;
	attr.border_pixel = screens[screen].black;
	attr.event_mask = ExposureMask | VisibilityChangeMask |
		StructureNotifyMask;
	attr.do_not_propagate_mask = ButtonPressMask | ButtonReleaseMask;
	screens[screen].window = XCreateWindow(dpy,
		RootWindow(dpy, screen), 0, 0,
		screens[screen].width, screens[screen].height, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask |
		CWDontPropagate|GCGraphicsExposures, &attr);

	XStoreName(dpy, screens[screen].window, "lock");

	/* Create GC. */
	gv.foreground = screens[screen].white;
	gv.background = screens[screen].black;
	gv.graphics_exposures = 0;
	/* gv.font = font->fid; */
	screens[screen].gc = XCreateGC(dpy, screens[screen].window,
		GCForeground | GCBackground, &gv);

	/* Hide the cursor. */
	/*
	no_cursor = XCreateGlyphCursor(dpy, font->fid, font->fid, ' ', ' ',
		&colour, &colour);
	XDefineCursor(dpy, screens[screen].window, no_cursor);*/

	/* Bring up the lock window. */
	XMapRaised(dpy, screens[screen].window);
}

ScreenInfo *
ScreenLocker::getScreenForWindow(Window w) {
	int screen;
	for (screen = 0; screen < screen_count; screen++)
		if (screens[screen].window == w)
			return &screens[screen];
	return 0;
}

void
ScreenLocker::dispatch(XEvent *ev) {
    switch(ev->type) {
    case Expose:
        expose(ev);
		return;
        break;
    case KeyPress:
        keypress(ev);
		return;
        break;
    case KeyRelease:
		return;
        break;
    case MappingNotify:
        mappingnotify(ev);
		return;
        break;
	}
	raiseLockWindows();
}

void
ScreenLocker::expose(XEvent * ev) {
	/* Only handle the last in a group of Expose events. */
	if (ev->xexpose.count != 0)
		return;
	
	exposeScreen(getScreenForWindow(ev->xexpose.window));
}

void
ScreenLocker::exposeScreen(ScreenInfo * screen) {
	int len;
	int width, height;
	int scr;
	int x, y;
    XGlyphInfo extents;
	XftColor color;
	if (screen != &screens[0])
		return;
	
	scr = DefaultScreen(dpy);
    Visual* visual = DefaultVisual(dpy, scr);
    Colormap colormap = DefaultColormap(dpy, scr);
    XftColorAllocName(dpy, visual, colormap, "#FFFFFF", &color);	
    XftDraw *draw = XftDrawCreate(dpy, screen->window, visual, colormap);


	/* Do the redraw. */
	if (drawtitle ==1) {
		len = strlen(lock_string);
    	XftTextExtents8(dpy, font, (XftChar8*)lock_string,
                    len, &extents);
		width = extents.width;
		height = extents.height;
	 	x = (screen->width - width)/2;
		y = screen->height/4;
	  	XSetForeground(dpy, screen->gc, screen->black); 
		XftDrawString8(draw, &color, font, x, y, (XftChar8*)lock_string, len);
 		drawtitle = 0;
	}
    XftTextExtents8(dpy, font, (XftChar8*)stars,
                    chars, &extents);

	width = extents.width;
	height = extents.height;
 	x = (screen->width - width)/2;
	y = screen->height/4;
	XSetForeground(dpy, screen->gc, screen->black); 
 	XFillRectangle(dpy, screen->window, screen->gc,
		0, screen->height/2 - 100, screen->width, 100);
 	XftDrawString8(draw, &color, font, (screen->width - width)/2, screen->height/2, (XftChar8*)stars, chars);
	XftDrawDestroy (draw);

 }

void
ScreenLocker::keypress(XEvent * ev) {
	char buffer[1];
	KeySym keysym;
	int c;
	
	c = XLookupString((XKeyEvent *) ev, buffer, sizeof buffer, &keysym, 0);

	if (keysym == XK_Return) {
		password[chars] = 0;
		if (CheckPassword(password)) {
			XUngrabKeyboard(dpy, CurrentTime);
#ifdef todovtlock
			{
				int fd = open("/dev/console", O_RDWR);
				if (fd != -1)
					ioctl(fd, VT_UNLOCKSWITCH, 0);
			}
#endif
			exit(EXIT_SUCCESS);
		} else {
			XBell(dpy, 100);
			chars = 0;
		}
	} else if (keysym == XK_BackSpace) {
		if (chars > 0)
			chars--;
	} else if (keysym == XK_Escape)
		chars = 0;
	else if (isprint(*buffer) && chars + 1 <= (MAXLEN_PASSWD -1))
		password[chars++] = *buffer;
	
	exposeScreen(&screens[0]);
}

void
ScreenLocker::mappingnotify(XEvent * ev) {
	XRefreshKeyboardMapping((XMappingEvent *) ev);
}

void
ScreenLocker::raiseLockWindows(void) {
	int screen;
	for (screen = 0; screen < screen_count; screen++)
		XRaiseWindow(dpy, screens[screen].window);
}
