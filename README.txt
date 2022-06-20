--Don't Be a Hero--

A quick a dirty 2D top-down swarming brawl game made as practice for Global Game Jam 2017.

CONTROLS (context senstive)
---------------------------
left mouse click	 	select unit (except goodman, who is non-selectable)
left mouse click-and-drag	select multiple units at once
left mouse click		set destination waypoint for selected units
left mouse click on goodman	send missle towards goodman with selected blue units	
space 				de-select units	

![Debug Graphics Enabled for (1) Collision (2) Navigation Grid Occupancy (3) Navigation Waypoints](https://github.com/tmf7/Dont-Be-a-Hero/graphics/DBaH_Debug_README.gif)

![Debug Graphics Disabled](https://github.com/tmf7/Dont-Be-a-Hero/graphics/DBaH_NoDebug_README.gif)

All image assets are from opengameart.org.
All sound assets were purchased from the Unity Asset store at https://www.assetstore.unity3d.com/en/#!/content/50235
All font assets are from http://all-free-download.com/font/sort-by-popular/page/3/ [ Levi Brush	author: Levi Szekeres ]

Project initially created using Microsoft Visual Studio Express 2015 for Windows Desktop,
as well as Simple Directmedia layer 2.0.3 and a few of its extensions.

Master branch tracks the current stable build.
Updates branch tracks changes in progress (not necessarily a stable build)
Both include my original source code and assets used.

To get the program running:
1) Pull or clone this repository
2) Create a new project with your IDE of choice
3) Follow the instructions for downloading and including SDL2, SDL_ttf, and SDL_image in your project at:
https://www.libsdl.org/download-2.0.php
https://www.libsdl.org/projects/SDL_ttf/
https://www.libsdl.org/projects/SDL_image/
respectively.
	
Notes: 
-> SDL_ttf is used as a font handling extension to SDL2
-> SDL_Image is used to load image file types beyond bitmaps



