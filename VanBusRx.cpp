/*
 * VanBus packet receiver for ESP8266
 *
 * Written by Erik Tromp
 *
 * Version 0.2.0 - November, 2020
 *
 * MIT license, all text above must be included in any redistribution.
 */

#include "VanBusRx.h"

static const uint16_t VAN_CRC_POLYNOM = 0x0F9D;

uint16_t _crc(const uint8_t bytes[], int size)
{
    uint16_t crc16 = 0x7FFF;

    for (int i = 1; i < size - 2; i++)  // Skip first byte (SOF, 0x0E) and last 2 (CRC)
    {
        uint8_t byte = bytes[i];

        for (int j = 0; j < 8; j++)
        {
            uint16_t bit = crc16 & 0x4000;
            if (byte & 0x80) bit ^= 0x4000;
            byte <<= 1;
            crc16 <<= 1;
            if (bit) crc16 ^= VAN_CRC_POLYNOM;
        } // for
    } // if

    crc16 ^= 0x7FFF;
    crc16 <<= 1;  // Shift left 1 bit to turn 15 bit result into 16 bit representation

    return crc16;
} // _crc

// Returns the IDEN field of a VAN packet
uint16_t TVanPacketRxDesc::Iden() const
{
    return bytes[1] << 4 | bytes[2] >> 4;
} // TVanPacketRxDesc::Iden

// Returns the Flags field of a VAN packet
uint8_t TVanPacketRxDesc::CommandFlags() const
{
    // Bits:
    // 3 : always 1
    // 2 (Request AcK, RAK) : 1 = requesting ack; 0 = no ack requested
    // 1 (Read/Write, R/W) : 1 = read; 0 = write
    // 0 (Remote Transmission Request, RTR; only when R/W == 1) : 1 = request for in-frame response
    return bytes[2] & 0x0F;
} // TVanPacketRxDesc::Flags

// Returns a pointer to the data bytes of a VAN packet
const uint8_t* TVanPacketRxDesc::Data() const
{
    return bytes + 3;
} // TVanPacketRxDesc::Data

// Returns the data length of a VAN packet
int TVanPacketRxDesc::DataLen() const
{
    // Total size minus SOF (1 byte), IDEN (1.5 bytes), COM (0.5 bytes) and CRC + EOD (2 bytes)
    return size - 5;
} // TVanPacketRxDesc::DataLen

// Calculates the CRC of a VAN packet
uint16_t TVanPacketRxDesc::Crc() const
{
    return _crc(bytes, size);
} // TVanPacketRxDesc::Crc

// Checks the CRC value of a VAN packet
bool TVanPacketRxDesc::CheckCrc() const
{
    uint16_t crc16 = 0x7FFF;

    for (int i = 1; i < size; i++)  // Skip first byte (SOF, 0x0E)
    {
        unsigned char byte = bytes[i];

        for (int j = 0; j < 8; j++)
        {
            uint16_t bit = crc16 & 0x4000;
            if (byte & 0x80) bit ^= 0x4000;
            byte <<= 1;
            crc16 <<= 1;
            if (bit) crc16 ^= VAN_CRC_POLYNOM;
        } // for
    } // if

    crc16 &= 0x7FFF;

    // Packet is OK if crc16 == 0x19B7
    return crc16 == 0x19B7;
} // TVanPacketRxDesc::CheckCrc

// Checks the CRC value of a VAN packet. If not, tries to repair it by flipping each bit.
// Note: let's keep the counters sane by calling this only once.
bool TVanPacketRxDesc::CheckCrcAndRepair()
{
    if (CheckCrc()) return true;

    VanBusRx.nCorrupt++;

    for (int atByte = 0; atByte < size; atByte++)
    {
        for (int atBit = 0; atBit < 8; atBit++)
        {
            uint8_t mask = 1 << atBit;
            bytes[atByte] ^= mask;  // Flip
            if (CheckCrc())
            {
                VanBusRx.nRepaired++;
                return true;
            } // if
            bytes[atByte] ^= mask;  // Flip back            
        } // for
    } // for

    return false;
} // TVanPacketRxDesc::CheckCrcAndRepair

// Dumps the raw packet bytes to a stream (e.g. 'Serial').
// Optionally specify the last character; default is "\n" (newline).
void TVanPacketRxDesc::DumpRaw(Stream& s, char last) const
{
    s.printf("Raw: #%04u (%*u/%u) %2d(%2d) ",
        seqNo % 10000,
        VAN_RX_QUEUE_SIZE > 100 ? 3 : VAN_RX_QUEUE_SIZE > 10 ? 2 : 1,  // This is all compile-time
        slot + 1,
        VAN_RX_QUEUE_SIZE,
        size - 5 < 0 ? 0 : size - 5,
        size);

    if (size >= 1) s.printf("%02X ", bytes[0]);  // SOF
    if (size >= 3) s.printf("%03X %s ", Iden(), CommandFlagsStr());

    for (int i = 3; i < size; i++) s.printf("%02X%c", bytes[i], i == size - 3 ? ':' : i < size - 1 ? '-' : ' ');

    s.print(AckStr());
    s.print(" ");
    s.print(ResultStr());
    s.printf(" %04X", Crc());
    s.printf(" %s", CheckCrc() ? "CRC_OK" : "CRC_ERROR");

    s.print(last);
} // TVanPacketRxDesc::DumpRaw

// Copy a VAN packet out of the receive queue, if available. Otherwise, returns false.
// If a valid pointer is passed to 'isQueueOverrun', will report then clear any queue overrun condition.
bool TVanPacketRxQueue::Receive(TVanPacketRxDesc& pkt, bool* isQueueOverrun)
{
    if (! Available()) return false;

    // Copy the whole packet descriptor out (including the debug info)
    // Note:
    // Instead of copying out, we could also just pass the pointer to the descriptor. However, then we would have to
    // wait with freeing the descriptor, thus keeping one precious queue slot allocated. It is better to copy the
    // packet into the (usually stack-allocated) memory of 'pkt' and free the queue slot as soon as possible. The
    // caller can now keep the packet as long as needed.
    pkt = *tail;

    if (isQueueOverrun)
    {
        *isQueueOverrun = IsQueueOverrun();
        ClearQueueOverrun();
    } // if

    // Indicate packet buffer is available for next packet
    tail->Init();

    AdvanceTail();

    return true;
} // TVanPacketRxQueue::Receive

// Simple function to generate a string representation of a float value.
// Note: passed buffer size must be (at least) MAX_FLOAT_SIZE bytes, e.g. declare like this:
//   char buffer[MAX_FLOAT_SIZE];
char* FloatToStr(char* buffer, float f, int prec)
{
    dtostrf(f, MAX_FLOAT_SIZE - 1, prec, buffer);

    // Strip leading spaces
    char* strippedStr = buffer;
    while (isspace(*strippedStr)) strippedStr++;

    return strippedStr;
} // FloatToStr

// Dumps packet statistics
void TVanPacketRxQueue::DumpStats(Stream& s) const
{
    uint32_t pktCount = GetCount();

   char floatBuf[MAX_FLOAT_SIZE];

    // Using shared buffer floatBuf, so only one invocation per printf
    s.printf_P(
        PSTR("received pkts: %lu, corrupt: %lu (%s%%)"),
        pktCount,
        nCorrupt,
        pktCount == 0
            ? "-.---"
            : FloatToStr(floatBuf, 100.0 * nCorrupt / pktCount, 3));

    s.printf_P(
        PSTR(", repaired: %lu (%s%%)"),
        nRepaired,
        nCorrupt == 0
            ? "---" 
            : FloatToStr(floatBuf, 100.0 * nRepaired / nCorrupt, 0));

    uint32_t overallCorrupt = nCorrupt - nRepaired;
    s.printf_P(
        PSTR(", overall: %lu (%s%%)\n"),
        overallCorrupt,
        pktCount == 0
            ? "-.---" 
            : FloatToStr(floatBuf, 100.0 * overallCorrupt / pktCount, 3));
} // TVanPacketRxQueue::DumpStats

