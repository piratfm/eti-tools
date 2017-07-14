/*
 * logging.c
 *
 *  Created on: 27 янв. 2011
 *      Author: tipok
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

#include "logging.h"

int verbosity = 1;

/*****************************************************************************
 * msg_Dbg
 *****************************************************************************/
void msg_Dbg( void *_unused, const char *psz_format, ... )
{
    if (verbosity > 2)
    {
        va_list args;
        char psz_fmt[MAX_MSG];
        va_start( args, psz_format );

        snprintf( psz_fmt, MAX_MSG, "debug: %s\n", psz_format );
        vfprintf( stderr, psz_fmt, args );
    }
}


/*****************************************************************************
 * msg_Log
 *****************************************************************************/
void msg_Log(const char *psz_format, ...)
{
	va_list args;
	time_t rawtime;
	struct tm * timeinfo;
	char psz_fmt[MAX_MSG];
	int size = 0;
	va_start( args, psz_format );

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	size = strftime(psz_fmt, MAX_MSG, "\x08[%x %X]", timeinfo);
	snprintf(psz_fmt + size, MAX_MSG - size, " %s", psz_format);
	vfprintf(stderr, psz_fmt, args);
	fprintf(stderr, "\n");
}




void msg_Dump(char *bytes, int len)
{
    int i;
    int count;
    int done = 0;
    fprintf(stderr, "dump %d bytes:\n", len);
    while (len > done) {
		if (len-done > 32){
			count = 32;
		} else {
			count = len-done;
		}

		fprintf(stderr, "\t\t\t\t");

		for (i=0; i<count; i++) {
	    	fprintf(stderr, "%02x ", (int)((unsigned char)bytes[done+i]));
		}

		for (; i<32; i++) {
	    	fprintf(stderr, "   ");
		}


		fprintf(stderr, "\t\"");

        for (i=0; i<count; i++) {
	    	fprintf(stderr, "%c", isprint(bytes[done+i]) ? bytes[done+i] : '.');
        }

        for (; i<32; i++) {
	    	fprintf(stderr, " ");
		}

		fprintf(stderr, "\"\n");
    	done += count;
    }
}
