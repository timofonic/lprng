/***************************************************************************
 * LPRng - An Extended Print Spooler System
 *
 * Copyright 1988-1999, Patrick Powell, San Diego, CA
 *     papowell@lprng.com
 * See LICENSE for conditions of use.
 * $Id: user_auth.stub,v 1.31 2002/05/06 16:03:45 papowell Exp $
 ***************************************************************************/

/*
 * This code is, sadly,  a whimpy excuse for the dynamically loadable
 * modules.  The idea is that you can put your user code in here and it
 * will get included in various files.
 * 
 * Supported Sections:
 *   User Authentication
 * 
 *   DEFINES      FILE WHERE INCLUDED PURPOSE
 *   USER_RECEIVE  lpd_secure.c       define the user authentication
 *                                    This is an entry in a table
 *   USER_SEND     sendauth.c         define the user authentication
 *                                    This is an entry in a table
 *   RECEIVE       lpd_secure.c       define the user authentication
 *                            This is the code referenced in USER_RECEIVE
 *   SENDING       sendauth.c       define the user authentication
 *                            This is the code referenced in USER_SEND
 * 
 */

#include "lp.h"
#include "user_auth.h"
#include "errorcodes.h"
#include "fileopen.h"
#include "linksupport.h"
#include "child.h"
#include "getqueue.h"
#include "lpd_secure.h"
#include "lpd_dispatch.h"
#include "permission.h"


/**************************************************************
 * Secure Protocol
 *
 * the following is sent on *sock:  
 * \REQ_SECUREprintername C/F user authtype \n        - receive a command
 *             0           1   2   3
 * \REQ_SECUREprintername C/F user authtype jobsize\n - receive a job
 *             0           1   2   3        4
 *          Printer_DYN    |   |   |        + jobsize
 *                         |   |   authtype 
 *                         |  user
 *                        from_server=1 if F, 0 if C
 *                         
 * The authtype is used to look up the security information.  This
 * controls the dispatch and the lookup of information from the
 * configuration and printcap entry for the specified printer
 *
 * The info line_list has the information, stripped of the leading
 * xxxx_ of the authtype name.
 * For example:
 *
 * forward_id=test      <- forward_id from configuration/printcap
 * id=test              <- id from configuration/printcap
 * 
 * If there are no problems with this information, a single 0 byte
 * should be written back at this point, or a nonzero byte with an
 * error message.  The 0 will cause the corresponding transfer
 * to be started.
 * 
 * The handshake and with the remote end should be done now.
 *
 * The client will send a string with the following format:
 * destination=test\n     <- destination ID (URL encoded)
 *                       (destination ID from above)
 * server=test\n          <- if originating from server, the server key (URL encoded)
 *                       (originator ID from above)
 * client=papowell\n      <- client id
 *                       (client ID from above)
 * input=%04t1\n          <- input or command
 * This information will be extracted by the server.
 * The 'Do_secure_work' routine can now be called,  and it will do the work.
 * 
 * ERROR MESSAGES:
 *  If you generate an error,  then you should log it.  If you want
 *  return status to be returned to the remote end,  then you have
 *  to take suitable precautions.
 * 1. If the error is detected BEFORE you send the 0 ACK,  then you
 *    can send an error back directly.
 * 2. If the error is discovered as the result of a problem with
 *    the encryption method,  it is strongly recommended that you
 *    simply send a string error message back.  This should be
 *    detected by the remote end,  which will then decide that this
 *    is an error message and not status.
 *
 **************************************************************/

/*
  Test_connect: send the validation information  
    expect to get back NULL or error message
 */

int Test_connect( struct job *job, int *sock,
	char *errmsg, int errlen,
	struct line_list *info, struct security *security )
{
	char *cmd, *secure;
	int status = 0, ack;
	secure = 0;
	
	if(DEBUGL1)Dump_line_list("Test_connect: info", info );
	if( *sock >= 0 ){
		cmd = Find_str_value(info, CMDVAL, Value_sep );
		secure = safestrdup2(cmd,"\n",__FILE__,__LINE__);
		DEBUG3("Test_connect: sending '%s'", secure );
		status = Link_send( RemoteHost_DYN, sock, Send_job_rw_timeout_DYN,
			secure, strlen(secure), &ack );
		DEBUG3("Test_connect: status '%s'", Link_err_str(status) );
		if( status ){
			SNPRINTF(errmsg, errlen)
				"Test_connect: error '%s'", Link_err_str(status) );
			status = JFAIL;
		}
	} else {
		status = JFAIL;
	}
	if( secure ) free( secure); secure = 0;
	return( status );
}

int Test_accept( int *sock,
	char *user, char *jobsize, int from_server, char *authtype,
	char *errmsg, int errlen,
	struct line_list *info, struct line_list *header_info,
	struct security *security )
{
	int status, n, len;
	char input[SMALLBUFFER];
	char *value;

	DEBUGFC(DRECV1)Dump_line_list("Test_accept: info", info );
	DEBUGFC(DRECV1)Dump_line_list("Test_accept: header_info", header_info );

	/* get information until empty lines */
	DEBUG1("Test_accept: sending ACK" );
	if( (status = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		"", 1, 0 )) ){
		SNPRINTF(errmsg,errlen)
			"error '%s' ACK to %s@%s",
			Link_err_str(status), RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	DEBUGF(DRECV1)("Test_accept: starting read from socket %d", *sock );
	do{
		len = sizeof(input)-1;
		status = Link_line_read(ShortRemote_FQDN,sock,
			Send_job_rw_timeout_DYN,input,&len);
		if( len >= 0 ) input[len] = 0;
		DEBUG1( "Test_accept: read status %d, len %d, '%s'",
			status, len, input );
		if( len == 0 ){
			DEBUG3( "Test_accept: zero length read" );
			break;
		}
		if( (value = safestrchr(input,'=')) ){
			*value++ = 0;
			Unescape(value);
		}
		Set_str_value(header_info,input,value);
		if( status ){
			logerr( LOG_DEBUG, _("Test_accept: cannot read information") );
			return(status);
		}
	} while( status == 0 );
	if( status == 0 ){
		if( (status = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
			"", 1, 0 )) ){
			SNPRINTF(errmsg,errlen)
				"error '%s' ACK to %s@%s",
				Link_err_str(status), RemotePrinter_DYN, RemoteHost_DYN );
			goto error;
		}
	}
	DEBUGFC(DRECV1)Dump_line_list("Test_accept: header_info", header_info );
 error:
	return( status );
}

