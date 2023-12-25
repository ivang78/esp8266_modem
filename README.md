Virtual modem for ESP8266
=========================

Copyright (C) 2015 Jussi Salin <salinjus@gmail.com> under GPLv3 license.
Modified 2023 ivang78

Overview
--------

ESP8266 is a tiny MCU module with WIFI. It already contains a virtual modem firmware by factory but I wanted to make one myself to support a wider range of baud rates. For example Commodore 64 requires 2400 or lower. Now it is also possible to add additional features in the future because this is open source. For example, translation tables for different character sets or file transfer protocol conversions on the fly with help of a buffer in MCU memory.

Modifications of original version
---------------------------------

* Addes simplest HTTPS support for ATGET command with tcpClientSecure 
* Added primitive HTML parser, highlighted some base HTML-tags (H1...H4, P, STRONG, B, LI using ANSI control codes)
* Added ATX command to set delay after sending each byte to serial interface. This feature sutable for computers with fast UART, 
	but slow CPU and small memory buffer. ATX parameter stored to EEPROM.
* After successful connect to WIFI network store to EEPROM. Call ATWIFI without parameters to connect to stored network.
* Remove usage of LED pin and some debug lines.

AT command examples
-------------------

* Change baud rate: AT115200
* Connect to WIFI: ATWIFIMyAccessPoint,MyPassword1234
* Connect to WIFI stored network: ATWIFI
* Connect by TCP: ATDTsome.bbs.com:23
* Disable telnet command handling: ATNET0
* Get my IP: ATIP
* Make a HTTP/HTTPS GET request: ATGEThttp://host:80/path
* Set delay after sending each byte to serial interface from 0 to 9ms: ATX0-ATX9
* Answer a RING: ATA
* Disconnect: +++ (following a delay of a second)

Note that the key and port are optional parameters. Port defaults to 23. All parameters are case sensitive, the command itself not. You must always connect to an access point before dialing, otherwise you get an error. When you connect to WIFI you get either OK or ERROR after a while, depending on if it succeeded. If you get ERROR the connection might still occur by itself some time later, in case you had a slow AP or slow DHCP server in the network. When dialing, you get either CONNECT when successfully connected or ERROR if the connection couldn't be made. Reasons can be that the remote service is down or the host name is mistyped.

Default Baud rate is defined in the code. 2400 is safe for C64 and 19200 for any PC and Amiga. 115200 for PC's with "new" 16550 UART.  You must always have that default rate on the terminal when powering on. After giving a command for a higher rate nothing is replied, just switch to the new baud rate in your terminal as well. Then you can give next command in the new baud rate. Note that the first command after switching baud rate might fail because the serial port hardware is not fully synchronized yet, so it might be good idea to simply give "AT" command and wait for "ERROR" or "OK" before giving an actual command.

Example communication
---------------------

	OK
	ATWIFIMyAccessPoint,MyPassword
	Connecting to MyAccessPoint/MyPassword
	OK
	ATDTbat.org
	Connecting to bat.org:23
	CONNECT


	    __|\/|__             __|\/|__           __|\/|__       __|\/|__  Logo
	     \<..>/               \<..>/             \<..>/         \<..>/    by:
	             |\_/|                                      |\_/|          Gar
	       ______|0 0|______   /////// //\\ \\\\\\\   ______|0 0|______
	        \| | |   |  | |/  //   // //  \\    \\     \| | |   |  | |/
	          \|..\./...|/   /////// ////\\\\    \\      \|..\./...|/

A more detailed example can be seen on my YouTube video at: https://youtu.be/oiP5Clx3D_s

