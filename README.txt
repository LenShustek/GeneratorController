This is the hardware and software design for an external custom 
controller for Cummins standby power generators and transfer switches. 

We have a couple of these generators in locations which are unoccupied 
for long periods of time. The normal operation of the standard 
setup is to run the generator whenever the power is out, until 
either the power returns or the fuel runs out. 

What we want instead, when the property is not occupied, is for the 
generator to run enough to keep the food in the freezers cold, but not 
to run all the time. That conserves fuel, and annoys the neighbors less. 

Our custom controller works like this: after the power fails, it waits 
for a few hours to see if the power comes back. If it doesn't, it runs 
the generator for 30 minutes every few hours so that the freezers are 
kept cold. If someone is home and wants the power to be on all the time, 
there is an "at home" button on the front panel that does that. All the 
times are, of course, configurable. 

The controller also has a WiFi interface and acts as a web server, so 
you can use a browser from a remote location to check the status and 
make changes. (That depends, of course, on whether your location has 
internet access without utility power or when on generator power.) It 
can also send a text message and/or an email when power fails or is 
restored, using the IFTTT ("If This Then That") free service. 

Other features:
  - display of the utility and generator voltage levels (0-250 VAC)
  - display of the load current on each of two phases (0-100A)
  - display of the starter battery voltage after the power fails,
    when it is not being charged
  - a configurable generator exercise schedule
  - a realtime clock with battery backup
  - non-volatile memory for configuration information and an event log
  - a hardware watchdog timer that restarts the processor if something 
    locks up the controller
  - manual operation of the generator and transfer switch for testing
  
We have this installed and running with these two generator/switch configurations:
  - Cummins RS22 22 kW generator with the RA100A automatic transfer switch
    (smart generator, dumb switch)
  - Cummins RS30000 30 kW generator with the RSS100 automatic transfer switch "with controller"
    (dumb generator, smart switch)
  
See more information in the "design notes" file and in the source code comments.

This is a non-trivial amateur construction project, and the design here comes, of course,
with no guarantee that it will work for you. That said, we are happy with the way it 
works for us.

Len Shustek
December 2019

