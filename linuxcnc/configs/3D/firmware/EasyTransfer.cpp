#include "EasyTransfer.h"

//Sends out struct in binary, with header, length info and checksum
void EasyTransfer::sendData(uint8_t* data, uint8_t size)
{
  uint8_t CS = size;
  _stream->write(0x06);
  _stream->write(0x85);
  _stream->write(size);
  for(uint8_t i = 0; i < size; i++)
  {
    CS ^= data[i];
    _stream->write(data[i]);
  }
  _stream->write(CS);
  _stream->flush();
}

int EasyTransfer::receiveData()
{
  //start off by looking for the header bytes. If they were already found in a previous call, skip it.
  if(rx_len == 0)
  {
  //this size check may be redundant due to the size check below, but for now I'll leave it the way it is.
    if(_stream->available() >= 3)
    {
	//this will block until a 0x06 is found or buffer size becomes less then 3.
      while(_stream->read() != 0x06) 
      {
    		//This will trash any preamble junk in the serial buffer
    		//but we need to make sure there is enough in the buffer to process while we trash the rest
    		//if the buffer becomes too empty, we will escape and try again on the next call
    		if(_stream->available() < 3)
    			return -1;
  		}
      if (_stream->read() == 0x85)
      {
        rx_len = _stream->read();
      }
    }
  }
  
  //we get here if we already found the header bytes, the struct size matched what we know, and now we are byte aligned.
  if(rx_len != 0)
  {
    while(_stream->available() && rx_array_inx <= rx_len)
    {
      rx_buffer[rx_array_inx++] = _stream->read();
    }
    
    if(rx_len == (rx_array_inx-1))
    {
      //seem to have got whole message
      //last uint8_t is CS
      calc_CS = rx_len;
      for (int i = 0; i<rx_len; i++)
      {
        calc_CS^=rx_buffer[i];
      } 
      
      if(calc_CS == rx_buffer[rx_array_inx-1])
      {//CS good
        packetSize = rx_len;
  		  rx_len = 0;
  		  rx_array_inx = 0;
  		  return 1;
  		}
  	  else
  	  {
    	  //failed checksum, need to clear this out anyway
    		rx_len = 0;
    		rx_array_inx = 0;
    		return -1;
  	  }
    }
  }
  
  return 0;
}

