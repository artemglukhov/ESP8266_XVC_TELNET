#include <ESP8266WiFi.h>
#include <algorithm> // std::min

// GPIO16, or D0, is not available since it is in its own pin register
#define GPIO_TDI    D6
#define GPIO_TDO    D4
#define GPIO_TCK    D2
#define GPIO_TMS    D5

#define MAX_WRITE_SIZE  512

// AP mode or STA mode:
const char *ssid = "yourSSID";
const char *pw = "yourPassword";

#define BAUD_SERIAL 115200
#define BAUD_LOGGER 115200
#define RXBUFFERSIZE 1024


#define STACK_PROTECTOR  512 // bytes

//how many clients should be able to telnet to this ESP8266
#define MAX_SRV_CLIENTS 2

IPAddress ip(192, 168, 1, 118);
IPAddress gateway(192, 168, 1, 254);
IPAddress netmask(255, 255, 255, 0);

const int port = 2542;  // XVC port

const int port2 = 23;   // telnet port


WiFiServer server2(port2);  // telnet server
WiFiServer server(port);    // XVC server

WiFiClient client;
WiFiClient serverClients[MAX_SRV_CLIENTS];

int jtag_state;

// JTAG buffers
uint8_t cmd[16];
uint8_t buffer[1024], result[512];

enum {

  test_logic_reset, run_test_idle,

  select_dr_scan, capture_dr, shift_dr,
  exit1_dr, pause_dr, exit2_dr, update_dr,

  select_ir_scan, capture_ir, shift_ir,
  exit1_ir, pause_ir, exit2_ir, update_ir,

  num_states
};

const int next_state[num_states][2] = {

  [test_logic_reset] = {run_test_idle, test_logic_reset},
  [run_test_idle] = {run_test_idle, select_dr_scan},

  [select_dr_scan] = {capture_dr, select_ir_scan},
  [capture_dr] = {shift_dr, exit1_dr},
  [shift_dr] = {shift_dr, exit1_dr},
  [exit1_dr] = {pause_dr, update_dr},
  [pause_dr] = {pause_dr, exit2_dr},
  [exit2_dr] = {shift_dr, update_dr},
  [update_dr] = {run_test_idle, select_dr_scan},

  [select_ir_scan] = {capture_ir, test_logic_reset},
  [capture_ir] = {shift_ir, exit1_ir},
  [shift_ir] = {shift_ir, exit1_ir},
  [exit1_ir] = {pause_ir, update_ir},
  [pause_ir] = {pause_ir, exit2_ir},
  [exit2_ir] = {shift_ir, update_ir},
  [update_ir] = {run_test_idle, select_dr_scan}
};

int sread(void *target, int len) {

  uint8_t *t = (uint8_t *) target;

  while (len) {

    int r = client.read(t, len);
    if (r <= 0)
      return r;
    t += r;
    len -= r;
  }

  return 1;
}

int srcmd(void * target, int maxlen) {
  uint8_t *t = (uint8_t *) target;

  while (maxlen) {
    int r = client.read(t, 1);
    if (r <= 0)
      return r;

    if (*t == ':') {
      return 1;
    }

    t += r;
    maxlen -= r;
  }

  return 0;
}

void ICACHE_RAM_ATTR bit_shift(int len, int nr_bytes) {
  // result[bp] & (1 << j)
  int i, j;
  uint32_t wsbuf, wcbuf;
  uint8_t *pms, *pdi, *pwr;
  uint8_t tmsbuf, tdibuf, resbuf;
  
  pms = buffer;
  pdi = buffer + nr_bytes;
  pwr = result;

  for (i = 0; i < len; ) {
    tmsbuf = *pms, tdibuf = *pdi;
    resbuf = 0;

    for (j = 0; i < len && j < 8; i++, j++) {
      int tms = tmsbuf & 0x01;
      int tdi = tdibuf & 0x01;

      // Before: read, change, rise, fall
      // Modifided: change, fall, read, rise

      wsbuf = 0;
      wcbuf = 1 << GPIO_TCK; // Embed TCK fall here

      if (tms) {
        wsbuf |= (1 << GPIO_TMS);
      } else {
        wcbuf |= (1 << GPIO_TMS);
      }

      if (tdi) {
        wsbuf |= (1 << GPIO_TDI);
      } else {
        wcbuf |= (1 << GPIO_TDI);
      }
      
      // Change + fall
      if (wsbuf) GPOS = wsbuf;
      GPOC = wcbuf;
      // Read
      resbuf |= ((GPI & (1<<GPIO_TDO)) >> GPIO_TDO) << j;
      // Rise
      GPOS = (1<<GPIO_TCK);

      // Track the state.
      jtag_state = next_state[jtag_state][tms];

      tmsbuf >>= 1;
      tdibuf >>= 1;
    }
    *pwr = resbuf;

    pms++, pdi++, pwr++;
  }
}

void setup() {

  Serial.begin(BAUD_SERIAL);
  Serial.setRxBufferSize(RXBUFFERSIZE);
  Serial.swap();

  WiFi.config(ip, gateway, netmask);
  WiFi.begin(ssid, pw);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  //start server
  server2.begin();
  server2.setNoDelay(true);

  pinMode(GPIO_TDI, OUTPUT);
  pinMode(GPIO_TDO, INPUT);
  pinMode(GPIO_TCK, OUTPUT);
  pinMode(GPIO_TMS, OUTPUT);

  server.begin();
  server.setNoDelay(true);
}

