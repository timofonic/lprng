/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-2001, Patrick Powell, San Diego, CA
 *     papowell@lprng.com
 * See LICENSE for conditions of use.
 * $Id: initialize.h,v 1.25 2001/10/15 13:25:38 papowell Exp $
 ***************************************************************************/



#ifndef _INITIALIZE_H
#define _INITIALIZE_H

/* PROTOTYPES */
void Initialize(int argc,  char *argv[], char *envp[], int debugchar );
void Setup_configuration();
char *Get_user_information( void );

#endif
