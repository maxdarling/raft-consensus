// TODO(ali): get protobufs to work
#include <iostream>
#include "RaftRPC.pb.h"
#include "Server.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    Server s;

    s.run();

    return 0;
}
