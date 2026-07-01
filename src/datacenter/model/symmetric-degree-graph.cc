/*
 * Author: Jinchao Ma
 */

 #include "symmetric-degree-graph.h"

 #include <algorithm>
 #include <iostream>
 #include <random>
 #include <vector>
 
 namespace ns3
 {
 
 SymmetricDegreeGraph::SymmetricDegreeGraph(uint32_t nNodes,
                                            uint32_t nDegrees,
                                            uint32_t seed,
                                            bool avoidSameTOR,
                                            bool avoidSamePod,
                                            uint32_t nNodesPerTOR,
                                            uint32_t nNodesPerPod)
     : m_nNodes(nNodes),
       m_nDegrees(nDegrees),
       m_seed(seed),
       m_avoidSameTOR(avoidSameTOR),
       m_avoidSamePod(avoidSamePod),
       m_nNodesPerTOR(nNodesPerTOR),
       m_nNodesPerPod(nNodesPerPod)
 {
     m_adjList.resize(m_nNodes);
     // m_nodeDegree = std::vector<uint32_t>(n, 0);
     // m_availableTargets = std::set<uint32_t>;
     SetSeed(seed);
     // m_rand = CreateObject<UniformRandomVariable>();
 }
 
 SymmetricDegreeGraph::~SymmetricDegreeGraph()
 {
 }
 
 void
 SymmetricDegreeGraph::SetSeed(uint32_t seed)
 {
     m_seed = seed;
     m_rand.seed(seed);
 }
 
 void
 SymmetricDegreeGraph::SetTORandPod(uint32_t nPerTOR, uint32_t nPerPod)
 {
     m_nNodesPerTOR = nPerTOR;
     m_nNodesPerPod = nPerPod;
 }
 
 void
 SymmetricDegreeGraph::SetAvoidSamePod(bool avoid)
 {
     // std::cout << avoid << std::endl;
     m_avoidSamePod = avoid;
 }
 
 void
 SymmetricDegreeGraph::SetAvoidSameTOR(bool avoid)
 {
     m_avoidSameTOR = avoid;
 }
 
 void
 SymmetricDegreeGraph::GenerateGraph()
 {
     uint32_t degree = m_nDegrees;
     // AddEdge(0, 4);
     // AddEdge(0, 5);
     // AddEdge(0, 4);
     // AddEdge(0, 5);
 
     // AddEdge(1, 4);
     // AddEdge(1, 5);
     // AddEdge(1, 4);
     // AddEdge(1, 5);
 
     // AddEdge(2, 6);
     // AddEdge(2, 7);
     // AddEdge(2, 6);
     // AddEdge(2, 7);
 
     // AddEdge(3, 6);
     // AddEdge(3, 7);
     // AddEdge(3, 6);
     // AddEdge(3, 7);
 
     // AddEdge(4, 0);
     // AddEdge(4, 1);
     // AddEdge(4, 0);
     // AddEdge(4, 1);
 
     // AddEdge(5, 0);
     // AddEdge(5, 1);
     // AddEdge(5, 0);
     // AddEdge(5, 1);
 
     // AddEdge(6, 2);
     // AddEdge(6, 3);
     // AddEdge(6, 2);
     // AddEdge(6, 3);
 
     // AddEdge(7, 2);
     // AddEdge(7, 3);
     // AddEdge(7, 2);
     // AddEdge(7, 3);
 
     // AddEdge(8, 12);
     // AddEdge(8, 13);
     // AddEdge(8, 12);
     // AddEdge(8, 13);
 
     // AddEdge(9, 12);
     // AddEdge(9, 13);
     // AddEdge(9, 12);
     // AddEdge(9, 13);
 
     // AddEdge(10, 14);
     // AddEdge(10, 15);
     // AddEdge(10, 14);
     // AddEdge(10, 15);
 
     // AddEdge(11, 14);
     // AddEdge(11, 15);
     // AddEdge(11, 14);
     // AddEdge(11, 15);
 
     // AddEdge(12, 8);
     // AddEdge(12, 9);
     // AddEdge(12, 8);
     // AddEdge(12, 9);
 
     // AddEdge(13, 8);
     // AddEdge(13, 9);
     // AddEdge(13, 8);
     // AddEdge(13, 9);
 
     // AddEdge(14, 10);
     // AddEdge(14, 11);
     // AddEdge(14, 10);
     // AddEdge(14, 11);
 
     // AddEdge(15, 10);
     // AddEdge(15, 11);
     // AddEdge(15, 10);
     // AddEdge(15, 11);
     while (degree > 0)
     {
         if (degree >= 2)
         {
             degree -= 2;
             GenerateUndirectedCycle();
         }
         else
         {
             degree -= 1;
             GenerateDirectedCycle();
         }
     }
 }
 
 void
 SymmetricDegreeGraph::GenerateDirectedCycle()
 {
     std::vector<uint32_t> availableTargets(m_nNodes);
     for (uint32_t i = 0; i < m_nNodes; i++)
     {
         availableTargets[i] = i;
     }
     // can add is or not in same TOR
     auto start = availableTargets.begin();
     uint32_t src = *start;
     availableTargets.erase(start);
 
     uint32_t last = src;
     while (!availableTargets.empty())
     {
         // auto it = availableTargets.begin();
         // std::advance(it, m_rand() % availableTargets.size());
         // uint32_t dst = *it;
         bool valid = false;
         uint32_t dst = 0;
         while (!valid)
         {
             size_t randIndex = m_rand() % availableTargets.size();
             dst = availableTargets[randIndex];
             if (m_avoidSameTOR && IsSameTOR(src, dst))
             {
                 continue;
             }
 
             if (m_avoidSamePod && IsSamePod(src, dst))
             {
                 continue;
             }
             valid = true;
         }
 
         AddEdge(src, dst);
         src = dst;
         // availableTargets.erase(it);
         availableTargets.erase(std::find(availableTargets.begin(), availableTargets.end(), dst));
     }
     AddEdge(src, last);
 }
 
 void
 SymmetricDegreeGraph::GenerateUndirectedCycle()
 {
     std::vector<uint32_t> availableTargets(m_nNodes);
     for (uint32_t i = 0; i < m_nNodes; i++)
     {
         availableTargets[i] = i;
     }
     // can add is or not in same TOR
     auto start = availableTargets.begin();
     uint32_t src = *start;
     availableTargets.erase(start);
 
     uint32_t last = src;
     while (!availableTargets.empty())
     {
         // auto it = availableTargets.begin();
         // std::advance(it, m_rand() % availableTargets.size());
         // uint32_t dst = *it;
         bool valid = false;
         uint32_t dst = 0;
         while (!valid)
         {
             size_t randIndex = m_rand() % availableTargets.size();
             dst = availableTargets[randIndex];
             if (m_avoidSameTOR && IsSameTOR(src, dst))
             {
                 continue;
             }
             // std::cout << m_nNodesPerPod << std::endl;
             if (m_avoidSamePod && IsSamePod(src, dst))
             {
                 continue;
             }
             valid = true;
         }
 
         AddEdge(src, dst);
         AddEdge(dst, src);
         src = dst;
         // availableTargets.erase(it);
         availableTargets.erase(std::find(availableTargets.begin(), availableTargets.end(), dst));
     }
     AddEdge(src, last);
     AddEdge(last, src);
 }
 
 void
 SymmetricDegreeGraph::AddEdge(uint32_t from, uint32_t to)
 {
     m_adjList[from].push_back(to);
 }
 
 std::vector<uint32_t>
 SymmetricDegreeGraph::GetDsts(uint32_t nodeId)
 {
     if (nodeId >= m_nNodes)
     {
         std::cerr << "Node ID out of range!" << std::endl;
         return {};
     }
 
     return m_adjList[nodeId];
 }
 
 void
 SymmetricDegreeGraph::Reset()
 {
     m_rand.seed(m_seed);
     for (auto& dsts : m_adjList)
     {
         dsts.clear();
     }
 }
 
 bool
 SymmetricDegreeGraph::IsSameTOR(uint32_t src, uint32_t dst)
 {
     return (src / m_nNodesPerTOR == dst / m_nNodesPerTOR);
 }
 
 bool
 SymmetricDegreeGraph::IsSamePod(uint32_t src, uint32_t dst)
 {
     return (src / m_nNodesPerPod == dst / m_nNodesPerPod);
 }
 
 } // namespace ns3