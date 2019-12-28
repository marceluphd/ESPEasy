// Local logging Tag
static const char TAG[] = "lora";

#if (USES_LMIC_LORA)
# include "../LoRa/lorawan.h"

# if CLOCK_ERROR_PROCENTAGE > 7
#  warning CLOCK_ERROR_PROCENTAGE value in lmic_config.h is too high; values > 7 will cause side effects
# endif // if CLOCK_ERROR_PROCENTAGE > 7

# if (TIME_SYNC_LORAWAN)
#  ifndef LMIC_ENABLE_DeviceTimeReq
#   define LMIC_ENABLE_DeviceTimeReq 1
#  endif // ifndef LMIC_ENABLE_DeviceTimeReq
# endif  // if (TIME_SYNC_LORAWAN)

static QueueHandle_t LoraSendQueue = nullptr;
static TaskHandle_t  lmicTask      = nullptr;
static TaskHandle_t  lorasendTask  = nullptr;

static configData_t cfg;

LMIC_Handler_struct::LMIC_Handler_struct() {}


LMIC_Handler_struct::~LMIC_Handler_struct() {
  reset();
}

void LMIC_Handler_struct::reset() {
  if (lmicTask != nullptr) {
    vTaskDelete(lmicTask);
    lmicTask = nullptr;
  }

  if (lorasendTask != nullptr) {
    vTaskDelete(lorasendTask);
    lorasendTask = nullptr;
  }

  if (LoraSendQueue != nullptr) {
    vQueueDelete(LoraSendQueue);
  }
}

bool LMIC_Handler_struct::isInitialized() const {
  return lmicTask != nullptr && lorasendTask != nullptr && LoraSendQueue != nullptr;
}

// table of LORAWAN MAC messages sent by the network to the device
// format: opcode, cmdname (max 19 chars), #bytes params
// source: LoRaWAN 1.1 Specification (October 11, 2017)
static const mac_t MACdn_table[] = {
  { 0x01, "ResetConf",           1                     },          { 0x02, "LinkCheckAns",     2         },
  { 0x03, "LinkADRReq",          4                     },          { 0x04, "DutyCycleReq",     1         },
  { 0x05, "RXParamSetupReq",     4                     },          { 0x06, "DevStatusReq",     0         },
  { 0x07, "NewChannelReq",       5                     },          { 0x08, "RxTimingSetupReq", 1         },
  { 0x09, "TxParamSetupReq",     1                     },          { 0x0A, "DlChannelReq",     4         },
  { 0x0B, "RekeyConf",           1                     },          { 0x0C, "ADRParamSetupReq", 1         },
  { 0x0D, "DeviceTimeAns",       5                     },          { 0x0E, "ForceRejoinReq",   2         },
  { 0x0F, "RejoinParamSetupReq", 1                     } };

// table of LORAWAN MAC messages sent by the device to the network
static const mac_t MACup_table[] = {
  { 0x01, "ResetInd",        1               },        { 0x02, "LinkCheckReq",        0               },
  { 0x03, "LinkADRAns",      1               },        { 0x04, "DutyCycleAns",        0               },
  { 0x05, "RXParamSetupAns", 1               },        { 0x06, "DevStatusAns",        2               },
  { 0x07, "NewChannelAns",   1               },        { 0x08, "RxTimingSetupAns",    0               },
  { 0x09, "TxParamSetupAns", 0               },        { 0x0A, "DlChannelAns",        1               },
  { 0x0B, "RekeyInd",        1               },        { 0x0C, "ADRParamSetupAns",    0               },
  { 0x0D, "DeviceTimeReq",   0               },        { 0x0F, "RejoinParamSetupAns", 1               } };


// DevEUI generator using devices's MAC address
void LMIC_Handler_struct::gen_lora_deveui(uint8_t *pdeveui) {
  uint8_t *p = pdeveui, dmac[6];
  int i = 0;

  esp_efuse_mac_get_default(dmac);

  // deveui is LSB, we reverse it so TTN DEVEUI display
  // will remain the same as MAC address
  // MAC is 6 bytes, devEUI 8, set first 2 ones
  // with an arbitrary value
  *p++ = 0xFF;
  *p++ = 0xFE;

  // Then next 6 bytes are mac address reversed
  for (i = 0; i < 6; i++) {
    *p++ = dmac[5 - i];
  }
}

