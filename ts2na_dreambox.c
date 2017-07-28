
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ts.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ost/dmx.h>

#include "tune.h"

dvbshout_tuning_t *dvbshout_tuning = NULL;

#define PACKET_SIZE 1448

int setPesFilter (uint16_t pid, char *demuxFile)
{
	int fd;
	struct dmxPesFilterParams flt;

	if ((fd = open(demuxFile, O_RDWR)) < 0)
		return -1;

	if (ioctl(fd, DMX_SET_BUFFER_SIZE, 1024 * 1024) < 0)
		return -1;

	flt.pid = pid;
	flt.input = DMX_IN_FRONTEND;
	flt.output = DMX_OUT_TS_TAP;
	flt.pesType = DMX_PES_OTHER;
	flt.flags = 0;

	if (ioctl(fd, DMX_SET_PES_FILTER, &flt) < 0)
		return -1;

	if (ioctl(fd, DMX_START, 0) < 0)
		return -1;

	return fd;
}

#define DEBUG(format, ...) fprintf (stderr, "DEBUG: "format"\n", ## __VA_ARGS__)
#define  INFO(format, ...) fprintf (stderr, "INFO:  "format"\n", ## __VA_ARGS__)
#define WARN(format, ...)  fprintf (stderr, "WARN:  "format"\n", ## __VA_ARGS__)
#define ERROR(format, ...) fprintf (stderr, "ERROR: "format"\n", ## __VA_ARGS__)


/*****************************************************************************
 * Main loop
 *****************************************************************************/
static void usage(const char *psz)
{
    fprintf(stderr, "usage: %s [-f freq] [-r rate] [-l pol] [-p pid] [-s offset] [-i <inputfile>] [-o <outputfile>]\nfor DVB input - use /dev/dvb/card0 as input.\n", psz);
    exit(EXIT_FAILURE);
}
    
int main(int i_argc, char **ppsz_argv)
{   
	char inputFileDVR[256] = "";
	char inputFileDEMUX[256] = "";
	int DVRfd = -1, DEMUXfd = -1;
	int use_dvb=0;

    int c;
    FILE *inputfile=stdin;
    FILE *outputfile=stdout;
    uint16_t offset=12, pid=0x0426;
    int i_last_cc = -1;
    int freq = 12111;
    int symbolrate = 27500;
    char pol = 'h';

    
    static const struct option long_options[] = {
        { "frequency",           required_argument, NULL, 'f' },
        { "symbolrate",           required_argument, NULL, 'r' },
        { "polarisation",           required_argument, NULL, 'l' },
        { "pid",           required_argument, NULL, 'p' },
        { "offset",            required_argument,       NULL, 's' },
        { "input",         required_argument,       NULL, 'i' },
        { "output",          required_argument,       NULL, 'o' },
        { 0, 0, 0, 0 }
    };
    
    while ((c = getopt_long(i_argc, ppsz_argv, "p:s:i:o:f:r:l:h", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'f':
            freq=strtoul(optarg, NULL, 0);
            break;
        case 'r':
            symbolrate=strtoul(optarg, NULL, 0);
            break;
        case 'l':
            pol=optarg[0];
            break;

        case 'p':
            pid=strtoul(optarg, NULL, 0);
            break;

        case 's':
            offset=strtoul(optarg, NULL, 0);
            break;

        case 'i':
        	if(!strncmp("/dev/dvb/card", optarg, 13) || !strncmp("/dev/dvb/adapter", optarg, 16)) {
        		strcpy(inputFileDVR, optarg);
        		strcpy(inputFileDEMUX, optarg);
        		strcat(inputFileDVR, "/dvr1");
        		strcat(inputFileDEMUX, "/demux1");
        		if ((DVRfd = open(inputFileDVR, O_RDONLY)) < 0) {
        			ERROR("cant open input file DVR!");
        			return EXIT_FAILURE;
                        }
        		use_dvb=1;

        	} else {
        		inputfile = fopen(optarg, "r");
        		if(!inputfile) {
        			ERROR("cant open input file!");
        			exit(1);
        		}
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
    if(use_dvb) {
        DEMUXfd = setPesFilter(pid, inputFileDEMUX);

    }

    unsigned long long packets=0;

    while (use_dvb || (!feof(inputfile) && !ferror(inputfile))) {
        uint8_t p_ts[TS_SIZE];

        if(use_dvb) {
        	int readed = 0;
        	int exit_flag=0;
        	while ((!exit_flag) && (readed < TS_SIZE)) {

				int r = read(DVRfd, p_ts + readed, TS_SIZE - readed);

				switch (r) {
				case -1:
					exit_flag = 1;
					r = 0;
					break;

				case 0:
					continue;

				default:
					readed += r;
					break;
				}

		        if (readed == TS_SIZE && !ts_validate(p_ts)) {
		            WARN("Invalid ts");
		            memmove(p_ts, p_ts+1, TS_SIZE - 1);
		            readed--;
		            continue;
		        }

			}
        } else {
        	size_t i_ret = fread(p_ts, TS_SIZE, 1, inputfile);
        	if (i_ret != 1) {
        		WARN("Can't read input ts");
        		break;
        	}
        }

        if (!ts_validate(p_ts)) {
            WARN("Invalid ts");
            continue;
        }

        if(ts_get_transporterror(p_ts))
            WARN("Transport error");

        if(ts_get_transportpriority(p_ts))
            WARN("Transport priority");

        uint16_t i_pid = ts_get_pid(p_ts);
        if(i_pid!=pid)
            continue;

        if(!ts_has_payload(p_ts)) {
            WARN("No payload");
            continue;
        }

        //WARN("TS CC: %u", ts_get_cc(p_ts));
        if(i_last_cc >= 0 && (0x0f & (i_last_cc+1)) != ts_get_cc(p_ts)) {
            if(i_last_cc == ts_get_cc(p_ts)){
                WARN("cc is eq");
                continue;
            }
            if( i_last_cc == (0x0f & (ts_get_cc(p_ts)+1))) {
                WARN("cc is +1");
                continue;
            }
            WARN("TS Discontinuity: %u != %u", (0x0f & (i_last_cc+1)), ts_get_cc(p_ts));
        }

        if(ts_has_adaptation(p_ts))
            WARN("Transport has adaptation");

        i_last_cc = ts_get_cc(p_ts);
        uint8_t *payload = ts_payload(p_ts);
        if(offset) {
            payload+=offset;
        }

        if(p_ts+TS_SIZE < payload) {
            ERROR("payload is out of ts by %ld bytes", payload-p_ts+TS_SIZE);
            break;
        }

        if(!use_dvb || packets > 100) {
        size_t o_ret = fwrite(payload, p_ts+TS_SIZE-payload, 1, outputfile);
        if (o_ret != 1) {
            WARN("Can't write output ts");
            break;
        }
        }
        packets++;
    }

    if(packets){
    	INFO("Successfully read %ld ts-packets", packets);
    }


//mainErr:
    if(use_dvb) {
    	close(DVRfd);
    	close(DEMUXfd);
    } else {
    	fclose(inputfile);
    }
    fclose(outputfile);
    return 0;
}
