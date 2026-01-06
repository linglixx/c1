#ifndef NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP
#define NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP

#include "strategy.hpp"
#include "fw/algorithm.hpp"
#include <map>
#include <vector>
#include <random>
#include <ctime>

namespace nfd {
namespace fw {

// Q-Learning状态（仅需实现<运算符，用于std::map的Key）
struct QLearningState {
  Name prefix;          // 兴趣包前缀
  FaceId ingressFaceId; // 入接口ID

  // 核心：实现<运算符（std::map必需，无需哈希）
  bool operator<(const QLearningState& other) const {
    if (prefix != other.prefix) {
      return prefix < other.prefix; // ndn::Name自带<运算符
    }
    return ingressFaceId < other.ingressFaceId;
  }

  // 实现==运算符（可选，方便逻辑判断）
  bool operator==(const QLearningState& other) const {
    return prefix == other.prefix && ingressFaceId == other.ingressFaceId;
  }
};

class QLearningStrategy : public Strategy
{
public:
  static const Name&
  getStrategyName()
  {
    static Name strategyName("/localhost/nfd/strategy/q-learning/%FD%01");
    return strategyName;
  }

  explicit
  QLearningStrategy(Forwarder& forwarder, const Name& name = getStrategyName());

  ~QLearningStrategy() override = default;

  // 重载核心方法
  void
  afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                       const shared_ptr<pit::Entry>& pitEntry) override;

  void
  afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                   const shared_ptr<pit::Entry>& pitEntry) override;

  void
  afterReceiveNack(const lp::Nack& nack, const FaceEndpoint& ingress,
                   const shared_ptr<pit::Entry>& pitEntry) override;

protected:
  // 改用std::map（无需哈希，彻底避开所有哈希报错）
  using Action = FaceId;
  using QTable = std::map<QLearningState, std::map<Action, double>>;

  // 获取可用下一跳
  std::vector<FaceId> getAvailableNextHops(const pit::Entry& pitEntry) const;
  
  // 更新Q值
  void updateQValue(const QLearningState& state, const Action& action, 
                    double reward, const QLearningState& nextState);
  
  // ε-贪心选择动作
  Action selectAction(const QLearningState& state, const std::vector<FaceId>& availableHops);

private:
  double m_alpha = 0.1;    // 学习率
  double m_gamma = 0.9;    // 折扣因子
  double m_epsilon = 0.1;  // 探索概率
  QTable m_qTable;         // Q表（std::map版）
  std::mt19937 m_rng;      // 随机数生成器
};

// 关键：宏放在命名空间内，参数仅类名（不带命名空间）
NFD_REGISTER_STRATEGY(QLearningStrategy);

} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_Q_LEARNING_STRATEGY_HPP
