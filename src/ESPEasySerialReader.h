#ifndef ESPEASY_SERIAL_READER_H
#define ESPEASY_SERIAL_READER_H

#include <ESPeasySoftwareSerial.h>

class SerialReaderStrategy {
public:
  SerialReaderStrategy(uint16_t buffSize = 64) :
      m_packet_start(buffSize), m_packet_end(buffSize), m_inPos(0), m_outPos(0), m_buffSize(buffSize), m_overflow(false) {
    m_buffSize = buffSize;
    m_buffer = (uint8_t*)malloc(m_buffSize);
  }

  virtual ~SerialReaderStrategy() {}


  // Store a byte into the buffer.
  // @return Whether it was possible to insert a byte in the buffer.
  bool store(uint8_t data) {
    int next = (m_inPos+1) % m_buffSize;
    if (next != m_outPos) {
      m_buffer[m_inPos] = rec;
      m_inPos = next;
    } else {
      m_overflow = true;
    }
    return !m_overflow;
  }

  // Check if the buffer has overflown and reset its overflow status.
  bool overflow() {
    bool res = m_overflow;
    m_overflow = false;
    return res;
  }

  // Check if the buffer is empty.
  bool empty() const {
    return (m_inPos == m_outPos);
  }

  bool full() const {
    const int next = (m_inPos+1) % m_buffSize;
    return (next == m_outPos);
  }

  // Get the number of bytes available in the buffer.
  uint16_t available() {
    int avail = m_inPos - m_outPos;
    if (avail < 0) avail += m_buffSize;
    return static_cast<uint16_t>(avail);
  }

  void flush() {
    m_inPos = m_outPos = 0;
    markPacketRead();
  }

  // Check to see if there is a valid packet in the buffer to be read.
  bool packetAvailable() const {
    return m_packet_start != m_buffSize && m_packet_end != m_buffSize;
  }

  // Things to do in this function:
  // - Find start of packet
  // - check packet validity (e.g. CRC)
  // - call markValidPacket
  virtual void checkForValidPacket() = 0;

  // Read byte for byte the packet data and increase the read pointer
  // to make room for the next packet. (so data can only read once)
  // @return whether a valid byte was read from the packet.
  bool readPacketByte(uint8_t& value) {
    if (!packetAvailable()) return false;
    bool result = read(value);
    if (m_outPos == m_packet_end)
      markPacketRead();
    return result;
  }

protected:

  // Read a byte from the buffer.
  // @return Whether the read was successful
  bool read(uint8_t& value) {
    if (empty()) return false;
    value = m_buffer[m_outPos];
    m_outPos = (m_outPos + 1) % m_buffSize;
    return true;
  }

  // Read from the buffer without incrementing the read pointer.
  // @param peek_offset The offset from the current read pointer.
  // @return whether the peek was successful
  bool peek(uint16_t peek_offset, uint8_t& value) const {
    if (peek_offset >= available()) return false;
    const uint16_t peek_pos = (m_outPos + peek_offset) % m_buffSize;
    value = m_buffer[peek_pos];
    return true;
  }

  // Flag the part in the buffer where the valid packet is located.
  // @param peek_offset Offset between current reading position and the start of the packet.
  // @param packet_length  The length of the found packet.
  void markValidPacket(uint16_t peek_offset, uint16_t packet_length) {
    m_packet_start = packet_start;
    m_packet_end = (packet_start + packet_length) % m_buffSize;
    // Set out pointer to start of packet to make room for next.
    m_outPos = m_packet_start;
  }

  void markPacketRead() {
    m_packet_start = m_buffSize;
    m_packet_end = m_buffSize;
  }

  uint16_t m_packet_start;
  uint16_t m_packet_end;
  uint16_t m_inPos;
  uint16_t m_outPos;
  uint16_t m_buffSize;
  uint8_t *m_buffer;
  bool m_overflow;
  std::vector<byte> _buffer;
}

class ESPEasySerialReader {
public:
  enum SelectedPort {
    HardwareSerial0 = 0,
    HardwareSerial1 = 1,

    None,
    SoftwareSerial
  };

  ESPEasySerialReader(const SerialReaderStrategy& strategy) : _strategy(strategy), swSerial(NULL), _selectedPort(None) {}

  virtual ~ESPEasySerialReader() {
    deleteSwSerial();
  }

  SelectedPort getSelectedPort() const { return _selectedPort; }

  bool configureSoftwareSerial(uint8_t rxPin, uint8_t txPin, long baudrate = 9600, bool inverse_logic = false, uint16_t buffSize = 64) {
    deleteSwSerial();
    swSerial = new ESPeasySoftwareSerial(rxPin, txPin, inverse_logic, buffSize);
    if (swSerial == NULL || !swSerial->isValidConstructed()) {
      deleteSwSerial();
      return false;
    }
    swSerial->begin(baudrate);
    swSerial->flush();
    _strategy.flush();
    _selectedPort = SoftwareSerial;
    return true;
  }

  // Select one of the hardware serial ports.
  // Setup of the hardware serial ports must be done before calling this function.
  bool configureHardwareSerial(SelectedPort selected, long baudrate) {
    switch (selected) {
      case HardwareSerial0:
      {
        Serial.begin(baudrate);
        Serial.flush();
        break;
      }
      case HardwareSerial1:
      {
        Serial1.begin(baudrate);
        Serial1.flush();
        break;
      }
      default:
        return false;
    }
    deleteSwSerial();
    _strategy.flush();
    _selectedPort = selected;
    return true;
  }

  void flush() {
    switch (_selectedPort) {
      case SoftwareSerial:  swSerial->flush(); break;
      case HardwareSerial0: Serial.flush();    break;
      case HardwareSerial1: Serial1.flush();   break;
      default:
        return;
    }
  }

  size_t write(uint8_t b) {
    switch (_selectedPort) {
      case SoftwareSerial:  return swSerial->write(b);
      case HardwareSerial0: return Serial.write(b);
      case HardwareSerial1: return Serial1.write(b);
      default:
        return 0;
    }
  }

  // The main loop for the reader, to get data into the strategy buffer.
  void process() {
    if (_strategy.full())
      return;
    int value = -1;
    switch (_selectedPort) {
      case SoftwareSerial:  value = swSerial->read(); break;
      case HardwareSerial0: value = Serial.read();    break;
      case HardwareSerial1: value = Serial1.read();    break;
      default:
        return;
    }
    if (value < 0 || value > 255) return;
    _strategy.store(static_cast<uint8_t>(value));
    if (!_strategy.packetAvailable()) {
      _strategy.checkForValidPacket();
    }
  }

  bool readPacketByte(uint8_t& value) {
    return _strategy.readPacketByte(value);
  }

  bool packetAvailable() const {
    return _strategy.packetAvailable();
  }

private:
  void deleteSwSerial() {
    if (swSerial != NULL) {
      delete swSerial;
      swSerial = NULL;
    }
  }


  SerialReaderStrategy _strategy;
  ESPeasySoftwareSerial *swSerial;
  SelectedPort _selectedPort;
}







#endif // ESPEASY_SERIAL_READER_H
