
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include "ts.h"

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
    int offset=12, pid=0x0;
    int i_last_cc = -1;

    static const struct option long_options[] = {
        { "pid",           required_argument,  NULL, 'p' },
        { "offset (-3:only TS-sync byte; 12:pid mode)", required_argument, NULL, 's' },
        { "input",         required_argument,  NULL, 'i' },
        { "output",        required_argument,  NULL, 'o' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(i_argc, ppsz_argv, "p:s:i:o:h", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'p':
            pid=strtoul(optarg, NULL, 0);
            if(pid >= 8192) {
                ERROR("bad pid value: %d!", pid);
                exit(1);
            }
            break;

        case 's':
            offset=strtol(optarg, NULL, 0);
            if(offset != -3 && offset != 12) {
                ERROR("bad offset value: %d!", offset);
                exit(1);
            }
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


    if (offset == -3) {
      INFO("Encapsulation mode: only 0x47 TS-Sync byte at each 188 bytes");
      pid = 0;
    } else if (offset == 12) {
      INFO("Encapsulation mode: payload over pid 0x%04x (%d)", pid, pid);
    }
    offset += 4;
    unsigned long int packets=0;
    while (!feof(inputfile) && !ferror(inputfile)) {
        uint8_t p_ts[TS_SIZE];
        if (offset == 1) {
                p_ts[0] = 0x47;
        } else {
                ts_pad(p_ts);
        }
        size_t i_ret = fread(p_ts+offset, TS_SIZE - offset, 1, inputfile);
        if (i_ret != 1) {
        	WARN("Can't read input ts");
        	break;
        }

        if(offset >= 16) {
                ts_set_pid(p_ts, pid);
                ts_set_cc(p_ts, ++i_last_cc);
        }

      	size_t o_ret = fwrite(p_ts, TS_SIZE, 1, outputfile);
        if (o_ret != 1) {
       	        WARN("Can't write output ts");
       	        break;
        }
        packets++;
    }

    if(packets){
    	INFO("Successfully writed %ld ts-packets", packets);
    }


//mainErr:
    fclose(inputfile);
    fclose(outputfile);
    return 0;
}
