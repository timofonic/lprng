/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-1997, Patrick Powell, San Diego, CA
 *     papowell@sdsu.edu
 * See LICENSE for conditions of use.
 *
 ***************************************************************************
 * MODULE: termclear.c
 * PURPOSE: clear a terminal screen
 **************************************************************************/

static char *const _id =
"$Id: termclear.c,v 3.1 1996/12/28 21:40:22 papowell Exp $";

#include "lp.h"
#include "termclear.h"
/**** ENDINCLUDE ****/

#if defined(IS_LINUX)
# include <termios.h>
#endif

/* terminfo has a termcap emulation */
#ifdef HAVE_CURSES_H
# include <curses.h>
#endif

#if defined(HAVE_TERMCAP_H) && !defined(__FreeBSD__) && !defined(__linux__)
# include <termcap.h>
#endif

/* solaris gets confused when you include term.h?
 * this is totally bizarre... so you need to worry about versions now?
 */
#if defined(HAVE_TERM_H) && (!defined(SOLARIS) || SOLARIS > 250)
# include <term.h>
#endif

#if defined(HAVE_TERMCAP_H) || defined(HAVE_CURSES_H) || defined(HAVE_TERM_H)
static int tinit_done;
static char *TE_string;
static char *CL_string;

/*
 * Michael Haardt: stop users from overwriting buffer.
 * this won't stop them, but it'll make it harder.
 */
static char bp[2048];
static char xp[2048];
static char *area = xp;

/*
 * Patrick Powell Fri Aug 11 23:04:11 PDT 1995
 *  The following obnoxious piece of code is why
 *  portablilty is sooooo hard to do.
 * 
 * SUNOS: 4.1.1-4.1.3 - termcap(1) man page
 *      tputs(cp, affcnt, outc)
 *      register char *cp;
 *      int affcnt;
 *      int (*outc)();
 * 
 * BSD-4.4 + - man page
 *      tputs(register char *cp, int affcnt, int (*outc)());
 * 
 * BSD-4.4 /usr/include/curses.h
 * 	int  tputs (char *, int, void (*)(int));
 * 
 * Solaris 2.4 term.h
 * extern  int  tputs(char *, int, int (*)(char))
 *
 *  Depending on you stdio package, putchar can return (void)
 *  or be a macro, returning wacko values.  We take the easy road,
 *  and return void(), be compatible with definitions.
 */

#define PPUTS_RETTYPE int
#define TPUTS_RETTYPE int
#define PPUTS_VALTYPE int
#define PPUTS_RETVAL(X) (X)
#if defined(IS_BSDI)
#undef PPUTS_RETTYPE
#undef PPUTS_RETVAL
#define PPUTS_RETTYPE void
#define PPUTS_RETVAL(X)
#endif
#if defined(SOLARIS)
#undef PPUTS_VALTYPE
#define PPUTS_VALTYPE char
#endif

#if !defined(HAVE_TGETSTR_DEF)
char *tgetstr(char *id, char **area);
#endif

static PPUTS_RETTYPE pputs(PPUTS_VALTYPE c)
{
	putchar(c); /* this does the work */
	return PPUTS_RETVAL(c);
}

void Term_clear(void)
{
    char *sp,*ti;

	DEBUG0("Term_clear");
    if (tinit_done == 0) 
    {
        tinit_done = 1;
        if (isatty (0) && (sp = getenv ("TERM")) != (char*)0) 
        {
            if (tgetent (bp, sp) > 0) 
            {
                if ((ti=tgetstr ("ti", &area))!=(char*)0) tputs(ti,0,pputs);
                TE_string = tgetstr ("te", &area);
                CL_string = tgetstr ("cl", &area);
            }
        }
    }
    if (CL_string) tputs (CL_string, 0, pputs);
    fflush(stdout);
}

void Term_finish(void)
{
    if (TE_string) tputs (TE_string, 0, pputs);
    fflush(stdout);
}

#else

void Term_clear(void)
{
	DEBUG0("Term_clear: dummy");
}
void Term_finish(void) {;}

#endif