# fastbird engine
'fastbird engine' is being developed by **[fastbird dev studio](http://jungwan.net)** creating a sci-fi
game currently. It is highly componentized engine that consists of
three layers - **Core**, **Engine** and **Facade/Dedicated** layer. Each layer contains serveral libraries(.lib) and modules(.dll). These libraries and modules can be easily reused for other applications. [fastbird engine architecture.pdf](http://jungwan.net/publications/fastbird_engine_architecture_en.pdf) explains the details about the engine structure.

Currently the engine supports Windows. OS X support is planned.

## External libraries for modules
Most modules of fastbird engine is self-contained and do not need external
libraries but the following modules are exceptions and the specified external 
libraries are required to build.

* boost::filesystem - FBFileSystem.dll
* lua 5.2 - FBLua.dll
* zlib - FBRendererD3D11.dll
* openal-soft - FBAudioPlayer.dll
* ALURE - FBAudioPlayer.dll
* libvorbis - FBAudioPlayer.dll
* libogg - FBAudioPlayer.dll, FBVideoPlayer.dll
* libtheora - FBVideoPlayer.dll
* bullet 2.82 - FBPhysics.dll

[APIDesign.uml](http://fastbirddev.net/download_APIDesign.php)([StarUML](http://staruml.io/) file) in the root directory is helpful to check the all of internal and external dependencies in the engine.
