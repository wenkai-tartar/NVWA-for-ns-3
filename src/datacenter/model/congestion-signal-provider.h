/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef CONGESTION_SIGNAL_PROVIDER_H
#define CONGESTION_SIGNAL_PROVIDER_H

#include "ns3/net-device.h"
#include "ns3/ptr.h"

#include <cstdint>

namespace ns3
{

enum class QueueMetric : uint8_t
{
    kNone = 0,
    kPackets = 1,
    kBytes = 2,
};

class CongestionSignalProvider
{
  public:
    virtual ~CongestionSignalProvider() = default;
    virtual uint64_t GetMetric(Ptr<NetDevice> dev) const = 0;
};

class NetDeviceQueueSignalProvider : public CongestionSignalProvider
{
  public:
    explicit NetDeviceQueueSignalProvider(QueueMetric metric = QueueMetric::kBytes);

    void SetMetric(QueueMetric metric);
    QueueMetric GetMetricType() const;

    uint64_t GetMetric(Ptr<NetDevice> dev) const override;

  private:
    QueueMetric m_metric{QueueMetric::kBytes};
};

} // namespace ns3

#endif // CONGESTION_SIGNAL_PROVIDER_H
