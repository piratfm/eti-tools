/* 

	parse_config.c
	(C) Nicholas J Humfrey <njh@aelius.com> 2006.
	
	Copyright notice:
	
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
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    
*/

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

/* socket programming */
#include <netdb.h>

#include "ni2http.h"

#ifdef HAVE_ZMQ
#include <zmq.h>
#endif

static void process_statement_server( char* name, char* value, int line_num )
{

	if (strcmp( "host", name ) == 0) {
		strncpy( ni2http_server.host, value, STR_BUF_SIZE);
	} else if (strcmp( "port", name ) == 0) {
		ni2http_server.port = atoi( value );
	} else if (strcmp( "user", name ) == 0) { 
		strncpy( ni2http_server.user, value, STR_BUF_SIZE);
	} else if (strcmp( "password", name ) == 0) { 
		strncpy( ni2http_server.password, value, STR_BUF_SIZE);
	} else if (strcmp( "protocol", name ) == 0) {
		if (strcmp( "http", value )==0 || strcmp( "icecast2", value )==0) {
			ni2http_server.protocol = SHOUT_PROTOCOL_HTTP;
		} else if (strcmp( "xaudiocast", value )==0 || strcmp( "icecast1", value )==0) {
			ni2http_server.protocol = SHOUT_PROTOCOL_XAUDIOCAST;
		} else if (strcmp( "icq", value )==0 || strcmp( "shoutcast", value )==0) {
			ni2http_server.protocol = SHOUT_PROTOCOL_ICY;
		} else {
			fprintf(stderr, "Error parsing configuation line %d: invalid protocol.\n", line_num);
			exit(-1);
		}
	} else {
		fprintf(stderr, "Error parsing configuation line %d: invalid statement in section 'server'.\n", line_num);
		exit(-1);
	}
	
}

static void process_statement_channel( char* name, char* value, int line_num )
{
	ni2http_channel_t *chan =  channels[ channel_count-1 ];
	
	if (strcmp( "name", name ) == 0) {
		strncpy( chan->name, value, STR_BUF_SIZE);
		
	} else if (strcmp( "mount", name ) == 0) { 
		strncpy( chan->mount, value, STR_BUF_SIZE);
		
	} else if (strcmp( "file", name ) == 0) {
		strncpy( chan->file_name, value, STR_BUF_SIZE);
#ifdef HAVE_ZMQ
	} else if (strcmp( "zmq", name ) == 0) {
		//strncpy( chan->zmq_uri, value, STR_BUF_SIZE);
		if(!zmq_context)
			zmq_context = zmq_ctx_new();
		chan->zmq_sock = zmq_socket(zmq_context, ZMQ_PUB);
		fprintf(stderr, "Connecting ZMQ to %s\n", value);
		if (zmq_connect(chan->zmq_sock, value) != 0) {
			fprintf(stderr, "Error occurred during zmq_connect: %s\n", zmq_strerror(errno));
		}
#endif
	} else if (strcmp( "sid", name ) == 0) {
	
		// Check PID is valid
		//chan->sid = atoi( value );
		chan->sid = strtoul(value, NULL, 0);
		if (chan->sid == 0) {
			fprintf(stderr,"Error parsing configuation line %d: invalid SID\n", line_num);
			exit(-1);
		}
#if 0
		// Add channel to the channel map
		if( channel_map[ chan->pid ] ) {
			fprintf(stderr,"Error parsing configuation line %d: duplicate PID\n", line_num);
			exit(-1);
		} else {
			channel_map[ chan->pid ] = chan;
		}
#endif
	} else if (strcmp( "genre", name ) == 0) { 
		strncpy( chan->genre, value, STR_BUF_SIZE);
	
	} else if (strcmp( "public", name ) == 0) { 
		chan->is_public = atoi( value );

	} else if (strcmp( "description", name ) == 0) { 
		strncpy( chan->description, value, STR_BUF_SIZE);

	} else if (strcmp( "url", name ) == 0) { 
		strncpy( chan->url, value, STR_BUF_SIZE);

	} else if (strcmp( "extract_pad", name ) == 0) {
		chan->extract_pad = atoi( value );
	} else if (strcmp( "extract_dabplus", name ) == 0) {
		chan->extract_dabplus = atoi( value );

	} else {
		fprintf(stderr, "Error parsing configuation line %d: invalid statement in section 'channel'.\n", line_num);
		exit(-1);
	}
	
}

int parse_config( char *filepath )
{

	FILE* file = NULL;
	char line[STR_BUF_SIZE];
	char section[STR_BUF_SIZE];
	char* ptr;
	int i, line_num=0;
	
	// Initialize strings
	line[0] = '\0';
	section[0] = '\0';
	
	
	// Open the input file
	file = fopen( filepath, "r" );
	if (file==NULL) {
		perror("Failed to open config file");
		exit(-1);
	}
	
	
	// Parse it line by line
	while( !feof( file ) ) {
		line_num++;
		
		if(!fgets( line, STR_BUF_SIZE, file )) break;
		
		// Ignore lines starting with a #
		if (line[0] == '#') continue;
		
		// Remove newline from end
		if (line[strlen(line)-1] == '\n')
			line[strlen(line)-1] = '\0';

		// Ignore empty lines
		if (strlen(line) == 0) continue;
		
		// Is it the start of a section?
		if (line[0]=='[') {
			ptr = &line[1];
			for(i=0; i<strlen(ptr); i++) {
				if (ptr[i] == ']') ptr[i] = '\0';
			}
			
			if (strcmp( ptr, "server")==0) {
				strcpy( section, ptr );
				
			} else if (strcmp( ptr, "tuning")==0) {
				strcpy( section, ptr );
			
			} else if (strcmp( ptr, "channel")==0) {
				ni2http_channel_t* chan=NULL;
				
				if (channel_count >= MAX_CHANNEL_COUNT) {
					fprintf(stderr, "Error: reached maximum number of channels allowed at line %d\n", line_num);
					return 1;
				}
				
				// Allocate memory for channel structure
				chan = calloc( 1, sizeof(ni2http_channel_t) );
				if (!chan) {
					perror("Failed to allocate memory for new channel");
					exit(-1);
				}

				chan->num = channel_count;
				chan->shout = NULL;
				chan->file = NULL;
				chan->zmq_sock = NULL;
				chan->file_name[0] = '\0';
				chan->extract_dabplus = 1;
				chan->extract_pad = 1;

				
				strcpy( section, ptr );
				channels[ channel_count ] = chan;
				channel_count++;
				
			} else {
				fprintf(stderr, "Error parsing configuation line %d: unknown section '%s'\n", line_num, ptr);
				exit(-1);
			}
			
			
			
		} else {
			char* name = line;
			char* value = NULL;
			
			// Split up the name and value
			for(i=0; i<strlen(name); i++) {
				if (name[i] == ':') {
					name[i] = '\0';
					value = &name[i+1];
					while(value[0] == ' ' || value[0] == '\x09' ) { value++; }
					if (strlen(value)==0) { value=NULL; }
				}
			}
			
			//fprintf(stderr, "%s: %s=%s.\n", section, name, value);
			
			// Ignore empty values
			if (value==NULL) continue;
			
			if (strcmp( section, "server")==0) {	
				process_statement_server( name, value, line_num );
			} else if (strcmp( section, "channel")==0) {	
				process_statement_channel( name, value, line_num );
			} else {
				fprintf(stderr, "Error parsing configuation line %d: missing section.\n", line_num);
				exit(-1);
			}

		}
	}
	

	fclose( file );
	
	return 0;
}

