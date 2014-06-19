LinuxDriver-MMA7660
===================

  The repository presents a Linux based driver for MMA7660, which is an I2C based accelerometer.
  
  About MMA7660
  -------------
  The device's datasheet could be obtained [here](http://cache.freescale.com/files/sensors/doc/data_sheet/MMA7660FC.pdf?pspll=1).
  The device has the following features :
  * 3-Axis Accelerometer
  * Digital output (I2C)
  * Power modes : Off mode, Standby mode & Active mode
  * Configurable samples per second
  * Tilt orientation detection for Potrait/Landscape capability
  * Gesture detection including shake detection and tap detection
  
  Driver (KERN_SRC/mma7660.c) features
  ------------------------------------
  * The driver makes use of Linux's I2C framework to talk with the device and input framework to talk with the user applications.
  * By default, the driver works with 120 samples/sec of the device.
  * As of yet, the driver dosen't supports interrupts. The data from the device is read in a polled fashion, as and when the device is opened.
  * The driver reports three input events, each corresponding to a single axis. 
  * It also supports a misc event, which reports shake detection.
  * The driver exports a sysfs attribute named 'shake_enable'. Writing '1' to it enables to output shake events. Conversely, writing '0'
    to it disables to output shake events.
  * A logic to detect taps on the hardware is also added, and a sysfs attribute is exported for the same, just to enable/disable it. 
    The enable/disable logic follows same as the above point.
  * A debugfs file corresponding to each device attached to the bus is created under /sys/kernel/debug/mma7660/stat_<i2c_addr>.
    A 'cat' on the file (read-operation) produces continuous output of the data read from the device.
  * The driver has also opted for power-optimisations using Linux's runtime power optimisation framework. The driver pushes the device into
    active mode as and when it is opened (device node or debugfs file). Once the number of references to the device reaches '0', the device is
    again put back into standby mode.
    
  Test user application (test_app.c)
  ----------------------------------
    The user application accepts the event device node (from /dev/input/eventXX) as its only argument. It prints the data continuously onto the
  console, until interrupted. 
  
