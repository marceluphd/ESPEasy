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

#define MODBUS_RECEIVE_BUFFER 256
#define MODBUS_SLAVE_ADDRESS 0xFE  // Modbus "any address"

byte _plugin_052_sendframe[8] = {0};
byte _plugin_052_sendframe_length = 0;
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
        Plugin_052_modbus_log_MEI(MODBUS_SLAVE_ADDRESS);
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

void Plugin_052_AddModRTU_CRC() {
  // CRC-calculation
  byte checksumHi = 0;
  byte checksumLo = 0;
  unsigned int crc = Plugin_052_ModRTU_CRC(_plugin_052_sendframe, _plugin_052_sendframe_length, checksumHi, checksumLo);
  _plugin_052_sendframe[_plugin_052_sendframe_length] = checksumLo;
  _plugin_052_sendframe[_plugin_052_sendframe_length + 1] = checksumHi;
}

void Plugin_052_buildFrame(
              byte slaveAddress,
              byte functionCode,
              short startAddress,
              short parameter)
{
  _plugin_052_sendframe[0] = slaveAddress;
  _plugin_052_sendframe[1] = functionCode;
  _plugin_052_sendframe[2] = (byte)(startAddress >> 8);
  _plugin_052_sendframe[3] = (byte)(startAddress);
  _plugin_052_sendframe[4] = (byte)(parameter >> 8);
  _plugin_052_sendframe[5] = (byte)(parameter);
  _plugin_052_sendframe_length = 6;
}

void Plugin_052_build_modbus_MEI_frame(
              byte slaveAddress,
              byte device_id,
              byte object_id)
{
  _plugin_052_sendframe[0] = slaveAddress;
  _plugin_052_sendframe[1] = 0x2B;
  _plugin_052_sendframe[2] = 0x0E;
  _plugin_052_sendframe[3] = device_id;
  _plugin_052_sendframe[4] = object_id;
  _plugin_052_sendframe_length = 5;
}

byte Plugin_052_parse_modbus_MEI_response(byte* receive_buf, byte length) {
  int pos = 3;  // Data skipped: slave_address, FunctionCode, MEI type
  const byte device_id = receive_buf[pos++];
  const byte conformity_level = receive_buf[pos++];
  const bool more_follows = receive_buf[pos++] != 0;
  const byte next_object_id = receive_buf[pos++];
  const byte number_objects = receive_buf[pos++];
  byte object_id = 0;
  for (int i = 0; i < number_objects; ++i) {
    object_id = receive_buf[pos++];
    const byte object_length = receive_buf[pos++];
    String object_value;
    object_value.reserve(object_length);
    for (int c = 0; c < object_length; ++c) {
      object_value += char(receive_buf[pos++]);
    }
    String object_name;
    switch (object_id) {
      case 0: object_name = F("VendorName"); break;
      case 1: object_name = F("ProductCode"); break;
      case 2: object_name = F("MajorMinorRevision"); break;
      case 3: object_name = F("VendorUrl"); break;
      case 4: object_name = F("ProductName"); break;
      case 5: object_name = F("ModelName"); break;
      case 6: object_name = F("UserApplicationName"); break;
      default:
        object_name = int(object_id);
        break;
    }
    addLog(LOG_LEVEL_INFO, String(F("Modbus MEI ")) + object_name + String(F(": ")) + object_value);
  }
  if (more_follows) return next_object_id;
  if (object_id < 0xFF) return object_id + 1;
  return 0;
}

// Check checksum in buffer with buffer length len
bool Plugin_052_validChecksum(byte* buf, int len) {
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
  String log = F("Modbus Checksum Failure");
  addLog(LOG_LEVEL_INFO, log);
  return false;
}

