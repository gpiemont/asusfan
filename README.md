Asus Advanced Fan Control Driver (v.0.8)
========================================

This acpi driver, based on previous work by Dmitry Ursegov 
(http://code.google.com/p/asusfan/),
aims to provide a simple way to manage and monitor system fan speed 
on a (possibly large) variety of asus laptop models.
As of now only the a8j, a8js and n50v[cn] platforms have been tested.
Notebook Hardware Control Specs (www.pbus-167.org) can be followed 
to extend this support.

Temperature values and status parameters can be retrieved via sysfs entries;
these values are also monitored by calculating a differential on a predefined
set of samples, in order to show up how temperature is changing.
An entry called temp_status is provided to have a quick look at this.

Fan Speed can also be arbitrary set by echoing the proper value into
/sys/module/asus_fan/parameters/target_speed (usually 0-255).
Althought this could be a dangerous operation, note that speed changing
is based on autodetected temperatures, so this value could also be lost
immediatly after being set or even ignored by the driver.

A brief look at the driver parameter follows:

 \# cat /sys/module/asus_fan/parameters/min_speed 
 
 0

 \# cat /sys/module/asus_fan/parameters/min_temp 
 
 0
 
 \# cat /sys/module/asus_fan/parameters/target_speed 
 
 -1
 
at the sampler behaviour:

 \# cat /sys/module/asus_fan/parameters/current_temp 

 70
 
 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  stable

 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  descending

 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  62

 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  ascending

 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  66
 
 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  ascending
 
 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  68

 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  descending
 
 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  61
 
 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  ascending
 
 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  67
 
 \# cat /sys/module/asus_fan/parameters/temp_status 
  
  stable
 
 \# cat /sys/module/asus_fan/parameters/current_temp 
  
  67
