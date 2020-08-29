#ifndef AUDIOHOME_ADF
#define AUDIOHOME_ADF

#define AUDIOHOME_ADF_BUFFER_SIZE 1300 //35000

extern int sendbuffer_datalen;
extern char sendbuffer[AUDIOHOME_ADF_BUFFER_SIZE];

extern int receivebuffer_datalen;
extern char receivebuffer[AUDIOHOME_ADF_BUFFER_SIZE];

void start_audio_pipes();

void read_send_buffer();
void write_receive_buffer();

#endif