/* new version, does it with well formed mac according IEEE spec, but is
   breaking change
   // DevEUI generator using devices's MAC address
   void gen_lora_deveui(uint8_t *pdeveui) {
   uint8_t *p = pdeveui, dmac[6];
   ESP_ERROR_CHECK(esp_efuse_mac_get_default(dmac));
   // deveui is LSB, we reverse it so TTN DEVEUI display
   // will remain the same as MAC address
   // MAC is 6 bytes, devEUI 8, set middle 2 ones
   // to an arbitrary value
 * p++ = dmac[5];
 * p++ = dmac[4];
 * p++ = dmac[3];
 * p++ = 0xfe;
 * p++ = 0xff;
 * p++ = dmac[2];
 * p++ = dmac[1];
 * p++ = dmac[0];
   }
 */

// Function to do a byte swap in a byte array
void LMIC_Handler_struct::RevBytes(unsigned char *b, size_t c) {
  u1_t i;

  for (i = 0; i < c / 2; i++) {
    unsigned char t = b[i];
    b[i]         = b[c - 1 - i];
    b[c - 1 - i] = t;
  }
}

// LMIC callback functions
void LMIC_Handler_struct::os_getDevKey(u1_t *buf) {
  memcpy(buf, APPKEY, 16);
}

void LMIC_Handler_struct::os_getArtEui(u1_t *buf) {
  memcpy(buf, APPEUI, 8);
  RevBytes(buf, 8); // TTN requires it in LSB First order, so we swap bytes
}

void LMIC_Handler_struct::os_getDevEui(u1_t *buf) {
  int i = 0, k = 0;

  memcpy(buf, DEVEUI, 8); // get fixed DEVEUI from loraconf.h

  for (i = 0; i < 8; i++) {
    k += buf[i];
  }

  if (k) {
    RevBytes(buf, 8);     // use fixed DEVEUI and swap bytes to LSB format
  } else {
    gen_lora_deveui(buf); // generate DEVEUI from device's MAC
  }

  // Get MCP 24AA02E64 hardware DEVEUI (override default settings if found)
# ifdef MCP_24AA02E64_I2C_ADDRESS
  get_hard_deveui(buf);
  RevBytes(buf, 8); // swap bytes to LSB format
# endif // ifdef MCP_24AA02E64_I2C_ADDRESS
}

void LMIC_Handler_struct::get_hard_deveui(uint8_t *pdeveui) {
  // read DEVEUI from Microchip 24AA02E64 2Kb serial eeprom if present
# ifdef MCP_24AA02E64_I2C_ADDRESS

  uint8_t i2c_ret;

  // Init this just in case, no more to 100KHz
  Wire.begin(SDA, SCL, 100000);
  Wire.beginTransmission(MCP_24AA02E64_I2C_ADDRESS);
  Wire.write(MCP_24AA02E64_MAC_ADDRESS);
  i2c_ret = Wire.endTransmission();

  // check if device was seen on i2c bus
  if (i2c_ret == 0) {
    char deveui[32] = "";
    uint8_t data;

    Wire.beginTransmission(MCP_24AA02E64_I2C_ADDRESS);
    Wire.write(MCP_24AA02E64_MAC_ADDRESS);
    Wire.endTransmission();

    Wire.requestFrom(MCP_24AA02E64_I2C_ADDRESS, 8);

    while (Wire.available()) {
      data = Wire.read();
      sprintf(deveui + strlen(deveui), "%02X ", data);
      *pdeveui++ = data;
    }
    ESP_LOGI(TAG, "Serial EEPROM found, read DEVEUI %s", deveui);
  } else {
    ESP_LOGI(TAG, "Could not read DEVEUI from serial EEPROM");
  }

  // Set back to 400KHz to speed up OLED
  Wire.setClock(400000);
# endif // MCP 24AA02E64
}

# if (VERBOSE)

// Display OTAA keys
void LMIC_Handler_struct::showLoraKeys(void) {
  // LMIC may not have used callback to fill
  // all EUI buffer so we do it here to a temp
  // buffer to be able to display them
  uint8_t buf[32];

  os_getDevEui((u1_t *)buf);
  printKey("DevEUI", buf, 8, true);
  os_getArtEui((u1_t *)buf);
  printKey("AppEUI", buf, 8, true);
  os_getDevKey((u1_t *)buf);
  printKey("AppKey", buf, 16, false);
}

# endif // VERBOSE