int Test_receive( int *sock, char *user, char *jobsize, int from_server,
	char *authtype, struct line_list *info,
	char *errmsg, int errlen, struct line_list *header_info, char *tempfile )
{
	int tempfd, status, n;
	char buffer[LARGEBUFFER];
	struct stat statb;

#if 0
	/* this shows how to create a temporary file for private use
	 * it gets unlinked safely.
	 */
	char *pgpfile
    pgpfile = safestrdup2(tempfile,".pgp",__FILE__,__LINE__); 
    Check_max(&Tempfiles,1);
    Tempfiles.list[Tempfiles.count++] = pgpfile;
#endif

	tempfd = -1;

	DEBUGFC(DRECV1)Dump_line_list("Test_receive: info", info );
	DEBUGFC(DRECV1)Dump_line_list("Test_receive: header_info", header_info );
	/* do validation and then write 0 */
	if( Write_fd_len( *sock, "", 1 ) < 0 ){
		status = JABORT;
		SNPRINTF( errmsg, errlen) "Test_receive: ACK 0 write error - %s",
			Errormsg(errno) );
		goto error;
	}

	/* open a file for the output */
	if( (tempfd = Checkwrite(tempfile,&statb,O_WRONLY|O_TRUNC,1,0)) < 0 ){
		Errorcode = JFAIL;
		logerr_die(LOG_INFO, "Test_receive: reopen of '%s' for write failed",
			tempfile );
	}

	DEBUGF(DRECV1)("Test_receive: starting read from socket %d", *sock );
	while( (n = read(*sock, buffer,sizeof(buffer)-1)) > 0 ){
		buffer[n] = 0;
		DEBUGF(DRECV4)("Test_receive: remote read '%d' '%s'", n, buffer );
		if( write( tempfd,buffer,n ) != n ){
			DEBUGF(DRECV1)( "Test_receive: bad write to '%s' - '%s'",
				tempfile, Errormsg(errno) );
			status = JFAIL;
			goto error;
		}
	}
	if( n < 0 ){
		DEBUGF(DRECV1)("Test_receive: bad read '%d' getting command", n );
		status = JFAIL;
		goto error;
	}
	close(tempfd); tempfd = -1;
	DEBUGF(DRECV4)("Test_receive: end read" );

	/*** at this point you can check the format of the received file, etc.
     *** if you have an error message at this point, you should write it
	 *** to the socket,  and arrange protocol can handle this.
	 ***/

	status = Do_secure_work( jobsize, from_server, tempfile, header_info );

	/*** if an error message is returned, you should write this
	 *** message to the tempfile and the proceed to send the contents
	 ***/
	DEBUGF(DRECV1)("Test_receive: doing reply" );
	if( (tempfd = Checkread(tempfile,&statb)) < 0 ){
		Errorcode = JFAIL;
		logerr_die(LOG_INFO, "Test_receive: reopen of '%s' for write failed",
			tempfile );
	}

	while( (n = read(tempfd, buffer,sizeof(buffer)-1)) > 0 ){
		buffer[n] = 0;
		DEBUGF(DRECV4)("Test_receive: sending '%d' '%s'", n, buffer );
		if( write( *sock,buffer,n ) != n ){
			DEBUGF(DRECV1)( "Test_receive: bad write to socket - '%s'",
				Errormsg(errno) );
			status = JFAIL;
			goto error;
		}
	}
	if( n < 0 ){
		DEBUGF(DRECV1)("Test_receive: bad read '%d' getting status", n );
		status = JFAIL;
		goto error;
	}
	DEBUGF(DRECV1)("Test_receive: reply done" );

 error:
	if( tempfd>=0) close(tempfd); tempfd = -1;
	return(status);
}

int Test_verify( int *sock, char *user, char *jobsize, int from_server,
	char *authtype, struct line_list *info,
	char *errmsg, int errlen, struct line_list *header_info, char *tempfile )
{
	int status = 0;
	char input[SMALLBUFFER];
	int len;

#if 0
	/* this shows how to create a temporary file for private use
	 * it gets unlinked safely.
	 */
	char *pgpfile
    pgpfile = safestrdup2(tempfile,".pgp",__FILE__,__LINE__); 
    Check_max(&Tempfiles,1);
    Tempfiles.list[Tempfiles.count++] = pgpfile;
#endif

	if(DEBUGL1)Dump_line_list("Test_verify: info", info );
	if(DEBUGL1)Dump_line_list("Test_verify: header_info", header_info );
	/* do validation and then write 0 */

	if( Write_fd_len( *sock, "", 1 ) < 0 ){
		status = JABORT;
		SNPRINTF( errmsg, errlen) "Test_verify: ACK 0 write error - %s",
			Errormsg(errno) );
		goto error;
	}

	len = sizeof(input)-1;
	status = Link_line_read(ShortRemote_FQDN,sock,
		Send_job_rw_timeout_DYN,input,&len);
	if( len >= 0 ) input[len] = 0;
	DEBUG1( "Test_verify: read status %d, len %d, '%s'",
		status, len, input );
	if( len == 0 ){
		DEBUG3( "Test_verify: zero length read" );
		cleanup(0);
	}
	if( status ){
		logerr_die( LOG_DEBUG, _("Test_verify: cannot read request") );
	}
	if( len < 2 ){
		fatal( LOG_INFO, _("Test_verify: bad request line '%s'"), input );
	}

	Dispatch_input(sock,input);
	cleanup(0);

 error:
	return(status);
}



/**************************************************************
 *Test_send:
 *A simple implementation for testing user supplied authentication
 *
 * The start of the actual file has:
 * destination=test     <- destination ID (URL encoded)
 *                       (destination ID from above)
 * server=test          <- if originating from server, the server key (URL encoded)
 *                       (originator ID from above)
 * client=papowell      <- client id
 *                       (client ID from above)
 * input=%04t1          <- input that is 
 *   If you need to set a 'secure id' then you set the 'FROM'
 *
 **************************************************************/

int Test_send( int *sock, int transfer_timeout, char *tempfile,
	char *errmsg, int errlen,
	struct security *security, struct line_list *info )
{
	char buffer[LARGEBUFFER];
	struct stat statb;
	int tempfd, len;
	int status = 0;

