//#######################################################################################################
//############################# Plugin 052: Senseair CO2 Sensors ########################################
//#######################################################################################################
/*
  Plugin originally written by: Daniel Tedenljung info__AT__tedenljungconsulting.com
  Rewritten by: Mikael Trieb mikael__AT__triebconsulting.se

  This plugin reads availble values of Senseair Co2 Sensors.
  Datasheet can be found here:
  S8: http://www.senseair.com/products/oem-modules/senseair-s8/
  K30: http://www.senseair.com/products/oem-modules/k30/
  K70/tSENSE: http://www.senseair.com/products/wall-mount/tsense/

  Circuit wiring
    GPIO Setting 1 -> RX
    GPIO Setting 2 -> TX
    Use 1kOhm in serie on datapins!
*/

#define PLUGIN_052
#define PLUGIN_ID_052         52
#define PLUGIN_NAME_052       "Gases - CO2 Senseair"
#define PLUGIN_VALUENAME1_052 ""

#define ANY_ADDRESS 0xFE

#define READ_HOLDING_REGISTERS 0x03
#define READ_INPUT_REGISTERS   0x04
#define WRITE_SINGLE_REGISTER  0x06

#define IR_METERSTATUS  0
#define IR_ALARMSTATUS  1
#define IR_OUTPUTSTATUS 2
#define IR_SPACE_CO2    3

#define HR_ACK_REG      0
#define HR_SPACE_CO2    3
#define HR_ABC_PERIOD   31


boolean Plugin_052_init = false;

#include <SoftwareSerial.h>
SoftwareSerial *Plugin_052_SoftSerial;

boolean Plugin_052(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_052;
        Device[deviceCount].Type = DEVICE_TYPE_DUAL;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = true;
        Device[deviceCount].ValueCount = 1;
        Device[deviceCount].SendDataOption = true;
        Device[deviceCount].TimerOption = true;
        Device[deviceCount].GlobalSyncOption = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_052);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_052));
        break;
      }

      case PLUGIN_WRITE:
          {
            String tmpString = string;

      			String cmd = parseString(tmpString, 1);
      			String param1 = parseString(tmpString, 2);


            if (cmd.equalsIgnoreCase(F("senseair_setrelay")))
            {
              if (param1.toInt() == 0 || param1.toInt() == 1 || param1.toInt() == -1) {
                Plugin_052_setRelayStatus(param1.toInt());
                addLog(LOG_LEVEL_INFO, String(F("Senseair command: relay=")) + param1);
              }
              success = true;
            }

            break;
          }

    case PLUGIN_WEBFORM_LOAD:
      {
          byte choice = Settings.TaskDevicePluginConfig[event->TaskIndex][0];
          String options[6] = { F("Error Status"), F("Carbon Dioxide"), F("Temperature"), F("Humidity"), F("Relay Status"), F("Temperature Adjustment") };
          addFormSelector(string, F("Sensor"), F("plugin_052"), 6, options, NULL, choice);

          success = true;
          break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
          String plugin1 = WebServer.arg(F("plugin_052"));
          Settings.TaskDevicePluginConfig[event->TaskIndex][0] = plugin1.toInt();
          success = true;
          break;
      }

    case PLUGIN_INIT:
      {
        Plugin_052_init = true;
        Plugin_052_SoftSerial = new SoftwareSerial(Settings.TaskDevicePin1[event->TaskIndex],
                                                   Settings.TaskDevicePin2[event->TaskIndex]);
        success = true;
        break;
      }

    case PLUGIN_READ:
      {

        if (Plugin_052_init)
        {

          String log = F("Senseair: ");
          switch(Settings.TaskDevicePluginConfig[event->TaskIndex][0])
          {
              case 0:
              {
                  int errorWord = Plugin_052_readErrorStatus();
                  for (size_t i = 0; i < 9; i++) {
                    if (bitRead(errorWord,i)) {
                      UserVar[event->BaseVarIndex] = i;
                      log += F("error code = ");
                      log += i;
                      break;
                    }
                  }

                  UserVar[event->BaseVarIndex] = -1;
                  log += F("error code = ");
                  log += -1;
                  break;
              }
              case 1:
              {
                  int co2 = Plugin_052_readCo2();
/*                  for (int i = 0; i< 32; ++i) {
                    int value = Plugin_052_readInputRegister(i);
                    if (value != -1) {
                      log += F("(");
                      log += i;
                      log += F(";");
                      log += value;
                      log += F("), ");
                    }
                  }
                  */
                  UserVar[event->BaseVarIndex] = co2;
                  log += F("co2 = ");
                  log += co2;
                  break;
              }
              case 2:
              {
                  float temperature = Plugin_052_readTemperature();
                  UserVar[event->BaseVarIndex] = (float)temperature;
                  log += F("temperature = ");
                  log += (float)temperature;
                  break;
              }
              case 3:
              {
                  float relativeHumidity = Plugin_052_readRelativeHumidity();
                  UserVar[event->BaseVarIndex] = (float)relativeHumidity;
                  log += F("humidity = ");
                  log += (float)relativeHumidity;
                  break;
              }
              case 4:
              {
                  int relayStatus = Plugin_052_readRelayStatus();
                  UserVar[event->BaseVarIndex] = relayStatus;
                  log += F("relay status = ");
                  log += relayStatus;
                  break;
              }
              case 5:
              {
                  int temperatureAdjustment = Plugin_052_readTemperatureAdjustment();
                  UserVar[event->BaseVarIndex] = temperatureAdjustment;
                  log += F("temperature adjustment = ");
                  log += temperatureAdjustment;
                  break;
              }
          }
          addLog(LOG_LEVEL_INFO, log);

          success = true;
          break;
        }
        break;
      }
  }
  return success;
}

