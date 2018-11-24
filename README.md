# pigpio-ir

An IR transmitter using libpigpio

## How to use

1. Install [pigpio](http://abyz.me.uk/rpi/pigpio/index.html)
2. `make`
3. Run `irtx` with proper arguments (described below)

```
usage: irtx [<repeat> <interval>] <data>
```

`data` must be encoded in HEX with no spaces.
eg. `23CB26010020080A304000000000000000B7`

Some equipments require redundant signal.  
Set `repeat` and `interval` if needed.  
By default, `repeat` is set to `1` and `interval` is set to `0`.
