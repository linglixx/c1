/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2024 Your Name/Organization
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

using namespace ns3;

int
main(int argc, char* argv[])
{
  // 设置默认参数
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("1Mbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("10ms"));
  Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", StringValue("20p"));

  // 解析命令行参数
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // 创建拓扑：3个节点（消费者-路由器-生产者）
  NodeContainer nodes;
  nodes.Create(3);

  PointToPointHelper p2p;
  p2p.Install(nodes.Get(0), nodes.Get(1)); // 消费者-路由器
  p2p.Install(nodes.Get(1), nodes.Get(2)); // 路由器-生产者

  // 安装NDN栈（修复：使用ns3::ndn::命名空间）
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // 安装全局路由
  ns3::ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // 为路由器节点安装Q-Learning策略（修复：使用ns3::ndn::命名空间）
  ns3::ndn::StrategyChoiceHelper::Install(nodes.Get(1), // 路由器节点
                                           "/prefix", 
                                           "/localhost/nfd/strategy/q-learning");

  // 安装生产者应用
  ns3::ndn::AppHelper producerHelper("ns3::ndn::Producer");
  producerHelper.SetPrefix("/prefix");
  producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
  producerHelper.Install(nodes.Get(2)); // 生产者节点

  // 安装消费者应用
  ns3::ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
  consumerHelper.SetPrefix("/prefix");
  consumerHelper.SetAttribute("Frequency", StringValue("10")); // 10 Interest/秒
  consumerHelper.Install(nodes.Get(0)); // 消费者节点

  // 添加路由信息并计算FIB
  ns3::ndn::GlobalRoutingHelper::AddOrigins("/prefix", nodes.Get(2));
  ns3::ndn::GlobalRoutingHelper::CalculateRoutes();

  // 启用追踪
  ns3::ndn::L3RateTracer::InstallAll("q-learning-rate-trace.txt", ns3::Seconds(1.0));
  ns3::ndn::CsTracer::InstallAll("q-learning-cs-trace.txt", ns3::Seconds(1.0));

  // 运行仿真
  Simulator::Stop(Seconds(20.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
