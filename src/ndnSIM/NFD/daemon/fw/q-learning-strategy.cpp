/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "q-learning-strategy.hpp"
#include "core/logger.hpp"
#include <boost/random/uniform_real_distribution.hpp>

NFD_LOG_INIT("QLearningStrategy");

namespace nfd {
namespace fw {

const Name&
QLearningStrategy::getStrategyName()
{
  static Name strategyName("ndn:/localhost/nfd/strategy/q-learning/%FD%01");
  return strategyName;
}

QLearningStrategy::QLearningStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder)
  , m_rng(std::random_device{}())
{
  this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

// 获取当前网络状态（简化版：节点ID + 前缀 + 下一跳链路延迟）
QLearningStrategy::QState
QLearningStrategy::getCurrentState(const FaceEndpoint& ingress, const Interest& interest)
{
  QState state;
  // 获取当前节点ID（从ns3的Node中提取，需结合ndnSIM的NetDeviceTransport）
  auto transport = dynamic_cast<ns3::ndn::NetDeviceTransport*>(ingress.face.getTransport());
  if (transport != nullptr) {
    state.nodeId = transport->GetNetDevice()->GetNode()->GetId();
  } else {
    state.nodeId = 0; // 默认值
  }
  state.prefix = interest.getName().getPrefix(2).toUri(); // 取前缀前2级，简化状态

  // 获取所有下一跳的链路延迟（简化为均值）
  auto pitEntry = this->getForwarder().getPit().find(interest);
  if (pitEntry != nullptr) {
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    for (const auto& nexthop : fibEntry.getNextHops()) {
      state.linkStats.push_back(getLinkDelay(nexthop.getFace()));
    }
  }
  return state;
}

// 获取链路延迟（从ns3的PointToPointChannel中提取）
double
QLearningStrategy::getLinkDelay(const Face& face)
{
  auto transport = dynamic_cast<ns3::ndn::NetDeviceTransport*>(face.getTransport());
  if (transport == nullptr) return 10.0; // 默认延迟10ms

  auto nd = transport->GetNetDevice()->GetObject<ns3::PointToPointNetDevice>();
  if (nd == nullptr) return 10.0;

  auto channel = ns3::DynamicCast<ns3::PointToPointChannel>(nd->GetChannel());
  if (channel == nullptr) return 10.0;

  // 返回链路延迟（单位：ms）
  return channel->GetDelay().GetMilliSeconds();
}

// ε-greedy选择动作（下一跳FaceID）
uint64_t
QLearningStrategy::chooseAction(const QState& state, const fib::NextHopList& nexthops)
{
  boost::random::uniform_real_distribution<> dist(0, 1);
  if (dist(m_rng) < m_epsilon) {
    // 探索：随机选择有效下一跳
    std::vector<uint64_t> validFaces;
    for (const auto& nh : nexthops) {
      if (wouldViolateScope(ingress.face, interest, nh.getFace())) continue;
      validFaces.push_back(nh.getFace().getId());
    }
    if (validFaces.empty()) return 0;
    boost::random::uniform_int_distribution<> faceDist(0, validFaces.size()-1);
    return validFaces[faceDist(m_rng)];
  } else {
    // 利用：选择Q值最大的动作
    double maxQ = -1e9;
    uint64_t bestFaceId = 0;
    auto it = m_qTable.find(state);
    if (it != m_qTable.end()) {
      for (const auto& nh : nexthops) {
        uint64_t faceId = nh.getFace().getId();
        if (wouldViolateScope(ingress.face, interest, nh.getFace())) continue;
        double qVal = it->second.count(faceId) ? it->second.at(faceId) : 0.0;
        if (qVal > maxQ) {
          maxQ = qVal;
          bestFaceId = faceId;
        }
      }
    }
    return bestFaceId == 0 ? nexthops.begin()->getFace().getId() : bestFaceId;
  }
}

// 更新Q表（Q-learning核心公式）
void
QLearningStrategy::updateQTable(const QState& state, uint64_t action, double reward, const QState& nextState)
{
  double oldQ = m_qTable[state][action]; // 若不存在则默认0
  // 计算nextState的最大Q值
  double maxNextQ = 0.0;
  auto it = m_qTable.find(nextState);
  if (it != m_qTable.end()) {
    for (const auto& pair : it->second) {
      if (pair.second > maxNextQ) maxNextQ = pair.second;
    }
  }
  // Q(s,a) = Q(s,a) + α*(R + γ*maxQ(s',a') - Q(s,a))
  double newQ = oldQ + m_alpha * (reward + m_gamma * maxNextQ - oldQ);
  m_qTable[state][action] = newQ;
  NFD_LOG_DEBUG("Update Q-table: state=" << state.nodeId << "," << state.prefix 
                << " action=" << action << " Q=" << newQ);
}

// 核心：Interest接收后转发决策
void
QLearningStrategy::afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                                       const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_TRACE("afterReceiveInterest");

  // 跳过已转发的Interest
  if (hasPendingOutRecords(*pitEntry)) return;

  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  const fib::NextHopList& nexthops = fibEntry.getNextHops();

  // 检查是否有有效下一跳
  if (!hasFaceForForwarding(ingress.face, nexthops, pitEntry)) {
    this->rejectPendingInterest(pitEntry);
    return;
  }

  // 1. 获取当前状态
  QState currentState = getCurrentState(ingress, interest);
  // 2. ε-greedy选择动作（下一跳）
  uint64_t chosenFaceId = chooseAction(currentState, nexthops);
  // 3. 找到对应的Face并转发
  for (const auto& nh : nexthops) {
    if (nh.getFace().getId() == chosenFaceId && 
        canForwardToNextHop(ingress.face, pitEntry, nh)) {
      this->sendInterest(interest, nh.getFace(), pitEntry);
      // 记录PIT条目对应的状态和动作，用于后续奖励计算
      m_pitActionMap[pitEntry] = {currentState, chosenFaceId};
      break;
    }
  }
}

// Data满足Interest时：正向奖励
void
QLearningStrategy::beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                                        const FaceEndpoint& ingress, const Data& data)
{
  auto it = m_pitActionMap.find(pitEntry);
  if (it == m_pitActionMap.end()) return;

  QState state = it->second.first;
  uint64_t action = it->second.second;
  QState nextState = getCurrentState(ingress, data.getName()); // 下一个状态（简化）
  
  // 正向奖励：10 - 链路延迟（延迟越低，奖励越高）
  double delay = getLinkDelay(ingress.face);
  double reward = 10.0 - (delay / 10.0); // 归一化延迟到0~10
  
  // 更新Q表
  updateQTable(state, action, reward, nextState);
  // 移除PIT记录
  m_pitActionMap.erase(it);
}

// Interest超时：负向奖励
void
QLearningStrategy::beforeExpirePendingInterest(const shared_ptr<pit::Entry>& pitEntry)
{
  auto it = m_pitActionMap.find(pitEntry);
  if (it == m_pitActionMap.end()) return;

  QState state = it->second.first;
  uint64_t action = it->second.second;
  QState nextState = state; // 超时无下一个状态，简化为当前状态
  
  // 负向奖励：-5
  double reward = -5.0;
  
  // 更新Q表
  updateQTable(state, action, reward, nextState);
  // 移除PIT记录
  m_pitActionMap.erase(it);
}

} // namespace fw
} // namespace nfd

// 注册策略（供ndnSIM调用）
NFD_REGISTER_STRATEGY(nfd::fw::QLearningStrategy);