void LMIC_Handler_struct::onEvent(ev_t ev) {
  char buff[24] = "";

  switch (ev) {
    case EV_SCAN_TIMEOUT:
      strcpy_P(buff, PSTR("SCAN TIMEOUT"));
      break;

    case EV_BEACON_FOUND:
      strcpy_P(buff, PSTR("BEACON FOUND"));
      break;

    case EV_BEACON_MISSED:
      strcpy_P(buff, PSTR("BEACON MISSED"));
      break;

    case EV_BEACON_TRACKED:
      strcpy_P(buff, PSTR("BEACON TRACKED"));
      break;

    case EV_JOINING:
      strcpy_P(buff, PSTR("JOINING"));
      break;

    case EV_JOINED:
      strcpy_P(buff, PSTR("JOINED"));

      // set data rate adaptation according to saved setting
      LMIC_setAdrMode(cfg.adrmode);

      // set data rate and transmit power to defaults only if we have no ADR
      if (!cfg.adrmode) {
        LMIC_setDrTxpow(assertDR(cfg.loradr), cfg.txpower);
      }

      // show current devaddr
      ESP_LOGI(TAG, "DEVaddr=%08X", LMIC.devaddr);
      ESP_LOGI(TAG, "Radio parameters %s / %s / %s",
               getSfName(updr2rps(LMIC.datarate)),
               getBwName(updr2rps(LMIC.datarate)),
               getCrName(updr2rps(LMIC.datarate)));
      break;

    case EV_RFU1:
      strcpy_P(buff, PSTR("RFU1"));
      break;

    case EV_JOIN_FAILED:
      strcpy_P(buff, PSTR("JOIN FAILED"));
      break;

    case EV_REJOIN_FAILED:
      strcpy_P(buff, PSTR("REJOIN FAILED"));
      break;

    case EV_TXCOMPLETE:
      strcpy_P(buff, PSTR("TX COMPLETE"));
      break;

    case EV_LOST_TSYNC:
      strcpy_P(buff, PSTR("LOST TSYNC"));
      break;

    case EV_RESET:
      strcpy_P(buff, PSTR("RESET"));
      break;

    case EV_RXCOMPLETE:

      // data received in ping slot
      strcpy_P(buff, PSTR("RX COMPLETE"));
      break;

    case EV_LINK_DEAD:
      strcpy_P(buff, PSTR("LINK DEAD"));
      break;

    case EV_LINK_ALIVE:
      strcpy_P(buff, PSTR("LINK_ALIVE"));
      break;

    case EV_SCAN_FOUND:
      strcpy_P(buff, PSTR("SCAN FOUND"));
      break;

    case EV_TXSTART:

      if (!(LMIC.opmode & OP_JOINING)) {
        strcpy_P(buff, PSTR("TX START"));
      }
      break;

    case EV_TXCANCELED:
      strcpy_P(buff, PSTR("TX CANCELLED"));
      break;

    case EV_RXSTART:
      strcpy_P(buff, PSTR("RX START"));
      break;

    case EV_JOIN_TXCOMPLETE:
      strcpy_P(buff, PSTR("JOIN WAIT"));
      break;

    default:
      sprintf_P(buff, PSTR("LMIC EV %d"), ev);
      break;
  }

  /*
     // Log & Display if asked
     if (*buff) {
      ESP_LOGI(TAG, "%s", buff);
      sprintf(lmic_event_msg, buff);
     }
   */
}

// LMIC send task
void LMIC_Handler_struct::lora_send(void *pvParameters) {
  configASSERT(((uint32_t)pvParameters) == 1); // FreeRTOS check

  static MessageBuffer_t SendBuffer;

  while (1) {
    // postpone until we are joined if we are not
    while (!LMIC.devaddr) {
      vTaskDelay(pdMS_TO_TICKS(500));
    }

    // fetch next or wait for payload to send from queue
    if (xQueueReceive(LoraSendQueue, &SendBuffer, portMAX_DELAY) != pdTRUE) {
      ESP_LOGE(TAG, "Premature return from xQueueReceive() with no data!");
      continue;
    }

    // attempt to transmit payload
    else {
      switch (LMIC_sendWithCallback_strict(
                SendBuffer.MessagePort, SendBuffer.Message, SendBuffer.MessageSize,
                (cfg.countermode & 0x02), myTxCallback, NULL)) {
        case LMIC_ERROR_SUCCESS:
          ESP_LOGI(TAG, "%d byte(s) sent to LORA", SendBuffer.MessageSize);
          break;
        case LMIC_ERROR_TX_BUSY:                         // LMIC already has a tx message pending
        case LMIC_ERROR_TX_FAILED:                       // message was not sent
          // ESP_LOGD(TAG, "LMIC busy, message re-enqueued"); // very noisy
          vTaskDelay(pdMS_TO_TICKS(1000 + random(500))); // wait a while
          lora_enqueuedata(&SendBuffer);                 // re-enqueue the undelivered message
          break;
        case LMIC_ERROR_TX_TOO_LARGE:                    // message size exceeds LMIC buffer size
        case LMIC_ERROR_TX_NOT_FEASIBLE:                 // message too large for current datarate
          ESP_LOGI(TAG,
                   "Message too large to send, message not sent and deleted");

          // we need some kind of error handling here -> to be done
          break;
        default: // other LMIC return code
          ESP_LOGE(TAG, "LMIC error, message not sent and deleted");
      } // switch
    }
    delay(2); // yield to CPU
  }
}

