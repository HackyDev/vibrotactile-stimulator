# vibrotactile-stimulator

This DIY device is for vibrotactile stimulation and consists of 8 vibration modules and a NodeMCU controller. The frequency, vibration duration, and other settings can be adjusted using a web browser through WIFI. It can be built for less than $20  (access to a 3D printer required).

This device does not have a specific designated use, so it can be used for any purpose of your choice. The initial settings are set to zero, and you will need to adjust them through a graphical interface to your own values.

Please note that this device was built by a hobbyist for personal use only and has not been tested to meet any safety standards or requirements. The code running on the controller may also contain bugs.

The software is likely to be more valuable since it can work with a variety of similar devices. In contrast, the hardware was assembled using whatever materials were available and may require a significant amount of effort to put together, including soldering and gluing. Additionally, the parts used may not be ideal, and I provide some suggestions for improvement throughout the document.

The repository will be updated with more information in the future. If you find it helpful for your own projects, please consider starring the repository. You can contact me at hackydev@gmail.com

![vibrotactile stimulation device](/images/device.jpg?raw=true)

## Settings

The device generates a WIFI network named "vtstim". Upon connecting to this network, you can access the device's settings page by entering "192.168.1.1" in your browser's address bar. From there, you can make various adjustments and update the device in real-time.

![vibrotactile stimulation settings form](/images/settings-form.jpg?raw=true)

![vibrotactile stimulation settings helper](/images/settings-helper.jpg?raw=true)

## Controller

The controller utilizes a [NodeMCU](https://www.amazon.com/OLatus-OL-nodeMCU-CH340-Wireless-Internet-development/dp/B07BM58B9K), which is compatible with Arduino and allows for the reuse of code in your Arduino projects with some modifications.

The system is composed of a NodeMCU unit, 8 220 Ohm resistors, 8 DIY vibrotactile modules, 8 1N4007 diodes, and 8 IRF1406 MOSFET transistors, as shown in the schematics below.

I initially utilized BJT transistors, but they resulted in the NodeMCU being unable to boot up. This was due to these transistors grounding certain controller pins, which prevents the NodeMCU from starting up. To address this issue, MOSFET transistors are required.

![vibration module concept](/images/schematics.jpg?raw=true)
![vibration stimulator controller open](/images/controller-open.jpg?raw=true)
![vibration stimulator controller top](/images/controller-top.jpg?raw=true)

## Vibration module

The vibration module consists of 3D printed parts, a small 10x10x1 permanent neodymium magnet, and a magnetic coil extracted from a power relay. Assembly can be quite time-consuming. For future versions, I would consider using an audio exciter such as [this one](ehttps://www.tectonicaudiolabs.com/product/teax09c005-8/)

![vibration module concept](/images/module-concept.jpg?raw=true)

![vibration module concept](/images/relay-board.jpg?raw=true)

![Relay coils](/images/relay-board-coils.jpg?raw=true)

![vibration module apart](/images/module-apart.jpg?raw=true)

![vibration module spring with magnet](/images/spring-in-print.jpg?raw=true)

![vibration module](/images/module.jpg?raw=true)
