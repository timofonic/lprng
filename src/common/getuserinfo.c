/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-1997, Patrick Powell, San Diego, CA
 *     papowell@astart.com
 * See LICENSE for conditions of use.
 *
 ***************************************************************************
 * MODULE: getuserinfo.c
 * PURPOSE: user name information lookup
 **************************************************************************/

static char *const _id =
"getuserinfo.c,v 3.3 1997/09/18 19:45:58 papowell Exp";
/********************************************************************
 * char *Get_user_information();
 *  get the user name
 *
 * 
 ********************************************************************/

#include "lp.h"
#include "getuserinfo.h"
#include "setuid.h"
/**** ENDINCLUDE ****/

char *Get_user_information( void )
{
	char *name = 0;
	static char uid_msg[32];
	uid_t uid = OriginalRUID;
#ifndef USER_ENV
	struct passwd *pw_ent;

	/* get the password file entry */
    if( (pw_ent = getpwuid( uid )) ){
		name =  pw_ent->pw_name;
	}

#else
	name = getenv( "LOGNAME" );
	if( name == 0 ){
		name = getenv( "USER" );
	}
#endif
	if( name == 0 ){
		plp_snprintf( uid_msg, sizeof(uid_msg), "UID_%d", uid );
		name = uid_msg;
	} else {
		safestrncpy( uid_msg, name );
	}
    return( uid_msg );
}

int Root_perms( void )
{
	return( getuid() == 0 );
}
