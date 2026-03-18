Stub (TODO)

Can be enabled via 

```cmake ../ -DENABLE_RSSI=1```

and then rebuilding the project.

**Warning:** This is currently a very bad implementation of RSSI. 
- It will change sooner than later
- DO NOT compare stream1090 RSSI values with other software
- DO NOT apply rules for your gain settings from other software to stream1090
- The value shown is an estimation (and a very bad one)! It is not even the value for that particular message demodulated.

### Rules of thumb
This needs further testing. 

### Experiment
If you are bored, have an airspy, and want to do a fun experiment:

Uncomment linearity or sensitivity gain settings and use manual settings.
Crank VGA and MIX up to max (13) and set LNA to something low. Say for example 6. 
```
# Optional: manual gain controls. If set, these override
# linearity_gain and sensitivity_gain.
lna_gain = 6
mixer_gain = 13
vga_gain = 13
```

Then slowly (this will take some minute(s) => i am working on reloading these settings at runtime), raise the LNA value until you get into some range that works. Works = until your Min RSSI value is above -45, -40 or more is ok. Avg should then get into its 30s or higher. Do not care about the max RSSI.   

Reasoning: LNA will up the SNR which will separate the good signal from the noise and weak signals. We do not want that. Stream1090 is a truffle pig. It will sniffle through the noise.

This should give you a lower bound for the LNA value.