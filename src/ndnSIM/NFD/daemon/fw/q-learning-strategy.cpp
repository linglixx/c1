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

#include "q-learning-strategy.hpp"
#include "algorithm.hpp"
#include "common/global.hpp"
#include "ndn-cxx/util/logger.hpp"
#include "table/pit.hpp"

NFD_LOG_INIT(QLearningStrategy);
NDN_LOG_DEBUG("Forward Interest: " << interest.getName());
NFD_REGISTER_STRATEGY(QLearningStrategy);

namespace nfd {
namespace fw {

const Name&
QLearningStrategy::getStrategyName()
{
  static const Name strategyName("/localhost/nfd/strategy/q-learning/%FD%01");
  return strategyName;
}

QLearningStrategy::QLearningStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder)
  , m_retxSuppression(RetxSuppressionExponential::DEFAULT_INITIAL_INTERVAL,
                      RetxSuppressionExponential::DEFAULT_MULTIPLIER,
                      RetxSuppressionExponential::DEFAULT_MAX_INTERVAL)
  , m_randomEngine(random::generateSeed())
{
  ParsedInstanceName parsed = parseInstanceName(name);
  if (!parsed.parameters.empty()) {
    NDN_THROW(std::invalid_argument("QLearningStrategy does not accept parameters"));
  }
  if (parsed.version && *parsed.version != getStrategyName()[-1].toVersion()) {
    NDN_THROW(std::invalid_argument(
      "QLearningStrategy does not support version " + to_string(*parsed.version)));
  }
  this->setInstanceName(makeInstanceName(name, getStrategyName()));
}

void
QLearningStrategy::afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_DEBUG("afterReceiveInterest: " << interest.getName());

  // 检查是否是重传
  RetxSuppressionResult suppressResult = m_retxSuppression.decidePerPitEntry(*pitEntry);
  if (suppressResult == RetxSuppressionResult::SUPPRESS) {
    NFD_LOG_DEBUG("Interest " << interest.getName() << " suppressed");
    return;
  }

  // 提取当前状态
  State currentState = extractState(interest, ingress, pitEntry);
  if (currentState.availableNextHops.empty()) {
    NFD_LOG_DEBUG("No available nexthops for " << interest.getName());
    rejectPendingInterest(pitEntry);
    return;
  }

  // 选择动作（下一跳）
  Action selectedFace = selectAction(currentState);
  Face* outFace = getFace(selectedFace);
  if (outFace == nullptr) {
    NFD_LOG_DEBUG("Selected face " << selectedFace << " not found");
    rejectPendingInterest(pitEntry);
    return;
  }

  // 转发Interest
  NFD_LOG_DEBUG("Forward Interest " << interest.getName() << " to face " << selectedFace);
  sendInterest(interest, *outFace, pitEntry);
}

void
QLearningStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_DEBUG("afterReceiveData: " << data.getName());

  // 修复：传入Name而非Interest（调用重载的extractState）
  State nextState = extractState(data.getName(), ingress, pitEntry);
  
  // 从PIT条目中获取原始Interest
  const Interest& originalInterest = pitEntry->getInterest();
  State currentState = extractState(originalInterest, ingress, pitEntry);

  // 计算奖励（成功接收Data）
  double reward = calculateReward(true, ingress.face.getId());
  
  // 更新Q值
  Action takenAction = ingress.face.getId();
  updateQValue(currentState, takenAction, reward, nextState);

  // 执行默认的Data转发逻辑
  Strategy::afterReceiveData(data, ingress, pitEntry);
}

void
QLearningStrategy::afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_DEBUG("afterReceiveNack: " << nack.getInterest().getName() << " reason: " << nack.getReason());

  // 修复：传入Name而非Interest（调用重载的extractState）
  State nextState = extractState(nack.getInterest().getName(), ingress, pitEntry);
  
  // 获取原始Interest
  const Interest& originalInterest = nack.getInterest();
  State currentState = extractState(originalInterest, ingress, pitEntry);

  // 计算奖励（Nack表示失败）
  double reward = calculateReward(false, ingress.face.getId());
  
  // 更新Q值
  Action takenAction = ingress.face.getId();
  updateQValue(currentState, takenAction, reward, nextState);

  // 执行默认的Nack处理逻辑
  Strategy::afterReceiveNack(nack, ingress, pitEntry);
}

