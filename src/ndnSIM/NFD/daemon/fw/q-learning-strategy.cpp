#include "q-learning-strategy.hpp"
#include "table/fib-entry.hpp"
#include <ns3/log.h>

NS_LOG_COMPONENT_DEFINE("QLearningStrategy");

namespace nfd {
namespace fw {

QLearningStrategy::QLearningStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder)
  // 修正：兼容所有环境的随机数初始化（避免std::random_device不可用）
  , m_rng(static_cast<unsigned int>(std::time(nullptr)))
{
  this->setInstanceName(makeInstanceName(name, getStrategyName()));
  NS_LOG_DEBUG("QLearningStrategy initialized");
}

void
QLearningStrategy::afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
  NS_LOG_DEBUG("Received Interest: " << interest.getName());

  // 避免重复转发
  if (hasPendingOutRecords(*pitEntry)) {
    return;
  }

  // 构建当前状态
  QLearningState currentState{interest.getName(), ingress.face.getId()};
  
  // 获取可用下一跳
  auto availableHops = getAvailableNextHops(*pitEntry);
  if (availableHops.empty()) {
    NS_LOG_DEBUG("No next hops for " << interest.getName());
    rejectPendingInterest(pitEntry);
    return;
  }

  // 选择下一跳并转发
  FaceId selectedFaceId = selectAction(currentState, availableHops);
  Face* selectedFace = this->getFace(selectedFaceId);
  if (selectedFace != nullptr) {
    sendInterest(interest, *selectedFace, pitEntry);
    NS_LOG_DEBUG("Forward to face " << selectedFaceId);
  } else {
    rejectPendingInterest(pitEntry);
  }
}

void
QLearningStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  // 强制输出（必看）
  printf("=== Data逻辑执行 === Name: %s, Face ID: %lu\n", 
         data.getName().toUri().c_str(), ingress.face.getId());
  NS_LOG_INFO("Received Data: " << data.getName() << " from face " << ingress.face.getId());

  // 正向奖励：收到数据+10
  QLearningState state{data.getName(), ingress.face.getId()};
  updateQValue(state, ingress.face.getId(), 10.0, state);

  // 调用父类方法完成数据转发
  Strategy::afterReceiveData(data, ingress, pitEntry);
}

void
QLearningStrategy::afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  NS_LOG_DEBUG("Received Nack: " << nack.getInterest().getName());

  // 负向奖励：收到Nack-5
  QLearningState state{nack.getInterest().getName(), ingress.face.getId()};
  updateQValue(state, ingress.face.getId(), -5.0, state);

  Strategy::afterReceiveNack(nack, ingress, pitEntry);
}

std::vector<FaceId>
QLearningStrategy::getAvailableNextHops(const pit::Entry& pitEntry) const
{
  std::vector<FaceId> nextHops;
  const fib::Entry& fibEntry = this->lookupFib(pitEntry);

  // 筛选UP状态的接口
  for (const auto& nh : fibEntry.getNextHops()) {
    const Face& face = nh.getFace();
    if (face.getTransport()->getState() == face::TransportState::UP) {
      nextHops.push_back(face.getId());
    }
  }
  return nextHops;
}

void
QLearningStrategy::updateQValue(const QLearningState& state, const Action& action,
                                double reward, const QLearningState& nextState)
{
  // Q-Learning核心公式（std::map兼容版）
  double oldQ = m_qTable[state][action];
  double maxNextQ = 0.0;

  // 找下一个状态的最大Q值
  auto it = m_qTable.find(nextState);
  if (it != m_qTable.end()) {
    for (const auto& [a, q] : it->second) {
      maxNextQ = std::max(maxNextQ, q);
    }
  }

  // 更新Q值
  double newQ = oldQ + m_alpha * (reward + m_gamma * maxNextQ - oldQ);
  m_qTable[state][action] = newQ;
}

QLearningStrategy::Action
QLearningStrategy::selectAction(const QLearningState& state, const std::vector<FaceId>& availableHops)
{
  // ε-贪心策略
  std::bernoulli_distribution exploreDist(m_epsilon);
  if (exploreDist(m_rng)) {
    // 探索：随机选
    std::uniform_int_distribution<> dist(0, availableHops.size() - 1);
    return availableHops[dist(m_rng)];
  } else {
    // 利用：选Q值最大的
    double maxQ = -1e9;
    FaceId bestAction = availableHops[0];

    for (FaceId faceId : availableHops) {
      double q = m_qTable[state][faceId];
      if (q > maxQ) {
        maxQ = q;
        bestAction = faceId;
      }
    }
    return bestAction;
  }
}

} // namespace fw
} // namespace nfd
