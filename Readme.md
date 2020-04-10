## FreeAge ##

FreeAge aims to be an open-source re-implementation of the game engine used by
Age of Empires 2: Definitive Edition (similar to [OpenAge](https://github.com/SFTtech/openage/)).
Its goal is to improve upon some aspects of the original that go along with its
dated engine (stemming from the original version of the game from 20 years ago),
for example:

* Preventing players with slow computers from slowing down multiplayer matches for all participants.
* Enabling games with more than 8 players in total, for example 5 vs 5 matches.
* Allowing to re-connect to multiplayer matches after a connection drop.

The game has been tested on Windows and Linux.
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
Contributions are welcome!

To know what to expect, here are some examples of known limitations:

* Units cannot be garrisoned into buildings.
* Units do not auto-attack enemy units; they all have to be microed manually.
  Think of them as all being villagers, or being in no-attack stance.
* Units often get stuck when moving in groups.
* Villagers do not automatically re-task to another nearby tile of e.g., berries, gold, or stone,
  if there are too many villagers tasked to a single tile.
* The up-to-date state of enemy buildings is visible through the fog of war after the
  terrain has been explored once.
* Villagers do not fell trees, instead they direcly harvest from the original trees.
* Resources never deplete.

### Download & Running ###

Binary downloads for Windows are provided via GitHub Releases.
On Linux, see below for how to build the project yourself.

TODO

Run `FreeAge.exe` (Windows) / `FreeAge` (Linux) to start the game.

A settings dialog will show where you may enter your player name and adjust
some preferences. In addition, the paths to the asset files from the original
game can be configured there. If you have the original game installed on Steam,
the game should automatically detect those. They may be set manually in case
this fails or if the game is installed via the Microsoft Store (for which no
automatic detection was implemented yet). Here are some examples of how these
paths should be set:

**Windows (Steam):**
```
AoE2DE folder path: TODO
Mods folder path: TODO
```

**Linux (Steam):**
```
AoE2DE folder path: /home/<username>/.local/share/Steam/steamapps/common/AoE2DE
Mods folder path: /home/<username>/.local/share/Steam/steamapps/compatdata/813780/pfx/drive_c/users/steamuser/Games/Age of Empires 2 DE/<some_large_number>/mods
```

*If you have the game installed via the Microsoft Store, let us know the paths on your system
and we can add it as an example here.*

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
* H:          Jump to town center
* A:          Build villager / Build militia / Economy buildings
* S:          Military buildings
* Q - R, A:   Grid mapping for constructing the corresponding buildings
* Del:        Delete building or unit
* 1 - 0:      Control groups (define by holding Ctrl)
* Space:      Jump to selected object(s)

### Playing over the Internet ###

For playing online, all participants of a match must connect to the same game server.
In principle, this game server may run either on a PC of one of the players, or
on an external server PC. Please notice that if the game server runs on a player PC,
it will exit once this player exits the game. So, in case other players in the match
want to play on after this player resigns, the game must be left running on this PC.

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
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja ..
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

Source files that are used by both the client and the server application go into
src/FreeAge/common. Otherwise they belong into src/FreeAge/client or src/FreeAge/server.
Unit test may be added to src/FreeAge/test.

Since it may perhaps be hard to read into, here is a brief overview of the main
(client-server) architecture:

All game-relevant actions on the client are sent to the server for execution via
network messages. All messages are defined in src/FreeAge/common/messages.hpp,
and most of the functions to create these messages can also be found there.

The server code for running the main part of the game, including handling incoming
messages, is in src/FreeAge/server/game.cpp. The server simulates the game with
a fixed frame rate. Every time something happens that needs to be sent to a client,
it is sent out within a large accumulated packet at the end of a simulation time
step, prefixed with the server time of this time step.

The clients attempt to keep track of when they expect to receive packets from the
server (relative to their own time measuring). Based on this, the ServerConnection
(src/FreeAge/client/server_connection.hpp) estimates what a good server time is
to be displayed each time the game state needs to be rendered, attempting to
display an as-recent-as-possible game state while (hopefully) not lacking the
reception of any incoming server messages that are needed to render it.
(This means that depending on the ping of each client, it may lag more or less
beind the actual server time in its display of the game. This is by design.
This way, clients with a good ping are not affected by clients with a bad ping.)
The ServerConnection smoothly adjusts the displayed server time based on new time
offset measurements (done via regularly sent ping messages). This means that the
displayed time on the client can run a little bit slower or faster than the actual
time when the estimated time offset to the server changes.

Fortunately, most of the time it is not required to keep all of the above in mind
when implementing new message types. The client stores all messages until the
displayed server time is at least the game step time that the server preceded the
message with, and parses them only at this point. This means that any actions
caused by parsing a message can be applied directly by the parsing code.

Notice that the client does not attempt to simulate the execution of actions that
were performed on the client, but not acknowledged by the server yet. While this
might be able to reduce the perceived lag somewhat, there are cases where a
proper simulation is not possible. For example, consider sending a scout into
terrain covered by the fog of war. To prevent cheating, the clients do not know
what lies within the fog of war. There might be a large forest, forcing the scout
to run around either to the left or the right of it. The client thus cannot know
the direction in which the scout would start to run, so it cannot simulate it
properly before it is told about it by the server.

14!
