#ifndef AUDIOHOME_MQTT
#define AUDIOHOME_MQTT

void mqtt_start();

void mqtt_send_play();
void mqtt_send_none_play();

void mqtt_log(char *message);

#endif