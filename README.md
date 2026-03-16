# moq-bench

Utility to evaluate relay performance.

## Configuration

Profile configuration per track is configured in a `config.ini` file that is provided to the program using
the `-c` option. See the [config.ini template](templates/config.template.ini) for an example configuration.

Each section in the `config.ini` defines a test for a publish track and subscribe track.
The `namespace` and `name` together should be **unique** for the section, which is the track.

Sections are laid out as follows:

```ini
[TRACK]               ; MUST be unique
namespace           = ; MAY be the same across tracks, entries delimited by /
name                = ; SHOULD be unique to other tracks
track_mode          = ; (datagram|stream)
priority            = ; (0-255)
ttl                 = ; TTL in ms
time_interval       = ; transmit interval in floating point ms
objects_per_group   = ; number of objects per group >=1
first_object_size   = ; size in bytes of the first object in a group
object_size         = ; size in bytes of remaining objects in a group
start_delay         = ; start delay in ms - after control messages are sent and acknowledged
total_transmit_time = ; total transmit time in ms
```

> [!IMPORTANT]
> Each section **MUST** not share the same `namespace + name` combination. If `namespace` is the same between sections, `name`
> **MUST** be different between sections.

## Building

Configure cmake using the following:

```
cmake -B ./build -DLINT=ON -DCMAKE_BUILD_TYPE=Release
```

Build the programs using the following:

```
cmake --build build -j 4
```

The binaries will be under `./build`

## Using

The `moqbench` program uses a config file to build tracks. It builds a conference
client by creating 1 publisher track for every Track section in the config file,
and N - 1 subscriber tracks for every Track section. The client does not subscribe
to its own publisher track.