void
QLearningStrategy::onDroppedInterest(const Interest& interest, Face& egress)
{
  NFD_LOG_DEBUG("onDroppedInterest: " << interest.getName());

  // 修复：使用m_forwarder获取PIT（FaceTable没有getForwarder方法）
  auto pitEntry = m_forwarder.getPit().find(interest);
  if (!pitEntry) {
    NFD_LOG_DEBUG("PIT entry not found for dropped interest");
    return;
  }

  // 提取状态
  FaceEndpoint ingress(egress, 0); // 简化：假设egress为入站face
  State currentState = extractState(interest, ingress, pitEntry);
  State nextState = extractState(interest.getName(), ingress, pitEntry);

  // 计算奖励（Interest被丢弃）
  double reward = calculateReward(false, egress.getId());
  
  // 更新Q值
  Action takenAction = egress.getId();
  updateQValue(currentState, takenAction, reward, nextState);
}

QLearningStrategy::State
QLearningStrategy::extractState(const Interest& interest, const FaceEndpoint& ingress,
                                const shared_ptr<pit::Entry>& pitEntry) const
{
  // 修复：调用Name版本的extractState（解决递归调用的类型错误）
  return extractState(interest.getName(), ingress, pitEntry);
}

// 修复：添加Name版本的extractState定义（匹配声明）
QLearningStrategy::State
QLearningStrategy::extractState(const Name& prefix, const FaceEndpoint& ingress,
                                const shared_ptr<pit::Entry>& pitEntry) const
{
  State state;
  state.prefix = prefix.getPrefix(-1); // 去掉版本号等后缀，保留基础前缀
  state.ingressFace = ingress.face.getId();
  state.availableNextHops = getAvailableNextHops(*pitEntry);
  
  return state;
}

QLearningStrategy::Action
QLearningStrategy::selectAction(const State& state)
{
  // ε-贪心策略：ε概率随机探索，1-ε概率利用最优动作
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  if (dist(m_randomEngine) < m_epsilon) {
    // 探索：随机选择一个可用下一跳
    std::uniform_int_distribution<size_t> idxDist(0, state.availableNextHops.size() - 1);
    size_t randomIdx = idxDist(m_randomEngine);
    // 修复：无符号比较警告（randomIdx改为size_t）
    if (randomIdx >= state.availableNextHops.size()) {
      randomIdx = state.availableNextHops.size() - 1;
    }
    return state.availableNextHops[randomIdx];
  }

  // 利用：选择Q值最大的动作
  double maxQ = -std::numeric_limits<double>::max();
  Action bestAction = face::INVALID_FACEID;
  
  const auto& qActions = m_qTable[state];
  for (const auto& [action, qValue] : qActions) {
    if (qValue > maxQ) {
      maxQ = qValue;
      bestAction = action;
    }
  }

  // 如果没有Q值记录，随机选一个
  if (bestAction == face::INVALID_FACEID && !state.availableNextHops.empty()) {
    bestAction = state.availableNextHops[0];
  }

  return bestAction;
}

void
QLearningStrategy::updateQValue(const State& currentState, Action action, double reward, const State& nextState)
{
  // Q学习更新公式：Q(s,a) = Q(s,a) + α * [r + γ * max(Q(s',a')) - Q(s,a)]
  double oldQ = m_qTable[currentState][action];
  
  // 计算nextState的最大Q值
  double maxNextQ = 0.0;
  if (!m_qTable[nextState].empty()) {
    maxNextQ = std::max_element(m_qTable[nextState].begin(), m_qTable[nextState].end(),
      [](const auto& a, const auto& b) { return a.second < b.second; })->second;
  }

  double newQ = oldQ + m_learningRate * (reward + m_discountFactor * maxNextQ - oldQ);
  m_qTable[currentState][action] = newQ;
  
  NFD_LOG_DEBUG("Update Q-value: state=" << currentState.prefix << " action=" << action 
                << " oldQ=" << oldQ << " newQ=" << newQ);
}

double
QLearningStrategy::calculateReward(bool isSuccess, FaceId faceId) const
{
  if (isSuccess) {
    return REWARD_SUCCESS;
  }
  return REWARD_FAILURE;
}

std::vector<FaceId>
QLearningStrategy::getAvailableNextHops(const pit::Entry& pitEntry) const
{
  std::vector<FaceId> nextHops;
  
  // 从FIB获取可用下一跳
  const fib::Entry& fibEntry = lookupFib(pitEntry);
  for (const fib::NextHop& nh : fibEntry.getNextHops()) {
    Face* face = getFace(nh.getFace().getId());
    // 修复：Face没有isDown()，改用Transport状态判断
    if (face != nullptr && face->getTransport()->getState() == TransportState::UP) {
      nextHops.push_back(face->getId());
    }
  }

  return nextHops;
}

} // namespace fw
} // namespace nfd
