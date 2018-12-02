/*
 * dvb-ip/mpe (ott extractor)
 *
 * refs:
 * ts-header: http://ecee.colorado.edu/~ecen5653/ecen5653/papers/iso13818-1.pdf chapter 2.4.3.1
 * datagram-header: http://broadcasting.ru/pdf-standard-specifications/multiplexing/dvb-data/den301192.v1.4.1.oap20041022_040623-041022.pdf chapter 7
 * ip-/udp-header: http://mars.netanya.ac.il/~unesco/cdrom/booklet/HTML/NETWORKING/node020.html
 * rtp-header: https://tools.ietf.org/pdf/rfc3550.pdf
 *
 * Created on: 22.11.2018
 * Author: xosef1234
 */

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

typedef struct t_stream
{
	char* name;
	unsigned long ip;
	unsigned int port;
	unsigned int pt;
	unsigned int pid;

}t_stream;

void getPayloadType(int tp, char *name, char *media_type){
	strcpy(name,"unknown"); strcpy(media_type,"unknown");
	switch (tp)
	{
		case 0: strcpy(name,"PCMU"); strcpy(media_type,"A"); break;
		case 1: strcpy(name,"reserved"); strcpy(media_type,"A"); break;
		case 2: strcpy(name,"reserved"); strcpy(media_type,"A"); break;
		case 3: strcpy(name,"GSM"); strcpy(media_type,"A"); break;
		case 4: strcpy(name,"G723"); strcpy(media_type,"A"); break;
		case 5: strcpy(name,"DVI4"); strcpy(media_type,"A"); break;
		case 6: strcpy(name,"DVI4"); strcpy(media_type,"A"); break;
		case 7: strcpy(name,"LPC"); strcpy(media_type,"A"); break;
		case 8: strcpy(name,"PCMA"); strcpy(media_type,"A"); break;
		case 9: strcpy(name,"G722"); strcpy(media_type,"A"); break;
		case 10: strcpy(name, "L16"); strcpy(media_type,"A"); break;
		case 11: strcpy(name, "L16"); strcpy(media_type,"A"); break;
		case 12: strcpy(name, "QCELP"); strcpy(media_type,"A"); break;
		case 13: strcpy(name, "CN"); strcpy(media_type,"A"); break;
		case 14: strcpy(name, "MPA"); strcpy(media_type,"A"); break;
		case 15: strcpy(name, "G728"); strcpy(media_type,"A"); break;
		case 16: strcpy(name, "DVI4"); strcpy(media_type,"A"); break;
		case 17: strcpy(name, "DVI4"); strcpy(media_type,"A"); break;
		case 18: strcpy(name, "G729"); strcpy(media_type,"A"); break;
		case 19: strcpy(name, "reserved"); strcpy(media_type,"A"); break;
		case 20: strcpy(name, "unassigned"); strcpy(media_type,"A"); break;
		case 21: strcpy(name, "unassigned"); strcpy(media_type,"A"); break;
		case 22: strcpy(name, "unassigned"); strcpy(media_type,"A"); break;
		case 23: strcpy(name, "unassigned"); strcpy(media_type,"A"); break;
		case 24: strcpy(name, "unassigned"); strcpy(media_type,"V"); break;
		case 25: strcpy(name, "DelB"); strcpy(media_type,"V"); break;
		case 26: strcpy(name, "JPEG"); strcpy(media_type,"V"); break;
		case 27: strcpy(name, "unassigned"); strcpy(media_type,"V"); break;
		case 28: strcpy(name, "nv"); strcpy(media_type,"V"); break;
		case 29: strcpy(name, "unassigned"); strcpy(media_type,"V"); break;
		case 30: strcpy(name, "unassigned"); strcpy(media_type,"V"); break;
		case 31: strcpy(name, "H261"); strcpy(media_type,"V"); break;
		case 32: strcpy(name, "MPV"); strcpy(media_type,"V"); break;
		case 33: strcpy(name, "MP2T"); strcpy(media_type,"AV"); break;
		case 34: strcpy(name, "H263"); strcpy(media_type,"V"); break;
	}
	if((tp>=35) && (tp<=71)){
		strcpy(name, "unassigned"); strcpy(media_type,"?");
	}
	if((tp>=72) && (tp<=76)){
		strcpy(name, "reserved"); strcpy(media_type,"N/A");
	}
	if((tp>=77) && (tp<=95)){
		strcpy(name, "unassigned"); strcpy(media_type,"?");
	}
	if((tp>=96) && (tp<=127)){
		strcpy(name, "dynamic"); strcpy(media_type,"?");
	}
}



