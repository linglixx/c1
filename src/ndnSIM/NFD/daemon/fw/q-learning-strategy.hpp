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

#ifndef NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP
#define NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP

#include "strategy.hpp"
#include "fw/retx-suppression-exponential.hpp"
#include <unordered_map>
#include <vector>
#include <random>

namespace nfd {
namespace fw {

class QLearningStrategy : public Strategy
{
public:
  explicit
  QLearningStrategy(Forwarder& forwarder, const Name& name = getStrategyName());

  static const Name&
  getStrategyName();

public: // triggers
  void
  afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                       const shared_ptr<pit::Entry>& pitEntry) override;

  void
  afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                   const shared_ptr<pit::Entry>& pitEntry) override;

  void
  afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                   const shared_ptr<pit::Entry>& pitEntry) override;

  void
  onDroppedInterest(const Interest& interest, Face& egress) override;

public: // 改为public，解决访问权限问题
  struct State
  {
    Name prefix;
    FaceId ingressFace;
    std::vector<FaceId> availableNextHops;

    // 用于哈希的辅助函数
    bool operator==(const State& other) const
    {
      return prefix == other.prefix && ingressFace == other.ingressFace &&
             availableNextHops == other.availableNextHops;
    }
  };

  using Action = FaceId; // 动作：选择下一跳FaceId
  using QTable = std::unordered_map<State, std::unordered_map<Action, double>>;

protected:
  // 重载extractState：支持Interest和Name两种参数
  State
  extractState(const Interest& interest, const FaceEndpoint& ingress,
               const shared_ptr<pit::Entry>& pitEntry) const;

  State
  extractState(const Name& prefix, const FaceEndpoint& ingress,
               const shared_ptr<pit::Entry>& pitEntry) const;

  Action
  selectAction(const State& state);

  void
  updateQValue(const State& currentState, Action action, double reward, const State& nextState);

  double
  calculateReward(bool isSuccess, FaceId faceId) const;

  std::vector<FaceId>
  getAvailableNextHops(const pit::Entry& pitEntry) const;

private:
  QTable m_qTable;
  RetxSuppressionExponential m_retxSuppression;
  std::mt19937 m_randomEngine;
  double m_learningRate = 0.1;    // 学习率α
  double m_discountFactor = 0.9;  // 折扣因子γ
  double m_epsilon = 0.1;         // ε-贪心策略的探索概率

  // 奖励值定义
  static constexpr double REWARD_SUCCESS = 10.0;
  static constexpr double REWARD_FAILURE = -5.0;
  static constexpr double REWARD_DROP = -10.0;
};

} // namespace fw
} // namespace nfd

// 自定义哈希函数，用于State的unordered_map
namespace std {
template<>
struct hash<nfd::fw::QLearningStrategy::State>
{
  size_t
  operator()(const nfd::fw::QLearningStrategy::State& state) const
  {
    size_t h = hash<ndn::Name>()(state.prefix);
    h ^= hash<nfd::FaceId>()(state.ingressFace) + 0x9e3779b9 + (h << 6) + (h >> 2);
    for (auto faceId : state.availableNextHops) {
      h ^= hash<nfd::FaceId>()(faceId) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    return h;
  }
};
} // namespace std

#endif // NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP
