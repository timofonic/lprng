/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-2001, Patrick Powell, San Diego, CA
 *     papowell@lprng.com
 * See LICENSE for conditions of use.
 *
 ***************************************************************************/

 static char *const _id =
"$Id: a.c,v 1.1 2002/02/25 17:43:10 papowell Exp $";


#include "lp.h"
#include "errormsg.h"
#include "errorcodes.h"
#include "child.h"
#include "getopt.h"
#include "getqueue.h"
/**** ENDINCLUDE ****/

#if defined(HAVE_SYSLOG_H)
# include <syslog.h>
#endif

/****************************************************************************/


 static void use_syslog( int kind, char *msg );

/****************************************************************************
 * char *Errormsg( int err )
 *  returns a printable form of the
 *  errormessage corresponding to the valie of err.
 *  This is the poor man's version of sperror(), not available on all systems
 *  Patrick Powell Tue Apr 11 08:05:05 PDT 1995
 ****************************************************************************/
/****************************************************************************/

#if !defined(HAVE_STRERROR)
# undef  num_errors
# if defined(HAVE_SYS_ERRLIST)
#  if !defined(HAVE_DECL_SYS_ERRLIST)
     extern const char *const sys_errlist[];
#  endif
#  if defined(HAVE_SYS_NERR)
#   if !defined(HAVE_DECL_SYS_NERR)
      extern int sys_nerr;
#   endif
#   define num_errors    (sys_nerr)
#  endif
# endif
# if !defined(num_errors)
#   define num_errors   (-1)            /* always use "errno=%d" */
# endif
#endif

const char * Errormsg ( int err )
{
    const char *cp;

	if( err == 0 ){
		cp = "No Error";
	} else {
#if defined(HAVE_STRERROR)
		cp = strerror(err);
#else
# if defined(HAVE_SYS_ERRLIST)
		if (err >= 0 && err < num_errors) {
			cp = sys_errlist[err];
		} else
# endif
		{
			static char msgbuf[32];     /* holds "errno=%d". */
			(void) SNPRINTF (msgbuf, sizeof(msgbuf)) "errno=%d", err);
			cp = msgbuf;
		}
#endif
	}
    return (cp);
}

 struct msgkind {
    int var;
    char *str;
};

 static struct msgkind msg_name[] = {
    {LOG_CRIT, " (CRIT)"},
    {LOG_ERR, " (ERR)"},
    {LOG_WARNING, " (WARN)"},
    {LOG_NOTICE, " (NOTICE)"},
    {LOG_INFO, " (INFO)"},
    {LOG_DEBUG, ""},
    {0}
};

 static char * putlogmsg(int kind)
{
    int i;
    static char b[32];

	b[0] = 0;
	if( kind < 0 ) return(b);
    for (i = 0; msg_name[i].str; ++i) {
		if ( msg_name[i].var == kind) {
			return (msg_name[i].str);
		}
    }
	/* SAFE USE of SNPRINTF */
    (void) SNPRINTF (b, sizeof(b)) "<BAD LOG FLAG %d>", kind);
    return (b);
}


 static void use_syslog(int kind, char *msg)
{
    /* testing mode indicates that this is not being used
     * in the "real world", so don't get noisy. */

	/* we get rid of first set of problems - stupid characters */
	char buffer[SMALLBUFFER];
	SNPRINTF(buffer,sizeof(buffer)-1)"%s",msg);
	msg = buffer;

#ifndef HAVE_SYSLOG_H
	/* Note: some people would open "/dev/console", as default
		Bad programmer, BAD!  You should parameterize this
		and set it up as a default value.  This greatly aids
		in testing for portability.
		Patrick Powell Tue Apr 11 08:07:47 PDT 1995
	 */
	int Syslog_fd;
	if	(Syslog_fd = open( Syslog_device_DYN,
				O_WRONLY|O_APPEND|O_NOCTTY, Spool_file_perms_DYN )) > 0 ) ){
		int len;

		Max_open( Syslog_fd);
		len = strlen(msg);
		msg[len] = '\n';
		msg[len+1] = 0;
		Write_fd_len( Syslog_fd, msg, len+1 );
		close( Syslog_fd );
		msg[len] = 0;
    }

#else                           /* HAVE_SYSLOG_H */
# ifdef HAVE_OPENLOG
	/* use the openlog facility */
	openlog(Name, LOG_PID | LOG_NOWAIT, SYSLOG_FACILITY );
	syslog(kind, "%s", msg);
	closelog();

