# ESP8266_XVC_TELNET
Simple mixture of things in order to have a Xilinx Virtual Cable server running alongside an UART reacheable through telnet

Almost directly taken from [esp-xvcd](https://github.com/gtortone/esp-xvcd) so all the merit goes to @gtortone.

Modified it to be able to connect staying on the local network instead of opening a new network in AP mode, and added a telnet server to read out the serial port, almost entirely from the [ESP8266WiFi Library Example](https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266WiFi/examples/WiFiTelnetToSerial/WiFiTelnetToSerial.ino).

This way it is possible to program and debug through the JTAG port an FPGA/SoC from Xilinx, and have a UART connected to the computer remotely, without the need of connecting many different cables.

# PinOut

|NAME | PIN|
|-----|-----|
|TDI | D6 |
|TDO | D4 |
|TCK | D2 |
|TMS | D5 |
|TX | D7 |
|RX | D8 |

# HOW TO
You need only to have the ESP8266WiFi library installed, in order to be able to compile.
Type your wifi SSID and password inside the code and the esp8266 will connect to your local network and take the IP address `192.168.1.118`.

## Connect to Xilinx Vivado (and SDK/Vitis)
Search for new hardware on local network on port 2542 with the IP address mentioned before, or press on localhost(0) with the right mouse click and add a XVC Xilinx Virtual Cable target.

## Connect to Telnet
(Linux, MacOS) Open the terminal and print `$ telnet 192.168.1.118 23`
(NB. telnet usually is default in line mode.)

# Pictures
![setup example with EBAZ4205](/IMG/Image1.png)
![working example of Vivado](/IMG/image2.jpg)