void loop() {
  if (server2.hasClient()) {
    //find free/disconnected spot
    int i;
    for (i = 0; i < MAX_SRV_CLIENTS; i++)
      if (!serverClients[i]) { // equivalent to !serverClients[i].connected()
        serverClients[i] = server2.available();
        break;
      }

    //no free/disconnected spot so reject
    if (i == MAX_SRV_CLIENTS) {
      server2.available().println("busy");
      // hints: server.available() is a WiFiClient with short-term scope
      // when out of scope, a WiFiClient will
      // - flush() - all data will be sent
      // - stop() - automatically too
    }
  }

  //check TCP clients for data
#if 1
  // Incredibly, this code is faster than the bufferred one below - #4620 is needed
  // loopback/3000000baud average 348KB/s
  for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    while (serverClients[i].available() && Serial.availableForWrite() > 0) {
      // working char by char is not very efficient
      Serial.write(serverClients[i].read());
    }
#else
  // loopback/3000000baud average: 312KB/s
  for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    while (serverClients[i].available() && Serial.availableForWrite() > 0) {
      size_t maxToSerial = std::min(serverClients[i].available(), Serial.availableForWrite());
      maxToSerial = std::min(maxToSerial, (size_t)STACK_PROTECTOR);
      uint8_t buf[maxToSerial];
      size_t tcp_got = serverClients[i].read(buf, maxToSerial);
      size_t serial_sent = Serial.write(buf, tcp_got);
      
    }
#endif

  // determine maximum output size "fair TCP use"
  // client.availableForWrite() returns 0 when !client.connected()
  int maxToTcp = 0;
  for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    if (serverClients[i]) {
      int afw = serverClients[i].availableForWrite();
      if (afw) {
        if (!maxToTcp) {
          maxToTcp = afw;
        } else {
          maxToTcp = std::min(maxToTcp, afw);
        }
      }
    }

  //check UART for data
  size_t len = std::min(Serial.available(), maxToTcp);
  len = std::min(len, (size_t)STACK_PROTECTOR);
  if (len) {
    uint8_t sbuf[len];
    int serial_got = Serial.readBytes(sbuf, len);
    // push UART data to all connected telnet clients
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
      // if client.availableForWrite() was 0 (congested)
      // and increased since then,
      // ensure write space is sufficient:
      if (serverClients[i].availableForWrite() >= serial_got) {
        size_t tcp_sent = serverClients[i].write(sbuf, serial_got);
        
      }
  }


  /* XVC PART BEGIN */

  start: if (!client.connected()) {
    
    // try to connect to a new client
    client = server.available();
    
  } else {
    
    // read data from the connected client
    if (client.available()) {

      while (client.connected()) {
      
        int seen_tlr = 0;
      
        do {
      
          if (srcmd(cmd, 8) != 1)
            goto start;
      
          if (memcmp(cmd, "getinfo:", 8) == 0) {
            client.write("xvcServer_v1.0:");
            client.print(MAX_WRITE_SIZE);
            client.write("\n");
            goto start;
          }

          if (memcmp(cmd, "settck:", 7) == 0) {
            int ntck;
            if (sread(&ntck, 4) != 1) {
              //reading tck failed
              goto start;
            }
            // Actually TCK frequency is fixed, but replying a fixed TCK will halt hw_server
            client.write((const uint8_t *)&ntck, 4);
            goto start;
          }

          if (memcmp(cmd, "shift:", 6) != 0) {
            cmd[15] = '\0';
            goto start;
          }

          int len;
          if (sread(&len, 4) != 1) {

           // reading length failed
            goto start;
          }
      
          int nr_bytes = (len + 7) / 8;
          if (nr_bytes * 2 > sizeof(buffer)) {
            // buffer size exceeded
            goto start;
          }
      
          if (sread(buffer, nr_bytes * 2) != 1) {
            // reading data failed
            goto start;
          }
             
          memset((uint8_t *)result, 0, nr_bytes);
      
          // Only allow exiting if the state is rti and the IR
          // has the default value (IDCODE) by going through test_logic_reset.
          // As soon as going through capture_dr or capture_ir no exit is
          // allowed as this will change DR/IR.
      
          seen_tlr = (seen_tlr || jtag_state == test_logic_reset) && (jtag_state != capture_dr) && (jtag_state != capture_ir);
      
          //
          // Due to a weird bug(??) xilinx impacts goes through another "capture_ir"/"capture_dr" cycle after
          // reading IR/DR which unfortunately sets IR to the read-out IR value.
          // Just ignore these transactions.
      
          if ((jtag_state == exit1_ir && len == 5 && buffer[0] == 0x17) || (jtag_state == exit1_dr && len == 4 && buffer[0] == 0x0b)) {

          } else
            bit_shift(len, nr_bytes);
  
          if(client) {
            
            if(client.write((const uint8_t *)result, nr_bytes) != nr_bytes) {
              // write error - nbytes
              goto start;
            }
            
          } else {
            // client error
          }
          
        } while (!(seen_tlr && jtag_state == run_test_idle));
      }
    }
  }
}