	if(DEBUGL1)Dump_line_list("Test_send: info", info );
	DEBUG1("Test_send: sending on socket %d", *sock );
	if( (tempfd = Checkread(tempfile,&statb)) < 0){
		SNPRINTF(errmsg, errlen)
			"Test_send: open '%s' for read failed - %s",
			tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	DEBUG1("Test_send: starting read");
	while( (len = read( tempfd, buffer, sizeof(buffer)-1 )) > 0 ){
		buffer[len] = 0;
		DEBUG4("Test_send: file information '%s'", buffer );
		if( write( *sock, buffer, len) != len ){
			SNPRINTF(errmsg, errlen)
				"Test_send: write to socket failed - %s", Errormsg(errno) );
			status = JABORT;
			goto error;
		}
	}
	if( len < 0 ){
		SNPRINTF(errmsg, errlen)
			"Test_send: read from '%s' failed - %s", tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	close(tempfd); tempfd = -1;
	/* we close the writing side */
	shutdown( *sock, 1 );

	DEBUG1("Test_send: sent file" );

	if( (tempfd = Checkwrite(tempfile,&statb,O_WRONLY|O_TRUNC,1,0)) < 0){
		SNPRINTF(errmsg, errlen)
			"Test_send: open '%s' for write failed - %s",
			tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	DEBUG1("Test_send: starting read");

	while( (len = read(*sock,buffer,sizeof(buffer)-1)) > 0 ){
		buffer[len] = 0;
		DEBUG4("Test_send: socket information '%s'", buffer);
		if( write(tempfd,buffer,len) != len ){
			SNPRINTF(errmsg, errlen)
				"Test_send: write to '%s' failed - %s", tempfile, Errormsg(errno) );
			status = JABORT;
			goto error;
		}
	}
	close( tempfd ); tempfd = -1;

 error:
	return(status);
}


/* 
* md5 authentication
 
 
 Connection Level: md5_connect and md5_verify
 
 The client and server do a handshake, exchanging salts and values as follows:
 Client connects to server
      Server sends 'salt'
 Client gets user and user secret 
        gets server id and server secret 
        makes 'userid serverid'
        hashs the whole mess and gets md5 hash (see code)
        sends 'userid serverid XXXXXXX' where XXXXX is md5 hash
 Server checks userid and server, generates the same hash
        if values match, all is happy
 
 File Transfer: md5_send and md5 receive
 
 Client connects to server
     Server sends 'salt'
 Client
        get user and user secret
        gets server id and server secret 
        hashes the mess and the file
        Sends the hash followed by the file
 Server reads the file
        gets the hash
        dumps the rest of stuff into a file
        get user and user secret
        gets server id and server secret 
        hashes the mess and the file
        checks to see that all is well
        performs action
  Client keyfile:
    xxx=yyy key   where xxx is md5_id=xxx value from printcap
        yyy should be sent to server as the 'from' value
  Server keyfile:
    yyy=key       where yyy is the 'from' id value

*/


#include "md5.h"

/* Set the name of the file we'll be getting our key from.
   The keyfile should only be readable by the owner and
   by whatever group the user programs run as.
   This way, nobody can write their own authentication module */

/* key length (for MD5, it's 16) */
#define KEY_LENGTH 16
 char *hexstr( char *str, int len, char *outbuf, int outlen );
 void MDString (char *inString, char *outstring, int inlen, int outlen);
 void MDFile( int fd, char *outstring, int outlen );
 int md5key( const char *keyfile, char *name, char *key, int keysize, char *errmsg, int errlen );

/* The md5 hashing function, which does the real work */
 void MDString (char *inString, char *outstring, int inlen, int outlen)
{
	MD5_CTX mdContext;

	MD5Init (&mdContext);
	MD5Update(&mdContext, inString, inlen);
	MD5Final(&mdContext);
	memcpy( outstring, mdContext.digest, outlen );
}

 void MDFile( int fd, char *outstring, int outlen )
{
	MD5_CTX mdContext;
	char buffer[LARGEBUFFER];
	int n;

	MD5Init (&mdContext);
	while( (n = read( fd, buffer, sizeof(buffer))) > 0 ){
		MD5Update(&mdContext, buffer, n);
	}
	MD5Final(&mdContext);
	memcpy( outstring, mdContext.digest, outlen );
}

 char *hexstr( char *str, int len, char *outbuf, int outlen )
{
	int i, j;
	for( i = 0; i < len && 2*(i+1) < outlen ; ++i ){
		j = ((unsigned char *)str)[i];
		SNPRINTF(&outbuf[2*i],4)"%02x",j);
	}
	if( outlen > 0 ) outbuf[2*i] = 0;
	return( outbuf );
}

 int md5key( const char *keyfile, char *name, char *key, int keysize, char *errmsg, int errlen )
{
	const char *keyvalue;
	int i,  keylength = -1;
	struct line_list keys;

	Init_line_list( &keys );
	memset(key,0,keysize);
	/*
		void Read_file_list( int required, struct line_list *model, char *str,
			const char *linesep, int sort, const char *keysep, int uniq, int trim,
			int marker, int doinclude, int nocomment, int depth, int maxdepth )
	*/
	Read_file_list( /*required*/0, /*model*/&keys, /*str*/(char *)keyfile,
		/*linesep*/Line_ends,/*sort*/1, /*keysep*/Value_sep,/*uniq*/1, /*trim*/1,
		/*marker*/0, /*doinclude*/0, /*nocomment*/1,/*depth*/0,/*maxdepth*/4 );
	/* read in the key from the key file */
	keyvalue = Find_exists_value( &keys, name, Value_sep );
	if( keyvalue == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5key: no key for '%s' in '%s'", name, keyfile );
		goto error;
	}
	DEBUG1("md5key: key '%s'", keyvalue );

	/* copy to string */
	for(i = 0; keyvalue[i] && i < keysize; ++i ){
		key[i] = keyvalue[i];
	}
	keylength = i;

 error:
	Free_line_list( &keys );
	return( keylength );
}


int md5_connect( struct job *job, int *sock, char **real_host,
	int connect_timeout, char *errmsg, int errlen,
	struct security *security, struct line_list *info  )
{
	int n, len;
	char destkey[KEY_LENGTH+1];
	char challenge[KEY_LENGTH+1];
	char response[KEY_LENGTH+1];
	int destkeylength, i, ack;
	char buffer[SMALLBUFFER];
	char smallbuffer[SMALLBUFFER];
	char keybuffer[SMALLBUFFER];
	char *s, *t, *cmd, *secure, *dest;
	const char *keyfile;

	secure = 0;
	errmsg[0] = 0;
	if(DEBUGL1)Dump_line_list("md5_connect: info", info );
	if( !Is_server ){
		/* we get the value of the MD5KEYFILE variable */
		keyfile = getenv("MD5KEYFILE");
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_connect: no MD5KEYFILE environment variable" );
			goto error;
		}
	} else {
		keyfile = Find_exists_value( info, "server_keyfile", Value_sep );
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_connect: no server_keyfile entry" );
			goto error;
		}
	}

	dest = Find_str_value( info, DESTINATION, Value_sep );
	if( dest == 0 ){
		SNPRINTF(errmsg, errlen)
			"md5_connect: no '%s' value in info", DESTINATION );
		goto error;
	}
	if( (destkeylength = md5key( keyfile, dest, keybuffer, sizeof(keybuffer),
		errmsg, errlen ) ) <= 0 ){
		goto error;
	}
	keybuffer[destkeylength] = 0;
	if((s = strpbrk(keybuffer, Whitespace))){
		*s++ = 0;
		dest = keybuffer;
		while( isspace(cval(s)) ) ++s;
	} else {
		s = keybuffer;
		dest = Find_str_value( info, FROM, Value_sep );
	}
	if( *s == 0 ){
		SNPRINTF(errmsg, errlen)
			"md5_connect: no '%s' value in keyfile", dest );
		goto error;
	}
	destkeylength = strlen(s);
	if( destkeylength >  KEY_LENGTH ) destkeylength = KEY_LENGTH;
	memcpy( destkey, s, destkeylength );

	*sock = Link_open_list( RemoteHost_DYN,
			real_host, 0, connect_timeout, 0, Unix_socket_path_DYN );
	if( *sock >= 0 ){
		cmd = Find_str_value(info, INPUT, Value_sep );
		secure = safestrdup2(cmd,"\n",__FILE__,__LINE__);
		DEBUG1("md5_connect: sending '%s'", secure );
		ack = 0;
		if( (n = Link_send( RemoteHost_DYN, sock, connect_timeout,
			secure, strlen(secure), &ack )) ){
			if( (s = strchr(secure,'\n')) ) *s = 0;
			DEBUG1("md5_connect: status '%s'", Link_err_str(n) );
			if( ack ){
				SNPRINTF(errmsg,errlen)
					"error '%s'\n with ack '%s' sending str '%s' to %s@%s",
					Link_err_str(n), Ack_err_str(ack), secure,
					RemotePrinter_DYN, RemoteHost_DYN );
			} else {
				SNPRINTF(errmsg,errlen)
					"error '%s'\n sending str '%s' to %s@%s",
					Link_err_str(n), secure,
					RemotePrinter_DYN, RemoteHost_DYN );
			}
			goto error;
		}
	} else {
		if( errmsg[0] == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_connect: connect error" );
		}
		goto error;
	}
	if( secure ) free( secure); secure = 0;

	/* Read the challenge from server */
	len = sizeof(buffer);
	if( (n = Link_line_read(ShortRemote_FQDN,sock,
		Send_query_rw_timeout_DYN,buffer,&len)) ){
		SNPRINTF(errmsg, errlen)
		"md5_connect: error reading challenge - '%s'", Link_err_str(n) );
		goto error;
	} else if( len == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_connect: zero length challenge");
		goto error;
	}
	DEBUG1("md5_connect: challenge '%s'", buffer );
	n = strlen(buffer);
	if( n == 0 || n % 2 || n/2 > KEY_LENGTH ){
		SNPRINTF(errmsg, errlen)
		"md5_connect: bad challenge length '%d'", strlen(buffer) );
		goto error;
	}
	memset(challenge, 0, sizeof(challenge));
	smallbuffer[2] = 0;
	for(i = 0; buffer[2*i] && i < KEY_LENGTH; ++i ){
		memcpy(smallbuffer,&buffer[2*i],2);
		challenge[i] = strtol(smallbuffer,0,16);
	}

	DEBUG1("md5_connect: decoded challenge '%s'",
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));
	DEBUG1("md5_connect: destkey '%s'", 
		hexstr( destkey, KEY_LENGTH, buffer, sizeof(buffer) ));
	/* xor the challenge with the dest key */
	n = 0;
	len = destkeylength;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^destkey[n]);
		n=(n+1)%len;
	}
	DEBUG1("md5_connect: challenge^destkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	DEBUG1("md5_connect: challenge^destkey^idkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	SNPRINTF( smallbuffer, sizeof(smallbuffer)) "%s", dest);

	/* now we xor the buffer with the key */
	len = strlen(smallbuffer);
	DEBUG1("md5_connect: idstuff len %d '%s'", len, smallbuffer );
	n = 0;
	for(i = 0; i < len; i++){
		challenge[n] = (challenge[n]^smallbuffer[i]);
		n=(n+1)%KEY_LENGTH;
	}
	DEBUG1("md5_connect: result idstuff^challenge^destkey^idkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* now, MD5 hash the string */
	MDString(challenge, response, KEY_LENGTH, KEY_LENGTH);

	/* return the response to the server */
	hexstr( response, KEY_LENGTH, buffer, sizeof(buffer) );
	n = strlen(smallbuffer);
	SNPRINTF(smallbuffer+n, sizeof(smallbuffer)-n-1) " %s", buffer );
	DEBUG1("md5_connect: response '%s'", smallbuffer );
	safestrncat(smallbuffer,"\n");

	ack = 0;
	if( (n =  Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		smallbuffer, strlen(smallbuffer), &ack )) ){
		/* keep the other end from trying to read */
		if( (s = strchr(buffer,'\n')) ) *s = 0;
		if( ack ){
			SNPRINTF(errmsg,errlen)
				"error '%s'\n with ack '%s' sending str '%s' to %s@%s",
				Link_err_str(n), Ack_err_str(ack), buffer,
				RemotePrinter_DYN, RemoteHost_DYN );
		} else {
			SNPRINTF(errmsg,errlen)
				"error '%s'\n sending str '%s' to %s@%s",
				Link_err_str(n), buffer,
				RemotePrinter_DYN, RemoteHost_DYN );
		}
		goto error;
	}
	DEBUG1("md5_connect: success");
	errmsg[0] = 0;

  error:
	if( errmsg[0] && *sock ){
		shutdown(*sock,1);
		len = 0;
		buffer[0] = 0;
		DEBUG1("md5_connect: error '%s'", errmsg );
		while( len < (int)sizeof(buffer)-1
			&& (n = read( *sock, buffer+len, sizeof(buffer)-len-1 )) > 0 ){
			buffer[len+n] = 0;
			while( (s = strchr(buffer,'\n')) ){
				t = "";
				*s++ = 0;
				n = strlen(errmsg);
				if( errmsg[n-1] != '\n' ){ t = "\n"; }
				DEBUG1("md5_connect: adding '%s' '%s'",t, buffer );
				SNPRINTF(errmsg+n,errlen-n)
					"%s %s\n", t,  buffer );
				memmove(buffer,s,strlen(s)+1);
			}
		}
		if( buffer[0] ){
			t = "";
			n = strlen(errmsg);
			if( errmsg[n-1] != '\n' ){ t = "\n"; }
			SNPRINTF(errmsg+n,errlen-n)
				"%s %s\n", t, buffer );
		}
	}
	if( errmsg[0] ){
		return( JFAIL );
	}
	return( JSUCC );
}


