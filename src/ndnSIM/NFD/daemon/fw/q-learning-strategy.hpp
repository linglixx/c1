/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef NFD_FW_Q_LEARNING_STRATEGY_HPP
#define NFD_FW_Q_LEARNING_STRATEGY_HPP

#include "fw/strategy.hpp"
#include "fw/algorithm.hpp"
#include <unordered_map>
#include <random>
#include <ns3/ndnSIM/model/ndn-net-device-transport.hpp>
#include <ns3/point-to-point-channel.h>

namespace nfd {
namespace fw {

// 定义状态类型：简化为「当前节点ID + Interest前缀 + 下一跳链路状态摘要」
struct QState {
  uint32_t nodeId;          // 当前节点ID
  std::string prefix;       // Interest前缀
  std::vector<double> linkStats; // 下一跳链路的延迟（简化为均值）

  // 重载哈希函数，用于Q表的key
  bool operator==(const QState& other) const {
    return nodeId == other.nodeId && prefix == other.prefix && linkStats == other.linkStats;
  }
};

// 自定义QState的哈希函数
struct QStateHash {
  size_t operator()(const QState& s) const {
    size_t h = std::hash<uint32_t>()(s.nodeId);
    h ^= std::hash<std::string>()(s.prefix) << 1;
    for (auto val : s.linkStats) {
      h ^= std::hash<double>()(val) << 1;
    }
    return h;
  }
};

// Q表类型：QState -> (动作(下一跳FaceID) -> Q值)
using QTable = std::unordered_map<QState, std::unordered_map<uint64_t, double>, QStateHash>;

class QLearningStrategy : public Strategy {
public:
  static const Name&
  getStrategyName();

  QLearningStrategy(Forwarder& forwarder, const Name& name);

  // 重写Interest接收后的处理函数（核心决策逻辑）
  void
  afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                       const shared_ptr<pit::Entry>& pitEntry) override;

  // 重写Data满足Interest时的回调（计算奖励、更新Q表）
  void
  beforeSatisfyInterest(const shared_ptr<pit::Entry>& pitEntry,
                        const FaceEndpoint& ingress, const Data& data) override;

  // 重写Interest超时时的回调（负奖励、更新Q表）
  void
  beforeExpirePendingInterest(const shared_ptr<pit::Entry>& pitEntry) override;

private:
  // 获取当前状态
  QState
  getCurrentState(const FaceEndpoint& ingress, const Interest& interest);

  // ε-greedy选择动作（下一跳FaceID）
  uint64_t
  chooseAction(const QState& state, const fib::NextHopList& nexthops);

  // 更新Q表
  void
  updateQTable(const QState& state, uint64_t action, double reward, const QState& nextState);

  // 获取链路状态（如延迟）
  double
  getLinkDelay(const Face& face);

private:
  QTable m_qTable;          // Q表
  double m_alpha = 0.1;     // 学习率
  double m_gamma = 0.9;     // 折扣因子
  double m_epsilon = 0.2;   // 探索率（20%概率随机选择动作）
  std::mt19937 m_rng;       // 随机数生成器

  // 记录「PIT条目 -> 所选动作+状态」，用于后续奖励计算
  std::unordered_map<shared_ptr<pit::Entry>, std::pair<QState, uint64_t>> m_pitActionMap;
};

} // namespace fw
} // namespace nfd

#endif // NFD_FW_Q_LEARNING_STRATEGY_HPP