# else
    (void) syslog(SYSLOG_FACILITY | kind, "%s", msg);
# endif							/* HAVE_OPENLOG */
#endif                          /* HAVE_SYSLOG_H */
}


 static void log_backend (int kind, char *log_buf)
{
    char stamp_buf[2*SMALLBUFFER];
	int n;
	char *s;
	/* plp_block_mask omask; / **/
	int err = errno;

    /* plp_block_all_signals (&omask); / **/
	/* put the error message into the status file as well */
    /*
     * Now build up the state info: timestamp, hostname, argv[0], pid, etc.
     * Don't worry about efficiency here -- it shouldn't happen very often
     * (unless we're debugging).
     */

	stamp_buf[0] = 0;
	/* remove trailing \n */
	if( (s = strrchr(log_buf, '\n')) && cval(s+1) == 0 ){
		*s = 0;
	}


	if( Is_server || DEBUGL1 ){
		/* log stuff */
		if( (LOG_EMERG < LOG_INFO && kind <= LOG_INFO )
			|| ( LOG_EMERG > LOG_INFO && kind >= LOG_INFO )){
			setstatus( 0, "%s", log_buf );
			use_syslog(kind, log_buf);
		}
		n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
		(void) SNPRINTF( s, n) "%s", Time_str(0,0) );
		if (ShortHost_FQDN ) {
			n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
			(void) SNPRINTF( s, n) " %s", ShortHost_FQDN );
		}
		if(Debug || DbgFlag){
			n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
			(void) SNPRINTF(s, n) " [%d]", getpid());
			n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
			if(Name) (void) SNPRINTF(s, n) " %s", Name);
			n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
			(void) SNPRINTF(s, n) " %s", putlogmsg(kind) );
		}
		n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
		(void) SNPRINTF(s, n) " %s", log_buf );
	} else {
		(void) SNPRINTF(stamp_buf, sizeof(stamp_buf)) "%s", log_buf );
	}

	if( strlen(stamp_buf) > sizeof(stamp_buf) - 8 ){
		stamp_buf[sizeof(stamp_buf)-8] = 0;
		strcpy(stamp_buf+strlen(stamp_buf),"...");
	}
	n = strlen(stamp_buf); s = stamp_buf+n; n = sizeof(stamp_buf)-n;
	(void) SNPRINTF(s, n) "\n" );


    /* use writes here: on HP/UX, using f p rintf really screws up. */
	/* if stdout or stderr is dead, you have big problems! */

	Write_fd_str( 2, stamp_buf );

    /* plp_unblock_all_signals ( &omask ); / **/
	errno = err;
}

/*****************************************************
 * Put the printer name at the start of the log buffer
 *****************************************************/
 
 static void prefix_printer( char *log_buf, int len )
{
	log_buf[0] = 0;
    if( Printer_DYN ){
		SNPRINTF( log_buf, len-4) "%s: ", Printer_DYN );
	}
}

/* VARARGS2 */
#ifdef HAVE_STDARGS
void logmsg(int kind, char *msg,...)
#else
void logmsg(va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    int kind;
    char *msg;
#endif
	int n;
	char *s;
	int err = errno;
	static int in_log;
	char log_buf[SMALLBUFFER];
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (kind, int);
    VA_SHIFT (msg, char *);

	if( in_log == 0 ){
		++in_log;
		prefix_printer(log_buf, sizeof(log_buf));
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n-4;
		(void) VSNPRINTF(s, n) msg, ap);
		log_backend (kind,log_buf);
		in_log = 0;
	}
    VA_END;
	errno = err;
}

/* VARARGS2 */
#ifdef HAVE_STDARGS
void fatal (int kind, char *msg,...)
#else
void fatal (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    int kind;
    char *msg;
#endif
	int n;
	char *s;
	char log_buf[SMALLBUFFER];
	static int in_log;
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (kind, int);
    VA_SHIFT (msg, char *);
	
	if( in_log == 0 ){
		++in_log;
		prefix_printer(log_buf, sizeof(log_buf));
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n-4;
		(void) VSNPRINTF(s, n) msg, ap);
		log_backend (kind, log_buf);
		in_log = 0;
	}

    VA_END;
    cleanup(0);
}

