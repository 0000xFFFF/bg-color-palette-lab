# bg-color-palette-lab
group wallpapers by color palette


## Requirements
* make
* clang++
* OpenCV (libopencv)

## Build & Install
```bash
make all
sudo make install
```


## Usage
```bash
# Recursevly validate images in dir. Test if they can be loaded. Multithreaded (fast).
./bgcpl-validator <dir>

# Show most dominant colors in image and make a color palette.
./bgcpl-palette <file.png/jpg/...>

# Group images by color groups below. Multithreaded.
./bgcpl-grouper <dir>
```

### Color Groups

| Name             | Hue Min | Hue Max | Sat Min | Sat Max | Bright Min | Bright Max | Representative Color (B, G, R) |
|------------------|---------|---------|---------|---------|-------------|-------------|-------------------------------|
| Blue_Cool        | 200     | 260     | 0.3     | 1.0     | 0.3         | 1.0         | (255, 100, 50)                |
| Red_Warm         | 340     | 20      | 0.3     | 1.0     | 0.3         | 1.0         | (50, 50, 255)                 |
| Green_Nature     | 80      | 140     | 0.3     | 1.0     | 0.3         | 1.0         | (50, 255, 100)                |
| Orange_Sunset    | 20      | 50      | 0.4     | 1.0     | 0.4         | 1.0         | (50, 165, 255)                |
| Purple_Mystical  | 260     | 300     | 0.3     | 1.0     | 0.3         | 1.0         | (255, 50, 200)                |
| Yellow_Bright    | 50      | 80      | 0.4     | 1.0     | 0.5         | 1.0         | (50, 255, 255)                |
| Pink_Soft        | 300     | 340     | 0.3     | 1.0     | 0.4         | 1.0         | (200, 100, 255)               |
| Cyan_Tech        | 160     | 200     | 0.4     | 1.0     | 0.4         | 1.0         | (255, 200, 100)               |
| Dark_Moody       | 0       | 360     | 0.0     | 1.0     | 0.0         | 0.25        | (40, 40, 40)                  |
| Light_Minimal    | 0       | 360     | 0.0     | 0.3     | 0.8         | 1.0         | (240, 240, 240)               |
| Monochrome       | 0       | 360     | 0.0     | 0.15    | 0.25        | 0.8         | (128, 128, 128)               |
| Earth_Tones      | 25      | 45      | 0.2     | 0.7     | 0.3         | 0.7         | (100, 150, 200)               |

