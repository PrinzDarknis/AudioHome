#ifndef AUDIOHOME_MULTICAST
#define AUDIOHOME_MULTICAST

void multicast_init();

void multicast_receive_from(char* addr, int port);
void multicast_stop_receiving();

void multicast_start_sending();
void multicast_stop_sending();
bool multicast_toogle_sending();

#endif