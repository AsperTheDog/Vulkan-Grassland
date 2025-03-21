# Grass Rendering in Vulkan
This is my attempt at making an efficient grass renderer with dynamic wind. A pretty good attempt, if I say so myself.
I am incredibly happy with the result, there are of course things that could be added and improved, but I am satisfied for now.

https://github.com/user-attachments/assets/d0db0110-cfd7-46b6-adc9-44769712be68

Scene properties:
- Hardware: Ryzen 5700X + RTX 3070ti
- Resolution: 1440p render
- Grass blades: around 5.000.000 before culling, around 1.800.000 actually rendered
- Performance: ~300fps

# Controls
- Move around with WASD. There is no gravity and no collision, so you just fly around in the direction you are looking at
- Unlock the mouse with the letter Q
- Toggle ImGui visibility with the letter O

# Features
### 100% Procedural
No imported models or textures — everything is generated at runtime. This was a fun challenge that came up while making it.

### Tessellated Terrain
The landscape is generated using a heightmap with procedurally calculated normals for the underlying terrain. 
The terrain barely gains anything from the tessellation as it tends to be completely covered by the grass, but I thought it was a good excuse to learn how to use tessellation in Vulkan

### Mass Grass Instancing
The meat of the project. The grass is rendered blade per blade by using GPU instancing. The layout is calculated with a GPU compute shader.
The system is done by dividing the region in tiles. These tiles are the discrete unit that the system uses to determine different properties and to filter when culling.
It features several optimization features to keep frametimes from tanking:
- 4 LOD regions with varying geometry detail and varying density. These are defined as square rings of a determined length.
- CPU frustum culling per tile. Only updated when needed (if the camera moves). I figured since everything else is done in the GPU (and since GPU culling is incredibly annoying due to how hard it is to have a flexible and scalable scan and compact algorithm for the GPU) I could leave it to the CPU and it'd be good enough (It is, in my opinion, I'm very happy with the result).
- Everything is calculated with one single GPU compute dispatch. This was specially challenging because tiles of different densities occupy different amounts of space. But I believe I was able to come up with a very efficient system to prevent the tile system from slowing down the rendering process.
- The system only calculates things if they must be calculated. This means that most of the data is reused every frame unless recalculation is absolutely necessary. Up to double the fps are obtained in some (ideal) circumstances thanks to this (fear not, however, the improvement is highly noticeable most of the time. You have to go out of your way to negate the benefits of this).

### Wind Simulation
A dynamic wind system sways the grass in a very calming way. 
What kind of grass renderer would this be if there was no wind to move the grass around??

### Automatic Terrain Generation
The terrain automatically expands and regenerates as the camera moves through the world.
The system detects when you move tiles and shifts everything accordingly, so (unless you look far into the horizon) it looks like the terrain is infinite.
Figured it would be a waste to have a fully procedural scene that couldn't generate dynamically around the camera.

### Procedural Skybox
A simple sky with a procedural sun to support atmospheric lighting.
This skybox is just a gradient with a simple sun. 
I may implement some physically based atmospheric scattering in the future, but this looks good to me for now.

### Atmospheric Fog
A very simple quadratic fog post-processing effect to tie it all together. 
This was incredibly basic, but it served as a good excuse to learn how to use input attachments in a renderpass.

### ImGui Customization
Every parameter that I could come up with can be updated at runtime with ImGui.

![image](https://github.com/user-attachments/assets/92db6a11-0172-4886-9e1f-982b639e5bd4)

# Building

The only dependency needed for the project is the VulkanSDK (The optional packages SDL2, Volk and GLM must be installed with the SDK) and C++ 20.
Right now the only provided project configuration is a visual studio solution. I plan on adding a CMake/Premake/Scons configuration in the future but for now if you want to run it in any other way you'll have to configure it yourself.

My personal library for Vulkan (https://github.com/AsperTheDog/VkPlayground) and Dear ImGui (https://github.com/ocornut/imgui) are also used in the project, they are both included in the repository as submodules, so just make sure to clone the repo with `--recursive` 

# Frame layout


![Untitled Diagram drawio(7)](https://github.com/user-attachments/assets/51066a42-2ed7-405d-9507-64989acf0586)


This could probably be improved by merging submissions together into one command buffer when the hardware allows it, but since most of these steps are rarely performed (only when ImGui parameters are edited or the player changes tiles) I don't think it would make much of a difference.
I may come back in the future and give it a try.

# Future work
I am happy with how this looks, and I was able to do what I wanted to do and even go beyond, so I doubt I'll implement any of these. But who knows, if I want to try implementing these things I may just come back and do it here
- Physically based atmospheric scattering for the skybox
- Day night cycle
- Better lighting for the grass
- Volumetric clouds
- Optiomized systems to render terrain very far away
- A river / water rendering
- More advanced terrain generation for the heightmap
