#ifndef QCON_TAG_HPP
#define QCON_TAG_HPP

#include "ns3/tag.h"
#include "ns3/packet.h"

namespace ns3 {
namespace ndn {

class QconTag : public Tag {
public:
  static TypeId GetTypeId(void);
  virtual TypeId GetInstanceTypeId(void) const;
  virtual uint32_t GetSerializedSize(void) const;
  virtual void Serialize(TagBuffer i) const;
  virtual void Deserialize(TagBuffer i);
  virtual void Print(std::ostream &os) const;

  // Getters and Setters
  void SetQValue(double q) { m_qValue = q; }
  double GetQValue() const { return m_qValue; }

  void SetCongestionInfo(double ci) { m_congestionInfo = ci; }
  double GetCongestionInfo() const { return m_congestionInfo; }

private:
  double m_qValue;
  double m_congestionInfo;
};

} // namespace ndn
} // namespace ns3

#endif
