#if ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

#include "hotend.h"

#define B 3950 // B-коэффициент
#define SERIAL_R 4700 // сопротивление последовательного резистора, 4.7 кОм
#define THERMISTOR_R 100000 // номинальное сопротивления термистора, 100 кОм
#define NOMINAL_T 25 // номинальная температура (при которой TR = 100 кОм)

void Hotend::start_input_read()
{
  m_read_count = 0;
  m_analog_value = 0;
}

void Hotend::input_read()
{
  m_values[m_read_count] = analogRead(m_pinIn);
  m_read_count++;
}

int compare_ints(int* v1, int* v2)
{
  return *v1 - *v2;
}

void Hotend::end_input_read()
{
  qsort(m_values, HOTEND_VALUES, sizeof(int), &compare_ints);
  //m_analog_value /= m_read_count;
  m_analog_value = m_values[6];

  if(m_analog_value != 0)
  {
    double tr = 1023.0 / m_analog_value - 1;
    tr = SERIAL_R / tr;
  
    m_currentTemp = tr / THERMISTOR_R; // (R/Ro)
    m_currentTemp = log(m_currentTemp); // ln(R/Ro)
    m_currentTemp /= (double) m_beta25; // 1/B * ln(R/Ro)
    m_currentTemp += 1.0 / (NOMINAL_T + 273.15); // + (1/To)
    m_currentTemp = 1.0 / m_currentTemp; // Invert
    m_currentTemp -= 273.15; 
  }
}

void Hotend::init()
{
    pinMode(m_pinIn, INPUT);
    pinMode(m_pinOut, OUTPUT);
    m_pid.SetMode(MANUAL);
    if(m_is_relay)
    {
      m_pid.SetOutputLimits(0, m_window_size);
    }
}

void Hotend::set_temperature(int temp)
{
  m_temp = (double) temp;
  if(!temp)
  {
    if(!m_off)
    {
      m_off = true;
      m_pid.SetMode(MANUAL);
      digitalWrite(m_pinOut, LOW);
    }
  } else
  {
    if(m_off)
    {
      m_off = false;
      m_pid.SetMode(AUTOMATIC);
      m_window_start_time = millis();
    }
  }
}

void Hotend::compute()
{
  if(!m_tune)
  {
    if(m_off)
    {
      return;
    }
    m_pid.Compute();
  }
  else
  {
    int val = m_aTune.Runtime();
    if(val != 0)
    {
      m_Kp = m_aTune.GetKp();
      m_Ki = m_aTune.GetKi();
      m_Kd = m_aTune.GetKd();
      m_pid.SetTunings(m_Kp, m_Ki, m_Kd);
      stop_tuning();
    }
  }

  if(m_is_relay)
  {
    long now = millis();
  
    if(now - m_window_start_time > m_window_size)
    { 
      //time to shift the Relay Window
      m_window_start_time += m_window_size;
    }
    if (m_pid_out > now - m_window_start_time) 
    {
      digitalWrite(m_pinOut, HIGH);
    } else 
    {
      digitalWrite(m_pinOut, LOW);
    }
  } else
  {
    analogWrite(m_pinOut, m_pid_out);
  }
}

void Hotend::start_tuning(double start_value)
{
  if(!m_tune)
  {
    m_aTune.SetNoiseBand(2);
    m_aTune.SetOutputStep(255);
    m_aTune.SetLookbackSec(30);
    m_aTune.start(0, start_value);
    m_tune = true;
  }
}

void Hotend::stop_tuning()
{
  m_off = true;
  m_pid.SetMode(MANUAL);
  digitalWrite(m_pinOut, LOW);
  m_tune = false;
  m_aTune.Cancel();
}