int md5_verify( int *sock, char *user, char *jobsize, int from_server,
	char *authtype, struct line_list *info,
	char *errmsg, int errlen, struct line_list *header_info, char *tempfile )
{
	char input[SMALLBUFFER];
	char buffer[SMALLBUFFER];
	char keybuffer[SMALLBUFFER];
	int destkeylength, i, n, len;
	char *s, *from, *hash;
	const char *keyfile;
	char destkey[KEY_LENGTH+1];
	char challenge[KEY_LENGTH+1];
	char response[KEY_LENGTH+1];


	if(DEBUGL1)Dump_line_list("md5_verify: info", info );
	if(DEBUGL1)Dump_line_list("md5_verify: header_info", header_info );
	/* do validation and then write 0 */

	if( !Is_server ){
		SNPRINTF(errmsg, errlen)
			"md5_verify: not server" );
		goto error;
	} else {
		keyfile = Find_exists_value( info, "server_keyfile", Value_sep );
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_verify: no server_keyfile entry" );
			goto error;
		}
	}

	DEBUG1("md5_verify: sending ACK" );
	if( (n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		"", 1, 0 )) ){
		SNPRINTF(errmsg,errlen)
			"error '%s' ACK to %s@%s",
			Link_err_str(n), RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}
	/* First, seed the random number generator */
	srand(time(NULL));

	/* Now, fill the challenge with 16 random values */
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = rand() >> 8;
	}
	hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) );
	DEBUG1("md5_verify: sending challenge '%s'", buffer );
	safestrncat(buffer,"\n");

	/* Send the challenge to the client */

	if( (n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		buffer, strlen(buffer), 0 )) ){
		/* keep the other end from trying to read */
		if( (s = strchr(buffer,'\n')) ) *s = 0;
		SNPRINTF(errmsg,errlen)
			"error '%s' sending str '%s' to %s@%s",
			Link_err_str(n), buffer,
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	/* now read response */
	DEBUG1("md5_verify: reading response");
	len = sizeof(input)-1;
	if( (n = Link_line_read(ShortRemote_FQDN,sock,
		Send_query_rw_timeout_DYN,input,&len) )){
		SNPRINTF(errmsg, errlen)
		"md5_verify: error reading challenge - '%s'", Link_err_str(n) );
		goto error;
	} else if( len == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: zero length response");
		goto error;
	} else if( len >= (int)sizeof( input) -2 ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: response too long");
		goto error;
	}
	DEBUG1("md5_verify: response '%s'", input );

	from = input;
	if( (s = strchr(input,' ')) ) *s++ = 0;
	if( s ){
		hash = s;
		if( strpbrk(hash,Whitespace) ){
			SNPRINTF(errmsg, errlen)
				"md5_verify: malformed response" );
			goto error;
		}
		n = strlen(hash);
		if( n == 0 || n%2 ){
			SNPRINTF(errmsg, errlen)
			"md5_verify: bad response hash length '%d'", n );
			goto error;
		}
	} else {
		SNPRINTF(errmsg, errlen)
			"md5_verify: no 'hash' in response" );
		goto error;
	}

	DEBUG1("md5_verify: from '%s', hash '%s', prefix '%s'",
		from, hash, buffer );

	if( (destkeylength = md5key( keyfile, from, keybuffer, sizeof(keybuffer),
		errmsg, errlen ) ) <= 0 ){
		goto error;
	}
	if( (s = strpbrk(keybuffer,Whitespace)) ){
		*s++ = 0;
		while( isspace(cval(s)) ) ++s;
	} else {
		s = keybuffer;
	}
	destkeylength = strlen(s);
	if( destkeylength > KEY_LENGTH ) destkeylength = KEY_LENGTH;
	memcpy( destkey, s, destkeylength );

	DEBUG1("md5_verify: challenge '%s'",
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));
	DEBUG1("md5_verify: destkey '%s'", 
		hexstr( destkey, KEY_LENGTH, buffer, sizeof(buffer) ));
	/* xor the challenge with the from key */
	n = 0;
	len = destkeylength;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^destkey[n]);
		n=(n+1)%len;
	}
	DEBUG1("md5_verify: challenge^destkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* now we xor the buffer with the key */
	len = strlen(input);
	DEBUG1("md5_verify: idstuff len %d '%s'", len, input );
	n = 0;
	for(i = 0; i < len; i++){
		challenge[n] = (challenge[n]^input[i]);
		n=(n+1)%KEY_LENGTH;
	}
	DEBUG1("md5_verify: result idstuff^challenge^destkey^idkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* now, MD5 hash the string */
	MDString(challenge, response, KEY_LENGTH, KEY_LENGTH);

	hexstr( response, KEY_LENGTH, buffer, sizeof(buffer) );

	DEBUG1("md5_verify: calculated hash '%s'", buffer );
	DEBUG1("md5_verify: sent hash '%s'", hash );

	if( strcmp( buffer, hash ) ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: bad response value");
		goto error;
	}
	
	DEBUG1("md5_verify: success, sending ACK" );

	if((n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN, "", 1, 0 )) ){
		/* keep the other end from trying to read */
		SNPRINTF(errmsg,errlen)
			"error '%s' sending ACK to %s@%s",
			Link_err_str(n),
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	DEBUG1("md5_verify: reading command" );

	len = sizeof(input)-1;
	n = Link_line_read(ShortRemote_FQDN,sock,
		Send_job_rw_timeout_DYN,input,&len);
	if( len >= 0 ) input[len] = 0;
	DEBUG1( "md5_verify: read status %d, len %d, '%s'", n, len, input );
	if( n ){
		SNPRINTF(errmsg,errlen)
			"error '%s' reading command from %s@%s",
			Link_err_str(n),
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	} else if( len == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: zero length command");
		goto error;
	} else if( len >= (int)sizeof( input) -2 ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: command too long");
		goto error;
	}
	if( len < 2 ){
		SNPRINTF(errmsg, errlen)
		"md5_verify: short request");
		goto error;
	}

	Perm_check.authfrom = user;

	Dispatch_input(sock,input);
	cleanup(0);

 error:
	if( errmsg[0] ){
		return(JFAIL);
	}
	return(JSUCC);
}

/**************************************************************
 *md5_send:
 *A simple implementation for testing user supplied authentication
 * The info line_list has the following fields
 * client=papowell      <- client id, can be forwarded
 * destination=test     <- destination ID - use for remote key
 * forward_id=test      <- forward_id from configuration/printcap
 * from=papowell        <- originator ID  - use for local key
 * id=test              <- id from configuration/printcap
 *
 * The start of the actual file has:
 * destination=test     <- destination ID (URL encoded)
 *                       (destination ID from above)
 * server=test          <- if originating from server, the server key (URL encoded)
 *                       (originator ID from above)
 * client=papowell      <- client id
 *                       (client ID from above)
 * input=%04t1          <- input that is 
 *   If you need to set a 'secure id' then you set the 'FROM'
 *
 **************************************************************/