/* VARARGS2 */
#ifdef HAVE_STDARGS
void logerr (int kind, char *msg,...)
#else
void logerr (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    int kind;
    char *msg;
#endif
	int err = errno;
	char log_buf[SMALLBUFFER];
	static int in_log;
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (kind, int);
    VA_SHIFT (msg, char *);

	if( in_log == 0 ){
		int n;
		char *s;
		++in_log;
		prefix_printer(log_buf, sizeof(log_buf)-4);
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n-4;
		(void) VSNPRINTF(s, n) msg, ap);
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n-4;
		if( err ) (void) SNPRINTF (s, n) " - %s", Errormsg (err));
		log_backend (kind, log_buf);
		in_log = 0;
	}
    VA_END;
    errno = err;
}

/* VARARGS2 */
#ifdef HAVE_STDARGS
void logerr_die (int kind, char *msg,...)
#else
void logerr_die (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    int kind;
    char *msg;
#endif
	int err = errno;
	char log_buf[SMALLBUFFER];
	static int in_log;
    VA_LOCAL_DECL

    VA_START (msg);
    VA_SHIFT (kind, int);
    VA_SHIFT (msg, char *);

	if( in_log == 0 ){
		int n;
		char *s;
		++in_log;
		prefix_printer(log_buf, sizeof(log_buf));
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n;
		(void) VSNPRINTF (s, n) msg, ap);
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n;
		if( err ) (void) SNPRINTF (s, n) " - %s", Errormsg (err));
		log_backend (kind, log_buf);
		in_log = 0;
	}
    cleanup(0);
    VA_END;
}

/***************************************************************************
 * Diemsg( char *m1, *m2, ...)
 * print error message to stderr, and die
 ***************************************************************************/

/* VARARGS1 */
#ifdef HAVE_STDARGS
void Diemsg (char *msg,...)
#else
void Diemsg (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    char *msg;
#endif
	char buffer[SMALLBUFFER];
	char *s;
	int n;
	static int in_log;
    VA_LOCAL_DECL

    VA_START (msg);
    VA_SHIFT (msg, char *);
	if( in_log == 0 ){
		++in_log;
		buffer[0] = 0;
		n = strlen(buffer); s = buffer + n; n = sizeof(buffer) - n;
		(void) SNPRINTF(s, n) "Fatal error - ");

		n = strlen(buffer); s = buffer + n; n = sizeof(buffer) - n;
		(void) VSNPRINTF (s, n) msg, ap);
		n = strlen(buffer); s = buffer + n; n = sizeof(buffer) - n;
		(void) SNPRINTF (s, n) "\n" );

		/* ignore error, we are dying anyways */
		Write_fd_str( 2, buffer );
		in_log = 0;
	}

    cleanup(0);
    VA_END;
}

/***************************************************************************
 * Warnmsg( char *m1, *m2, ...)
 * print warning message to stderr
 ***************************************************************************/

/* VARARGS1 */
#ifdef HAVE_STDARGS
void Warnmsg (char *msg,...)
#else
void Warnmsg (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    char *msg;
#endif
	char buffer[SMALLBUFFER];
	char *s = buffer;
	int n;
	int err = errno;
	static int in_log;
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (msg, char *);

	if( in_log ) return;
	++in_log;
	buffer[0] = 0;
	n = strlen(buffer); s = buffer+n; n = sizeof(buffer)-n;
    (void) SNPRINTF(s, n) "Warning - ");
	n = strlen(buffer); s = buffer+n; n = sizeof(buffer)-n;
    (void) VSNPRINTF (s, n) msg, ap);
	n = strlen(buffer); s = buffer+n; n = sizeof(buffer)-n;
    (void) SNPRINTF(s, n) "\n");

	Write_fd_str( 2, buffer );
	in_log = 0;
	errno = err;
    VA_END;
}


/***************************************************************************
 * Message( char *m1, *m2, ...)
 * print warning message to stderr
 ***************************************************************************/

/* VARARGS1 */
#ifdef HAVE_STDARGS
void Message (char *msg,...)
#else
void Message (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    char *msg;
#endif
	char buffer[SMALLBUFFER];
	char *s = buffer;
	int n;
	int err = errno;
	static int in_log;
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (msg, char *);

	if( in_log ) return;
	++in_log;
	buffer[0] = 0;
	n = strlen(buffer); s = buffer+n; n = sizeof(buffer)-n;
    (void) VSNPRINTF (s, n) msg, ap);
	n = strlen(buffer); s = buffer+n; n = sizeof(buffer)-n;
    (void) SNPRINTF(s, n) "\n");

	Write_fd_str( 2, buffer );
	in_log = 0;
	errno = err;
    VA_END;
}