void quicksort_pid(t_stream number[50],int first,int last){
   int i, j, pivot;
   t_stream temp;

   if(first<last){
      pivot=first;
      i=first;
      j=last;

      while(i<j){
         while(number[i].pid<=number[pivot].pid&&i<last)
            i++;
         while(number[j].pid>number[pivot].pid)
            j--;
         if(i<j){
            temp=number[i];
            number[i]=number[j];
            number[j]=temp;
         }
      }

      temp=number[pivot];
      number[pivot]=number[j];
      number[j]=temp;
      quicksort_pid(number,first,j-1);
      quicksort_pid(number,j+1,last);

   }
}


void getTransportStreamHeader(unsigned char *ts_error, unsigned char *pusi, unsigned int *pid, unsigned char *af, unsigned int *len, unsigned int offset, unsigned char *buf)
{
		*ts_error = (buf[offset+1] >> 7) & 0x1;
		*pusi = (buf[offset+1] >> 6) & 0x1;
		*pid = ((buf[offset+1] & 0x1F) << 8) | (buf[offset+2]);
		*af = ((buf[offset+3] >> 4) & 0x3);
		*len = 4;
	return;
}


void getDatagramSectionHeader(unsigned int *slen, unsigned int *len, unsigned int offset, unsigned char *buf)
{
    	*slen = ((buf[offset+1] & 0xF) << 8) | (buf[offset+2]);
    	*len = 12;
	return;
}

void getIPDatagramHeader(unsigned long *dest_add_number, unsigned char *dest_add, unsigned int *len_bytes, unsigned int *len, unsigned int offset, unsigned char *buf)
{
	*len = ((buf[offset+0]) & 0xF);
	*dest_add_number = (buf[offset+16]);
	*dest_add_number = (*dest_add_number << 8) | (buf[offset+17]);
	*dest_add_number = (*dest_add_number << 8) | (buf[offset+18]);
	*dest_add_number = (*dest_add_number << 8) | (buf[offset+19]);


	dest_add[0] = buf[offset+16+0];
	dest_add[1] = buf[offset+16+1];
	dest_add[2] = buf[offset+16+2];
	dest_add[3] = buf[offset+16+3];
	*len_bytes = *len*4;
	return;
}

void getUDPDatagramHeader(unsigned int *dest_port, unsigned int *ulen, unsigned int *len, unsigned int offset, unsigned char *buf)
{

	*dest_port = ((buf[offset+2] << 8) | buf[offset+3]);
	*ulen = ((buf[offset+4] << 8) | buf[offset+5]);
	*len = 8;
	return;
}

void getRTPHeader(unsigned char *ver, unsigned char *csrc_count, unsigned char *marker, unsigned int *pt, unsigned long *ssrc, unsigned long *csrc, unsigned int *len, unsigned int offset, unsigned char *buf)
{
	*ver =((buf[offset+0] >> 6) & 0x3);
	*csrc_count = ((buf[offset+0]) & 0xF);
	*marker = ((buf[offset+1] >> 7) & 0x1);
	*pt = ((buf[offset+1]) & 0x7F);
	*ssrc = (buf[offset+8]);
	*ssrc = (*ssrc <<8) | (buf[offset+9]);
	*ssrc = (*ssrc <<8) | (buf[offset+10]);
	*ssrc = (*ssrc <<8) | (buf[offset+11]);

	*csrc = (buf[offset+12]);
	*csrc = (*csrc <<8) | (buf[offset+13]);
	*csrc = (*csrc <<8) | (buf[offset+14]);
	*csrc = (*csrc <<8) | (buf[offset+15]);

        *len = 12+(1)*4;
	return;
}

