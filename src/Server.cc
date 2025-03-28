#include <omnetpp.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <string>
#include <functional>
#include "messages_m.h"  // Import auto-generated messages

using namespace omnetpp;
using namespace std;

// Helper structure to encapsulate a subtask's data.
struct SubtaskData {
    vector<int> values;
    SubtaskData() {}
    SubtaskData(const vector<int>& vec) : values(vec) {}
};

// Utility function to process the subtask array and return the maximum value.
int computeMaxValue(const vector<int>& data, bool maliciousMode) {
    if (data.empty())
        return 0;
    int maxVal = *max_element(data.begin(), data.end());
    // If in malicious mode, intentionally return an incorrect result.
    if (maliciousMode) {
        maxVal -= 10;
    }
    return maxVal;
}

// Utility function to extract an integer vector from a SubtaskMessage.
SubtaskData extractSubtaskData(SubtaskMessage *msg) {
    SubtaskData subtask;
    int len = msg->getDataArraySize();
    for (int i = 0; i < len; ++i)
        subtask.values.push_back(msg->getData(i));
    return subtask;
}

class Server : public cSimpleModule {
  private:
    bool maliciousMode;
    string serverIdentifier;
    // Counter for processed tasks (for logging or future use)
    int tasksProcessed = 0;

    // Log the received task information.
    void logTaskReception(const SubtaskMessage *task);
    // Prepare and send the result message to the correct output gate.
    void sendResultMessage(const SubtaskMessage *task, int result, int gateIndex);
  
  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

Define_Module(Server);

void Server::initialize() {
    serverIdentifier = par("serverId").stringValue();
    maliciousMode = par("isMalicious").boolValue();
    tasksProcessed = 0;
    EV << "Server " << serverIdentifier << " initialized. Malicious mode: " 
       << (maliciousMode ? "ON" : "OFF") << endl;
}

void Server::logTaskReception(const SubtaskMessage *task) {
    EV << "Server " << serverIdentifier << " received subtask from Client " 
       << task->getClientId() << ". Processing task #" << (tasksProcessed + 1) << endl;
}

void Server::sendResultMessage(const SubtaskMessage *task, int result, int gateIndex) {
    ResultMessage *resMsg = new ResultMessage("ResultMessage");
    resMsg->setClientId(task->getClientId());
    resMsg->setResult(result);
    // Send the result back via the corresponding gate.
    send(resMsg, "peer$o", gateIndex);
}

void Server::handleMessage(cMessage *msg) {
    // Expecting a SubtaskMessage only.
    auto *subtaskMsg = dynamic_cast<SubtaskMessage *>(msg);
    if (!subtaskMsg) {
        EV << "Server " << serverIdentifier << " received an unknown message type.\n";
        delete msg;
        return;
    }
    
    // Log task reception.
    logTaskReception(subtaskMsg);
    
    // Extract the data from the message.
    SubtaskData taskData = extractSubtaskData(subtaskMsg);
    
    // Compute the maximum value from the data.
    int computedMax = computeMaxValue(taskData.values, maliciousMode);
    
    // Increment the processed tasks counter.
    tasksProcessed++;
    
    // Determine the output gate index (based on the arrival gate).
    int outputGateIndex = msg->getArrivalGate()->getIndex();
    
    // Send the result message.
    sendResultMessage(subtaskMsg, computedMax, outputGateIndex);
    
    // Clean up the received message.
    delete msg;
}