// Calculate number of bits from a number of elapsed CPU cycles
// TODO - does ICACHE_RAM_ATTR have any effect on an inline function?
inline unsigned int ICACHE_RAM_ATTR nBitsFromCycles(uint32_t nCycles, uint32_t& jitter)
{
    // Here is the heart of the machine; lots of voodoo magic here...

    // Theory:
    // - VAN bus rate = 125 kbit/sec = 125 000 bits/sec
    //   1 bit = 1/125000 = 0.000008 sec = 8.0 usec
    // - CPU rate is 80 MHz
    //   1 cycle @ 80 MHz = 0.0000000125 sec = 0.0125 usec
    // --> So, 1 VAN-bus bit is 8.0 / 0.0125 = 640 cycles
    //
    // Real-world test #1:
    //   1 bit time varies between 636 and 892 cycles
    //   2 bit times varies between 1203 and 1443 cycles
    //   3 bit times varies between 1833 and 2345 cycles
    //   4 bit times varies between 2245 and 2786 cycles
    //   5 bit times varies between 3151 and 3160 cycles
    //   6 bit times varies between 4163 and 4206 cycles
    //
    // Real-world test #2:
    //   1 bit time varies between 612 and 800 cycles   
    //   2 bit times varies between 1222 and 1338 cycles
    //   3 bit times varies between 1863 and 1976 cycles
    //   4 bit times varies between 2510 and 2629 cycles
    //   5 bit times varies between 3161 and 3255 cycles
    //                                                  

    // Prevent calculations with roll-over (e.g. if nCycles = 2^32 - 1, adding 347 will roll over to 346)
    // Well no... do we really care?
    //if (nCycles > 999999 * 694 - 347) return 999999;

    // Sometimes, samples are stretched, because the ISR is called too late. If that happens,
    // we must compress the "sample time" for the next bit.
    nCycles += jitter;
    jitter = 0;
    if (nCycles < 1124 * CPU_F_FACTOR)
    {
        if (nCycles > 800 * CPU_F_FACTOR) jitter = nCycles - 800 * CPU_F_FACTOR;
        return 1;
    } // if
    if (nCycles < 1744 * CPU_F_FACTOR)
    {
        if (nCycles > 1380 * CPU_F_FACTOR) jitter = nCycles - 1380 * CPU_F_FACTOR;
        return 2;
    } // if
    if (nCycles < 2383 * CPU_F_FACTOR)
    {
        if (nCycles > 2100 * CPU_F_FACTOR) jitter = nCycles - 2100 * CPU_F_FACTOR;
        return 3;
    } // if
    if (nCycles < 3045 * CPU_F_FACTOR)
    {
        if (nCycles > 2655 * CPU_F_FACTOR) jitter = nCycles - 2655 * CPU_F_FACTOR;
        return 4;
    } // if
    if (nCycles < 3665 * CPU_F_FACTOR)
    {
        if (nCycles > 3300 * CPU_F_FACTOR) jitter = nCycles - 3300 * CPU_F_FACTOR;
        return 5;
    } // if

    // We hardly ever get here. And if we do, the "number of bits" is not so important.
    //unsigned int nBits = (nCycles + 347) / 694;
    //unsigned int nBits = (nCycles + 335) / 640; // ( 305...944==1; 945...1584==2; 1585...2224==3; 2225...2864==4, ...
    //unsigned int nBits = (nCycles + 347) / 660;
    //unsigned int nBits = (nCycles + 250) / 640;
    //unsigned int nBits = (nCycles + 300 * CPU_F_FACTOR) / 640 * CPU_F_FACTOR;
    unsigned int nBits = (nCycles + 300 * CPU_F_FACTOR) / 650 * CPU_F_FACTOR;

    return nBits;
} // nBitsFromCycles

