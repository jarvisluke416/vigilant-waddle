C++ Song Maker — Build & Run Instructions
Requirements

You will need:

Windows
Visual Studio Code
MinGW-w64 / GCC
A working C++ compiler
The project files, including main.cpp and the song .txt files
1. Install MinGW-w64

If you don't already have a C++ compiler installed, install MinGW-w64 and make sure g++ is added to your Windows PATH.

To check that it is installed, open a new terminal in VS Code and run:

g++ --version

If you see a GCC version number, the compiler is ready.

2. Open the Project

Open the project folder in VS Code.

Your folder should look something like:

C++Audio/
│
├── main.cpp
├── song1.txt
├── song2.txt
├── song3.txt
├── song4.txt
└── README.md

Important: Keep the song .txt files in the same folder as the program executable.

3. Open the VS Code Terminal

In VS Code, select:

Terminal → New Terminal

Make sure the terminal is located in your project folder.

For example:

C:\Users\YourName\Downloads\C++Audio>

You can check the current folder with:

cd
4. Compile the Program

Run:

g++ main.cpp -o CppSongMaker.exe -lwinmm -mwindows

This compiles main.cpp and creates:

CppSongMaker.exe

The -lwinmm option links the Windows multimedia library required for audio playback.

The -mwindows option builds the program as a Windows GUI application instead of opening a console window.

5. Run the Program

After compiling, run:

.\CppSongMaker.exe

The Song Maker window should open.

6. If You Get a song.txt Error

The program needs to be able to find its song files.

If the program says:

Could not open song.txt

make sure the .txt files are located in the same directory from which the program is running.

For example:

C++Audio/
│
├── CppSongMaker.exe
├── main.cpp
├── song1.txt
├── song2.txt
├── song3.txt
└── song4.txt

If your program specifically expects a file named song.txt, make sure that file exists as well.

7. Recompile After Making Changes

Whenever you modify main.cpp, compile it again:

g++ main.cpp -o CppSongMaker.exe -lwinmm -mwindows

Then run:

.\CppSongMaker.exe
8. Recommended One-Line Build Command

For quick development, you can use:

g++ main.cpp -std=c++17 -O2 -o CppSongMaker.exe -lwinmm -mwindows

This enables C++17 and basic compiler optimization.

Troubleshooting
g++ is not recognized

Your compiler is either not installed or its bin directory is not in your Windows PATH.

Check with:

g++ --version

If that fails, install/configure MinGW-w64.

fatal error: windows.h: No such file or directory

You are probably using a compiler/environment that does not include the Windows development headers. Use a Windows MinGW-w64 installation.

undefined reference to ... winmm

Make sure -lwinmm is included in the compile command:

g++ main.cpp -o CppSongMaker.exe -lwinmm -mwindows
The program opens but cannot find the song

Make sure the required .txt files are beside the .exe, and run the program from the project directory.

Quick Start

Once everything is installed, the basic workflow is:

cd "C:\path\to\C++Audio"
g++ main.cpp -std=c++17 -O2 -o CppSongMaker.exe -lwinmm -mwindows
.\CppSongMaker.exe

That's it. After changing the C++ code, simply compile again and run the executable.
