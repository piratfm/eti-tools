
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <bitstream/mpeg/ts.h>

#define DEBUG(format, ...) fprintf (stderr, "DEBUG: "format"\n", ## __VA_ARGS__)
#define  INFO(format, ...) fprintf (stderr, "INFO:  "format"\n", ## __VA_ARGS__)
#define WARN(format, ...)  fprintf (stderr, "WARN:  "format"\n", ## __VA_ARGS__)
#define ERROR(format, ...) fprintf (stderr, "ERROR: "format"\n", ## __VA_ARGS__)


/*****************************************************************************
 * Main loop
 *****************************************************************************/
static void usage(const char *psz)
{
    fprintf(stderr, "usage: %s [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]\n", psz);
    exit(EXIT_FAILURE);
}
    
int main(int i_argc, char **ppsz_argv)
{   
    int c;
    FILE *inputfile=stdin;
    FILE *outputfile=stdout;
    uint16_t offset=12, pid=0x0426;
    int i_last_cc = -1;
    
    static const struct option long_options[] = {
        { "pid",           required_argument, NULL, 'p' },
        { "offset",            required_argument,       NULL, 's' },
        { "input",         required_argument,       NULL, 'i' },
        { "output",          required_argument,       NULL, 'o' },
        { 0, 0, 0, 0 }
    };
    
    while ((c = getopt_long(i_argc, ppsz_argv, "p:s:i:o:h", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'p':
            pid=strtoul(optarg, NULL, 0);
            break;

        case 's':
            offset=strtoul(optarg, NULL, 0);
            break;

        case 'i':
            inputfile = fopen(optarg, "r");
            if(!inputfile) {
                ERROR("cant open input file!");
                exit(1);
            }
            break;

        case 'o':
            outputfile = fopen(optarg, "w");
            if(!outputfile) {
                ERROR("cant open output file!");
                exit(1);
            }
            break;


        case 'h':
        default:
            usage(ppsz_argv[0]);
        }
    }

    INFO("Using pid: 0x%04x (%d)", pid, pid);
    unsigned long int packets=0;

    while (!feof(inputfile) && !ferror(inputfile)) {
        uint8_t p_ts[TS_SIZE];
        size_t i_ret = fread(p_ts, sizeof(p_ts), 1, inputfile);
        if (i_ret != 1) {
        	WARN("Can't read input ts");
        	break;
        }
        if (ts_validate(p_ts)) {
            uint16_t i_pid = ts_get_pid(p_ts);
            if(i_pid==pid) {
            	if(i_last_cc > 0 && (0x0f & (i_last_cc+1)) != ts_get_cc(p_ts)) {
            		WARN("TS Discontinuity");
            	}
            	i_last_cc = ts_get_cc(p_ts);
            	uint8_t *payload = ts_payload(p_ts);
            	if(offset) {
            		payload+=offset;
            	}

            	if(p_ts+TS_SIZE < payload) {
                	ERROR("payload is out of ts by %ld bytes", payload-p_ts+TS_SIZE);
                	break;
            	}

            	size_t o_ret = fwrite(payload, p_ts+TS_SIZE-payload, 1, outputfile);
                if (o_ret != 1) {
                	WARN("Can't write output ts");
                	break;
                }
                packets++;
            }
        }
    }

    if(packets){
    	INFO("Successfuly readed %ld ts-packets", packets);
    }


//mainErr:
    fclose(inputfile);
    fclose(outputfile);
    return 0;
}
