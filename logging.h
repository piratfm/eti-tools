/*
 * logging.h
 *
 *  Created on: 27 янв. 2011
 *      Author: tipok
 */

#ifndef LOGGING_H_
#define LOGGING_H_

#define MAX_MSG 1024
extern int verbosity;

void msg_Dbg( void *_unused, const char *psz_format, ... );
void msg_Log(const char *psz_format, ...);
void msg_Dump(char *bytes, int len);

#endif /* LOGGING_H_ */