esp_err_t LMIC_Handler_struct::lora_stack_init() {
  assert(SEND_QUEUE_SIZE);
  LoraSendQueue = xQueueCreate(SEND_QUEUE_SIZE, sizeof(MessageBuffer_t));

  if (LoraSendQueue == 0) {
    ESP_LOGE(TAG, "Could not create LORA send queue. Aborting.");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "LORA send queue created, size %d Bytes",
           SEND_QUEUE_SIZE * sizeof(MessageBuffer_t));

  // start lorawan stack
  ESP_LOGI(TAG, "Starting LMIC...");
  xTaskCreatePinnedToCore(lmictask,   // task function
                          "lmictask", // name of task
                          4096,       // stack size of task
                          (void *)1,  // parameter of the task
                          5,          // priority of the task
                          &lmicTask,  // task handle
                          1);         // CPU core

  if (!LMIC_startJoining()) {         // start joining
    ESP_LOGI(TAG, "Already joined");
  }

  // start lmic send task
  xTaskCreatePinnedToCore(lora_send,      // task function
                          "lorasendtask", // name of task
                          3072,           // stack size of task
                          (void *)1,      // parameter of the task
                          1,              // priority of the task
                          &lorasendTask,  // task handle
                          1);             // CPU core

  return ESP_OK;
}

void LMIC_Handler_struct::lora_enqueuedata(MessageBuffer_t *message) {
  // enqueue message in LORA send queue
  BaseType_t ret = pdFALSE;
  MessageBuffer_t DummyBuffer;
  sendprio_t prio = message->MessagePrio;

  switch (prio) {
    case prio_high:

      // clear some space in queue if full, then fallthrough to prio_normal
      if (uxQueueSpacesAvailable(LoraSendQueue) == 0) {
        xQueueReceive(LoraSendQueue, &DummyBuffer, (TickType_t)0);
        ESP_LOGW(TAG, "LORA sendqueue purged, data is lost");
      }
    case prio_normal:
      ret = xQueueSendToFront(LoraSendQueue, (void *)message, (TickType_t)0);
      break;
    case prio_low:
    default:
      ret = xQueueSendToBack(LoraSendQueue, (void *)message, (TickType_t)0);
      break;
  }

  if (ret != pdTRUE) {
    ESP_LOGW(TAG, "LORA sendqueue is full");
  }
}

void LMIC_Handler_struct::lora_queuereset(void) {
  xQueueReset(LoraSendQueue);
}

# if (TIME_SYNC_LORAWAN)
void IRAM_ATTR LMIC_Handler_struct::user_request_network_time_callback(void *pVoidUserUTCTime,
                                                                       int   flagSuccess) {
  // Explicit conversion from void* to uint32_t* to avoid compiler errors
  time_t *pUserUTCTime = (time_t *)pVoidUserUTCTime;

  // A struct that will be populated by LMIC_getNetworkTimeReference.
  // It contains the following fields:
  //  - tLocal: the value returned by os_GetTime() when the time
  //            request was sent to the gateway, and
  //  - tNetwork: the seconds between the GPS epoch and the time
  //              the gateway received the time request
  lmic_time_reference_t lmicTimeReference;

  if (flagSuccess != 1) {
    ESP_LOGW(TAG, "LoRaWAN network did not answer time request");
    return;
  }

  // Populate lmic_time_reference
  flagSuccess = LMIC_getNetworkTimeReference(&lmicTimeReference);

  if (flagSuccess != 1) {
    ESP_LOGW(TAG, "LoRaWAN time request failed");
    return;
  }

  // mask application irq to ensure accurate timing
  mask_user_IRQ();

  // Update userUTCTime, considering the difference between the GPS and UTC
  // time, and the leap seconds until year 2019
  *pUserUTCTime = lmicTimeReference.tNetwork + 315964800;

  // Current time, in ticks
  ostime_t ticksNow = os_getTime();

  // Time when the request was sent, in ticks
  ostime_t ticksRequestSent = lmicTimeReference.tLocal;

  // Add the delay between the instant the time was transmitted and
  // the current time
  time_t requestDelaySec = osticks2ms(ticksNow - ticksRequestSent) / 1000;

  // Update system time with time read from the network
  setMyTime(*pUserUTCTime + requestDelaySec, 0, _lora);

finish:

  // end of time critical section: release app irq lock
  unmask_user_IRQ();
} // user_request_network_time_callback

