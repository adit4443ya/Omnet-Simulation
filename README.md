
# Distributed Remote Execution Simulation using OMNeT++

This project simulates a remote program execution environment where clients split a task (e.g., finding the maximum in an array) into subtasks and send them to multiple servers for processing. Servers may behave honestly or maliciously, and clients use a majority vote and a gossip protocol to determine the correct results and evaluate server performance over two rounds of task execution.

The simulation is implemented using OMNeT++ and is structured to be modular and extensible. The project comprises several files that together define the network topology, module behavior, and simulation parameters.

---

## Project Structure

```
├── Makefile
├── out
│   ├── clang-debug
│   │   └── src
│   │       ├── Client.o
│   │       ├── Client.o.d
│   │       ├── messages_m.h.d
│   │       ├── messages_m.o
│   │       ├── messages_m.o.d
│   │       ├── Server.o
│   │       ├── Server.o.d
│   │       └── Test_dbg
│   └── clang-release
│       └── src
│           ├── Client.o.d
│           ├── messages_m.h.d
│           ├── messages_m.o
│           ├── messages_m.o.d
│           ├── Server.o
│           ├── Server.o.d
│           └── Test
├── README.md
├── simulations
│   ├── omnetpp.ini
│   ├── package.ned
│   ├── results
│   │   └── General-#0.sca
│   ├── run
│   └── topo.txt
└── src
    ├── Client.cc
    ├── Client.ned
    ├── DistributedNetwork.ned
    ├── Makefile
    ├── messages_m.cc
    ├── messages_m.h
    ├── messages.msg
    ├── package.ned
    ├── Server.cc
    ├── Server.ned
    ├── Test
    └── Test_dbg

9 directories, 34 files
```

---

## File Descriptions

### src/Client.cc
- **Purpose:**  
  Implements the Client module responsible for:
  - Reading the network topology from `topo.txt`
  - Establishing dynamic connections to server and client modules
  - Dividing a task (processing an array of integers) into subtasks
  - Sending subtasks to a majority of connected servers
  - Receiving and aggregating subtask results from servers
  - Exchanging gossip messages with other clients to share server performance scores
  - Initiating a second round of task execution using top servers based on the averaged scores

- **Key Features:**  
  - **Modularity:** Code is divided into helper functions such as `loadTopology()`, `establishServerLinks()`, `dispatchInitialSubtasks()`, and `broadcastGossip()`.
  - **Data Structures:** Uses maps, vectors, and sets to track server scores, results, and processed gossip messages.
  - **Gossip Protocol:** Implements message logging to avoid duplicate message processing.
  - **Dynamic Connection Handling:** Reads a topology file and creates bidirectional connections among modules.

### src/Server.cc
- **Purpose:**  
  Implements the Server module responsible for:
  - Receiving subtasks from clients
  - Extracting and processing an array of integers to compute the maximum value
  - Optionally modifying the result if the server is set to operate maliciously (e.g., subtracting 10)
  - Sending the computed result back to the appropriate client using the correct output gate

- **Key Features:**  
  - **Modular Helper Functions:** Functions like `extractSubtaskData()`, `computeMaxValue()`, `logTaskReception()`, and `sendResultMessage()` break down the task into clear, reusable units.
  - **Logging & Counters:** Tracks the number of tasks processed and logs each task reception for debugging.
  - **Configurable Behavior:** The server's behavior (honest or malicious) is determined by a module parameter.

### Client.ned & Server.ned
- **Purpose:**  
  Define the NED descriptions for the Client and Server modules. These files declare the module parameters, gate definitions, and display properties.
  
- **Key Features:**  
  - **Parameter Definitions:** Specify unique identifiers (e.g., `clientId`, `serverId`) and other configuration parameters.
  - **Gate Vectors:** Define input and output gates (e.g., `peer$o` and `peer$i`) used for dynamic connections.
  - **Visualization:** Include display hints to visually distinguish Clients and Servers in the OMNeT++ IDE.

### DistributedNetwork.ned
- **Purpose:**  
  Describes the overall network topology, assembling client and server modules into a complete network.
  
- **Key Features:**  
  - **Network Composition:** Specifies the number of client and server submodules using parameters such as `numClients` and `numServers`.
  - **Connectivity:** May include pre-defined connections or enable dynamic connection creation based on the topology file (`topo.txt`).

### omnetpp.ini
- **Purpose:**  
  The OMNeT++ configuration file that defines simulation settings and parameters.
  
- **Key Features:**  
  - **Simulation Settings:** Sets the simulation time limit (`sim-time-limit = 500s`) and selects the network to simulate (e.g., `network = test.DistributedNetwork`).
  - **Module Parameters:** Assigns client and server identifiers and behavior (e.g., `*.client[0].clientId = "C1"`, `*.server[1].isMalicious = true`).
  - **Dynamic Scaling:** Defines the number of clients and servers using `*.numClients` and `*.numServers`.

### topo.txt
- **Purpose:**  
  Provides a human-editable file that defines the topology of the network. It lists the number of clients and servers and specifies which nodes should be connected.
  
- **Key Features:**  
  - **Simple Format:** Contains entries like `Clients=3` and `Servers=5`, followed by connection instructions (e.g., `Client_0 Server_0`, `Client_2 Client_0`).
  - **Dynamic Topology:** The file is parsed during simulation initialization to dynamically establish bidirectional connections between modules.
  - **Note on Connection Uniqueness:** Ensure that connections are unique to avoid gate connection conflicts.

---

## Compilation and Execution

1. **Setup:**
   - Install OMNeT++ following the [OMNeT++ Installation Guide](https://doc.omnetpp.org/omnetpp/InstallGuide.pdf).
   - Import the project directory into the OMNeT++ IDE.

2. **Build:**
   - Compile the project within the OMNeT++ IDE. All source files in the `src/` folder and NED files in the `ned/` folder will be built.

3. **Run:**
   - Use the OMNeT++ IDE to run the simulation. The configuration file `omnetpp.ini` (located in the `simulation/` folder) will be used to set simulation parameters.
   - Monitor the console and output files (if enabled) for logging information about task processing, gossip message exchanges, and simulation status.

---