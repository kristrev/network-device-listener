Network Device Listener
========

Network Device Listener is an application which automatically sets new network
interfaces as up, and interfaces are detected by listening to messages generated
by udev. This application is particularly handy when dealing with USB network
devices on systems without a complete network manager (for example Gnome's NM).

For example, the Huawei HiLink modem's all have the same MAC address, making it
impossible to write udev-rules to ensure consistent naming. Also, the modems
typically get different names each time they are inserted (ethX, renameX and so
on), ruling out static entries in /etc/network/interfaces as well.

Before the interface can be used, dhclient/ifconfig/... must be used to set up
IP address and routes.

The application accepts the following command line argument:

* -d : Run as daemon. Log messages are written to
  /var/log/net\_device\_listener.log

If you want to compile the easy way, cmake is required. Also, the application
depends on libudev. I have only been able to test with version 175, but please
let me know if it works with older versions.
