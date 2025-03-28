#include <omnetpp.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <numeric>
#include <functional>
#include "messages_m.h"

using namespace omnetpp;
using namespace std;

// Utility function: Ensure a module's gate vector has at least the requested size.
static void ensureGateCapacity(cModule *module, const char *gateName, int requiredSize) {
    int currentSize = module->gateSize(gateName);
    if (requiredSize > currentSize)
        module->setGateSize(gateName, requiredSize);
}

// Structure to store score information for a server.
struct ServerScore {
    int serverId;
    int score;
    ServerScore() : serverId(0), score(0) {}
    ServerScore(int id, int sc) : serverId(id), score(sc) {}
};

class Client : public cSimpleModule {
  private:
    // Basic client identity and connection counts.
    string clientIdentifier;
    int totalServers, totalClients;
    int connectedServersCount = 0;
    int majorityThreshold = 0;  // (connectedServersCount / 2) + 1

    // Data structures to track results and server scores.
    map<int, vector<int>> serverResults; // Key: server module id, Value: list of results from that server.
    int totalResultsReceived = 0;
    map<int, int> localScores;           // Key: server id, value: cumulative score.

    // Mapping from server id to our local gate index.
    map<int, int> serverGateMapping;

    // Gossip data: holds scores received from other clients.
    map<string, map<int, int>> gossipScores; // Key: client id, Value: map of server scores.
    unordered_set<string> gossipHistory;     // To avoid reprocessing duplicate gossip messages.

    // Data for the task round (initial and round 2).
    vector<int> processingArray;
    bool roundTwoInitiated = false;

    // Additional internal configuration.
    int arraySize = 12;         // Length of array to process.
    int seedForRandom = time(nullptr); // Optional seed for randomness.

    // --- Modular Helper Structures and Functions ---
    // Structure to hold parsed gossip message details.
    struct ParsedGossip {
        string timestamp;
        string senderId;
        map<int, int> serverScoreMap;
    };

    // Parse a single gossip message content.
    ParsedGossip parseGossipContent(const string &content) {
        ParsedGossip parsed;
        istringstream iss(content);
        getline(iss, parsed.timestamp, ':');
        getline(iss, parsed.senderId, ':');
        string scoreSection;
        getline(iss, scoreSection, ':');

        istringstream scoreStream(scoreSection);
        string scoreEntry;
        while (getline(scoreStream, scoreEntry, '#')) {
            if (scoreEntry.empty()) continue;
            size_t equalPos = scoreEntry.find('=');
            if (equalPos != string::npos) {
                // scoreEntry format "s<serverId>=<score>"
                int sId = stoi(scoreEntry.substr(1, equalPos - 1));
                int sScore = stoi(scoreEntry.substr(equalPos + 1));
                parsed.serverScoreMap[sId] = sScore;
            }
        }
        return parsed;
    }

    // Compose gossip message content from a score map.
    string composeGossipContent(const map<int, int>& scoreMap) {
        stringstream content;
        content << time(nullptr) << ":" << clientIdentifier << ":";
        for (const auto &entry : scoreMap)
            content << "s" << entry.first << "=" << entry.second << "#";
        return content.str();
    }

    // Broadcast a gossip message on all client gates.
    void broadcastGossip(const string &gossipContent);

    // Extract our own index from the module (e.g., client[0] returns 0).
    int getClientIndex() {
        string fullName = getFullName();
        size_t pos1 = fullName.find('[');
        size_t pos2 = fullName.find(']');
        if (pos1 != string::npos && pos2 != string::npos)
            return stoi(fullName.substr(pos1 + 1, pos2 - pos1 - 1));
        return -1;
    }

    // Divides an array into nearly equal chunks.
    vector<vector<int>> partitionArray(const vector<int>& array, int parts) {
        vector<vector<int>> chunks;
        int n = array.size();
        int chunkSize = n / parts;
        int remainder = n % parts;
        int start = 0;
        for (int i = 0; i < parts; ++i) {
            int currentChunkSize = chunkSize + (i < remainder ? 1 : 0);
            vector<int> chunk(array.begin() + start, array.begin() + start + currentChunkSize);
            chunks.push_back(chunk);
            start += currentChunkSize;
        }
        return chunks;
    }

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;

    // Modular helper functions.
    void loadTopology();
    void establishServerLinks();
    void dispatchInitialSubtasks();
    void processSubtaskResult(ResultMessage *resultMsg);
    void initiateRoundTwo();
    map<int, double> computeAverageScores();
    vector<int> selectTopServers(int count);
};

Define_Module(Client);

void Client::initialize() {
    // Initialize client identification and parameters.
    clientIdentifier = par("clientId").stdstringValue();
    EV << "[Client " << clientIdentifier << "] Initialization started.\n";
    loadTopology();
    establishServerLinks();

    if (connectedServersCount == 0) {
        EV << "[Client " << clientIdentifier << "] No server connections detected. Terminating simulation.\n";
        endSimulation();
        return;
    }
    majorityThreshold = (connectedServersCount / 2) + 1;
    EV << "[Client " << clientIdentifier << "] Connected Servers: " << connectedServersCount
       << " | Majority Threshold: " << majorityThreshold << "\n";

    // Dispatch first round of subtasks.
    dispatchInitialSubtasks();
}

