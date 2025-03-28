package test;
simple Server
{
    parameters:
        string serverId;
        bool isMalicious = false; // default to honest
        @display("i=block/Server");
    gates:
        inout peer[];
}