void ICACHE_RAM_ATTR SetTxBitTimer()
{
    timer1_disable(); 

    if (VanBusRx.txTimerIsr)
    {
        // Turn on the Tx bit timer
        timer1_attachInterrupt(VanBusRx.txTimerIsr);

        // Clock to timer (prescaler) is always 80MHz, even F_CPU is 160 MHz
        timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);

        timer1_write(VanBusRx.txTimerTicks);
    } // if
} // SetTxBitTimer

// If the timeout expires, the packet is VAN_RX_DONE. 'ack' has already been initially set to VAN_NO_ACK,
// and then to VAN_ACK if a new bit was received within the time-out period.
void ICACHE_RAM_ATTR WaitAckIsr()
{
    SetTxBitTimer();

    VanBusRx._AdvanceHead();
} // WaitAckIsr

// Pin level change interrupt handler
void ICACHE_RAM_ATTR RxPinChangeIsr()
{
    // The logic is:
    // - if pinLevelChangedTo == VAN_LOGICAL_HIGH, we've just had a series of VAN_LOGICAL_LOW bits.
    // - if pinLevelChangedTo == VAN_LOGICAL_LOW, we've just had a series of VAN_LOGICAL_HIGH bits.
    int pinLevelChangedTo = GPIP(VanBusRx.pin);  // GPIP() is faster than digitalRead()?
    static int prevPinLevelChangedTo = VAN_BIT_RECESSIVE;

    static uint32_t prev = 0;
    uint32_t curr = ESP.getCycleCount();  // Store CPU cycle counter value as soon as possible

    // Return quickly when it is a spurious interrupt (pin level not changed).
    if (pinLevelChangedTo == prevPinLevelChangedTo) return;
    prevPinLevelChangedTo = pinLevelChangedTo;

    // Media access detection for packet transmission
    if (pinLevelChangedTo == VAN_BIT_RECESSIVE)
    {
        // Pin level just changed to 'recessive', so that was the end of the media access ('dominant')
        VanBusRx.lastMediaAccessAt = curr;
    } // if

    uint32_t nCycles = curr - prev;  // Arithmetic has safe roll-over
    prev = curr;

    static uint32_t jitter = 0;
    unsigned int nBits = nBitsFromCycles(nCycles, jitter);
 
    TVanPacketRxDesc* rxDesc = VanBusRx._head;
    PacketReadState_t state = rxDesc->state;
    rxDesc->slot = rxDesc - VanBusRx.pool;

#ifdef VAN_RX_ISR_DEBUGGING
    // Record some data to be used for debugging outside this ISR

    TIsrDebugPacket* isrDebugPacket = &rxDesc->isrDebugPacket;
    TIsrDebugData* debugIsr = isrDebugPacket->samples + isrDebugPacket->at;

    // Only write into buffer if there is space
    // TODO - no, this is incorrect. We should just overwrite the oldest (unread) slot. Then also keep a "Rx Packet
    // Lost" counter.
    if (state != VAN_RX_DONE && isrDebugPacket->at < VAN_ISR_DEBUG_BUFFER_SIZE)
    {
        debugIsr->pinLevel = pinLevelChangedTo;
        debugIsr->nCycles = nCycles;
        debugIsr->slot = rxDesc->slot;
    } // if

    // Just before returning from this ISR, record some data for debugging
    #define return \
    { \
        if (state != VAN_RX_DONE && isrDebugPacket->at < VAN_ISR_DEBUG_BUFFER_SIZE) \
        { \
            debugIsr->pinLevelAtReturnFromIsr = GPIP(VanBusRx.pin); \
            debugIsr->nCyclesProcessing = ESP.getCycleCount() - curr; \
            isrDebugPacket->at++; \
        } \
        return; \
    }
#endif // VAN_RX_ISR_DEBUGGING

    static unsigned int atBit = 0;
    static uint16_t readBits = 0;

    if (state == VAN_RX_VACANT)
    {
        // Wait until we've seen a series of VAN_LOGICAL_HIGH bits
        if (pinLevelChangedTo == VAN_LOGICAL_LOW)
        {
            rxDesc->state = VAN_RX_SEARCHING;
            rxDesc->ack = VAN_NO_ACK;
            atBit = 0;
            readBits = 0;
            rxDesc->size = 0;

            //timer1_disable(); // TODO - necessary?
        } // if

        return;
    } // if

    if (state == VAN_RX_WAITING_ACK)
    {
        rxDesc->ack = VAN_ACK;

        // The timer ISR 'WaitAckIsr' will do this
        //VanBusRx._AdvanceHead();

        return;
    } // if

    // If the current head packet is already VAN_RX_DONE, the circular buffer is completely full
    if (state != VAN_RX_SEARCHING && state != VAN_RX_LOADING)
    {
        VanBusRx._overrun = true;
        //SetTxBitTimer();
        return;
    } // if

    // During packet reception, the "Enhanced Manchester" encoding guarantees at most 5 bits are the same,
    // except during EOD when it can be 6.
    // However, sometimes the Manchester bit is missed (bug in driver chip?). Let's be tolerant with that, and just
    // pretend it was there, by accepting up to 9 equal bits.
    if (nBits > 9)
    {
        if (state == VAN_RX_SEARCHING)
        {
            atBit = 0;
            readBits = 0;
            rxDesc->size = 0;
            return;
        } // if

        rxDesc->result = VAN_RX_ERROR_NBITS;
        VanBusRx._AdvanceHead();
        //WaitAckIsr();

        return;
    } // if

    // Wait at most one extra bit time for the Manchester bit (5 --> 4, 10 --> 9)
    // But... Manchester bit error at bit 10 is needed to see EOD, so skip that.
    if (nBits > 1
        && (atBit + nBits == 5
            /*|| (rxDesc->size < 5 && atBit + nBits == 9)*/))
    {
        nBits--;
        jitter = 500;
    } // if

    atBit += nBits;
    readBits <<= nBits;

    // Remember: if pinLevelChangedTo == VAN_LOGICAL_LOW, we've just had a series of VAN_LOGICAL_HIGH bits
    uint16_t pattern = 0;
    if (pinLevelChangedTo == VAN_LOGICAL_LOW) pattern = (1 << nBits) - 1;
    readBits |= pattern;

    if (atBit >= 10)
    {
        atBit -= 10;

        // uint16_t, not uint8_t: we are reading 10 bits per byte ("Enhanced Manchester" encoding)
        uint16_t currentByte = readBits >> atBit;

        if (state == VAN_RX_SEARCHING)
        {
            // First 10 bits must be 00 0011 1101 (0x03D) (SOF, Start Of Frame)
            if (currentByte != 0x03D)
            {
                rxDesc->state = VAN_RX_VACANT;
                //SetTxBitTimer();
                return;
            } // if

            rxDesc->state = VAN_RX_LOADING;
        } // if

        // Get ready for next byte
        readBits &= (1 << atBit) - 1;

        // Remove the 2 Manchester bits 'm'; the relevant 8 bits are 'X':
        //   9 8 7 6 5 4 3 2 1 0
        //   X X X X m X X X X m
        uint8_t readByte = (currentByte >> 2 & 0xF0) | (currentByte >> 1 & 0x0F);

        rxDesc->bytes[rxDesc->size++] = readByte;

        // EOD detected?
        if ((currentByte & 0x003) == 0)
        {
            // Not really necessary to do this within the limited time there is inside this ISR
            #if 0
            if (   atBit != 0  // EOD must end with a transition 0 -> 1
                || (currentByte >> 1 & 0x20) == (currentByte & 0x20))
            {
                rxDesc->result = VAN_RX_ERROR_MANCHESTER;
            } // if
            #endif

            rxDesc->state = VAN_RX_WAITING_ACK;

            // Set a timeout for the ACK bit
            timer1_disable();
            timer1_attachInterrupt(WaitAckIsr);

            // Clock to timer (prescaler) is always 80MHz, even F_CPU is 160 MHz
            timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);

            //timer1_write(12 * 5); // 1.5 time slots = 1.5 * 8 us = 12 us
            timer1_write(16 * 5); // 2 time slots = 2 * 8 us = 12 us

            return;
        } // if

        if (rxDesc->size >= VAN_MAX_PACKET_SIZE)
        {
            rxDesc->result = VAN_RX_ERROR_MAX_PACKET;
            VanBusRx._AdvanceHead();
            //WaitAckIsr();

            return;
        } // if

        // Not really necessary to do this within the limited time there is inside this ISR
        #if 0
        // Check "Enhanced Manchester" encoding: bit 5 must be inverse of bit 6, and bit 0 must be inverse of bit 1
        if (   (currentByte >> 1 & 0x20) == (currentByte & 0x20)
            || (currentByte >> 1 & 0x01) == (currentByte & 0x01) )
        {
            rxDesc->result = VAN_RX_ERROR_MANCHESTER;
            //SetTxBitTimer();
            return;
        } // if
        #endif
    } // if

    return;

    #undef return

} // RxPinChangeIsr

