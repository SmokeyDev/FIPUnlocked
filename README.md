# Saitek FIP Controller

## Description
This project aims to support Saitek/Logitech FIP (Flight Instrument Panel) in not supported games.
Current version allows to clone selected parts of the monitors onto FIP 1-6 pages.
S1-S6 buttons can be configured to work as simple keyboard emualtor supporting single key presses or combinations like `LShift + LAlt + L`.

Virtual Display Driver can be used to add virtual monitor, which can be used to display exported panels from DCS - e.g. with [Helios](https://github.com/HeliosVirtualCockpit/Helios)


## Requirements
- Windows (x64)
- Saitek FIP device
- Virtual Display Driver (optional) [VirtualDrivers/VirtualDisplayDriver](https://github.com/VirtualDrivers/Virtual-Display-Driver)

## Installation
- Download latest release .zip from [Releases](https://github.com/SmokeyDev/FIPUnlocked/releases/latest)

## Usage
1. Edit `config.json` to your liking
2. Connect Saitek FIP
3. Run the application


## Known Limitations / Bugs
- Cloning the screen in native FIP resolution (320/240px) maxes out at ~21 FPS
- Cloning the screen in higher resolution requires scaling that affects output FPS

## Performance
During my testing, app uses only ~7MB of RAM and negligible CPU usage


## Page Config Example
```
			{
			  "name": "Name of the screen",
			  "capture_region": {
			    "x": 0, // x coordinate of the left side of the capture region
			    "y": 0, // y coordinate of the top side of the capture region
			    "width": 320, // width of the capture region - will be scaled to 320px
			    "height": 240 // height of the capture region - will be scaled to 240px
			  },
			  "fip_offset": {
			    "x": 0, // offset from the left side of the screen
			    "y": 0 // offset from the top side of the screen
			  },
			  "scale_mode": "nearest" // or "bilinear"
			},
```

## Next Steps
- DCS-BIOS integration (input)
- Custom Gauges / Panels support with DCS-Bios interation (output)
- Config editor within the app
- Gauges editor within the app