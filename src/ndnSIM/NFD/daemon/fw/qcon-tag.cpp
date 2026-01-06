#include "qcon-tag.hpp"

namespace ns3 {
namespace ndn {

TypeId QconTag::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::ndn::QconTag")
      .SetParent<Tag>()
      .AddConstructor<QconTag>();
  return tid;
}
TypeId QconTag::GetInstanceTypeId(void) const { return GetTypeId(); }
uint32_t QconTag::GetSerializedSize(void) const { return sizeof(double) * 2; }

void QconTag::Serialize(TagBuffer i) const {
  i.WriteDouble(m_qValue);
  i.WriteDouble(m_congestionInfo);
}

void QconTag::Deserialize(TagBuffer i) {
  m_qValue = i.ReadDouble();
  m_congestionInfo = i.ReadDouble();
}

void QconTag::Print(std::ostream &os) const {
  os << "Q=" << m_qValue << " CI=" << m_congestionInfo;
}

} // namespace ndn
} // namespace ns3
