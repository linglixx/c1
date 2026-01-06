#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"
#include "NFD/daemon/fw/qcon-strategy.hpp" // Include your header

using namespace ns3;

int main(int argc, char* argv[]) {
    // 1. Register the strategy in the StrategyChoiceHelper
    ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/qcon");

    // 2. Topology Setup (3x3 Grid as per paper)
    AnnotatedTopologyReader topologyReader("", 25);
    topologyReader.SetFileName("src/ndnSIM/examples/topologies/topo-grid-3x3.txt");
    topologyReader.Read();

    // 3. Install NDN Stack
    ndn::StackHelper ndnHelper;
    ndnHelper.SetDefaultRoutes(true);
    ndnHelper.InstallAll();

    // 4. Set Strategy
    // Note: The string must match the GetStrategyName() in your C++ class
    ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/qcon");

    // 5. Traffic Setup (Consumer/Producer)
    // ... (Standard ndnSIM consumer setup) ...

    Simulator::Stop(Seconds(20.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
