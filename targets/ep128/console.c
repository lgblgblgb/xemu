/* Xep128: Minimalistic Enterprise-128 emulator with focus on "exotic" hardware
   Copyright (C)2016 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>
   http://xep128.lgb.hu/

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#include "xep128.h"
#include "console.h"
#include "emu_monitor.h"

#include <SDL.h>

#ifdef NO_CONSOLE
int console_is_open = 0;
void console_close_window ( void ) {
}
void console_close_window_on_exit ( void ) {
}
void console_open_window ( void ) {
}
void console_monitor_ready ( void ) {
}
#else


#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#else
#ifndef XEMU_HAS_READLINE
#error "We need libreadline for this target/platform, but XEMU_HAS_READLINE is not defined. Maybe libreadline cannot be detected?"
#endif
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define USE_MONITOR	1

int console_is_open = 0;
static int ok_for_monitor = 0;
static volatile int monitor_running = 0;
static SDL_Thread *mont = NULL;




/* Monitor thread waits for console input and enqueues the request.
   The thread is NOT executes the command itself! Even the answer
   is printed by the main thread already!
   Honestly, I was lazy, this may be also implemented in the main
   main thread, with select() based scheme / async I/O on UNIX, but I have
   no idea about Windows ... */
static int console_monitor_thread ( void *ptr )
{
	printf("Welcome to " WINDOW_TITLE " monitor. Use \"help\" for help" NL);
	while (monitor_running) {
		char *p;
#ifdef _WIN32
		char buffer[256];
		printf(WINDOW_TITLE "> ");
		p = fgets(buffer, sizeof buffer, stdin);
#else
		p = readline(WINDOW_TITLE "> ");
#endif
		if (p == NULL) {
			SDL_Delay(10);	// avoid flooding the CPU in case of I/O problem for fgets ...
		} else {
			// Queue the command!
			while (monitor_queue_command(p) && monitor_running)
				SDL_Delay(10);	// avoid flooding the CPU in case of not processed-yet command in the "queue" buffer
#ifndef _WIN32
			if (p[0])
				add_history(p);
			free(p);
#endif
			// Wait for command completed
			while (monitor_queue_used() && monitor_running)
				SDL_Delay(10);	// avoid flooding the CPU while waiting for command being processed and answered on the console
		}
	}
	printf("MONITOR: thread is about to exit" NL);
	return 0;
}



static void monitor_start ( void )
{
	if (!ok_for_monitor || !console_is_open || monitor_running || !USE_MONITOR)
		return;
	DEBUGPRINT("MONITOR: start" NL);
	monitor_running = 1;
	mont = SDL_CreateThread(console_monitor_thread, WINDOW_TITLE " monitor", NULL);
	if (mont == NULL)
		monitor_running = 0;
}



static int monitor_stop ( void )
{
	int ret;
	if (!monitor_running)
		return 0;
	DEBUGPRINT("MONITOR: stop" NL);
	monitor_running = 0;
	if (mont != NULL) {
		printf(NL NL "*** PRESS ENTER TO EXIT ***" NL);
		// Though Info window here is overkill, I am still interested why it causes a segfault when I've tried ...
		//INFO_WINDOW("Monitor runs on console. You must press ENTER there to continue");
		SDL_WaitThread(mont, &ret);
		mont = NULL;
		DEBUGPRINT("MONITOR: thread joined, status code is %d" NL, ret);
	}
	return 1;
}



void console_open_window ( void )
{
#ifdef _WIN32
	int hConHandle;
	HANDLE lStdHandle;
	CONSOLE_SCREEN_BUFFER_INFO coninfo;
	FILE *fp;
	if (console_is_open)
		return;
	console_is_open = 0;
	FreeConsole();
	if (!AllocConsole()) {
		ERROR_WINDOW("Cannot allocate windows console!");
		return;
	}
	SetConsoleTitle(WINDOW_TITLE " console");
	// set the screen buffer to be big enough to let us scroll text
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &coninfo);
	coninfo.dwSize.Y = 1024;
	//coninfo.dwSize.X = 100;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), coninfo.dwSize);
	// redirect unbuffered STDOUT to the console
	lStdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	hConHandle = _open_osfhandle((INT_PTR)lStdHandle, _O_TEXT);
	fp = _fdopen( hConHandle, "w" );
	*stdout = *fp;
	setvbuf( stdout, NULL, _IONBF, 0 );
	// redirect unbuffered STDIN to the console
	lStdHandle = GetStdHandle(STD_INPUT_HANDLE);
	hConHandle = _open_osfhandle((INT_PTR)lStdHandle, _O_TEXT);
	fp = _fdopen( hConHandle, "r" );
	*stdin = *fp;
	setvbuf( stdin, NULL, _IONBF, 0 );
	// redirect unbuffered STDERR to the console
	lStdHandle = GetStdHandle(STD_ERROR_HANDLE);
	hConHandle = _open_osfhandle((INT_PTR)lStdHandle, _O_TEXT);
	fp = _fdopen( hConHandle, "w" );
	*stderr = *fp;
	setvbuf( stderr, NULL, _IONBF, 0 );
	// make cout, wcout, cin, wcin, wcerr, cerr, wclog and clog point to console as well
	// ios::sync_with_stdio();
	// Attributes
	//SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
	SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
	SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
	DEBUGPRINT("WINDOWS: console is open" NL);
#endif
	console_is_open = 1;
	monitor_start();
}



void console_close_window ( void )
{
	if (!console_is_open)
		return;
	monitor_stop();
#ifdef _WIN32
	if (!FreeConsole())
		ERROR_WINDOW("Cannot release windows console!");
	else
		console_is_open = 0;
#else
	console_is_open = 0;
#endif
}



void console_close_window_on_exit ( void )
{
#ifdef _WIN32
	if (console_is_open && !monitor_stop())
		INFO_WINDOW("Click to close console window");
#else
	monitor_stop();
#endif
	console_close_window();
}



void console_monitor_ready ( void )
{
	ok_for_monitor = 1;
	monitor_start();
}
#endif
