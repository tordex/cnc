#include "PID_v1.h"
#include "PID_AutoTune_v0.h"
#include "hotend.h"
#include "EasyTransfer.h"

struct HOTEND_OUT_INFO
{
  uint32_t  cur_temp;
  uint32_t  temp;
  float     Kp;
  float     Ki;
  float     Kd;
  uint8_t   in_tune;
};

struct HOTEND_TEMP
{
  uint32_t  extruder;
  uint32_t  hotbed;
};

struct SET_TUNE_PARAMS
{
  float     Kp;
  float     Ki;
  float     Kd;
};

Hotend extruder(A0, 3, false);
Hotend bed(A1, 5, false);

EasyTransfer ET(&Serial);

void setup() 
{
    Serial.begin( 9600 );
    analogReference(EXTERNAL);
    extruder.init();
    bed.init();
}


void process_hotends()
{
    extruder.start_input_read();
    bed.start_input_read();
    for(int i = 0; i < 11; i++)
    {
      extruder.input_read();
      bed.input_read();
      delay(2);
    }
    extruder.end_input_read();
    bed.end_input_read();

    extruder.compute();
    bed.compute();
}

#define CMD_GET_DATA          0x80
#define CMD_SET_TUNE_PARAMS   0x81
#define CMD_SET_TEMPERATURE   0x82
#define CMD_SET_START_TUNING  0x83
#define CMD_SET_STOP_TUNING   0x84
#define CMD_GET_TEMP          0x85

void loop() 
{
  process_hotends();
  int recv_res = ET.receiveData();
  if(recv_res > 0)
  {
    // We received data packet
    uint8_t* rx_buffer = ET.get_rx_buffer();
    // First byte is packet type aka command
    uint8_t cmd = rx_buffer[0];
    // Second byte 0: extruder 1: hotbed
    Hotend* hotend = rx_buffer[1] ? &bed : &extruder;
    rx_buffer += 2;
    switch(cmd)
    {
      case CMD_GET_DATA:
        if(ET.get_packet_size() == 2)
        {
          HOTEND_OUT_INFO info;
          info.cur_temp    = hotend->get_current_temp();
          info.temp        = hotend->get_temp();
          info.Kp          = hotend->get_Kp();
          info.Ki          = hotend->get_Ki();
          info.Kd          = hotend->get_Kd();
          info.in_tune     = hotend->in_tuning();
          ET.sendData((uint8_t*) &info, sizeof(HOTEND_OUT_INFO));
        }
        break;
      case CMD_GET_TEMP:
        if(ET.get_packet_size() == 2)
        {
          HOTEND_TEMP info;
          info.extruder    = extruder.get_current_temp();
          info.hotbed      = bed.get_current_temp();
          ET.sendData((uint8_t*) &info, sizeof(HOTEND_TEMP));
        }
        break;
      case CMD_SET_TUNE_PARAMS:
        if(ET.get_packet_size() == 2 + sizeof(SET_TUNE_PARAMS))
        {
          SET_TUNE_PARAMS* params = (SET_TUNE_PARAMS*) rx_buffer;
          hotend->set_tuning(params->Kp, params->Ki, params->Kd);
        }
        break;
      case CMD_SET_TEMPERATURE:
        if(ET.get_packet_size() == 2 + sizeof(uint16_t))
        {
          uint16_t* params = (uint16_t*) rx_buffer;
          hotend->set_temperature((int) params[0]);
        }
        break;
      case CMD_SET_START_TUNING:
        if(ET.get_packet_size() == 2 + sizeof(float))
        {
          float* params = (float*) rx_buffer;
          hotend->start_tuning(params[0]);
        }
        break;
      case CMD_SET_STOP_TUNING:
        if(ET.get_packet_size() == 2)
        {
          hotend->stop_tuning();
        }
        break;
    }
  }
}

