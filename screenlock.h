/* SLiM - Simple Login Manager
   Copyright (C) 1997, 1998 Per Liden
   Copyright (C) 2004-06 Simone Rota <sip@varlock.com>
   Copyright (C) 2004-06 Johannes Winkelmann <jw@tks6.net>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#ifndef _SCREENLOCK_H_
#define _SCREENLOCK_H_

#include <X11/Xft/Xft.h>

#define lock_string	"This X display is locked. Please supply the password."
#define font_name	"Verdana:bold:size=12"
#define MAXLEN_PASSWD 50
#define stars "**************************************************"

typedef struct ScreenInfo ScreenInfo;
struct ScreenInfo {
	Window window;
	GC gc;
	int width, height;
	unsigned long black, white;
};

class ScreenLocker {
public:
	ScreenLocker();
	~ScreenLocker();
	void Lock();

private:
	Display * dpy;
	XftFont * font;
	char * argv0;
	int screen_count;
	ScreenInfo * screens;
	ScreenInfo * getScreenForWindow(Window);
	int CheckPassword(char * p);
	void initScreens(void);
	void initScreen(int);
	
	void dispatch(XEvent *);
	void expose(XEvent *);
	void exposeScreen(ScreenInfo *);
	void keypress(XEvent *);
	void mappingnotify(XEvent *);
	void raiseLockWindows(void);

	char password[MAXLEN_PASSWD];
	int chars;
	int drawtitle;

};
#endif