/* VARARGS1 */
#ifdef HAVE_STDARGS
void logDebug (char *msg,...)
#else
void logDebug (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    char *msg;
#endif
	int err = errno;
	char log_buf[SMALLBUFFER];
	static int in_log;
    VA_LOCAL_DECL
    VA_START (msg);
    VA_SHIFT (msg, char *);

	if( in_log == 0 ){
		int n;
		char *s;

		++in_log;
		prefix_printer(log_buf, sizeof(log_buf));
		n = strlen(log_buf); s = log_buf+n; n = sizeof(log_buf)-n;
		(void) VSNPRINTF(s, n) msg, ap);
		log_backend(LOG_DEBUG, log_buf);
		in_log = 0;
	}
	errno = err;
    VA_END;
}

/***************************************************************************
 * char *Sigstr(n)
 * Return a printable form the the signal
 ***************************************************************************/

 struct signame {
    char *str;
    int value;
};

#undef PAIR
#ifndef _UNPROTO_
# define PAIR(X) { #X , X }
#else
# define __string(X) "X"
# define PAIR(X) { __string(X) , X }
#endif

 struct signame signals[] = {
{ "NO SIGNAL", 0 },
#ifdef SIGHUP
 PAIR(SIGHUP),
#endif
#ifdef SIGINT
 PAIR(SIGINT),
#endif
#ifdef SIGQUIT
 PAIR(SIGQUIT),
#endif
#ifdef SIGILL
 PAIR(SIGILL),
#endif
#ifdef SIGTRAP
 PAIR(SIGTRAP),
#endif
#ifdef SIGIOT
 PAIR(SIGIOT),
#endif
#ifdef SIGABRT
 PAIR(SIGABRT),
#endif
#ifdef SIGEMT
 PAIR(SIGEMT),
#endif
#ifdef SIGFPE
 PAIR(SIGFPE),
#endif
#ifdef SIGKILL
 PAIR(SIGKILL),
#endif
#ifdef SIGBUS
 PAIR(SIGBUS),
#endif
#ifdef SIGSEGV
 PAIR(SIGSEGV),
#endif
#ifdef SIGSYS
 PAIR(SIGSYS),
#endif
#ifdef SIGPIPE
 PAIR(SIGPIPE),
#endif
#ifdef SIGALRM
 PAIR(SIGALRM),
#endif
#ifdef SIGTERM
 PAIR(SIGTERM),
#endif
#ifdef SIGURG
 PAIR(SIGURG),
#endif
#ifdef SIGSTOP
 PAIR(SIGSTOP),
#endif
#ifdef SIGTSTP
 PAIR(SIGTSTP),
#endif
#ifdef SIGCONT
 PAIR(SIGCONT),
#endif
#ifdef SIGCHLD
 PAIR(SIGCHLD),
#endif
#ifdef SIGCLD
 PAIR(SIGCLD),
#endif
#ifdef SIGTTIN
 PAIR(SIGTTIN),
#endif
#ifdef SIGTTOU
 PAIR(SIGTTOU),
#endif
#ifdef SIGIO
 PAIR(SIGIO),
#endif
#ifdef SIGPOLL
 PAIR(SIGPOLL),
#endif
#ifdef SIGXCPU
 PAIR(SIGXCPU),
#endif
#ifdef SIGXFSZ
 PAIR(SIGXFSZ),
#endif
#ifdef SIGVTALRM
 PAIR(SIGVTALRM),
#endif
#ifdef SIGPROF
 PAIR(SIGPROF),
#endif
#ifdef SIGWINCH
 PAIR(SIGWINCH),
#endif
#ifdef SIGLOST
 PAIR(SIGLOST),
#endif
#ifdef SIGUSR1
 PAIR(SIGUSR1),
#endif
#ifdef SIGUSR2
 PAIR(SIGUSR2),
#endif
{0,0}
    /* that's all */
};

#ifndef NSIG
# define  NSIG 32
#endif