void Plugin_052_buildFrame(
              byte slaveAddress,
              byte functionCode,
              short startAddress,
              short parameter,
              byte* frame)
{
  frame[0] = slaveAddress;
  frame[1] = functionCode;
  frame[2] = (byte)(startAddress >> 8);
  frame[3] = (byte)(startAddress);
  frame[4] = (byte)(parameter >> 8);
  frame[5] = (byte)(parameter);
  // CRC-calculation
  byte checksumHi = 0;
  byte checksumLo = 0;
  unsigned int crc = Plugin_052_ModRTU_CRC(frame, 6, checksumHi, checksumLo);
  frame[6] = checksumLo;
  frame[7] = checksumHi;
}

// Check checksum in buffer with buffer length len
bool Plugin_052_validChecksum(byte buf[], int len) {
  if (len < 4) {
    // too short
    return false;
  }
  byte checksumHi = 0;
  byte checksumLo = 0;
  Plugin_052_ModRTU_CRC(buf, (len - 2), checksumHi, checksumLo);
  if (buf[len - 2] == checksumLo && buf[len - 1] == checksumHi) {
    return true;
  }
  String log = F("Senseair Checksum Failure");
  addLog(LOG_LEVEL_INFO, log);
  return false;
}

bool Plugin_052_processException(byte received_functionCode, byte value) {
  if ((received_functionCode & 0x80) == 0) {
    return true;
  }
  // Exception Response
  switch (value) {
    case 1: {
      addLog(LOG_LEVEL_INFO, F("Illegal Function"));
      break;
    }
    case 2: {
      addLog(LOG_LEVEL_INFO, F("Illegal Data Address"));
      break;
    }
    case 3: {
      addLog(LOG_LEVEL_INFO, F("Illegal Data Value"));
      break;
    }
    default:
      addLog(LOG_LEVEL_INFO, String(F("Unknown Exception. function: "))+ received_functionCode + String(F(" value: ")) + value);
      break;
  }
  return false;
}