# endif // TIME_SYNC_LORAWAN

// LMIC lorawan stack task
void LMIC_Handler_struct::lmictask(void *pvParameters) {
  configASSERT(((uint32_t)pvParameters) == 1); // FreeRTOS check

  os_init();                                   // initialize lmic run-time environment
  LMIC_reset();                                // initialize lmic MAC
  LMIC_setLinkCheckMode(0);

# if defined(CFG_eu868)

  // Note that The Things Network uses the non-standard SF9BW125 data rate for
  // RX2 in Europe and switches between RX1 and RX2 based on network congestion.
  // Thus, to avoid occasionally join failures, we set datarate to SF9 and
  // bypass the LORAWAN spec-compliant RX2 == SF12 setting
  LMIC_setDrTxpow(EU868_DR_SF9, KEEP_TXPOW);
# else // if defined(CFG_eu868)

  // Set the data rate to Spreading Factor 7.  This is the fastest supported
  // rate for 125 kHz channels, and it minimizes air time and battery power.
  // Set the transmission power to 14 dBi (25 mW).
  LMIC_setDrTxpow(DR_SF7, 14);
# endif // if defined(CFG_eu868)

  // This tells LMIC to make the receive windows bigger, in case your clock is
  // faster or slower. This causes the transceiver to be earlier switched on,
  // so consuming more power. You may sharpen (reduce) CLOCK_ERROR_PERCENTAGE
  // in src/lmic_config.h if you are limited on battery.
# ifdef CLOCK_ERROR_PROCENTAGE
  LMIC_setClockError(MAX_CLOCK_ERROR * CLOCK_ERROR_PROCENTAGE / 100);
# endif // ifdef CLOCK_ERROR_PROCENTAGE

  // #if defined(CFG_US915) || defined(CFG_au921)
# if CFG_LMIC_US_like

  // in the US, with TTN, it saves join time if we start on subband 1
  // (channels 8-15). This will get overridden after the join by parameters
  // from the network. If working with other networks or in other regions,
  // this will need to be changed.
  LMIC_selectSubBand(1);
# endif // if CFG_LMIC_US_like

  // register a callback for downlink messages. We aren't trying to write
  // reentrant code, so pUserData is NULL.
  LMIC_registerRxMessageCb(myRxCallback, NULL);

  while (1) {
    os_runloop_once(); // execute lmic scheduled jobs and events
    delay(2);          // yield to CPU
  }
} // lmictask

// receive message handler
void LMIC_Handler_struct::myRxCallback(void *pUserData, uint8_t port, const uint8_t *pMsg,
                                       size_t nMsg) {
  // display type of received data
  if (nMsg) {
    ESP_LOGI(TAG, "Received %u byte(s) of payload on port %u", nMsg, port);
  }
  else if (port) {
    ESP_LOGI(TAG, "Received empty message on port %u", port);
  }

  // list MAC messages, if any
  uint8_t nMac = pMsg - &LMIC.frame[0];

  if (port != MACPORT) {
    --nMac;
  }

  if (nMac) {
    ESP_LOGI(TAG, "%u byte(s) downlink MAC commands", nMac);

    // NOT WORKING YET
    // whe need to unwrap the MAC command from LMIC.frame here
    // mac_decode(LMIC.frame, nMac, MACdn_table, sizeof(MACdn_table) /
    // sizeof(MACdn_table[0]));
  }

  if (LMIC.pendMacLen) {
    ESP_LOGI(TAG, "%u byte(s) uplink MAC commands", LMIC.pendMacLen);
    mac_decode(LMIC.pendMacData, LMIC.pendMacLen, MACup_table,
               sizeof(MACup_table) / sizeof(MACup_table[0]));
  }

  switch (port) {
    // ignore mac messages
    case MACPORT:
      break;

    // rcommand received -> call interpreter
    case RCMDPORT:

      // FIXME TD-er: We don't yet support remote commands via LoRa.
      // rcommand(pMsg, nMsg);
      break;

    default:

# if (TIME_SYNC_LORASERVER)

      // valid timesync answer -> call timesync processor
      if ((port >= TIMEANSWERPORT_MIN) && (port <= TIMEANSWERPORT_MAX)) {
        recv_timesync_ans(port, pMsg, nMsg);
        break;
      }
# endif // if (TIME_SYNC_LORASERVER)

      // unknown port -> display info
      ESP_LOGI(TAG, "Received data on unsupported port %u", port);
      break;
  } // switch
}