void getCounter(unsigned char *byte3, unsigned char *byte4, unsigned long *int_value, unsigned int *len, unsigned int offset, unsigned char *buf)
{
		*byte3 = buf[offset+2];
		*byte4 = buf[offset+3];

		*int_value = (buf[offset+0]);
		*int_value = ((*int_value << 8) | (buf[offset+1]));
		*int_value = ((*int_value << 8) | (buf[offset+2]));
		*int_value = ((*int_value << 8) | (buf[offset+3]));

		*len = 4;
	return;
}


void dump_buffer(void *buf, int buf_size)
{
	int i;

	for(i = 0;i < buf_size;i++)
		printf("%02X", ((unsigned char *)buf)[i]);
	printf("\n");

}

void dump_buffer_file(void *buf, int buf_size, FILE *ofh)
{
	fwrite(buf,sizeof *buf,buf_size,ofh);

}

void processSection(unsigned int pid, unsigned long dest_ip, unsigned int dest_port, FILE *ofh, unsigned long *current_csrc, unsigned char *section)
{
	unsigned int dat_len;
	unsigned int dat_slen;
	unsigned long ip_dest_add_number;
	unsigned char *ip_dest_add;
	unsigned int ip_len_bytes;
	unsigned int ip_len;
	unsigned int udp_dest_port;
	unsigned int udp_ulen;
	unsigned int udp_len;
	unsigned char rtp_ver;
	unsigned char rtp_csrc_count;
	unsigned char rtp_marker;
	unsigned int rtp_pt;
	unsigned long rtp_ssrc;
	unsigned long rtp_csrc;
	unsigned int rtp_len;
	unsigned char counter_byte3;
	unsigned char counter_byte4;
	unsigned long counter_int_value;
	unsigned int counter_len;
	unsigned int data_length;
	unsigned int total_header_length;
	unsigned int in_dex;
	unsigned long temp_current_csrc;


    ip_dest_add = (unsigned char *) malloc(sizeof(char) * 4+1);

	getDatagramSectionHeader(&dat_slen,&dat_len,0,section);
	getIPDatagramHeader(&ip_dest_add_number,ip_dest_add,&ip_len_bytes,&ip_len,dat_len,section);


	getUDPDatagramHeader(&udp_dest_port,&udp_ulen,&udp_len,dat_len+ip_len_bytes,section);
	getRTPHeader(&rtp_ver,&rtp_csrc_count,&rtp_marker,&rtp_pt,&rtp_ssrc,&rtp_csrc,&rtp_len,dat_len+ip_len_bytes+udp_len,section);
	getCounter(&counter_byte3,&counter_byte4,&counter_int_value,&counter_len,dat_len+ip_len_bytes+udp_len+rtp_len,section);

	data_length = udp_ulen-8-rtp_len-counter_len;
	total_header_length = dat_len+ip_len_bytes+udp_len+rtp_len+counter_len;

	if ((dest_ip == ip_dest_add_number) && (dest_port == udp_dest_port)) {

		if ((rtp_ver==2) && (rtp_marker==0)){
			if (*current_csrc==0) {
				*current_csrc=rtp_csrc;
			}

			if (((rtp_csrc & 0x3F) ==0) && (counter_int_value==0)){
				*current_csrc=rtp_csrc;
			}
			if (*current_csrc==rtp_csrc) {
				if (data_length>0) {
					for(in_dex=total_header_length; in_dex<(total_header_length+data_length); in_dex++)
						{
							fprintf(ofh, "%c", section[in_dex]);
						}
				}
			}

		}
		else if ((rtp_ver==0) && (rtp_pt==1) && (rtp_marker==0)){
			if (((rtp_csrc >> 28) & 0xF) != 0xB) {
				if ((counter_byte3==0x2) && (counter_byte4==0x1)){
					for(in_dex=(total_header_length+0x1B); in_dex<(total_header_length+data_length); in_dex++)
						{
							fprintf(ofh, "%c", section[in_dex]);
						}
					*current_csrc= ((section[total_header_length+0x13] << 24) | (section[total_header_length+0x14] << 16) | (section[total_header_length+0x15] << 8) | (section[total_header_length+0x16]));
				} else if ((counter_byte3==0x2) && (counter_byte4==0x2)){
					for(in_dex=(total_header_length); in_dex<(total_header_length+data_length); in_dex++)
						{
							fprintf(ofh, "%c", section[in_dex]);
						}
				} else if ((counter_byte3==0x1) && (counter_byte4==0x1)){
					temp_current_csrc=((section[total_header_length+0x13] << 24) | (section[total_header_length+0x14] << 16) | (section[total_header_length+0x15] << 8) | (section[total_header_length+0x16]));
					if (*current_csrc==temp_current_csrc) {
						for(in_dex=(total_header_length+0x1B); in_dex<(total_header_length+data_length); in_dex++)
							{
								fprintf(ofh, "%c", section[in_dex]);
							}
					}
				}

			}

		}

	}

	free(ip_dest_add);

	return;
}