bool Plugin_052_processModbusException(byte received_functionCode, byte value) {
  if ((received_functionCode & 0x80) == 0) {
    return true;
  }
  // Exception Response, see: http://digital.ni.com/public.nsf/allkb/E40CA0CFA0029B2286256A9900758E06?OpenDocument
  switch (value) {
    case 1: {
      // The function code received in the query is not an allowable action for the slave.
      // If a Poll Program Complete command was issued, this code indicates that no program function preceded it.
      addLog(LOG_LEVEL_INFO, F("Illegal Function"));
      break;
    }
    case 2: {
      // The data address received in the query is not an allowable address for the slave.
      addLog(LOG_LEVEL_INFO, F("Illegal Data Address"));
      break;
    }
    case 3: {
      // A value contained in the query data field is not an allowable value for the slave
      addLog(LOG_LEVEL_INFO, F("Illegal Data Value"));
      break;
    }
    case 4: {
      // An unrecoverable error occurred while the slave was attempting to perform the requested action
      addLog(LOG_LEVEL_INFO, F("Slave Device Failure"));
      break;
    }
    case 5: {
      // The slave has accepted the request and is processing it, but a long duration of time will be
      // required to do so. This response is returned to prevent a timeout error from occurring in the master.
      // The master can next issue a Poll Program Complete message to determine if processing is completed.
      addLog(LOG_LEVEL_INFO, F("Acknowledge"));
      break;
    }
    case 6: {
      // The slave is engaged in processing a long-duration program command.
      // The master should retransmit the message later when the slave is free.
      addLog(LOG_LEVEL_INFO, F("Slave Device Busy"));
      break;
    }
    default:
      addLog(LOG_LEVEL_INFO, String(F("Unknown Exception. function: "))+ received_functionCode + String(F(" Exception code: ")) + value);
      break;
  }
  return false;
}

int Plugin_052_processCommand()
{
  Plugin_052_AddModRTU_CRC();
  Plugin_052_SoftSerial->write(_plugin_052_sendframe, _plugin_052_sendframe_length + 2); //Send the byte array
  delay(50);

  // Read answer from sensor
  int ByteCounter = 0;
  byte recv_buf[MODBUS_RECEIVE_BUFFER] = {0xff};
  while(Plugin_052_SoftSerial->available() && ByteCounter < MODBUS_RECEIVE_BUFFER) {
    recv_buf[ByteCounter] = Plugin_052_SoftSerial->read();
    ByteCounter++;
  }
  if (!Plugin_052_validChecksum(recv_buf, ByteCounter)) {
    return 0;
  }
  if(Plugin_052_processModbusException(recv_buf[1], recv_buf[2])) {
    // Valid response, no exception
    switch(recv_buf[1]) {
      case 0x2B: // Read Device Identification
      {
        return Plugin_052_parse_modbus_MEI_response(recv_buf, ByteCounter);
      }
      default:
        break;
    }
    long value = (recv_buf[3] << 8) | (recv_buf[4]);
    return value;
  }
  return -1;
}

int Plugin_052_readInputRegister(short address) {
  // Only read 1 register
  return Plugin_052_processRegister(MODBUS_SLAVE_ADDRESS, READ_INPUT_REGISTERS, address, 1);
}

int Plugin_052_readHoldingRegister(short address) {
  // Only read 1 register
  return Plugin_052_processRegister(MODBUS_SLAVE_ADDRESS, READ_HOLDING_REGISTERS, address, 1);
}

// Write to holding register.
int Plugin_052_writeSingleRegister(short address, short value) {
  return Plugin_052_processRegister(MODBUS_SLAVE_ADDRESS, WRITE_SINGLE_REGISTER, address, value);
}

void Plugin_052_modbus_log_MEI(byte slaveAddress) {
  for (int device_id = 1; device_id <= 4; ++device_id) {
    // Basic, Regular, Extended
    byte object_id_lo = 0;
    byte object_id_hi = 0xff;
    switch (device_id) {
      case 1: // basic
        object_id_lo = 0;
        object_id_hi = 2;
        break;
      case 2: // Regular
        object_id_lo = 0x03;
        object_id_hi = 0x06;
        break;
      case 3: // Extended
        object_id_lo = 0x80;
        object_id_hi = 0x83;
        break;
    }
    bool more_follows = true;
    byte object_id = object_id_lo;
    while (more_follows) {
      Plugin_052_build_modbus_MEI_frame(slaveAddress, device_id, object_id);
      object_id = Plugin_052_processCommand();
      more_follows = object_id != 0;// && object_id > object_id_lo && object_id <= object_id_hi;
    }
  }
}

int Plugin_052_processRegister(
              byte slaveAddress,
              byte functionCode,
              short startAddress,
              short parameter)
{
  Plugin_052_buildFrame(slaveAddress, functionCode, startAddress, parameter);
  return Plugin_052_processCommand();
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
