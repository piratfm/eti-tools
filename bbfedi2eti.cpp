/*
 * fedi2eti.c
 * Uses parts of astra-sm and edi2eti.c
 *
 * Created on: 01.06.2018
 *     Author: athoik
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

/*
 * edi-tools edi2eti.c
 */
#include <arpa/inet.h>
extern "C" {
#include "edi_parser.h"
}
#include "network.h"
#include "logging.h"

#include<stdio.h>
#include<unistd.h>
#include<iostream>
#include<cstring>

extern char *optarg;
extern int optind, opterr, optopt;

bool has_src_ip=false;
bool has_dst_ip=false;
bool has_src_port=false;
bool has_dst_port=false;
bool has_mis=false;

unsigned char src_ip[4];
unsigned char dst_ip[4];
unsigned char src_port[4];
unsigned char dst_port[4];
unsigned char mis;

static
void write_file(void *privData, void *etiData, int etiLen)
{
    FILE *fh = (FILE *) privData;
    fwrite(etiData, 1, etiLen, fh);
}

using namespace std;

istream* inbuf;
ostream* outbuf;
char indata[7268];
int active=0;
unsigned char** fragmentor;
unsigned int fragmentorLength[256];
unsigned int fragmentorPos[256];
edi_handler_t *edi_p;

typedef struct {
	unsigned char MaType1;
	unsigned char MaType2;
	unsigned char Upl1;
	unsigned char Upl2;
	unsigned char Dfl1;
	unsigned char Dfl2;
	char Sync;
	char SyncD1;
	char SyncD2;
	char Crc8;
} bbheader;

struct layer3{
	unsigned char L3Sync;
	/*unsigned char AcmCommand;
	unsigned char CNI;
	unsigned char PlFrameId;*/
	bbheader header;
	unsigned char payload[7264-10];
};

bool isSelected(unsigned char* buf)
{
	if(has_src_ip && (!(buf[0]==src_ip[0] && buf[1]==src_ip[1] && buf[2]==src_ip[2] && buf[3]==src_ip[3])))
		return false;
	if(has_dst_ip && (!(buf[4]==dst_ip[0] && buf[5]==dst_ip[1] && buf[6]==dst_ip[2] && buf[7]==dst_ip[3])))
		return false;
	if(has_src_port &&(!(buf[0x8]==src_port[0] && buf[0x9]==src_port[1])))
		return false;
	if(has_dst_port &&(!(buf[0xa]==dst_port[0] && buf[0xb]==dst_port[1])))
		return false;
        return true;
}


bool processBBframe(unsigned char* payload, unsigned int gseLength)
{
			//fprintf(stderr, "GSELength:%x\n", gseLength);
			unsigned int offset=0;
			unsigned int fragID=0;
			//START=1 STOP=0
			if((payload[0]&0xC0)==0x80) {
				fragID=payload[2];
				unsigned int length=(payload[3]<<8) | payload[4];
				if(fragmentor[fragID]!=0)
					delete fragmentor[fragID];
				fragmentor[fragID]=new unsigned char[length+2];
				fragmentorLength[fragID]=length;
				fragmentorPos[fragID]=0;
				fragmentor[fragID][0]=payload[0];
				fragmentor[fragID][1]=payload[1];
				//SET START=1 STOP=1
				fragmentor[fragID][0]|=0xC0;
				memcpy(&fragmentor[fragID][2], &payload[5], gseLength-3);
				fragmentorPos[fragID]+=gseLength-1;
			}
			//START=0 STOP=0
			else if((payload[0]&0xC0)==0x00) {
				fragID=payload[2];
				if(fragmentor[fragID]==0)
					return true;
				memcpy(&fragmentor[fragID][fragmentorPos[fragID]], &payload[3], gseLength-1);
				fragmentorPos[fragID]+=gseLength-1;
			}
			//START=0 STOP=1
			else if((payload[0]&0xC0)==0x40) {
				fragID=payload[2];
				if(fragmentor[fragID]==0)
					return true;
				memcpy(&fragmentor[fragID][fragmentorPos[fragID]], &payload[3], gseLength-1);
				fragmentorPos[fragID]+=gseLength-1;
				processBBframe(fragmentor[fragID],fragmentorLength[fragID]);
				delete fragmentor[fragID];
				fragmentor[fragID]=0;

			}
			//START=1 STOP=1
			else if((payload[0]&0xC0)==0xC0) {
				if(payload[offset+2]==0x00 && payload[offset+3]==0x04) {
					//LABEL
					if((payload[0]&0x30)==0x01) {
						offset += 3;
					}
					else if((payload[0]&0x30)==0x00) {
						offset += 6;
					}
					//fprintf(stderr, "Start of 00 04 packet:%02x %02x %02x %02x %02x\n",payload[6], payload[7], payload[8], payload[9], payload[10]);
					if(payload[6]&0x80) {
						offset+=3;
						offset += 2;
						offset += 0x10;
						if(isSelected(&payload[offset])) {
								offset +=0x10;
								active=payload[7];
							}
							else { 
								active=0;
								return true;
							}
						}
					else if(active==payload[7]) {
						offset+=9;
					}
					else {
						return true;
					}
				}
				else if(payload[offset+2]==0x00 && payload[offset+3]==0x00) {
					//LABEL
					if((payload[0]&0x30)==0x01) {
						offset += 3;
					}
					else if((payload[0]&0x30)==0x00) {
						offset += 6;
					}
					offset += 2;
					offset += 0x10;
					if(isSelected(&payload[offset])) {
						offset+=0x10;
					}
					else return true;
				}	
				else if(payload[offset+2]==0x08 && payload[offset+3]==0x00) {
					if((payload[0]&0x30)==0x01) {
						offset += 3;
					}
					else if((payload[0]&0x30)==0x00) {
						offset += 6;
					}
					offset += 0x10;
					if(isSelected(&payload[offset])) {
						offset+=0x10;
					}
					else return true;
				}	
//				outbuf->write((char*)&payload[offset],gseLength + 2 -offset);
    HandleEDIPacket(edi_p, &payload[offset], gseLength+2-offset);
				return true;
			}	
			//PADDING
			else if((payload[0]&0xf0)==0x00) {
				return false;
			}
			return true;
}