int main (int argc, char **argv){
	FILE *fh = NULL;
	FILE *ofh = NULL;
	char *inputfile = NULL;
	char *outputfile = NULL;
	unsigned int pid = 0;
	unsigned int port = 0;
	unsigned int pusi_packets = 0;
	unsigned int hflag =0;
	char *add_array[4];
	unsigned long ip_address=0;
	int i=0;
	int c;




	opterr = 0;


	if (argc >1){
		while ((c = getopt (argc, argv, "hi:o:p:s:a:n:")) != -1){
			switch (c)
			{
				case 'i':
					inputfile = optarg;

					if (!strcmp(inputfile,"-")){
						fh=stdin;
					} else {
						if((fh = fopen(inputfile,"rb")) == NULL){
							perror("Error opening file");
							return EXIT_FAILURE;
						}

					}
					break;
				case 'o':
					outputfile = optarg;
					if (!strcmp(outputfile,"-")){
						ofh=stdout;
					} else {
						if((ofh = fopen(outputfile,"w+b")) == NULL){
							perror("Error opening file");
							return EXIT_FAILURE;
						}
					}
					break;
				case 'p':
					pid = (int)atoi(optarg);

					if((pid>0x1FFF) || (pid<0x0020)){
						perror("Wrong pid value");
						return EXIT_FAILURE;
					}
					break;
				case 's':
					pusi_packets = (int)atoi(optarg);
					break;
				case 'a':

					add_array[i] = strtok(optarg,".");
					while(add_array[i]!=NULL)
					{
						add_array[++i] = strtok(NULL,".");
					}
					ip_address = (int)atoi(add_array[0]);
					ip_address = (ip_address << 8) | (int)atoi(add_array[1]);
					ip_address = (ip_address << 8) | (int)atoi(add_array[2]);
					ip_address = (ip_address << 8) | (int)atoi(add_array[3]);

					break;
				case 'n':
					port = (int)atoi(optarg);
					break;
				case 'h':
					hflag = 1;
					break;
				default:

					break;
			}



		}

		if((pid) && (ip_address) && (port) && (inputfile != NULL) && (outputfile != NULL) && (!pusi_packets) && (!hflag)){
			unsigned int readsize;
			unsigned int readlength = 188;
			unsigned int sectionlength =4096;
			unsigned char *buf;
			unsigned char *section;
			unsigned char ts_error;
			unsigned char pusi;
			unsigned int current_pid;
			unsigned char af;
			unsigned int header_len;
			unsigned char started=0;
			unsigned int pusi_af_offset;
			unsigned int pusi_offset;
			unsigned int payload_bytes=0;
			unsigned long current_csrc=0;
			unsigned int start_pos;
			unsigned int dat_slen;
			unsigned int dat_len;
			unsigned int section_data_length=0;
			unsigned int current_section_length=0;

			buf = (unsigned char *) malloc(sizeof(char) * readlength+1);
			section = (unsigned char *) malloc(sizeof(char) * sectionlength+1);
			while( (readsize=fread(buf, sizeof *buf,readlength, fh)) >0 )
			{
				getTransportStreamHeader(&ts_error, &pusi, &current_pid, &af, &header_len, 0, buf);

				if ((current_pid == pid) && (ts_error==0)){

					if ((pusi==0) && (started==1)) {
						pusi_af_offset=188;
						if (af == 0x1) {
							pusi_af_offset=0;
						} else if (af == 0x3) {
							pusi_af_offset=buf[4]+1;
						}

						memcpy(&section[current_section_length],&buf[pusi_af_offset+header_len],(readlength-pusi_af_offset-header_len)*sizeof(char));
						current_section_length += (readlength-pusi_af_offset-header_len)*sizeof(char);

					} else if (pusi==1){
						pusi_af_offset=188;
						pusi_offset=188;
						if (af == 0x1) {
							pusi_af_offset=1+buf[4];
							pusi_offset=buf[4];
						} else if (af == 0x3) {
							pusi_af_offset=2+buf[4]+buf[5+buf[4]];
							pusi_offset=buf[buf[4]+5];
						}
						if ((started==1) && (payload_bytes>0)){

							memcpy(&section[current_section_length],&buf[pusi_af_offset+header_len-pusi_offset],(pusi_offset)*sizeof(char));
							current_section_length += (pusi_offset)*sizeof(char);

							processSection(pid, ip_address, port, ofh, &current_csrc, section);

						}

						start_pos=pusi_af_offset+header_len;

						if ((start_pos<readlength) && (buf[start_pos] == 0x3E)) {

							if (start_pos<122) {
								getDatagramSectionHeader(&dat_slen,&dat_len,start_pos,buf);

								section_data_length =dat_slen+3;

							} else {
								section_data_length=readlength-start_pos;
							}
							payload_bytes =readlength-start_pos;

							while (section_data_length<payload_bytes) {

								current_section_length =0;
								memcpy(&section[current_section_length],&buf[start_pos],(section_data_length)*sizeof(char));
								current_section_length += (section_data_length)*sizeof(char);

								processSection(pid, ip_address, port, ofh, &current_csrc, section);

								start_pos += section_data_length;
								if ((start_pos<readlength) && (buf[start_pos] == 0x3E)) {
									if (start_pos<122) {
										getDatagramSectionHeader(&dat_slen,&dat_len,start_pos,buf);

										section_data_length =dat_slen+3;
									}else {
										section_data_length=readlength-start_pos;
									}
									payload_bytes =readlength-start_pos;
								} else {
									payload_bytes =0;
								}
							}

							if ((start_pos<readlength) && (buf[start_pos] == 0x3E)) {

								current_section_length =0;
								memcpy(&section[current_section_length],&buf[start_pos],(readlength-start_pos)*sizeof(char));
								current_section_length += (readlength-start_pos)*sizeof(char);

							}
							started=1;
						}
					}
				}

			}
			free(buf);
			free(section);
			fclose(fh);
			fclose(ofh);
		} else if((argc==5) && (inputfile != NULL) && (pusi_packets) && (!pid) && (!ip_address) && (!port) && (outputfile == NULL) && (!hflag)){
			unsigned int readsize;
			unsigned int readlength = 188;
			unsigned char *buf;
			char *channel_list;
			unsigned int max_channel_list_length =4096;
			unsigned int channel_list_length=0;
			unsigned char ts_error;
			unsigned char pusi;
			unsigned int current_pid;
			unsigned char af;
			unsigned int header_len;
			unsigned int offset=188;
			unsigned int packet_counter=0;
			int pid_channels=-1;
			unsigned int section_data_length=0;
			unsigned int dat_len;
			unsigned int dat_slen;
			unsigned long ip_dest_add_number;
			unsigned char ip_dest_add;
			unsigned int ip_len_bytes;
			unsigned int ip_len;
			unsigned int udp_dest_port;
			unsigned int udp_ulen;
			unsigned int udp_len;
			unsigned char rtp_ver;
			unsigned char rtp_csrc_count;
			unsigned char rtp_marker;
			unsigned int rtp_pt;
			unsigned long rtp_ssrc;
			unsigned long rtp_csrc;
			unsigned int rtp_len;
			unsigned char counter_byte3;
			unsigned char counter_byte4;
			unsigned long counter_int_value;
			unsigned int counter_len;
			unsigned int total_header_length;
			t_stream stream[50];
			unsigned int stream_counter=0;
			unsigned int i,j,k,pos=0;
			unsigned int found=0;
			unsigned long temp_ip=0;
			unsigned int temp_port=0;
			char *temp_name="";
			char *temp_ip_str="";
			char *temp_port_str="";
			char *temp_str="";
			char *s1,*s2;
			char tp_name[10];
			char tp_media_type[10];



			buf = (unsigned char *) malloc(sizeof(char) * readlength+1);
			channel_list = (char *) malloc(sizeof(char) * max_channel_list_length+1);
			while( (readsize=fread(buf, sizeof *buf,readlength, fh)) >0 )
			{
				getTransportStreamHeader(&ts_error, &pusi, &current_pid, &af, &header_len, 0, buf);
				offset = header_len;
				if (pusi==1){
					if (af == 0x1) {
						offset+=1+buf[4];
					} else if (af == 0x3) {
						offset+=buf[4]+2+buf[5+buf[4]];
					} else {
						offset=188;
					}
				} else {
					if (af == 0x1) {
						offset+=0;
					} else if (af == 0x3) {
						offset+=buf[4]+1;
					} else {
						offset=188;
					}
				}

				if ((pusi==1) && (ts_error == 0) && (current_pid > 0x1F)){
					if (offset<128) {
						getDatagramSectionHeader(&dat_slen,&dat_len,offset,buf);
						getIPDatagramHeader(&ip_dest_add_number,&ip_dest_add,&ip_len_bytes,&ip_len,offset+dat_len,buf);
						getUDPDatagramHeader(&udp_dest_port,&udp_ulen,&udp_len,offset+dat_len+ip_len_bytes,buf);
						getRTPHeader(&rtp_ver,&rtp_csrc_count,&rtp_marker,&rtp_pt,&rtp_ssrc,&rtp_csrc,&rtp_len,offset+dat_len+ip_len_bytes+udp_len,buf);
						getCounter(&counter_byte3,&counter_byte4,&counter_int_value,&counter_len,offset+dat_len+ip_len_bytes+udp_len+rtp_len,buf);
						total_header_length = offset+dat_len+ip_len_bytes+udp_len+rtp_len+counter_len;


						if ((pid_channels==-1) && (rtp_ssrc==ip_dest_add_number) && (counter_int_value==0)) {
							pid_channels=current_pid;

							stream[stream_counter].name="Channel List";
							stream[stream_counter].ip=ip_dest_add_number;
							stream[stream_counter].port=udp_dest_port;
							stream[stream_counter].pid=current_pid;
							stream_counter++;

							section_data_length =dat_slen-61;
							if ((readlength-1-total_header_length-1)<= section_data_length) {

								memcpy(&channel_list[channel_list_length],&buf[total_header_length],(readlength-total_header_length)*sizeof(char));
								channel_list_length +=readlength-total_header_length;

								section_data_length -=readlength-total_header_length;
							} else {

								memcpy(&channel_list[channel_list_length],&buf[total_header_length],(section_data_length)*sizeof(char));
								channel_list_length +=section_data_length;

								section_data_length -= section_data_length;
							}
						} else if ((pid_channels==current_pid) && (rtp_ssrc==ip_dest_add_number) && (counter_int_value==channel_list_length)) {
							section_data_length =dat_slen-61;
							if ((readlength-1-total_header_length-1)<= section_data_length) {

								memcpy(&channel_list[channel_list_length],&buf[total_header_length],(readlength-total_header_length)*sizeof(char));
								channel_list_length +=readlength-total_header_length;

								section_data_length -=readlength-total_header_length;
							} else {

								memcpy(&channel_list[channel_list_length],&buf[total_header_length],(section_data_length)*sizeof(char));
								channel_list_length +=section_data_length;

								section_data_length -= section_data_length;
							}
						} else if ((pid_channels==current_pid) && (rtp_ssrc==ip_dest_add_number) && (counter_int_value==0) && ((channel_list_length)>0)) {
							pid_channels=-2;
						}

						if (((rtp_ver==2) && (rtp_marker==0) && (counter_int_value==0) ) ||
							((rtp_ver==0) && (rtp_marker==0) && (rtp_pt==1) )){
							packet_counter+=1;
							found=0;
							for(i=0;i<stream_counter;i++){
								if ((stream[i].ip==ip_dest_add_number) && (stream[i].port==udp_dest_port)){
									stream[i].pt=rtp_pt;
									stream[i].pid=current_pid;

									found=1;
								}
							}
							if (!found){
								stream[stream_counter].ip=ip_dest_add_number;
								stream[stream_counter].port=udp_dest_port;
								stream[stream_counter].pt=rtp_pt;
								stream[stream_counter].pid=current_pid;
								stream[stream_counter].name="";
								stream_counter++;

							}
						}
					}
				} else {
					if ((pid_channels==current_pid) && (section_data_length>0)) {
						if ((readlength-1-offset-1)<= section_data_length) {

							memcpy(&channel_list[channel_list_length],&buf[offset],(readlength-offset)*sizeof(char));
							channel_list_length +=readlength-offset;

							section_data_length -=readlength-offset;
						} else {

							memcpy(&channel_list[channel_list_length],&buf[offset],(section_data_length)*sizeof(char));
							channel_list_length +=section_data_length;

							section_data_length -= section_data_length;
						}
					}


				}
				if (packet_counter==pusi_packets) { break;}
			}


			pos=0;
			s1=&channel_list[0];
			s2=&channel_list[0];
			while ((s1!=NULL))
			{

				s1=strstr(&channel_list[pos],";mi=");
				s2=strstr(&channel_list[pos],"&mp=");
				if(s1!=NULL){
					if(s2!=NULL){
						i=(int)(s1-&channel_list[0]);
						j=(int)(s2-s1-4);
						pos=(int)(i+j);
						temp_ip_str = (char *) malloc(sizeof(char) * j+1);
						for(k=0;k<j;k++){
							memcpy(&temp_ip_str[k],&channel_list[i+4+k],1);
						}
						i=0;
						add_array[i] = strtok(temp_ip_str,".");
						while(add_array[i]!=NULL)
						{
							add_array[++i] = strtok(NULL,".");
						}
						temp_ip = (int)atoi(add_array[0]);
						temp_ip = (temp_ip << 8) | (int)atoi(add_array[1]);
						temp_ip = (temp_ip << 8) | (int)atoi(add_array[2]);
						temp_ip = (temp_ip << 8) | (int)atoi(add_array[3]);
					}
				}

				s1=strstr(&channel_list[pos],"&mp=");
				s2=strstr(&channel_list[pos],"&mii=");
				if(s1!=NULL){
					if(s2!=NULL){
						i=(int)(s1-&channel_list[0]);
						j=(int)(s2-s1-4);
						pos=(int)(i+j);
						temp_port_str = (char *) malloc(sizeof(char) * j+1);
						for(k=0;k<j;k++){
							temp_port_str[k]=channel_list[i+4+k];
						}
						temp_port=(int)atoi(temp_port_str);
					}
				}

				s1=strstr(&channel_list[pos],"&sri=");
				s2=strstr(&channel_list[pos],"&la=");
				if(s1!=NULL){
					if(s2!=NULL){
						i=(int)(s1-&channel_list[0]);
						j=(int)(s2-s1-5);
						pos=(int)(i+j);
						temp_name = (char *) malloc(sizeof(char) * j+1);
						for(k=0;k<j;k++){
							temp_name[k]=channel_list[i+5+k];
						}
					}
				}
				s1=strstr(&channel_list[pos],";mi=");

				for(i=0;i<stream_counter;i++){

					if(stream[i].ip==temp_ip){
						if(stream[i].port==temp_port){
							stream[i].name=temp_name;
						}
					}
				}
			}
			quicksort_pid(stream,0,stream_counter-1);

			printf("%-3s | %-4s | %-15s | %-5s | %-16s | %-30s\n", "Nr.","PID","Dest-IP-Address","Port","Payload Type","Channel Name");
			printf("%-3s | %-4s | %-15s | %-5s | %-16s | %-30s\n", "---","----","---------------","-----","----------------","------------------------------");
			for(i=0;i<stream_counter;i++){
				temp_str = (char *) malloc(sizeof(char) * 3+1);
				temp_ip_str = (char *) malloc(sizeof(char) * 15+1);
				snprintf(temp_str,4,"%lu",((stream[i].ip>>24)& 0xFF));
				strcat(temp_ip_str,temp_str);
				strcat(temp_ip_str,".");
				snprintf(temp_str,4,"%lu",((stream[i].ip>>16)& 0xFF));
				strcat(temp_ip_str,temp_str);
				strcat(temp_ip_str,".");
				snprintf(temp_str,4,"%lu",((stream[i].ip>>8)& 0xFF));
				strcat(temp_ip_str,temp_str);
				strcat(temp_ip_str,".");
				snprintf(temp_str,4,"%lu",((stream[i].ip)& 0xFF));
				strcat(temp_ip_str,temp_str);
				getPayloadType(stream[i].pt,tp_name,tp_media_type);
				printf("%-3d | %-4d | %-15s | %-5d | %-3d %-10s %-2s | %-30s\n",i+1,stream[i].pid,temp_ip_str,stream[i].port,stream[i].pt,tp_name,tp_media_type,stream[i].name);
			}


			free(buf);
			free(channel_list);
			fclose(fh);
		} else if(hflag){
			printf("\nUsage: dvb-ip-mpe2ts [OPTIONS]\n");
			printf("----------------------------------------------\n");
			printf("dvb-ip-mpe2ts v0.7a 22/11/2018\n");
			printf("\n");
			printf("Extract ts-file:\n");
			printf("-i,	DVB-IP-MPE TS input file ('-' for STDIN)\n");
			printf("-o,	extracted TS output file ('-' for STDOUT)\n");
			printf("-p,	pid to be processed\n");
			printf("-a,	destination IP-address, e.g. 127.0.0.1\n");
			printf("-n,	destination port, e.g. 3000\n");
			printf("\n");
			printf("Scan file for pids, destination IP-addresses and port:\n");
			printf("-s,	number of valid TS packets with pusi to be scanned in DVB-IP-MPE TS input file, e.g 100\n");
			printf("-i,	DVB-IP-MPE TS input file ('-' for STDIN)\n");
			printf("\n");
			printf("Help:\n");
			printf("-h,	print this help\n");
			printf("\n");
			printf("Examples:\n");
			printf("dvb-ip-mpe2ts -i inputfile.ts -o outputfile.ts -p 200 -a 127.0.0.1 -n 3000\n");
			printf("dvb-ip-mpe2ts -i inputfile.ts -o - -p 200 -a 127.0.0.1 -n 3000 > outputfile.ts\n");
			printf("cat inputfile.ts | dvb-ip-mpe2ts -i - -o - -p 200 -a 127.0.0.1 -n 3000 > outputfile.ts\n");
			printf("dvb-ip-mpe2ts -i inputfile.ts -s 100\n");
			printf("\n");
		} else {
			printf("Wrong usage! Use flag -h for help.\n");
		}

	} else {
		printf("Wrong usage! Use flag -h for help.\n");
	}
  return 0;
}

