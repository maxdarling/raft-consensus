// TODO(ali): get protobufs to work
#include <iostream>
#include "RaftRPC.pb.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    std::cout << "ali was here\n";

    RPC::AppendEntries test_msg;

    return 0;
}