int main(int argc, char** argv) {
	   int c;
	   if(argc<=1) {
		   fprintf(stderr, "Usage: %s [-src-ip x.x.x.x] [-src-port xxxx] [-dst-ip x.x.x.x] [-dst-port xxxx] [-mis xxxx]\n",argv[0]);
		   fprintf(stderr, "Note: At least one argument is required\n");
		   exit(1);
	   }

           while (1) {
               int this_option_optind = optind ? optind : 1;
               int option_index = 0;
               static struct option long_options[] = {
                   {"src-ip", required_argument,     0,  0 },
                   {"dst-ip",  required_argument,    0,  0 },
                   {"src-port", required_argument,   0,  0 },
                   {"dst-port",  required_argument,  0,  0 },
                   {"mis",       required_argument,  0,  0 },
                   {"help",    no_argument         , 0,  0 },
                   {0,         0,                    0,  0 }
               };

               c = getopt_long_only(argc, argv, "",
                        long_options, &option_index);
               if (c == -1)
                   break;

               switch (c) {
               case 0:
                   fprintf(stderr,"option %s", long_options[option_index].name);
		   if(option_index==0) //src-ip
		   {
			   has_src_ip=true;
			   char** ptr=&optarg;
			   char* inv;
			   for(int i=0; i<4;i++)
			   {
			   	src_ip[i]=(unsigned char) strtol(*ptr,&inv,10);
				inv++;
				ptr=&inv;
			   }

		   }
		   else if(option_index==1) //dst_ip
		   {
			   has_dst_ip=true;
			   char** ptr=&optarg;
			   char* inv;
			   for(int i=0; i<4;i++)
			   {
			   	dst_ip[i]=(unsigned char) strtol(*ptr,&inv,10);
				inv++;
				ptr=&inv;
			   }
		   }
		   else if(option_index==2) //src_port
		   {
			   has_src_port=true;
			   unsigned short tmp=(unsigned short) strtol(optarg,NULL,10);
			   src_port[0]=tmp>>8;
			   src_port[1]=tmp&0xff;
		   }
		   else if(option_index==3) //dst_port
		   {
			   has_dst_port=true;
			   unsigned short tmp=(unsigned short) strtol(optarg,NULL,10);
			   dst_port[0]=tmp>>8;
			   dst_port[1]=tmp&0xff;
		   }
		   else if(option_index==4) //mis
		   {
			   has_mis=true;
			   mis=(unsigned char) strtol(optarg,NULL,10);
		   }

                   break;

               case '?':
                   break;

               default:
                   fprintf(stderr,"?? getopt returned character code 0%o ??\n", c);
               }
           }

           if (optind < argc) {
               fprintf(stderr,"non-option ARGV-elements: ");
               while (optind < argc)
                   fprintf(stderr,"%s ", argv[optind++]);
               fprintf(stderr,"\n");
	       exit(1);
           }
	unsigned char active=0;
	inbuf=&cin;
	outbuf=&cout;
	struct layer3 * mylayer=(struct layer3*) indata;
    	edi_p = initEDIHandle(ETI_FMT_RAW, write_file, stdout);
	fragmentor=new unsigned char*[256];
	for(int i=0; i<256;i++)
	{
		fragmentor[i]=0;
	}
	while(1)
	{
		if(inbuf->peek() == 0xb8) break;
		inbuf->read(indata,1);

	}
	while(1)
	{
                inbuf->read(indata, 11);
		if(inbuf->fail()) break;
		if(mylayer->L3Sync!=0xb8) {
			while(1)
			{
				if(inbuf->peek() == 0xb8) break;
				inbuf->read(indata,1);
				if(inbuf->fail()) break;

			}
			continue;
		}		
		unsigned int bblength=mylayer->header.Dfl1<<8 | mylayer->header.Dfl2;
		bblength>>=3;
		//fprintf(stderr, "%x %x %x %x %x\n",mylayer->L3Sync, mylayer->header.MaType1, mylayer->header.MaType2, mylayer->header.Dfl1, mylayer->header.Dfl2);
		//fprintf(stderr, "BBlength:%x\n",bblength);
                inbuf->read(&indata[11], bblength);
		if(inbuf->fail()) break;
		if(has_mis && (mylayer->header.MaType2 != mis)) continue;
		//fprintf(stderr, "GSE Header:%x %x %x\n",mylayer->payload[0],mylayer->payload[1],mylayer->payload[2]);
		int pos=0;
		while(pos<bblength - 4) { //last 4 bytes contain crc32
			unsigned int gseLength=((mylayer->payload[pos]&0x0f)<<8) | (mylayer->payload[pos+1]);
			if((mylayer->payload[pos]&0xf0)==0) break;
			if(gseLength+2>bblength-pos) break;
			if(!processBBframe(&mylayer->payload[pos], gseLength)) break;
			pos+=gseLength+2;
		}	
		if(inbuf->fail()) break;
	}
	::closeEDIHandle(edi_p);
	return 0;
}
