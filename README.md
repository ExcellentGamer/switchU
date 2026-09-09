# SwitchU
A remake of the Nintendo Switch's UI for the Wii U

## Controls:
- `L Stick/DPad`: move around the menu
- `A Button`: enter menus, load games, ect.
- `B Button`: close open menus or subcategories
- `+ Button`: open the options menu for a game

## Misc:
- You can ignore games by creating an "ignore.txt" file in your "sd://switchU/" folder!
- You can create custom icons for any game in the "sd://switchU/custom_icons/" folder!

## Building:
### Dependencies
- [wut](https://github.com/devkitPro/wut)
- [librpxloader](https://github.com/wiiu-env/librpxloader)
- SDL2 Wii U
- SDL2 image Wii U
- SDL2 ttf Wii U

Install Devkitpro following [the official guide for your OS](https://devkitpro.org/wiki/Getting_Started)

Install wut:
```
(sudo) (dkp-)pacman -Syu --needed wiiu-dev
```

Install The SDL2 Wii U Libraries:
```
(sudo) (dkp-)pacman -S wiiu-sdl2 wiiu-sdl2_image wiiu-sdl2_ttf
```

### Compiling
Simply run `make` at the root of the repo
```
make (path to Makefile)
```

## Building using the Dockerfile
You can also build SwitchU using Docker:

```bash
# Build docker image (only needed once)
docker build . -t switchu_builder

# make 
docker run -it --rm -v ${PWD}:/project switchu_builder make

# make clean
docker run -it --rm -v ${PWD}:/project switchu_builder make clean

# Credits
- [BenchatonDev](https://github.com/BenchatonDev) Co-writer on the projects code.
- [Ashquarky](https://github.com/ashquarky) For porting SDL2 to Wii U
- [Dimok](https://github.com/dimok789) For making VPADInput.h and WPADInput.h
- [GaryOderNichts](https://github.com/GaryOderNichts) For making Wii U ident which I used as 'base'
- DanielKO For emotional support and helping me figure out the rpx loader