// transmit complete message handler
void LMIC_Handler_struct::myTxCallback(void *pUserData, int fSuccess) {
# if (TIME_SYNC_LORASERVER)

  // if last packet sent was a timesync request, store TX timestamp
  if (LMIC.pendTxPort == TIMEPORT) {
    store_time_sync_req(osticks2ms(LMIC.txend)); // milliseconds
  }
# endif // if (TIME_SYNC_LORASERVER)
}

// decode LORAWAN MAC message
void LMIC_Handler_struct::mac_decode(const uint8_t cmd[], const uint8_t cmdlen, const mac_t table[],
                                     const uint8_t tablesize) {
  if (!cmdlen) {
    return;
  }

  uint8_t foundcmd[cmdlen], cursor = 0;

  while (cursor < cmdlen) {
    int i = tablesize;                      // number of commands in table

    while (i--) {
      if (cmd[cursor] == table[i].opcode) { // lookup command in opcode table
        cursor++;                           // strip 1 byte opcode

        if ((cursor + table[i].params) <= cmdlen) {
          memmove(foundcmd, cmd + cursor,
                  table[i].params); // strip opcode from cmd array
          cursor += table[i].params;
          ESP_LOGD(TAG, "MAC command %s", table[i].cmdname);
        } else {
          ESP_LOGD(TAG, "MAC command 0x%02X with missing parameter(s)",
                   table[i].opcode);
        }
        break;   // command found -> exit table lookup loop
      }          // end of command validation
    }            // end of command table lookup loop

    if (i < 0) { // command not found -> skip it
      ESP_LOGD(TAG, "Unknown MAC command 0x%02X", cmd[cursor]);
      cursor++;
    }
  } // command parsing loop
}   // mac_decode()

uint8_t LMIC_Handler_struct::getBattLevel() {
  /*
     return values:
     MCMD_DEVS_EXT_POWER   = 0x00, // external power supply
     MCMD_DEVS_BATT_MIN    = 0x01, // min battery value
     MCMD_DEVS_BATT_MAX    = 0xFE, // max battery value
     MCMD_DEVS_BATT_NOINFO = 0xFF, // unknown battery level
   */

  // FIXME TD-er: Implement reading bat voltage.
# if (defined HAS_PMU || defined BAT_MEASURE_ADC)
  uint16_t voltage = 0; // read_voltage();

  switch (voltage) {
    case 0:
      return MCMD_DEVS_BATT_NOINFO;
    case 0xffff:
      return MCMD_DEVS_EXT_POWER;
    default:
      return voltage > OTA_MIN_BATT ? MCMD_DEVS_BATT_MAX : MCMD_DEVS_BATT_MIN;
  }
# else // we don't have any info on battery level
  return MCMD_DEVS_BATT_NOINFO;
# endif // if (defined HAS_PMU || defined BAT_MEASURE_ADC)
}      // getBattLevel()

// u1_t os_getBattLevel(void) { return getBattLevel(); };

const char * LMIC_Handler_struct::getSfName(rps_t rps) {
  const char *const t[] = { "FSK",  "SF7",  "SF8",  "SF9",
                            "SF10", "SF11", "SF12", "SF?" };

  return t[getSf(rps)];
}

const char * LMIC_Handler_struct::getBwName(rps_t rps) {
  const char *const t[] = { "BW125", "BW250", "BW500", "BW?" };

  return t[getBw(rps)];
}

const char * LMIC_Handler_struct::getCrName(rps_t rps) {
  const char *const t[] = { "CR 4/5", "CR 4/6", "CR 4/7", "CR 4/8" };

  return t[getCr(rps)];
}

#endif // USES_LMIC_LORA