int Plugin_052_processCommand(const byte* command)
{
  Plugin_052_SoftSerial->write(command, 8); //Send the byte array
  delay(50);

  // Read answer from sensor
  int ByteCounter = 0;
  byte recv_buf[32] = {0xff};
  while(Plugin_052_SoftSerial->available() && ByteCounter < 32) {
    recv_buf[ByteCounter] = Plugin_052_SoftSerial->read();
    ByteCounter++;
  }
  if (!Plugin_052_validChecksum(recv_buf, ByteCounter)) {
    return 0;
  }

  byte data_buf[2] = {0xff};
  data_buf[0] = recv_buf[3];
  data_buf[1] = recv_buf[4];
  long value = (data_buf[0] << 8) | (data_buf[1]);
  if(Plugin_052_processException(recv_buf[1], recv_buf[2])) {
    // Valid response, no exception
    return value;
  }
  return -1;
}

int Plugin_052_readInputRegister(short address) {
  // Only read 1 register
  return Plugin_052_processRegister(0xFE, READ_INPUT_REGISTERS, address, 1);
}

int Plugin_052_readHoldingRegister(short address) {
  // Only read 1 register
  return Plugin_052_processRegister(0xFE, READ_HOLDING_REGISTERS, address, 1);
}

// Write to holding register.
int Plugin_052_writeSingleRegister(short address, short value) {
  return Plugin_052_processRegister(0xFE, WRITE_SINGLE_REGISTER, address, value);
}

int Plugin_052_processRegister(
              byte slaveAddress,
              byte functionCode,
              short startAddress,
              short parameter)
{
  byte frame[8] = {0};
  Plugin_052_buildFrame(slaveAddress, functionCode, startAddress, parameter, frame);
  return Plugin_052_processCommand(frame);
}

int Plugin_052_readErrorStatus(void)
{
  return Plugin_052_readInputRegister(0x00);
}

int Plugin_052_readCo2(void)
{
  return Plugin_052_readInputRegister(0x03);
}

float Plugin_052_readTemperature(void)
{
  int temperatureX100 = Plugin_052_readInputRegister(0x04);
  float temperature = (float)temperatureX100/100;
  return temperature;
}

float Plugin_052_readRelativeHumidity(void)
{
  int rhX100 = Plugin_052_readInputRegister(0x05);
  float rh = 0.0;
  rh = (float)rhX100/100;
  return rh;
}

int Plugin_052_readRelayStatus(void)
{
  int status = Plugin_052_readInputRegister(0x1C);
  bool result = status >> 8 & 0x1;
  return result;
}

int Plugin_052_readTemperatureAdjustment(void)
{
  return Plugin_052_readInputRegister(0x0A);
}

void Plugin_052_setRelayStatus(int status) {
  short relaystatus = 0; // 0x3FFF represents 100% output.
  //  Refer to sensor model’s specification for voltage at 100% output.
  switch (status) {
    case 0: relaystatus = 0; break;
    case 1: relaystatus = 0x3FFF; break;
    default: relaystatus = 0x7FFF; break;
  }
  Plugin_052_writeSingleRegister(0x18, relaystatus);
}

int Plugin_052_readABCperiod(void) {
  return Plugin_052_readHoldingRegister(0x1F);
}

// Compute the MODBUS RTU CRC
unsigned int Plugin_052_ModRTU_CRC(byte* buf, int len, byte& checksumHi, byte& checksumLo)
{
  unsigned int crc = 0xFFFF;

  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned int)buf[pos];          // XOR byte into least sig. byte of crc

    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
  checksumHi = (byte)((crc >> 8) & 0xFF);
  checksumLo = (byte)(crc & 0xFF);
  return crc;
}

bool getBitOfInt(int reg, int pos) {
  // Create a mask
  int mask = 0x01 << pos;

  // Mask the status register
  int masked_register = mask & reg;

  // Shift the result of masked register back to position 0
  int result = masked_register >> pos;
  return (result == 1);
}
