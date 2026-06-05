# DV2551 DirectX 12 Rendering Project
This repository contains a custom DirectX 12 rendering engine developed for a university course (DV2551 - 3D Programming III).
The project implements screen-space raymarched volumetric fog using both a traditional Pixel Shader approach and a Compute
Shader approach in order to compare performance between the two.
## Prerequisites
* Windows 10/11
* CMake
* DirectX 12 compatible GPU

## Build Instructions
* Clone or download the repository and navigate to the root directory.
* Generate build files:
```bash
cmake -B build
```
* Compile the project:
```bash
cmake --build build --config Release 
```
* Run the generated executable located in the `build/Release` directory.

## Controls and Hotkeys
`W` - Move camera forward
<br> `A` - Move camera left
<br> `S` - Move camera backward
<br> `D` - Move camera right
<br> `Space` - Move camera up
<br> `Left Shift` - Move camera down

`F` - Toggle between Pixel Shader and Compute Shader fog
<br> `G` - Toggle volumetric fog on/off
 
`1` - Set raymarching steps to 2
<br> `2` - Set raymarching steps to 4
<br> `3` - Set raymarching steps to 8
<br> `4` - Set raymarching steps to 16
<br> `5` - Set raymarching steps to 32
<br> `6` - Set raymarching steps to 64
<br> `7` - Set raymarching steps to 128

`ESC` - Close the application

## Project Structure
* `/src` contains all C++ headers and source files.
* `/src/Renderer.cpp` contains most of the rendering code.
* `/src/shaders` contains all HLSL shaders.
* `/src/shaders/VolumetricFogPS.ps.hlsl` contains pixel shader volumetric fog implementation.
* `/src/shaders/VolumetricFogCS.cs.hlsl` contains compute shader volumetric fog implementation.
* `/D3D12` contains the DirectX 12 Agility SDK includes and binaries.