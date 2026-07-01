/*
 * Author: Jinchao Ma
 */

 #ifndef SYMMETRIC_DEGREE_GRAPH
 #define SYMMETRIC_DEGREE_GRAPH
 
 // #include "ns3/random-variable-stream.h"
 #include <algorithm>
 #include <iostream>
 #include <random>
 #include <set>
 #include <vector>
 
 namespace ns3
 {
 
 class SymmetricDegreeGraph
 {
   public:
     SymmetricDegreeGraph(uint32_t nNodes,
                          uint32_t nDegrees,
                          uint32_t seed = 1,
                          bool avoidSameTOR = false,
                          bool avoidSamePod = false,
                          uint32_t nNodesPerTOR = 1,
                          uint32_t nNodesPerPod = 1);
     ~SymmetricDegreeGraph();
     void GenerateGraph();
     void GenerateDirectedCycle();
     void GenerateUndirectedCycle();
     void AddEdge(uint32_t from, uint32_t to);
     void SetSeed(uint32_t seed);
     void SetTORandPod(uint32_t nPerTOR, uint32_t nPerPod);
     void SetAvoidSameTOR(bool avoid);
     void SetAvoidSamePod(bool avoid);
     std::vector<uint32_t> GetDsts(uint32_t nodeId);
 
     bool IsSameTOR(uint32_t src, uint32_t dst);
     bool IsSamePod(uint32_t src, uint32_t dst);
 
     void Reset();
 
     uint32_t m_nNodes;
     uint32_t m_nDegrees;
     uint32_t m_seed;
     // Ptr<UniformRandomVariable> m_rand;
     std::default_random_engine m_rand;
     std::vector<std::vector<uint32_t>> m_adjList; // adjacency matrix
     bool m_avoidSameTOR;
     bool m_avoidSamePod;
     uint32_t m_nNodesPerTOR;
     uint32_t m_nNodesPerPod;
     // std::vector<uint32_t> m_nodeDegree;
     // std::set<uint32_t> m_availableTargets;
 };
 
 } // namespace ns3
 
 #endif