void Client::loadTopology() {
    ifstream topoFile("topo.txt");
    if (!topoFile.is_open()) {
        EV << "[Client " << clientIdentifier << "] ERROR: Unable to open topo.txt. Ending simulation.\n";
        endSimulation();
        return;
    }
    string line;
    int myIdx = getClientIndex();
    while (getline(topoFile, line)) {
        if (line.empty())
            continue;
        // Format lines: "clients= <num>" or "servers= <num>" or "clientX Y" where Y can be client or server.
        if (line.find("Clients=") == 0) {
            totalClients = stoi(line.substr(8));
        } else if (line.find("Servers=") == 0) {
            totalServers = stoi(line.substr(8));
        } else if (line.find("Client_") == 0) {
            istringstream iss(line);
            string source, target;
            iss >> source >> target;
            int srcIdx = stoi(source.substr(7));
            if (srcIdx != myIdx)
                continue; // Process only our own connection entries.
            // Determine the type from target name.
            string targetType = (target.find("Client_") == 0) ? "client" : "server";
            int targetIdx = stoi(target.substr(7));
            cModule *netModule = getParentModule();
            cModule *sourceMod = netModule->getSubmodule("client", srcIdx);
            cModule *targetMod = netModule->getSubmodule(targetType.c_str(), targetIdx);

            // Create bidirectional connection using "peer" gate vectors.
            int currentGateIdx = sourceMod->gateSize("peer");
            ensureGateCapacity(sourceMod, "peer", currentGateIdx + 1);
            ensureGateCapacity(targetMod, "peer", currentGateIdx + 1);
            // sourceMod->setGateSize("peer", currentGateIdx + 1);
            // targetMod->setGateSize("peer", currentGateIdx + 1);

            sourceMod->gate("peer$o", currentGateIdx)->connectTo(targetMod->gate("peer$i", currentGateIdx));
            targetMod->gate("peer$o", currentGateIdx)->connectTo(sourceMod->gate("peer$i", currentGateIdx));
        }
    }
    topoFile.close();
}

void Client::establishServerLinks() {
    // Iterate over our "peer$o" gates to locate connections to server modules.
    int peerCount = gateSize("peer$o");
    for (int i = 0; i < peerCount; ++i) {
        cGate *currentGate = gate("peer$o", i);
        if (!currentGate->isConnected())
            continue;
        cModule *neighbor = currentGate->getPathEndGate()->getOwnerModule();
        if (string(neighbor->getComponentType()->getName()) == "Server") {
            int neighborId = neighbor->getId();
            serverGateMapping[neighborId] = i;
            localScores[neighborId] = 0;
            connectedServersCount++;
        }
    }
}

void Client::dispatchInitialSubtasks() {
    // Generate a random array of integers to process.
    processingArray.clear();
    for (int i = 0; i < arraySize; ++i) {
        processingArray.push_back(intuniform(1, 100));
    }
    EV << "[Client " << clientIdentifier << "] Generated array of size " << arraySize << ".\n";

    // Partition the array equally among all connected servers.
    vector<vector<int>> partitions = partitionArray(processingArray, connectedServersCount);
    int serversToSend = min(majorityThreshold, connectedServersCount);
    int sentCount = 0;

    // Loop through the mapping and send subtasks.
    for (const auto &entry : serverGateMapping) {
        if (sentCount >= serversToSend)
            break;
        int gateIndex = entry.second;
        cModule *targetModule = gate("peer$o", gateIndex)->getPathEndGate()->getOwnerModule();
        if (string(targetModule->getComponentType()->getName()) != "Server")
            continue;

        // Create and fill a SubtaskMessage.
        SubtaskMessage *subMsg = new SubtaskMessage("Subtask");
        subMsg->setClientId(getIndex());
        int currentChunkSize = partitions[sentCount].size();
        subMsg->setDataArraySize(currentChunkSize);
        for (int j = 0; j < currentChunkSize; ++j)
            subMsg->setData(j, partitions[sentCount][j]);

        send(subMsg, "peer$o", gateIndex);
        sentCount++;
    }
}

