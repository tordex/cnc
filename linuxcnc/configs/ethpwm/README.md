# Ethernet PWM Controller for Linux CNC

These modules allow to control PWM on NanoPi Neo via Ethernet connection from LinuxCNC.

## Files list
 - ``ethpwm.c`` - HAL module for LinuxCNC
 - ``send_ethpwm.c`` - Simple command line utility to send **ethpwm** packet
 - ``kernel/ethpwm.dts`` - Device Tree overlay file for **ethpwm** device. Applied on NanoPi Neo (Armbian) 
 - ``kernel/ethpwm_mod.c`` - Kernel module for NanoPi Neo (Armbian)

## HAL Module
Your can compile HAL module with command:
``
sudo halcompile --install ethpwm.c
``

Then add lines into your HAL file:
``
loadrt ethpwm iface="eth1" dst="02:81:3a:fe:15:7a"
addf ethpwm.update servo-thread
net aout-00 => ethpwm.0.value
net spindle-on <= motion.spindle-on => ethpwm.0.enable
setp ethpwm.0.pwm-freq 20000.0
setp ethpwm.0.scale 1000.0
setp ethpwm.0.offset 0
``
Actual configuration depends of you machine. ``loadrt`` loads **ethpwm** module with parameters:

 - iface - interface name to send packets
 - dst - MAC address of destination. This is MAC address of NanoPi network interface.
Module pins are the same as for standard ``pwmgen`` module.

## Kernel module

Kernel module is designed for NanoPi Neo with Armbian installed. Generally it can be used with other boards, but you have to change the DTS file.

### Installation
- Compile with command ``make``
- Install DTS with command ``sudo armbian-add-overlay ethpwm.dts``. More information you can find on [armbian web site](https://docs.armbian.com/Hardware_Allwinner_overlays/).
- Load module with command ``sudo insmod ethpwm_proto.ko``

Check ``dmesg`` output to see if the module is loaded.