int md5_send( int *sock, int transfer_timeout, char *tempfile,
	char *errmsg, int errlen,
	struct security *security, struct line_list *info )
{
	char destkey[KEY_LENGTH+1];
	char challenge[KEY_LENGTH+1];
	char response[KEY_LENGTH+1];
	char filehash[KEY_LENGTH+1];
	int destkeylength, i, n;
	char smallbuffer[SMALLBUFFER];
	char keybuffer[SMALLBUFFER];
	char *s, *dest;
	const char *keyfile;
	char buffer[LARGEBUFFER];
	struct stat statb;
	int tempfd = -1, len, ack;
	int status = 0;

	errmsg[0] = 0;
	if(DEBUGL1)Dump_line_list("md5_send: info", info );
	if( !Is_server ){
		/* we get the value of the MD5KEYFILE variable */
		keyfile = getenv("MD5KEYFILE");
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_send: no MD5KEYFILE environment variable" );
			goto error;
		}
	} else {
		keyfile = Find_exists_value( info, "server_keyfile", Value_sep );
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_send: no server_keyfile entry" );
			goto error;
		}
	}

	dest = Find_str_value( info, DESTINATION, Value_sep );
	if( dest == 0 ){
		SNPRINTF(errmsg, errlen)
			"md5_send: no '%s' value in info", DESTINATION );
		goto error;
	}
	if( (destkeylength = md5key( keyfile, dest, keybuffer,
		sizeof(keybuffer), errmsg, errlen ) ) <= 0 ){
		goto error;
	}
	if( (s = strpbrk(keybuffer,Whitespace)) ){
		*s++ = 0;
		while( (isspace(cval(s))) ) ++s;
		if( *s == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_send: no '%s' value in keyfile", dest );
			goto error;
		}
		dest = keybuffer;
	} else {
		s = keybuffer;
		dest = Find_str_value( info, FROM, Value_sep );
		if( !dest ){
			SNPRINTF(errmsg,errlen)
				"md5_send: no '%s' value in info", FROM );
			goto error;
		}
	}
	destkeylength = strlen(s);
	if( destkeylength > KEY_LENGTH ) destkeylength = KEY_LENGTH;
	memcpy( destkey, s, destkeylength );

	DEBUG1("md5_send: sending on socket %d", *sock );
	/* Read the challenge dest server */
	len = sizeof(buffer);
	if( (n = Link_line_read(ShortRemote_FQDN,sock,
		Send_query_rw_timeout_DYN,buffer,&len)) ){
		SNPRINTF(errmsg, errlen)
		"md5_send: error reading challenge - '%s'", Link_err_str(n) );
		goto error;
	} else if( len == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_send: zero length challenge");
		goto error;
	}
	DEBUG1("md5_send: challenge '%s'", buffer );
	n = strlen(buffer);
	if( n == 0 || n % 2 || n/2 > KEY_LENGTH ){
		SNPRINTF(errmsg, errlen)
		"md5_send: bad challenge length '%d'", strlen(buffer) );
		goto error;
	}
	memset(challenge, 0, sizeof(challenge));
	smallbuffer[2] = 0;
	for(i = 0; buffer[2*i] && i < KEY_LENGTH; ++i ){
		memcpy(smallbuffer,&buffer[2*i],2);
		challenge[i] = strtol(smallbuffer,0,16);
	}

	DEBUG1("md5_send: decoded challenge '%s'",
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));
	DEBUG1("md5_send: destkey '%s'", 
		hexstr( destkey, KEY_LENGTH, buffer, sizeof(buffer) ));
	/* xor the challenge with the dest key */
	n = 0;
	len = destkeylength;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^destkey[n]);
		n=(n+1)%len;
	}
	DEBUG1("md5_send: challenge^destkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	DEBUG1("md5_send: challenge^destkey^idkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	DEBUG1("md5_send: opening tempfile '%s'", tempfile );

	if( (tempfd = Checkread(tempfile,&statb)) < 0){
		SNPRINTF(errmsg, errlen)
			"md5_send: open '%s' for read failed - %s",
			tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	DEBUG1("md5_send: doing md5 of file");
	MDFile( tempfd, filehash, KEY_LENGTH);
	DEBUG1("md5_send: filehash '%s'", 
		hexstr( filehash, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* xor the challenge with the file key */
	n = 0;
	len = KEY_LENGTH;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^filehash[n]);
		n=(n+1)%len;
	}

	DEBUG1("md5_send: challenge^destkey^idkey^filehash '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	SNPRINTF( smallbuffer, sizeof(smallbuffer)) "%s", dest );

	/* now we xor the buffer with the key */
	len = strlen(smallbuffer);
	DEBUG1("md5_send: idstuff len %d '%s'", len, smallbuffer );
	n = 0;
	for(i = 0; i < len; i++){
		challenge[n] = (challenge[n]^smallbuffer[i]);
		n=(n+1)%KEY_LENGTH;
	}
	DEBUG1("md5_send: result challenge^destkey^idkey^filehash^idstuff '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* now, MD5 hash the string */
	MDString(challenge, response, KEY_LENGTH, KEY_LENGTH);

	/* return the response to the server */
	hexstr( response, KEY_LENGTH, buffer, sizeof(buffer) );
	n = strlen(smallbuffer);
	SNPRINTF(smallbuffer+n, sizeof(smallbuffer)-n-1) " %s", buffer );
	DEBUG1("md5_send: sending response '%s'", smallbuffer );
	safestrncat(smallbuffer,"\n");
	ack = 0;
	if( (n =  Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		smallbuffer, strlen(smallbuffer), &ack )) || ack ){
		/* keep the other end dest trying to read */
		if( (s = strchr(smallbuffer,'\n')) ) *s = 0;
		SNPRINTF(errmsg,errlen)
			"error '%s'\n sending str '%s' to %s@%s",
			Link_err_str(n), smallbuffer,
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	if( lseek( tempfd, 0, SEEK_SET ) < 0 ){
		SNPRINTF(errmsg,errlen)
			"md5_send: seek failed - '%s'", Errormsg(errno) );
		goto error;
	}

	DEBUG1("md5_send: starting transfer of file");
	while( (len = read( tempfd, buffer, sizeof(buffer)-1 )) > 0 ){
		buffer[len] = 0;
		DEBUG4("md5_send: file information '%s'", buffer );
		if( write( *sock, buffer, len) != len ){
			SNPRINTF(errmsg, errlen)
				"md5_send: write to socket failed - %s", Errormsg(errno) );
			status = JABORT;
			goto error;
		}
	}
	if( len < 0 ){
		SNPRINTF(errmsg, errlen)
			"md5_send: read dest '%s' failed - %s", tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	close(tempfd); tempfd = -1;
	/* we close the writing side */
	shutdown( *sock, 1 );

	DEBUG1("md5_send: sent file" );

	if( (tempfd = Checkwrite(tempfile,&statb,O_WRONLY|O_TRUNC,1,0)) < 0){
		SNPRINTF(errmsg, errlen)
			"md5_send: open '%s' for write failed - %s",
			tempfile, Errormsg(errno) );
		status = JABORT;
		goto error;
	}
	DEBUG1("md5_send: starting read of response");

	if( (len = read(*sock,buffer,1)) > 0 ){
		n = cval(buffer);
		DEBUG4("md5_send: response byte '%d'", n);
		status = n;
		if( isprint(n) && write(tempfd,buffer,1) != 1 ){
			SNPRINTF(errmsg, errlen)
				"md5_send: write to '%s' failed - %s", tempfile, Errormsg(errno) );
			status = JABORT;
			goto error;
		}
	}
	while( (len = read(*sock,buffer,sizeof(buffer)-1)) > 0 ){
		buffer[len] = 0;
		DEBUG4("md5_send: socket information '%s'", buffer);
		if( write(tempfd,buffer,len) != len ){
			SNPRINTF(errmsg, errlen)
				"md5_send: write to '%s' failed - %s", tempfile, Errormsg(errno) );
			status = JABORT;
			goto error;
		}
	}
	close( tempfd ); tempfd = -1;

 error:
	if( tempfd >= 0 ) close(tempfd); tempfd = -1;
	return(status);
}