// Initializes the VAN packet receiver
void TVanPacketRxQueue::Setup(uint8_t rxPin)
{
    pin = rxPin;
    pinMode(rxPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(rxPin), RxPinChangeIsr, CHANGE);
    timer1_isr_init();
    timer1_disable();
} // TVanPacketRxQueue::Setup

#ifdef VAN_RX_ISR_DEBUGGING

void TIsrDebugPacket::Dump(Stream& s) const
{
    // Parse packet outside ISR

    unsigned int atBit = 0;
    unsigned int readBits = 0;
    boolean eodSeen = false;
    uint32_t totalCycles;
    uint32_t totalBits;
    int size = 0;
    int i = 0;

    #define reset() \
    { \
        atBit = 0; \
        readBits = 0; \
        eodSeen = false; \
        totalCycles = 0; \
        totalBits = 0; \
        size = 0; \
    }

    while (at > 2 && i < at)
    {
        const TIsrDebugData* isrData = samples + i;
        uint8_t slot = isrData->slot + 1;
        if (i == 0)
        {
            s.printf_P(PSTR("%sSlot # CPU nCycles -> nBits pinLVLs data\n"), slot >= 10 ? " " : "");
        } // if

        if (i <= 1) reset();

        s.printf("#%d", slot);

        s.printf("%4u", i);

        uint32_t nCyclesProcessing = isrData->nCyclesProcessing;
        if (nCyclesProcessing > 999) s.printf(">999 ");
        else s.printf("%4lu ", nCyclesProcessing);

        uint32_t nCycles = isrData->nCycles;
        if (nCycles > 999999)
        {
            totalCycles = 0;
            //s.printf(">999999 (%7lu -> %5u)", totalCycles, 0);
            s.printf(">999999", totalCycles);
        }
        else
        {
            totalCycles += nCycles;
            // Note: nBitsFromCycles has state information ("static uint32_t jitter"), so calling
            // twice makes result different
            //s.printf("%7lu (%7lu -> %5u)", nCycles, totalCycles, nBitsFromCycles(totalCycles));
            s.printf("%7lu", nCycles);
        } // if
        s.print(" -> ");

        static uint32_t jitter = 0;
        unsigned int nBits = nBitsFromCycles(nCycles, jitter);

        if (nBits > 9999)
        {
            totalBits = 0;
            s.printf(">9999");
            //s.printf(">9999 (%5u)", totalBits);
        }
        else
        {
            totalBits += nBits;
            s.printf("%5u", nBits);
            //s.printf("%5u (%5u)", nBits, totalBits);
        } // if

        // Wait at most one extra bit time for the Manchester bit (5 --> 4, 10 --> 9)
        // But... Manchester bit error at bit 10 is needed to see EOD, so skip that.
        if (nBits > 1
            && (atBit + nBits == 5
                || (size < 5 && atBit + nBits == 10)))
        {
            nBits--;
            jitter = 500;
            s.printf("*%u ", nBits);
        } // if

        unsigned char pinLevelChangedTo = isrData->pinLevel;
        unsigned char pinLevelAtReturnFromIsr = isrData->pinLevelAtReturnFromIsr;
        s.printf(" \"%u\",\"%u\" ", pinLevelChangedTo, pinLevelAtReturnFromIsr);

        // Sometimes the ISR is called very late; this is recognized as difference in pin levels:
/*
        if (nBits > 1 && pinLevelChangedTo != pinLevelAtReturnFromIsr)
        {
            nBits--;
            s.printf("*%u ", nBits);
        } // if
*/

        // During packet reception, the "Enhanced Manchester" encoding guarantees at most 5 bits are the same,
        // except during EOD when it can be 6.
        // However, sometimes the Manchester bit is missed (bug in driver chip?). Let's be tolerant with that, and just
        // pretend it was there, by accepting up to 9 equal bits.
        if (nBits > 9)
        {
            // Show we just had a long series of 1's (shown as '1.....') or 0's (shown as '-.....')
            s.print(pinLevelChangedTo == VAN_LOGICAL_LOW ? "1....." : "-.....");
            s.println();

            reset();
            i++;
            continue;
        } // if

        // Print the read bits one by one, in a column of 6
        if (nBits > 6)
        {
            s.print(pinLevelChangedTo == VAN_LOGICAL_LOW ? "1....1" : "-....-");
        }
        else
        {
            for (int i = 0; i < nBits; i++) s.print(pinLevelChangedTo == VAN_LOGICAL_LOW ? "1" : "-");
            for (int i = nBits; i < 6; i++) s.print(" ");
        } // if

        // Print current value
        s.printf(" %04X << %1u", readBits, nBits);

        atBit += nBits;
        readBits <<= nBits;

        // Print new value
        s.printf(" = %04X", readBits);

        uint8_t pattern = 0;
        if (pinLevelChangedTo == VAN_LOGICAL_LOW) pattern = (1 << nBits) - 1;
        readBits |= pattern;

        s.printf(" | %2X = %04X", pattern, readBits);

        if (eodSeen)
        {
            if (pinLevelChangedTo == VAN_LOGICAL_LOW && nBits == 1)
            {
                s.print(" ACK");
                reset();
            }
        } // if

        else if (atBit >= 10)
        {
            atBit -= 10;

            // uint16_t, not uint8_t: we are reading 10 bits per byte ("Enhanced Manchester" encoding)
            uint16_t currentByte = readBits >> atBit;

            s.printf(" >> %u = %03X", atBit, currentByte);

            // Get ready for next byte
            readBits &= (1 << atBit) - 1;

            // Remove the 2 manchester bits 'm'; the relevant 8 bits are 'X':
            //   9 8 7 6 5 4 3 2 1 0
            //   X X X X m X X X X m
            uint8_t readByte = (currentByte >> 2 & 0xF0) | (currentByte >> 1 & 0x0F);

            s.printf(" --> %02X (#%d)", readByte, size);
            size++;

            // EOD detected?
            if ((currentByte & 0x003) == 0)
            {
                if (
                    atBit != 0  // EOD must end with a transition 0 -> 1
                    || (currentByte >> 1 & 0x20) == (currentByte & 0x20)
                   )
                {
                    s.print(" Manchester error");
                } // if

                eodSeen = true;
                s.print(" EOD");
            } // if

            // Check if bit 5 is inverse of bit 6, and if bit 0 is inverse of bit 1
            // TODO - keep this, or ignore?
            else if (   (currentByte >> 1 & 0x20) == (currentByte & 0x20)
                     || (currentByte >> 1 & 0x01) == (currentByte & 0x01) )
            {
                s.print(" Manchester error");
            } // if
        } // if

        s.println();

        i++;
    } // while

    #undef reset()
} // TIsrDebugPacket::Dump

#endif // VAN_RX_ISR_DEBUGGING

TVanPacketRxQueue VanBusRx;
