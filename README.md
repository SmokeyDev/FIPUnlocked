# Saitek FIP Controller

## Description

This project aims to support Saitek/Logitech FIP (Flight Instrument Panel) in unsupported games.
Current version allows cloning selected parts of the monitors onto FIP 1-6 pages.
S1-S6 buttons can be configured to work as simple keyboard emulator supporting single key presses or combinations like `LShift + LAlt + L`.

Virtual Display Driver can be used to add virtual monitor, which can be used to display exported panels from DCS - e.g. with [Helios](https://github.com/HeliosVirtualCockpit/Helios)


![FIP Controller Screenshot](/preview.jpg)

## Requirements

- Windows (x64)
- Saitek FIP device
- Virtual Display Driver (optional) [VirtualDrivers/VirtualDisplayDriver](https://github.com/VirtualDrivers/Virtual-Display-Driver)

## Installation

- Download latest release .zip from [Releases Page](https://github.com/SmokeyDev/FIPUnlocked/releases/latest)

## Usage

1. Edit `config.json` to your liking
2. Connect Saitek FIP
3. Run the `FIPUnlocked.exe`

## Known Limitations / Bugs

- Cloning the screen in native FIP resolution (320/240px) maxes out at ~20 FPS
- Cloning the screen in higher resolution requires scaling that affects output FPS

## Performance

During my testing, app uses only ~7MB of RAM and negligible CPU usage

For the best performance I suggest capturing native resolution - 320x240px

## Config Example
**!!! Do _not_ add any comments to your config.**
Comment is a text with `//` prefix. If you want to copy the config below you'll need to remove them.

Available modifier keys:
- `LShift` / `RShift` - Left / Right Shift
- `LAlt` / `RAlt` - Left Right Alt
- `LControl` / `LCtrl` / `RControl` / `RCtrl` - Left / Right Control
- Function keys like `F1`, `F5`
- Special characters like `\`, `'` or `,`
- `Escape`, `Space`, `Enter`
- Arrows: `ArrowLeft`, `ArrowRight`, `ArrowUp`, `ArrowDown`

```
{
 "debug": false, // Shows more detailed logs within the app if set to true
 "show_fps": true, // Shows FPS in top left corner of the FIP
 "show_screen_names": true, // Shows screen names in top left corner of the FIP
 "target_fps": 30, // Target FPS. 30 is recommended, likely not achievable. Setting this number higher might result in higher CPU/RAM usage.
 "button_mappings": {
  "S1": "LShift+A",
  "S2": "RAlt+C",
  "S3": "3",
  "S4": "4",
  "S5": "",
  "S6": "" // Leave empty for unassigned (button will not illuminate)
 },
 "pages": [
  {
   "name": "Name of the screen",
   "capture_region": {
    "x": 0, // x coordinate of the left side of the capture region - offset from the left of the main monitor
    "y": 0, // y coordinate of the top side of the capture region - offset from the top of the main monitor
    "width": 320, // width of the capture region - will be scaled to 320px
    "height": 240 // height of the capture region - will be scaled to 240px
   },
   "scale_mode": "nearest" // or "bilinear". Nearest is faster, bilinear might look better with text but may lower FPS
  },
  {
   "name": "Test 2",
   "capture_region": {
    "x": 100,
    "y": 100,
    "width": 640,
    "height": 480
   },
   "scale_mode": "nearest"
  }
  // Add more screens if needed. 6 is max
 ]
}

```

## Next Steps

- DCS-BIOS integration (input)
- Custom Gauges / Panels support with DCS-Bios integration (output)
- Config editor within the app
- Profiles
- Gauges editor within the app