const char *Sigstr (int n)
{
    static char buf[40];
	const char *s = 0;

	if( n == 0 ){
		return( "No signal");
	}
    if (n < NSIG && n >= 0) {
#if defined(HAVE_SYS_SIGLIST)
# if !defined(HAVE_SYS_SIGLIST_DEF)
		extern const char *sys_siglist[];
# endif
		s = sys_siglist[n];
#else
# if defined(HAVE__SYS_SIGLIST)
#  if !defined(HAVE__SYS_SIGLIST_DEF)
		extern const char *_sys_siglist[];
#  endif
		s = _sys_siglist[n];
# else
		int i;
		for( i = 0; signals[i].str && signals[i].value != n; ++i );
		s = signals[i].str;
# endif
#endif
	}
	if( s == 0 ){
		s = buf;
		(void) SNPRINTF (buf, sizeof(buf)) "signal %d", n);
	}
    return(s);
}

/***************************************************************************
 * Decode_status (plp_status_t *status)
 * returns a printable string encoding return status
 ***************************************************************************/

const char *Decode_status (plp_status_t *status)
{
    static char msg[LINEBUFFER];

	int n;
    *msg = 0;		/* just in case! */
    if (WIFEXITED (*status)) {
		n = WEXITSTATUS(*status);
		if( n > 0 && n < 32 ) n += JFAIL-1;
		(void) SNPRINTF (msg, sizeof(msg))
		"exit status %d (%s)", WEXITSTATUS(*status),
				 Server_status(n) );
    } else if (WIFSTOPPED (*status)) {
		(void) strcpy(msg, "stopped");
    } else {
		(void) SNPRINTF (msg, sizeof(msg)) "died%s",
			WCOREDUMP (*status) ? " and dumped core" : "");
		if (WTERMSIG (*status)) {
			(void) SNPRINTF(msg + strlen (msg), sizeof(msg)-strlen(msg))
				 ", %s", Sigstr ((int) WTERMSIG (*status)));
		}
    }
    return (msg);
}

/***************************************************************************
 * char *Server_status( int d )
 *  translate the server status;
 ***************************************************************************/

	static struct signame statname[] = {
	PAIR(JSUCC),
	PAIR(JFAIL),
	PAIR(JABORT),
	PAIR(JREMOVE),
	PAIR(JHOLD),
	PAIR(JNOSPOOL),
	PAIR(JNOPRINT),
	PAIR(JSIGNAL),
	PAIR(JFAILNORETRY),
	PAIR(JSUSP),
	PAIR(JTIMEOUT),
	PAIR(JWRERR),
	PAIR(JRDERR),
	PAIR(JCHILD),
	PAIR(JNOWAIT),
	{0,0}
	};

char *Server_status( int d )
{
	char *s;
	int i;
	static char msg[LINEBUFFER];

	if( d > 0 && d < 32 ) d += 31;
	for( i = 0; (s = statname[i].str) && statname[i].value != d; ++i );
	if( s == 0 ){
		s = msg;
		SNPRINTF( msg, sizeof(msg)) "UNKNOWN STATUS '%d'", d );
	}
	return(s);
}

/*
 * Error status on STDERR
 */
/* VARARGS2 */
#ifdef HAVE_STDARGS
void setstatus (struct job *job,char *fmt,...)
#else
void setstatus (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    struct job *job;
    char *fmt;
#endif
	char msg_b[SMALLBUFFER];
	static int insetstatus;
	struct stat statb;
    VA_LOCAL_DECL

    VA_START (fmt);
    VA_SHIFT (job, struct job * );
    VA_SHIFT (fmt, char *);
	if( Doing_cleanup || fmt == 0 || *fmt == 0 || insetstatus ) return;

	insetstatus = 1;
	(void) VSNPRINTF( msg_b, sizeof(msg_b)-4) fmt, ap);
	DEBUG1("setstatus: msg '%s'", msg_b);
	if( !Is_server ){
		if( Verbose || !Is_lpr ){
			(void) VSNPRINTF(msg_b, sizeof(msg_b)-2) fmt, ap);
			strcat( msg_b,"\n" );
			if( Write_fd_str( 2, msg_b ) < 0 ) cleanup(0);
		} else {
			Add_line_list(&Status_lines,msg_b,0,0,0);
		}
	} else {
		if( Status_fd <= 0 
			|| ( Max_status_size_DYN > 0 && fstat( Status_fd, &statb ) != -1
			&& (statb.st_size / 1024 ) > Max_status_size_DYN ) ){
			Status_fd = Trim_status_file( Status_fd, Queue_status_file_DYN,
				Max_status_size_DYN, Min_status_size_DYN );
		}
		send_to_logger( Status_fd, Mail_fd, job, PRSTATUS, msg_b );
	}
	insetstatus = 0;
	VA_END;
}


