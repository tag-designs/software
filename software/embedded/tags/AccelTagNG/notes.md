Accel Tag Operation
-------------------

## Configuration

The tag has the following configuration information

* Sample rate
* Sensitivity (g)
* Data format (8,12,14 bit)
* Channels (X,y,z, or xyz)
* activity monitor
    * On (boolean)
    * activity threshold and time
    * inactivity time (threshold is fixed)
    * Presample data (boolean) -- full chunck
* Start,stop,hibernate configuration

## Data collection

Data is collected in chunks.  Each chunk has a header (in stm32 flash) that

* Has a timestamp (seconds, milliseconds)
* Has a data count (shorts)

## Continuous Mode

In continous mode, the tag collects samples until it reaches a stop or hibernate condition or until there is no more room in the flash.

## Activity monitor mode

In activity monitor mode, the tag collects samples (in chuncks) while active, stopping at chunk boundary when inactive

## Activity state information

* Awake -- set on transition to active, cleared on transition to inactive at chunk boundary.