void Client::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) {
        // Self-message to trigger round 2.
        if (strcmp(msg->getName(), "TriggerRound2") == 0) {
            EV << "[Client " << clientIdentifier << "] Initiating Round 2...\n";
            vector<int> topServers = selectTopServers(majorityThreshold);
            if (topServers.empty()) {
                EV << "[Client " << clientIdentifier << "] No top servers found. Aborting round 2.\n";
                delete msg;
                return;
            }
            // Create a new random array for round 2.
            vector<int> newArray(arraySize);
            generate(newArray.begin(), newArray.end(), [&](){ return intuniform(1, 100); });
            vector<vector<int>> chunks = partitionArray(newArray, topServers.size());

            for (size_t i = 0; i < topServers.size(); ++i) {
                SubtaskMessage *roundMsg = new SubtaskMessage("Subtask");
                roundMsg->setDataArraySize(chunks[i].size());
                for (size_t j = 0; j < chunks[i].size(); ++j)
                    roundMsg->setData(j, chunks[i][j]);
                int localGate = serverGateMapping[topServers[i]];
                send(roundMsg, "peer$o", localGate);
            }
            roundTwoInitiated = true;
            delete msg;
        }
        return;
    }

    // Process incoming messages: Either a ResultMessage or a GossipMessage.
    if (auto *resMsg = dynamic_cast<ResultMessage *>(msg)) {
        processSubtaskResult(resMsg);
    } else if (auto *gossipMsg = dynamic_cast<GossipMessage *>(msg)) {
        string content = gossipMsg->getContent();
        string contentHash = to_string(hash<string>{}(content));
        if (gossipHistory.find(contentHash) == gossipHistory.end()) {
            gossipHistory.insert(contentHash);
            ParsedGossip parsed = parseGossipContent(content);
            gossipScores[parsed.senderId] = parsed.serverScoreMap;
            // Forward the gossip message to all connected client peers (except sender).
            int arrivalIdx = gossipMsg->getArrivalGate()->getIndex();
            int totalPeers = gateSize("peer$o");
            for (int i = 0; i < totalPeers; ++i) {
                cGate *peerGate = gate("peer$o", i);
                if (peerGate->isConnected() && i != arrivalIdx) {
                    cModule *peerModule = peerGate->getPathEndGate()->getOwnerModule();
                    if (string(peerModule->getComponentType()->getName()) == "Client")
                        send(gossipMsg->dup(), "peer$o", i);
                }
            }
        }
        delete gossipMsg;
    } else {
        // For unknown message types, simply log and delete.
        EV << "[Client " << clientIdentifier << "] Received unrecognized message: " << msg->getName() << "\n";
        delete msg;
    }
}

void Client::processSubtaskResult(ResultMessage *resultMsg) {
    // Identify the server using the arrival gate.
    cGate *arrival = resultMsg->getArrivalGate();
    cGate *senderGate = arrival->getPreviousGate();
    int serverModuleId = senderGate->getOwnerModule()->getId();
    serverResults[serverModuleId].push_back(resultMsg->getResult());
    resultMsg->setCounter(resultMsg->getCounter() + 1);
    totalResultsReceived++;

    // Check if we have received enough results to process (using majority threshold).
    if (totalResultsReceived >= majorityThreshold && !roundTwoInitiated) {
        // For demonstration, we compute an expected sum from the original array.
        int expectedSum = accumulate(processingArray.begin(), processingArray.end(), 0);
        for (auto &entry : serverResults) {
            for (int result : entry.second) {
                // Increment score if correct; otherwise decrement.
                localScores[entry.first] += (result == expectedSum) ? 1 : -1;
            }
        }
        // Broadcast our local server scores via gossip.
        broadcastGossip(composeGossipContent(localScores));
        // Schedule round 2.
        scheduleAt(simTime() + 0.1, new cMessage("TriggerRound2"));
    }
    delete resultMsg;
}

void Client::broadcastGossip(const string &gossipContent) {
    GossipMessage *gossipMsg = new GossipMessage("Gossip");
    // convert string to char *
    const char *gossipContent_c = gossipContent.c_str();
    gossipMsg->setContent(gossipContent_c);
    gossipMSg->setCounter(gossipMsg->getCounter()+1);
    string hashVal = to_string(hash<string>{}(gossipContent));
    gossipHistory.insert(hashVal);
    // Also record our own gossip scores.
    gossipScores[clientIdentifier] = localScores;

    // Broadcast on all client peer connections.
    for (int i = 0; i < gateSize("peer$o"); ++i) {
        cGate *peerGate = gate("peer$o", i);
        if (peerGate->isConnected()) {
            cModule *destMod = peerGate->getPathEndGate()->getOwnerModule();
            if (string(destMod->getComponentType()->getName()) == "Client") {
                send(gossipMsg->dup(), "peer$o", i);
            }
        }
    }
    delete gossipMsg;
}

map<int, double> Client::computeAverageScores() {
    map<int, double> averageScores;
    // Totals structure: key = server id, value = pair(sum, count)
    map<int, pair<int,int>> totals;
    for (auto &clientEntry : gossipScores) {
        for (auto &scoreEntry : clientEntry.second) {
            int sId = scoreEntry.first;
            // Only consider servers we are connected to.
            if (serverGateMapping.find(sId) != serverGateMapping.end()) {
                totals[sId].first += scoreEntry.second;
                totals[sId].second += 1;
            }
        }
    }
    for (auto &entry : totals) {
        averageScores[entry.first] = static_cast<double>(entry.second.first) / entry.second.second;
    }
    return averageScores;
}

vector<int> Client::selectTopServers(int count) {
    auto avgMap = computeAverageScores();
    vector<pair<int, double>> serverAvgList(avgMap.begin(), avgMap.end());
    // Sort in descending order by average score.
    sort(serverAvgList.begin(), serverAvgList.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
    vector<int> topServers;
    for (int i = 0; i < min(count, static_cast<int>(serverAvgList.size())); ++i) {
        topServers.push_back(serverAvgList[i].first);
    }
    return topServers;
}