/***************************************************************************
 * void setmessage (struct job *job,char *header, char *fmt,...)
 * put the message out (if necessary) to the logger
 ***************************************************************************/

/* VARARGS2 */
#ifdef HAVE_STDARGS
void setmessage (struct job *job,const char *header, char *fmt,...)
#else
void setmessage (va_alist) va_dcl
#endif
{
#ifndef HAVE_STDARGS
    struct job *job;
    char *header;
    char *fmt;
#endif
	char msg_b[SMALLBUFFER];

    VA_LOCAL_DECL

    VA_START (fmt);
    VA_SHIFT (job, struct job * );
    VA_SHIFT (header, char *);
    VA_SHIFT (fmt, char *);

	if( Doing_cleanup ) return;
	(void) VSNPRINTF( msg_b, sizeof(msg_b)-4) fmt, ap);
	DEBUG1("setmessage: msg '%s'", msg_b);
	if( Is_server ){
		send_to_logger( -1, -1, job, header, msg_b );
	} else {
		strcat( msg_b,"\n" );
		if( Write_fd_str( 2, msg_b ) < 0 ) cleanup(0);
	}
	VA_END;
}


/***************************************************************************
 * send_to_logger( struct job *job, char *msg )
 *  This will try and send to the logger.
 ***************************************************************************/

 void send_to_logger( int send_to_status_fd, int send_to_mail_fd,
	struct job *job, const char *header, char *msg_b )
{
	char *s, *t;
	char *id, *tstr;
	int num,pid;
	char out_b[4*SMALLBUFFER];
	struct line_list l;

	if( !Is_server || Doing_cleanup ) return;
	Init_line_list(&l);
	if(DEBUGL4){
		char buffer[32];
		SNPRINTF(buffer,sizeof(buffer)-5)"%s", msg_b );
		if( msg_b ) safestrncat( buffer,"...");
		LOGDEBUG("send_to_logger: Logger_fd fd %d, send_to_status_fd %d, send_to_mail fd %d, header '%s', body '%s'",
			Logger_fd, send_to_status_fd, send_to_mail_fd, header, buffer );
	}
	if( send_to_status_fd <= 0 && send_to_mail_fd <= 0 && Logger_fd <= 0 ) return;
	s = t = id = tstr = 0;
	num = 0;
	if( job ){
		Set_str_value(&l,IDENTIFIER,
			(id = Find_str_value(&job->info,IDENTIFIER,Value_sep)) );
		Set_decimal_value(&l,NUMBER,
			(num = Find_decimal_value(&job->info,NUMBER,Value_sep)) );
	}
	Set_str_value(&l,UPDATE_TIME,(tstr=Time_str(0,0)));
	Set_decimal_value(&l,PROCESS,(pid=getpid()));

	SNPRINTF( out_b, sizeof(out_b)) "%s at %s ## %s=%s %s=%d %s=%d\n",
		msg_b, tstr, IDENTIFIER, id, NUMBER, num, PROCESS, pid );

	if( send_to_status_fd > 0 && Write_fd_str( send_to_status_fd, out_b ) < 0 ){
		DEBUG4("send_to_logger: write to send_to_status_fd %d failed - %s",
			send_to_status_fd, Errormsg(errno) );
	}
	if( send_to_mail_fd > 0 && Write_fd_str( send_to_mail_fd, out_b ) < 0 ){
		DEBUG4("send_to_logger: write to send_to_mail_fd %d failed - %s",
			send_to_mail_fd, Errormsg(errno) );
	}
	if( Logger_fd > 0 ){
		Set_str_value(&l,PRINTER,Printer_DYN);
		Set_str_value(&l,HOST,FQDNHost_FQDN);
		s = Escape(msg_b,1);
		Set_str_value(&l,VALUE,s);
		if(s) free(s); s = 0;
		t = Join_line_list(&l,"\n");
		s = Escape(t,1); 
		if(t) free(t); t = 0;
		t = safestrdup4(header,"=",s,"\n",__FILE__,__LINE__);
		Write_fd_str( Logger_fd, t );
		if( s ) free(s); s = 0;
		if( t ) free(t); t = 0;
	}
	Free_line_list(&l);
}
