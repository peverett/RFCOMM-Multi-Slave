# RFCOMM-Multi-Slave

This is a project based on Cambridge Silicon Radion (CSR) BC417 modules that have external flash allowing them to be reprogrammed. These modules are readily available with Serial Port Profile (SPP) allowing them to be used as UART to radio modules at both ends of a link. 

Example modules such as the HC-06 Wireless Bluetooth module, easily available on Ebay.

While a point to point link is a useful application, a Bluetooth module can support up to seven data links. This project is to provide a simple RFCOMM Master to multiple RFCOMM slaves. This allows simple piconets to be constructed with extremely low throughput speeds.

Honestly, Bluetooth Low Energy modules supporting the new MESH profiles for GATT/ATT are probably better for this application, but as they're not really commercially available yet...

Version 1.0 Requirements
* Simple command line control interface over UART serial link to the module
* Master can have two slave connections.
* Slave can only have one connection to a Master.
* Module can be configured as either Master or Slave, but not both.
* Secure Simple Pairing 'Just Works' association model e.g. no authentication, no Man-in-the-middle protection.

This source code and project files are developed using the CSR BlueLab 5.1 SDK and tools. These tools are also required for flashing the updated software and firmware to the module. This is a commercial SDK available from [CSR][1] (now Qualcomm Technologies International Ltd).

[1]: https://www.csrsupport.com

