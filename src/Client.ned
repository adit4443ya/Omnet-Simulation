package test;
simple Client
{
    parameters:
        string clientId;
        @display("i=block/Client");
    gates:
        inout peer[]; // allows connecting to multiple servers/clients
}