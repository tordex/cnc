#ifndef HOTEND_H
#define HOTEND_H

#include "PID_v1.h"
#include "PID_AutoTune_v0.h"

#define HOTEND_VALUES   11

class Hotend
{
  int           m_pinIn;
  int           m_pinOut;
  double        m_temp;
  int           m_analog_value;
  int           m_values[HOTEND_VALUES];
  double        m_currentTemp;
  double        m_pid_out;
  int           m_power;
  int           m_read_count;
  double        m_Kp;
  double        m_Ki;
  double        m_Kd;
  bool          m_off;
  int           m_window_size;
  unsigned long m_window_start_time;
  bool          m_tune;
  bool          m_is_relay;

  PID           m_pid;
  PID_ATune     m_aTune;
public:
  Hotend(int pin_in, int pin_out, bool is_relay) : 
        m_is_relay(is_relay),
        m_pinIn(pin_in),
        m_pinOut(pin_out),
        m_temp(0),
        m_analog_value(0),
        m_currentTemp(0),
        m_power(0),
        m_read_count(0),
        m_Kp(15),
        m_Ki(0.3),
        m_Kd(0),
        m_pid_out(0),
        m_pid(&m_currentTemp, &m_pid_out, &m_temp, m_Kp, m_Ki, m_Kd, DIRECT),
        m_aTune(&m_currentTemp, &m_pid_out),
        m_off(true),
        m_window_size(5000),
        m_tune(false)
  {}

  void init();
  void start_input_read();
  void input_read();
  void end_input_read();
  void set_tuning(double Kp, double Ki, double Kd)
  {
    m_Ki = Ki; m_Kp = Kp; m_Kd = Kd;
    m_pid.SetTunings(Kp, Ki, Kd);
  }
  void compute();
  void start_tuning(double start_value);
  void stop_tuning();
  void set_temperature(int temp);
  int get_temp()  { return (int) m_temp; }
  int get_current_temp()  { return (int) m_currentTemp; }
  double get_Kp()   { return m_Kp; }
  double get_Ki()   { return m_Ki; }
  double get_Kd()   { return m_Kd; }
  uint8_t in_tuning()  { return m_tune ? 1 : 0; }
};

#endif

