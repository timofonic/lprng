/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-1997, Patrick Powell, San Diego, CA
 *     papowell@sdsu.edu
 * See LICENSE for conditions of use.
 *
 ***************************************************************************
 * MODULE: sendlpc.c
 * PURPOSE: Send a lpc request to the remote host
 *
 **************************************************************************/

static char *const _id =
"$Id: sendlpc.c,v 3.2 1997/01/19 14:34:56 papowell Exp $";

#include "lp.h"
#include "sendlpc.h"
#include "control.h"
#include "errorcodes.h"
#include "getprinter.h"
#include "killchild.h"
#include "linksupport.h"
#include "readstatus.h"
#include "sendauth.h"
#include "setstatus.h"
#include "fileopen.h"
/**** ENDINCLUDE ****/


/***************************************************************************
Commentary:
The protocol used to send a lpc request to a remote host consists of the
following:

\6printer user function options

 ***************************************************************************/

/***************************************************************************
 * Send_lpcrequest
 * 1. Open connection to remote host
 * 2. Send a line of the form:
 *     - short status  \6Printer user command <options>
 * 3. Read from the connection until closed
 ***************************************************************************/

void Send_lpcrequest(
	char *user,					/* user name */
	char **options,				/* options to send */
	int connect_timeout,		/* timeout on connection */
	int transfer_timeout,		/* timeout on transfer */
	int output,					/* output file descriptor */
	int action					/* type of action */
	)
{
	char *s;
	int i, len, status, argc;
	int sock = -1;		/* socket to use */
	char line[LINEBUFFER];
	int err;

	DEBUG1("Send_lpcrequest: connect_timeout %d, transfer_timeout %d",
			connect_timeout, transfer_timeout );

	/*
	 * arguments are of form: 
	 *  action [printer]
	 */

	for( argc = 0; options[argc]; ++argc );
	if( argc == 0 ){
		return;
	}

	if( argc > 1 && action != LPD && action != REREAD && action != PRINTCAP ){
		/* second argument is the printer */
		Printer = options[1];
		RemoteHost = 0;
		RemotePrinter = 0;
		/* Find the remote printer and host name */
		Fix_remote_name(0);
		if( RemotePrinter ){
			options[1] = RemotePrinter;
		}
	}

	if( RemoteHost == 0 || *RemoteHost == 0 ){
		RemoteHost = 0;
		if( Default_remote_host && *Default_remote_host ){
			RemoteHost = Default_remote_host;
		} else if( FQDNHost && *FQDNHost ){
			RemoteHost = FQDNHost;
		}
	}
	if( RemoteHost == 0 ){
		plp_snprintf( line, sizeof(line)-2, 
			"no remote host for printer `%s'", Printer );
		goto error;
	}

	DEBUG3("Send_lpcrequest: Printer '%s', Remote printer %s, RemoteHost '%s'",
		Printer, RemotePrinter, RemoteHost );
	/* now format the option line */
	plp_snprintf( line, sizeof(line), "%c%s %s\n",
		REQ_CONTROL, RemotePrinter?RemotePrinter:Printer, user );

	for( i = 0; (s = options[i]); ++i ){
		len = strlen(line) - 1;
		if( len + strlen(s) >= sizeof(line) - 4 ){
			break;
		}
		plp_snprintf( line+len, sizeof(line) - len,
			" %s\n", s );
	}
	if( s ){
		plp_snprintf( line, sizeof(line)-2,
			"too many options or options too long" );
		goto error;
	}

	DEBUG0("Send_lpcrequest: sending '%s'", line );
	sock = Link_open( RemoteHost, 0, connect_timeout );
	err = errno;
	if( sock < 0 ){
		plp_snprintf( line, sizeof(line)-2,
			"cannot open connection to `%s@%s' - %s",
			Printer, RemoteHost, Errormsg(err) );
		goto error;
	}

	Fix_auth();

	/* send the command */
	if( Use_auth || Use_auth_flag ){
		DEBUG3("Send_lpcrequest: using authentication" );
		status = Send_auth_command( RemotePrinter, RemoteHost, &sock,
			transfer_timeout, line, output );
	} else {
		DEBUG3("Send_lpcrequest: sending '%s'", line );
		status = Link_send( RemoteHost, &sock, transfer_timeout, line,
			strlen( line ), 0 );
	}
	Read_status_info( Printer, 0, sock, RemoteHost, output );
	Link_close( &sock );
	return;

error:
	Errorcode = JFAIL;
	if( Interactive ){
		safestrncat(line, "\n" );
		Write_fd_str( output, line );
	} else {
		setstatus( NORMAL, line );
	}
	Link_close( &sock );
	return;
}