int md5_receive( int *sock, char *user, char *jobsize, int dest_server,
	char *authtype, struct line_list *info,
	char *errmsg, int errlen, struct line_list *header_info, char *tempfile )
{
	char input[SMALLBUFFER];
	char buffer[LARGEBUFFER];
	char keybuffer[LARGEBUFFER];
	int destkeylength, i, n, len, tempfd = -1;
	char *s, *dest, *hash;
	const char *keyfile;
	char destkey[KEY_LENGTH+1];
	char challenge[KEY_LENGTH+1];
	char response[KEY_LENGTH+1];
	char filehash[KEY_LENGTH+1];
	struct stat statb;
	int status_error = 0;


	if(DEBUGL1)Dump_line_list("md5_receive: info", info );
	if(DEBUGL1)Dump_line_list("md5_receive: header_info", header_info );
	/* do validation and then write 0 */

	if( !Is_server ){
		SNPRINTF(errmsg, errlen)
			"md5_receive: not server" );
		goto error;
	} else {
		keyfile = Find_exists_value( info, "server_keyfile", Value_sep );
		if( keyfile == 0 ){
			SNPRINTF(errmsg, errlen)
				"md5_receive: no server_keyfile entry" );
			goto error;
		}
	}

	DEBUGF(DRECV1)("md5_receive: sending ACK" );
	if( (n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		"", 1, 0 )) ){
		SNPRINTF(errmsg,errlen)
			"error '%s' ACK to %s@%s",
			Link_err_str(n), RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}
	/* First, seed the random number generator */
	srand(time(NULL));

	/* Now, fill the challenge with 16 random values */
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = rand() >> 8;
	}
	hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) );
	DEBUGF(DRECV1)("md5_receive: sending challenge '%s'", buffer );
	safestrncat(buffer,"\n");

	/* Send the challenge to the client */

	if( (n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN,
		buffer, strlen(buffer), 0 )) ){
		/* keep the other end dest trying to read */
		if( (s = strchr(buffer,'\n')) ) *s = 0;
		SNPRINTF(errmsg,errlen)
			"error '%s' sending str '%s' to %s@%s",
			Link_err_str(n), buffer,
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	/* now read response */
	DEBUGF(DRECV1)("md5_receive: reading response");
	len = sizeof(input)-1;
	if( (n = Link_line_read(ShortRemote_FQDN,sock,
		Send_query_rw_timeout_DYN,input,&len) )){
		SNPRINTF(errmsg, errlen)
		"md5_receive: error reading challenge - '%s'", Link_err_str(n) );
		goto error;
	} else if( len == 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_receive: zero length response");
		goto error;
	} else if( len >= (int)sizeof( input) -2 ){
		SNPRINTF(errmsg, errlen)
		"md5_receive: response too long");
		goto error;
	}
	DEBUGF(DRECV1)("md5_receive: response '%s'", input );

	dest = input;
	if( (s = strchr(input,' ')) ) *s++ = 0;
	if( s ){
		hash = s;
		if( strpbrk(hash,Whitespace) ){
			SNPRINTF(errmsg, errlen)
				"md5_receive: malformed response" );
			goto error;
		}
		n = strlen(hash);
		if( n == 0 || n%2 ){
			SNPRINTF(errmsg, errlen)
			"md5_receive: bad response hash length '%d'", n );
			goto error;
		}
	} else {
		SNPRINTF(errmsg, errlen)
			"md5_receive: no 'hash' in response" );
		goto error;
	}


	DEBUGF(DRECV1)("md5_receive: dest '%s', hash '%s', prefix '%s'",
		dest, hash, buffer );
	if( (destkeylength = md5key( keyfile, dest, keybuffer, KEY_LENGTH, errmsg, errlen ) ) <= 0 ){
		goto error;
	}
	if( (s = strpbrk(keybuffer,Whitespace)) ){
		*s++ = 0;
		while( isspace(cval(s))) ++s;
	} else {
		s = keybuffer;
	}
	destkeylength = strlen(s);
	if( destkeylength > KEY_LENGTH ) destkeylength = KEY_LENGTH;
	memcpy(destkey,s,destkeylength);

	
	DEBUGF(DRECV1)("md5_receive: success, sending ACK" );

	if((n = Link_send( RemoteHost_DYN, sock, Send_query_rw_timeout_DYN, "", 1, 0 )) ){
		/* keep the other end dest trying to read */
		SNPRINTF(errmsg,errlen)
			"error '%s' sending ACK to %s@%s",
			Link_err_str(n),
			RemotePrinter_DYN, RemoteHost_DYN );
		goto error;
	}

	DEBUGF(DRECV1)("md5_receive: reading file" );

	/* open a file for the output */
	if( (tempfd = Checkwrite(tempfile,&statb,O_WRONLY|O_TRUNC,1,0)) < 0 ){
		SNPRINTF(errmsg, errlen)
			"md5_receive: reopen of '%s' for write failed",
			tempfile );
	}

	DEBUGF(DRECV1)("md5_receive: starting read dest socket %d", *sock );
	while( (n = read(*sock, buffer,sizeof(buffer)-1)) > 0 ){
		buffer[n] = 0;
		DEBUGF(DRECV4)("md5_receive: remote read '%d' '%s'", n, buffer );
		if( write( tempfd,buffer,n ) != n ){
			SNPRINTF(errmsg, errlen)
				"md5_receive: bad write to '%s' - '%s'",
				tempfile, Errormsg(errno) );
			goto error;
		}
	}

	if( n < 0 ){
		SNPRINTF(errmsg, errlen)
		"md5_receive: bad read '%d' reading file ", n );
		goto error;
	}
	close(tempfd); tempfd = -1;
	DEBUGF(DRECV4)("md5_receive: end read" );

	DEBUG1("md5_receive: opening tempfile '%s'", tempfile );

	if( (tempfd = Checkread(tempfile,&statb)) < 0){
		SNPRINTF(errmsg, errlen)
			"md5_receive: open '%s' for read failed - %s",
			tempfile, Errormsg(errno) );
		goto error;
	}
	DEBUG1("md5_receive: doing md5 of file");
	MDFile( tempfd, filehash, KEY_LENGTH);
	DEBUG1("md5_receive: filehash '%s'", 
		hexstr( filehash, KEY_LENGTH, buffer, sizeof(buffer) ));
	close(tempfd); tempfd = -1;

	DEBUGF(DRECV1)("md5_receive: challenge '%s'",
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));
	DEBUGF(DRECV1)("md5_receive: destkey '%s'", 
		hexstr( destkey, KEY_LENGTH, buffer, sizeof(buffer) ));
	/* xor the challenge with the dest key */
	n = 0;
	len = destkeylength;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^destkey[n]);
		n=(n+1)%len;
	}
	DEBUGF(DRECV1)("md5_receive: challenge^destkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	DEBUGF(DRECV1)("md5_receive: challenge^destkey^idkey '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));


	DEBUGF(DRECV1)("md5_receive: filehash '%s'", 
		hexstr( filehash, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* xor the challenge with the file key */
	n = 0;
	len = KEY_LENGTH;
	for(i = 0; i < KEY_LENGTH; i++){
		challenge[i] = (challenge[i]^filehash[n]);
		n=(n+1)%len;
	}
	DEBUGF(DRECV1)("md5_receive: challenge^destkey^idkey^filehash '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));


	/* now we xor the buffer with the key */
	len = strlen(input);
	DEBUGF(DRECV1)("md5_receive: idstuff len %d '%s'", len, input );
	n = 0;
	for(i = 0; i < len; i++){
		challenge[n] = (challenge[n]^input[i]);
		n=(n+1)%KEY_LENGTH;
	}
	DEBUGF(DRECV1)("md5_receive: result challenge^destkey^idkey^filehash^deststuff '%s'", 
		hexstr( challenge, KEY_LENGTH, buffer, sizeof(buffer) ));

	/* now, MD5 hash the string */
	MDString(challenge, response, KEY_LENGTH, KEY_LENGTH);

	hexstr( response, KEY_LENGTH, buffer, sizeof(buffer) );

	DEBUGF(DRECV1)("md5_receive: calculated hash '%s'", buffer );
	DEBUGF(DRECV1)("md5_receive: sent hash '%s'", hash );

	if( strcmp( buffer, hash ) ){
		SNPRINTF(errmsg, errlen)
		"md5_receive: bad response value");
		goto error;
	}
	
	DEBUGF(DRECV1)("md5_receive: success" );
	Set_str_value(header_info,FROM,dest);
	status_error = Do_secure_work( jobsize, dest_server, tempfile, header_info );
	DEBUGF(DRECV1)("md5_receive: Do_secure_work returned %d", status_error );

	/* we now have the encoded output */
	if( (tempfd = Checkread(tempfile,&statb)) < 0 ){
		SNPRINTF( errmsg, errlen)
			"md5_receive: reopen of '%s' for read failed - %s",
			tempfile, Errormsg(errno) );
		goto error;
	}
	len = statb.st_size;
	DEBUGF(DRECV1)( "md5_receive: return status encoded size %0.0f",
		len);
	if( len || status_error ){
		buffer[0] = ACK_FAIL;
		write( *sock,buffer,1 );
	}
	while( (n = read(tempfd, buffer,sizeof(buffer)-1)) > 0 ){
		buffer[n] = 0;
		DEBUGF(DRECV4)("md5_receive: sending '%d' '%s'", n, buffer );
		if( write( *sock,buffer,n ) != n ){
			SNPRINTF( errmsg, errlen)
				"md5_receive: bad write to socket - '%s'",
				Errormsg(errno) );
			goto error;
		}
	}
	if( n < 0 ){
		SNPRINTF( errmsg, errlen)
			"md5_receive: read '%s' failed - %s",
			tempfile, Errormsg(errno) );
		goto error;
	}

	return( 0 );


 error:
	return(JFAIL);
}

 struct security SecuritySupported[] = {
	/* name, config_name, flags,
        client  connect, send, send_done
		server  accept, receive, receive_done
	*/
#if defined(KERBEROS)
# if defined(MIT_KERBEROS4)
	{ "kerberos4", "kerberos", SEC_AUTH_SOCKET },
# endif
	{ "kerberos*", "kerberos", 0,
		Krb5_connect, Krb5_send, Krb5_get_reply, Keb5_rcv_done,
		Krb5_accept, Krb5_recv, Krb5_send, Keb5_send_done,
		 },
#endif
	{ "test", "test", SEC_AUTH_EXCHANGE,
		Test_connect,0,0,
		Test_accept,0,0
		},
#if 0
	{ "pgp",       "pgp",   0, 0, Pgp_receive, },
	                                 /* name, config_tag, connect,  transfer,  receive */
/* for connect/verify - USER_SEND/RECEIVE    { "md5", "md5",   md5_connect, 0          md5_verify }, */
/* for send/receive   - USER_SEND/RECIEVE    { "md5", "md5",   0,           md5_send,  md5_receive}, */
/*	{ "test", "test", Test_connect,0,        Test_verify }, */
/*	{ "test", "test", 0,           Test_send,Text_receive  },  */

#define USER_SEND \
	{ "md5", "md5",   0,           md5_send,  md5_receive },
	
#define USER_RECEIVE \
	{ "md5", "md5",   0,           md5_send,  md5_receive },

#endif
	{0,0,0,
		0,0,0,
		0,0,0}
};