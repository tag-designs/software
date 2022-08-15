# Proto Directory

The this directory is organized as follows

```
.
├── CMakeLists.txt
├── config-gen.cc
├── README.md
├── tag.proto
├── tagdata.proto

```
The files `tag.proto` and `tagdata.proto` contain the Protobuf message definitions
for the host/tag communication.  The directory `proto-cpp` contains the directions (cmake file) for
building the host libraries using the Protobuf c++ libraries.  The directory `proto-c` contains directions 
for building nanopb libraries for specific tags.  

Each tag is provided a separate subdirectory within the `proto-c` directory.  
This subdirectory contains nanopb options for the protocol definitions 
that define the maximum number of repeated messages and messages that should be elided from the library for that tag -- for
example, datalog messages for other tag types.

Finally, the `config-defaults` directory includes a `json` file for each tag type that defines the default configuration 
message for that tag.  The directory includes a program to translate the `json` file into a binary message definition.  When
an unconfigured tag is asked for its configuration, this binary file is returned and decoded by the host libraries.  Host
applications can use this message to determine the "capabilities" of a specific tag.
