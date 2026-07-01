/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef INGRESS_CLASSIFIER_H
#define INGRESS_CLASSIFIER_H

#include "routing-common.h"

#include <cstdint>
#include <vector>

namespace ns3
{

class PortSetIngressClassifier
{
  public:
    void Build(const PortSet& ports, uint32_t numIfs);

    const IngressInfo& Get(uint32_t ifIndex) const;

    const std::vector<IngressInfo>& GetAll() const
    {
        return m_byIf;
    }

  private:
    void SetIf(uint32_t ifIndex, const IngressInfo& info, const char* reason);

    std::vector<IngressInfo> m_byIf;
    IngressInfo m_unknown;
};

} // namespace ns3

#endif // INGRESS_CLASSIFIER_H
