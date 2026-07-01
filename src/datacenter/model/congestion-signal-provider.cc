/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "congestion-signal-provider.h"

#include "ns3/log.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("CongestionSignalProvider");

NetDeviceQueueSignalProvider::NetDeviceQueueSignalProvider(QueueMetric metric)
    : m_metric(metric)
{
}

void
NetDeviceQueueSignalProvider::SetMetric(QueueMetric metric)
{
    m_metric = metric;
}

QueueMetric
NetDeviceQueueSignalProvider::GetMetricType() const
{
    return m_metric;
}

uint64_t
NetDeviceQueueSignalProvider::GetMetric(Ptr<NetDevice> dev) const
{
    if (!dev || m_metric == QueueMetric::kNone)
    {
        return 0;
    }

    Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice>(dev);
    if (!p2p)
    {
        return 0;
    }

    Ptr<Queue<Packet>> q = p2p->GetQueue();
    if (!q)
    {
        return 0;
    }

    switch (m_metric)
    {
    case QueueMetric::kPackets:
        return q->GetNPackets();
    case QueueMetric::kBytes:
        return q->GetNBytes();
    case QueueMetric::kNone:
    default:
        return 0;
    }
}

} // namespace ns3
