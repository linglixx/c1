#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

namespace ns3 {

int
main(int argc, char* argv[])
{
  // 基础配置
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // 3节点拓扑
  NodeContainer nodes;
  nodes.Create(3);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("1ms"));
  p2p.Install(nodes.Get(0), nodes.Get(1));
  p2p.Install(nodes.Get(1), nodes.Get(2));

  // 安装NDN栈
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // 全局路由
  ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // 关键：启用Q-Learning策略
  ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/q-learning/%FD%01");

  // 消费者（节点0）
  ndn::AppHelper consumer("ns3::ndn::ConsumerCbr");
  consumer.SetPrefix("/prefix");
  consumer.SetAttribute("Frequency", DoubleValue(5.0)); // 5个/秒
  consumer.Install(nodes.Get(0));

  // 生产者（节点2）
  ndn::AppHelper producer("ns3::ndn::Producer");
  producer.SetPrefix("/prefix");
  producer.SetAttribute("PayloadSize", UintegerValue(1024));
  producer.Install(nodes.Get(2));

  // 路由配置
  ndnGlobalRoutingHelper.AddOrigins("/prefix", nodes.Get(2));
  ndn::GlobalRoutingHelper::CalculateAllPossibleRoutes();

  // 运行仿真
  Simulator::Stop(Seconds(10.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

} // namespace ns3

int main(int argc, char* argv[]) { return ns3::main(argc, argv); }
