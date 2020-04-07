## FreeAge ##

FreeAge aims to be an open-source re-implementation of the game engine used by
Age of Empires 2: Definitive Edition (similar to [OpenAge](https://github.com/SFTtech/openage/)).
Its goal is to improve upon some aspects of the original that go along with its
dated engine (stemming from the original version of the game from 20 years ago),
for example:

* Preventing players with slow computers from slowing down multiplayer matches for all participants.
* Enabling games with more than 8 players in total, for example 5 vs 5 matches.
* Allowing to re-connect to multiplayer matches after a connection drop.

To play FreeAge, you must own an officially purchased copy of Age of Empires 2:
Definitive Edition, since FreeAge uses the original game assets.

FreeAge is a fan project and is not endorsed by or affiliated with Microsoft,
Forgotten Empires, Tantalus Games, Wicked Witch or any other company related to
the original game in any way. The project is one among similar efforts such as
for example [OpenAge](https://github.com/SFTtech/openage/) for the same game, or
[OpenRA](http://openra.net/) and [OpenSAGE](https://github.com/OpenSAGE/OpenSAGE/) for others.

If we by mistake violate any trademarks / copyright / etc., please
tell us and we will aim to correct it as soon as possible. Our goal is to improve
the game (and therefore eventually increase its popularity), not to cause any
harm to the owners of the franchise.

### State ###

The project is currently in an early state. Many but not all parts of the gameplay
within the Dark Age are implemented, for example, commanding units with simple
pathfinding, gathering resources, constructing
buildings, and fighting with militia, villagers, and the starting scout.
The game is however already fully playable over the Internet and thus offers a
good foundation to incrementally add the missing functionalities.
Contributions are welcome.

The game has been tested on Windows and Linux.

### Download & Running ###

Binary downloads for Windows are provided via GitHub Releases.
On Linux, see below for how to build the project yourself.

TODO

Run `FreeAge.exe` (Windows) / `FreeAge` (Linux) to start the game.

A settings dialog will show where you may enter your player name and adjust
some preferences. In addition, the paths to the asset files from the original
game can be configured there. Normally, the game should automatically detect
those, but they may be set manually in case this fails.

There are three options to create or join a game lobby:

* **Create new lobby**: Creates a new lobby, starting the game server locally on your PC.
* **Create lobby on existing server**: Creates a new lobby, connecting to an
  already running instance of the game server (that may run on another computer such
  as an external server).
* **Join existing lobby**: Connects to a game server where a lobby is already being hosted on.

The lobby itself should be mostly familiar from the original game. Once all players
confirmed that they are ready, the host may start the game. It is possible to
start a game with only a single player for testing purposes.

Hotkeys are currently hard-coded to the following:

* Arrow keys: Scrolling
* H: Jump to town center
* A: Build villager / Build militia / Economy buildings
* S: Military buildings
* Q - R, A: Grid mapping for constructing the corresponding buildings

### Playing over the Internet ###

For playing online, all participants of a match must connect to the same game server.
In principle, this game server may run either on a PC of one of the players, or
on an external server PC.

In practice, direct connections without an external server PC may unfortunately
be difficult to achieve (unless all players are within the same local network)
due to the way Internet connections work. The following conditions must be met:

* All players must use either IPv4 or IPv6.
* Any firewalls must permit the required connections.
* Any NAT (network address translation) schemes must let the required connections through.

Since the game server listens on TCP port 49100 for incoming TCP connections,
this protocol/port combination must therefore be forwarded in all relevant routers,
and firewall(s) must be configured to allow incoming connections for this for
the PC that runs the game server.

In practice, techniques used by ISPs such as "DS (DualStack) Lite" that assign one IPv4
address to many clients, and others not having IPv6 connectivity, may make it
very hard to impossible to establish direct connections. An alternative is to
run the game server on an external server PC having a unique IPv4 address. Virtual
servers are very cheap to rent and should be suitable, since the game server
does not require many hardware resources. (However, you will need some expertise to
set the server up correctly.)

To run the game server on its own, start it from the terminal as follows (assuming a
Linux environment):

```
./FreeAgeServer <6-character-host-password>
```

The server will then listen for incoming host connections (where the given password
needs to be used). After a match is started and all players disconnected from it again,
the server process will exit.

### Building ###

If you want to build the application yourself, you need Qt5 as a single
external dependency. Version 5.12.0 is known to work, although similar versions
would also be expected to work with few or no changes required.

The project uses C++17, so a relatively recent compiler may be required.

On **Linux**, compiling has been tested with GCC 8. You can follow the standard scheme
for building CMake-based projects, for example as follows:

```bash
mkdir build
cd build
cmake -CMAKE_BUILD_TYPE=RelWithDebInfo -GNinja ..
ninja
```

On **Windows**, compiling has been tested with Visual Studio 2019. Simply create
a .sln file with CMake (specifying the path to the Qt5 installation), open and
build it. Note that in contrast to GCC, no effort
has been made to fix compiler warnings reported by Visual Studio.

**Both Windows and Linux**: Notice that the game caches the texture atlas
configurations that it computes for all sprites, since these take some time
to compute. Existing cached files are distributed with the binary releases.
You may want to copy the folder `graphics_cache` from a binary distribution into
the working directory where you execute the compiled binary. Otherwise, the first
game start will be somewhat slow.

### Contributing ###

Contributions are welcome. Please follow the existing coding style to keep it
consistent.

14!
