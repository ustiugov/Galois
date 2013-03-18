/** Local Computation graphs -*- C++ -*-
 * @file
 * @section License
 *
 * Galois, a framework to exploit amorphous data-parallelism in irregular
 * programs.
 *
 * Copyright (C) 2013, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 *
 * @section Description
 *
 * There are two main classes, ::FileGraph and ::LC_XXX_Graph. The former
 * represents the pure structure of a graph (i.e., whether an edge exists between
 * two nodes) and cannot be modified. The latter allows values to be stored on
 * nodes and edges, but the structure of the graph cannot be modified.
 *
 * @author Andrew Lenharth <andrewl@lenharth.org>
 * @author Donald Nguyen <ddn@cs.utexas.edu>
 */
#ifndef GALOIS_GRAPH_LCGRAPH_H
#define GALOIS_GRAPH_LCGRAPH_H

#include "Galois/Galois.h"
#include "Galois/LargeArray.h"
#include "Galois/Graph/FileGraph.h"
#include "Galois/Graph/Util.h"
#include "Galois/Runtime/MethodFlags.h"
#include "Galois/Runtime/mm/Mem.h"

#include <boost/utility/enable_if.hpp>
#include <boost/iterator/iterator_facade.hpp>

#include <iterator>
#include <new>

namespace Galois {
namespace Graph {

/**
 * Local computation graph (i.e., graph structure does not change).
 *
 * An example of use:
 * 
 * \code
 * typedef Galois::Graph::LC_CSR_Graph<int,int> Graph;
 * 
 * // Create graph
 * Graph g;
 * g.structureFromFile(inputfile);
 *
 * // Traverse graph
 * for (Graph::iterator ii = g.begin(), ei = g.end(); ii != ei; ++ii) {
 *   Graph::GraphNode src = *ii;
 *   for (Graph::edge_iterator jj = g.edge_begin(src), ej = g.edge_end(src); jj != ej; ++jj) {
 *     Graph::GraphNode dst = g.getEdgeDst(jj);
 *     int edgeData = g.getEdgeData(jj);
 *     int nodeData = g.getData(dst);
 *   }
 * }
 * \endcode
 *
 * And in C++11:
 *
 * \code
 * typedef Galois::Graph::LC_CSR_Graph<int,int> Graph;
 * 
 * // Create graph
 * Graph g;
 * g.structureFromFile(inputfile);
 *
 * // Traverse graph
 * for (Graph::GraphNode src : g) {
 *   for (Graph::edge_iterator edge : g.out_edges(src)) {
 *     Graph::GraphNode dst = g.getEdgeDst(edge);
 *     int edgeData = g.getEdgeData(edge);
 *     int nodeData = g.getData(dst);
 *   }
 * }
 * \endcode
 *
 * @tparam NodeTy data on nodes
 * @tparam EdgeTy data on out edges
 */
template<typename NodeTy, typename EdgeTy>
class LC_CSR_Graph: boost::noncopyable {
protected:
  typedef LargeArray<EdgeTy,false> EdgeData;
  typedef LargeArray<uint32_t,true> EdgeDst;
  typedef NodeInfoBase<NodeTy> NodeInfo;
  typedef LargeArray<uint64_t,true> EdgeIndData;
  typedef LargeArray<NodeInfo,false> NodeData;

  NodeData nodeData;
  EdgeIndData edgeIndData;
  EdgeDst edgeDst;
  EdgeData edgeData;

  uint64_t numNodes;
  uint64_t numEdges;

  uint64_t raw_neighbor_begin(uint32_t N) const {
    return (N == 0) ? 0 : edgeIndData[N-1];
  }

  uint64_t raw_neighbor_end(uint32_t N) const {
    return edgeIndData[N];
  }

  uint64_t getEdgeIdx(uint32_t src, uint32_t dst) {
    for (uint64_t ii = raw_neighbor_begin(src),
	   ee = raw_neighbor_end(src); ii != ee; ++ii)
      if (edgeDst[ii] == dst)
        return ii;
    return ~static_cast<uint64_t>(0);
  }

  EdgeSortIterator<EdgeDst,EdgeData> edge_sort_begin(uint32_t src) {
    return EdgeSortIterator<EdgeDst,EdgeData>(raw_neighbor_begin(src), &edgeDst, &edgeData);
  }

  EdgeSortIterator<EdgeDst,EdgeData> edge_sort_end(uint32_t src) {
    return EdgeSortIterator<EdgeDst,EdgeData>(raw_neighbor_end(src), &edgeDst, &edgeData);
  }
  
public:
  typedef uint32_t GraphNode;
  typedef EdgeTy edge_data_type;
  typedef NodeTy node_data_type;
  typedef typename EdgeData::reference edge_data_reference;
  typedef typename NodeInfo::reference node_data_reference;
  typedef boost::counting_iterator<uint64_t> edge_iterator;
  typedef boost::counting_iterator<uint32_t> iterator;
  typedef iterator const_iterator;
  typedef iterator local_iterator;
  typedef iterator const_local_iterator;

  node_data_reference getData(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::checkWrite(mflag, false);
    NodeInfo& NI = nodeData[N];
    Galois::Runtime::acquire(&NI, mflag);
    return NI.getData();
  }

  bool hasNeighbor(GraphNode src, GraphNode dst, MethodFlag mflag = MethodFlag::ALL) {
    return getEdgeIdx(src, dst) != ~static_cast<uint64_t>(0);
  }

  edge_data_reference getEdgeData(edge_iterator ni, MethodFlag mflag = MethodFlag::NONE) {
    Galois::Runtime::checkWrite(mflag, false);
    return edgeData[*ni];
  }

  GraphNode getEdgeDst(edge_iterator ni) {
    return edgeDst[*ni];
  }

  uint64_t size() const { return numNodes; }
  uint64_t sizeEdges() const { return numEdges; }

  iterator begin() const { return iterator(0); }
  iterator end() const { return iterator(numNodes); }

  local_iterator local_begin() const { return iterator(localStart(numNodes)); }
  local_iterator local_end() const { return iterator(localEnd(numNodes)); }

  edge_iterator edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&nodeData[N], mflag);
    if (Galois::Runtime::shouldLock(mflag)) {
      for (uint64_t ii = raw_neighbor_begin(N), ee = raw_neighbor_end(N); ii != ee; ++ii) {
        Galois::Runtime::acquire(&nodeData[edgeDst[ii]], mflag);
      }
    }
    return edge_iterator(raw_neighbor_begin(N));
  }

  edge_iterator edge_end(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    NodeInfo& NI = nodeData[N];
    Galois::Runtime::acquire(&NI, mflag);
    return edge_iterator(raw_neighbor_end(N));
  }

  EdgesIterator<LC_CSR_Graph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return EdgesIterator<LC_CSR_Graph>(*this, N, mflag);
  }

  /**
   * Sorts outgoing edges of a node. Comparison function is over EdgeTy.
   */
  template<typename CompTy>
  void sortEdgesByEdgeData(GraphNode N, const CompTy& comp = std::less<EdgeTy>(), MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&nodeData[N], mflag);
    std::sort(edge_sort_begin(N), edge_sort_end(N), EdgeSortCompWrapper<EdgeSortValue<EdgeTy>,CompTy>(comp));
  }

  /**
   * Sorts outgoing edges of a node. Comparison function is over <code>EdgeSortValue<EdgeTy></code>.
   */
  template<typename CompTy>
  void sortEdges(GraphNode N, const CompTy& comp, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&nodeData[N], mflag);
    std::sort(edge_sort_begin(N), edge_sort_end(N), comp);
  }

  void structureFromFile(const std::string& fname) { Graph::structureFromFile(*this, fname); }

  void structureFromGraph(FileGraph& graph) {
    numNodes = graph.size();
    numEdges = graph.sizeEdges();
    nodeData.allocate(numNodes);
    edgeIndData.allocate(numNodes);
    edgeDst.allocate(numEdges);
    edgeData.allocate(numEdges);

    typedef typename EdgeData::value_type EDV;

    if (EdgeData::has_value)
      edgeData.copyIn(graph.edge_data_begin<EDV>(), graph.edge_data_end<EDV>());
    std::copy(graph.edge_id_begin(), graph.edge_id_end(), edgeIndData.data());
    std::copy(graph.node_id_begin(), graph.node_id_end(), edgeDst.data());
  }
};

namespace InOutGraphImpl {

template<typename EdgeDst, typename EdgeIndData>
struct InEdgesBase {
  typedef typename EdgeDst::value_type NodeIdx;
  typedef typename EdgeIndData::value_type EdgeIdx;
  
  EdgeIndData edgeIndDataStore;
  EdgeDst edgeDstStore;
  EdgeIndData* edgeIndData;
  EdgeDst* edgeDst;

  EdgeIdx raw_begin(NodeIdx n) const {
    return (n == 0) ? 0 : (*edgeIndData)[n-1];
  }

  EdgeIdx raw_end(NodeIdx n) const {
    return (*edgeIndData)[n];
  }

  NodeIdx getEdgeDst(EdgeIdx e) {
    return (*edgeDst)[e];
  }
};

//! Dispatch type to handle implementation of in edges
template<typename EdgeTy, typename EdgeDst, typename EdgeIndData, bool HasPointerData>
class InEdges;

template<typename EdgeTy, typename EdgeDst, typename EdgeIndData>
class InEdges<EdgeTy,EdgeDst,EdgeIndData,false>: public InEdgesBase<EdgeDst,EdgeIndData> {
  typedef InEdgesBase<EdgeDst,EdgeIndData> Super;

  typedef Galois::LargeArray<EdgeTy,false> EdgeData;

  EdgeData edgeDataStore;
  EdgeData* edgeData;

  typedef typename Super::NodeIdx NodeIdx;
  typedef typename Super::NodeIdx EdgeIdx;

public:
  EdgeSortIterator<EdgeDst,EdgeData> edge_sort_begin(NodeIdx n) {
    return EdgeSortIterator<EdgeDst,EdgeData>(this->raw_begin(n), this->edgeDst, this->edgeData);
  }

  EdgeSortIterator<EdgeDst,EdgeData> edge_sort_end(NodeIdx n) {
    return EdgeSortIterator<EdgeDst,EdgeData>(this->raw_end(n), this->edgeDst, this->edgeData);
  }

  typename EdgeData::reference getEdgeData(EdgeIdx e) {
    return (*edgeData)[e];
  }

  void initialize(EdgeIndData* edgeIndDataPtr, EdgeDst* edgeDstPtr, EdgeData* edgeDataPtr, bool symmetric) {
    if (symmetric) {
      this->edgeIndData = edgeIndDataPtr;
      this->edgeDst = edgeDstPtr;
      this->edgeData = edgeDataPtr;
      return;
    }

    // TODO: Transpose graph
    abort();
  }

  void initialize(FileGraph& transpose) {
    typedef typename EdgeData::value_type EDV;

    this->edgeIndData = &this->edgeIndDataStore;
    this->edgeDst = &this->edgeDstStore;
    edgeData = &edgeDataStore;

    size_t numNodes = transpose.size();
    size_t numEdges = transpose.sizeEdges();

    this->edgeIndDataStore.allocate(numNodes);
    this->edgeDstStore.allocate(numEdges);
    edgeDataStore.allocate(numEdges);

    if (EdgeData::has_value)
      edgeData->copyIn(transpose.edge_data_begin<EDV>(), transpose.edge_data_end<EDV>());
    std::copy(transpose.edge_id_begin(), transpose.edge_id_end(), this->edgeIndData->data());
    std::copy(transpose.node_id_begin(), transpose.node_id_end(), this->edgeDst->data());
  }
};

template<typename EdgeTy, typename EdgeDst, typename EdgeIndData>
class InEdges<EdgeTy,EdgeDst,EdgeIndData,true>: public InEdgesBase<EdgeDst,EdgeIndData> {
  // TODO: implement for when we don't store copies of edges
};

} // end namespace

/**
 * Local computation graph (i.e., graph structure does not change) that
 * supports in and out edges.
 *
 * An additional template parameter specifies whether the in edge stores a
 * reference to the corresponding out edge data (the default) or a copy of the
 * corresponding out edge data. If you want to populate this graph from a
 * FileGraph that is already symmetric (i.e., (u,v) \in E ==> (v,u) \in E),
 * this parameter should be true.
 *
 * @tparam NodeTy data on nodes
 * @tparam EdgeTy data on out edges
 * @tparam CopyInEdgeData in edges hold a copy of out edge data
 */
template<typename NodeTy, typename EdgeTy, bool CopyInEdgeData = false>
class LC_CSR_InOutGraph: public LC_CSR_Graph<NodeTy,EdgeTy> {
  typedef LC_CSR_Graph<NodeTy,EdgeTy> Super;

protected:
  InOutGraphImpl::InEdges<EdgeTy,typename Super::EdgeDst,typename Super::EdgeIndData, !CopyInEdgeData> inEdges;

public:
  typedef typename Super::GraphNode GraphNode;
  typedef typename Super::edge_data_type edge_data_type;
  typedef typename Super::node_data_type node_data_type;
  typedef typename Super::edge_data_reference edge_data_reference;
  typedef typename Super::node_data_reference node_data_reference;
  typedef typename Super::edge_iterator edge_iterator;
  typedef edge_iterator in_edge_iterator;
  typedef typename Super::iterator iterator;
  typedef typename Super::const_iterator const_iterator;
  typedef typename Super::local_iterator local_iterator;
  typedef typename Super::const_local_iterator const_local_iterator;

  edge_data_reference getInEdgeData(in_edge_iterator ni, MethodFlag mflag = MethodFlag::NONE) { 
    Galois::Runtime::checkWrite(mflag, false);
    return inEdges.getEdgeData(*ni);
  }

  GraphNode getInEdgeDst(in_edge_iterator ni) {
    return inEdges.getEdgeDst(*ni);
  }

  in_edge_iterator in_edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&this->nodeData[N], mflag);
    if (Galois::Runtime::shouldLock(mflag)) {
      for (uint64_t ii = inEdges.raw_begin(N), ee = inEdges.raw_end(N); ii != ee; ++ii) {
        Galois::Runtime::acquire(&this->nodeData[inEdges.getEdgeDst(ii)], mflag);
      }
    }
    return in_edge_iterator(inEdges.raw_begin(N));
  }

  in_edge_iterator in_edge_end(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&this->nodeData[N], mflag);
    return in_edge_iterator(inEdges.raw_end(N));
  }

  InEdgesIterator<LC_CSR_InOutGraph> in_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return InEdgesIterator<LC_CSR_InOutGraph>(*this, N, mflag);
  }

  /**
   * Sorts incoming edges of a node. Comparison function is over EdgeTy.
   */
  template<typename CompTy>
  void sortInEdgesByEdgeData(GraphNode N, const CompTy& comp = std::less<EdgeTy>(), MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&this->nodeData[N], mflag);
    std::sort(inEdges.edge_sort_begin(N), inEdges.edge_sort_end(N), EdgeSortCompWrapper<EdgeSortValue<EdgeTy>,CompTy>(comp));
  }

  /**
   * Sorts incoming edges of a node. Comparison function is over <code>EdgeSortValue<EdgeTy></code>.
   */
  template<typename CompTy>
  void sortInEdges(GraphNode N, const CompTy& comp, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(&this->nodeData[N], mflag);
    std::sort(inEdges.edge_sort_begin(N), inEdges.edge_sort_end(N), comp);
  }

  void structureFromFile(const std::string& fname, bool symmetric) { Graph::structureFromFile(*this, fname, symmetric); }

  void structureFromGraph(FileGraph& graph, FileGraph& transpose, typename boost::enable_if_c<CopyInEdgeData>::type* dummy = 0) {
    if (graph.size() != transpose.size()) {
      GALOIS_ERROR(true, "number of nodes in graph and its transpose do not match");
    } else if (graph.sizeEdges() != transpose.sizeEdges()) {
      GALOIS_ERROR(true, "number of edges in graph and its transpose do not match");
    }

    Super::structureFromGraph(graph);
    inEdges.initialize(transpose);
  }

  void structureFromGraph(FileGraph& graph, bool symmetric) {
    Super::structureFromGraph(graph);
    inEdges.initialize(&this->edgeIndData, &this->edgeDst, &this->edgeData, symmetric);
  }
};

//! Local computation graph (i.e., graph structure does not change)
template<typename NodeTy, typename EdgeTy>
class LC_CSRInline_Graph: boost::noncopyable {
protected:
  struct NodeInfo;
  typedef EdgeInfoBase<NodeInfo, EdgeTy> EdgeInfo;
  
  struct NodeInfo: public NodeInfoBase<NodeTy> {
    EdgeInfo* edgeBegin;
    EdgeInfo* edgeEnd;
  };

  LargeArray<NodeInfo,false> nodeData;
  LargeArray<EdgeInfo,true> edgeData;
  uint64_t numNodes;
  uint64_t numEdges;
  NodeInfo* endNode;

  uint64_t getEdgeIdx(uint64_t src, uint64_t dst) {
    NodeInfo& NI = nodeData[src];
    for (uint64_t x = NI.edgeBegin; x < NI.edgeEnd; ++x)
      if (edgeData[x].dst == dst)
        return x;
    return ~static_cast<uint64_t>(0);
  }

public:
  typedef NodeInfo* GraphNode;
  typedef EdgeTy edge_data_type;
  typedef NodeTy node_data_type;
  typedef typename EdgeInfo::reference edge_data_reference;
  typedef typename NodeInfo::reference node_data_reference;
  typedef EdgeInfo* edge_iterator;

  class iterator : std::iterator<std::random_access_iterator_tag, GraphNode> {
    NodeInfo* at;

  public:
    iterator(NodeInfo* a) :at(a) {}
    iterator(const iterator& m) :at(m.at) {}
    iterator& operator++() { ++at; return *this; }
    iterator operator++(int) { iterator tmp(*this); ++at; return tmp; }
    iterator& operator--() { --at; return *this; }
    iterator operator--(int) { iterator tmp(*this); --at; return tmp; }
    bool operator==(const iterator& rhs) { return at == rhs.at; }
    bool operator!=(const iterator& rhs) { return at != rhs.at; }
    GraphNode operator*() { return at; }
  };

  typedef iterator const_iterator;
  typedef iterator local_iterator;
  typedef iterator const_local_iterator;

  ~LC_CSRInline_Graph() {
    if (!EdgeInfo::has_value) return;
    if (nodeData.data() == endNode) return;

    for (EdgeInfo* ii = nodeData[0]->edgeBegin, ei = endNode->edgeEnd; ii != ei; ++ii) {
      ii->destroy();
    }
  }

  node_data_reference getData(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::checkWrite(mflag, false);
    Galois::Runtime::acquire(N, mflag);
    return N->getData();
  }

  edge_data_reference getEdgeData(edge_iterator ni, MethodFlag mflag = MethodFlag::NONE) const {
    Galois::Runtime::checkWrite(mflag, false);
    return ni->get();
   }

  GraphNode getEdgeDst(edge_iterator ni) const {
    return ni->dst;
  }

  uint64_t size() const { return numNodes; }
  uint64_t sizeEdges() const { return numEdges; }

  iterator begin() const { return iterator(nodeData.data()); }
  iterator end() const { return iterator(endNode); }

  local_iterator local_begin() const { return iterator(&nodeData[localStart(numNodes)]); }
  local_iterator local_end() const { return iterator(&nodeData[localEnd(numNodes)]); }

  edge_iterator edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    if (Galois::Runtime::shouldLock(mflag)) {
      for (edge_iterator ii = N->edgeBegin, ee = N->edgeEnd; ii != ee; ++ii) {
        Galois::Runtime::acquire(ii->dst, mflag);
      }
    }
    return N->edgeBegin;
  }

  edge_iterator edge_end(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    return N->edgeEnd;
  }

  EdgesIterator<LC_CSRInline_Graph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return EdgesIterator<LC_CSRInline_Graph>(*this, N, mflag);
  }

  void structureFromFile(const std::string& fname) { Graph::structureFromFile(*this, fname); }

  void structureFromGraph(FileGraph& graph) {
    typedef typename EdgeInfo::value_type EDV;

    numNodes = graph.size();
    numEdges = graph.sizeEdges();
    nodeData.allocate(numNodes);
    edgeData.allocate(numEdges);

    std::vector<NodeInfo*> node_ids;
    node_ids.resize(numNodes);
    for (FileGraph::iterator ii = graph.begin(), ee = graph.end(); ii != ee; ++ii) {
      NodeInfo* curNode = &nodeData[*ii];
      node_ids[*ii] = curNode;
    }
    endNode = &nodeData[numNodes];

    //layout the edges
    EdgeInfo* curEdge = edgeData.data();
    for (FileGraph::iterator ii = graph.begin(), ee = graph.end(); ii != ee; ++ii) {
      node_ids[*ii]->edgeBegin = curEdge;
      for (FileGraph::neighbor_iterator ni = graph.neighbor_begin(*ii), 
          ne = graph.neighbor_end(*ii); ni != ne; ++ni) {
        if (EdgeInfo::has_value)
          curEdge->construct(graph.getEdgeData<EDV>(ni));
        curEdge->dst = node_ids[*ni];
        ++curEdge;
      }
      node_ids[*ii]->edgeEnd = curEdge;
    }
  }
};

//! Local computation graph (i.e., graph structure does not change)
template<typename NodeTy, typename EdgeTy>
class LC_Linear_Graph: boost::noncopyable {
protected:
  struct NodeInfo;
  typedef EdgeInfoBase<NodeInfo,EdgeTy> EdgeInfo;
  typedef LargeArray<NodeInfo*,true> Nodes;

  struct NodeInfo : public NodeInfoBase<NodeTy> {
    int numEdges;

    EdgeInfo* edgeBegin() {
      NodeInfo* n = this;
      ++n; //start of edges
      return reinterpret_cast<EdgeInfo*>(n);
    }

    EdgeInfo* edgeEnd() {
      EdgeInfo* ei = edgeBegin();
      ei += numEdges;
      return ei;
    }

    NodeInfo* next() {
      NodeInfo* ni = this;
      EdgeInfo* ei = edgeEnd();
      while (reinterpret_cast<char*>(ni) < reinterpret_cast<char*>(ei))
        ++ni;
      return ni;
    }
  };
 
  LargeArray<char,true> data;
  uint64_t numNodes;
  uint64_t numEdges;
  Nodes nodes;

  EdgeInfo* getEdgeIdx(NodeInfo* src, NodeInfo* dst) {
    EdgeInfo* eb = src->edgeBegin();
    EdgeInfo* ee = src->edgeEnd();
    for (; eb != ee; ++eb)
      if (eb->dst == dst)
        return eb;
    return 0;
  }

public:
  typedef NodeInfo* GraphNode;
  typedef EdgeTy edge_data_type;
  typedef NodeTy node_data_type;
  typedef typename NodeInfo::reference node_data_reference;
  typedef typename EdgeInfo::reference edge_data_reference;
  typedef EdgeInfo* edge_iterator;
  typedef NodeInfo** iterator;
  typedef NodeInfo*const * const_iterator;
  typedef iterator local_iterator;
  typedef const_iterator const_local_iterator;

  LC_Linear_Graph() { }

  ~LC_Linear_Graph() { 
    for (typename Nodes::iterator ii = nodes.begin(), ei = nodes.end(); ii != ei; ++ii) {
      NodeInfo* n = *ii;
      EdgeInfo* edgeBegin = n->edgeBegin();
      EdgeInfo* edgeEnd = n->edgeEnd();

      n->destruct();
      if (EdgeInfo::has_value) {
        while (edgeBegin != edgeEnd) {
          edgeBegin->destroy();
          ++edgeBegin;
        }
      }
    }
  }

  node_data_reference getData(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::checkWrite(mflag, false);
    Galois::Runtime::acquire(N, mflag);
    return N->getData();
  }
  
  edge_data_reference getEdgeData(edge_iterator ni, MethodFlag mflag = MethodFlag::NONE) const {
    Galois::Runtime::checkWrite(mflag, false);
    return ni->get();
  }

  GraphNode getEdgeDst(edge_iterator ni) const {
    return ni->dst;
  }

  uint64_t size() const { return numNodes; }
  uint64_t sizeEdges() const { return numEdges; }
  iterator begin() { return &nodes[0]; }
  iterator end() { return &nodes[numNodes]; }
  const_iterator begin() const { return &nodes[0]; }
  const_iterator end() const { return &nodes[numNodes]; }
  local_iterator local_begin() { return &nodes[localStart(numNodes)]; }
  local_iterator local_end() { return &nodes[localEnd(numNodes)]; }
  const_local_iterator local_begin() const { return &nodes[localStart(numNodes)]; }
  const_local_iterator local_end() const { return &nodes[localEnd(numNodes)]; }

  edge_iterator edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    if (Galois::Runtime::shouldLock(mflag)) {
      for (edge_iterator ii = N->edgeBegin(), ee = N->edgeEnd(); ii != ee; ++ii) {
        Galois::Runtime::acquire(ii->dst, mflag);
      }
    }
    return N->edgeBegin();
  }

  edge_iterator edge_end(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    return N->edgeEnd();
  }

  EdgesIterator<LC_Linear_Graph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return EdgesIterator<LC_Linear_Graph>(*this, N, mflag);
  }

  /**
   * Sorts outgoing edges of a node. Comparison function is over EdgeTy.
   */
  template<typename CompTy>
  void sortEdgesByEdgeData(GraphNode N, const CompTy& comp = std::less<EdgeTy>(), MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    std::sort(N->edgeBegin(), N->edgeEnd(), EdgeSortCompWrapper<EdgeInfo,CompTy>(comp));
  }

  /**
   * Sorts outgoing edges of a node. Comparison function is over <code>EdgeSortValue<EdgeTy></code>.
   */
  template<typename CompTy>
  void sortEdges(GraphNode N, const CompTy& comp, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    std::sort(N->edgeBegin(), N->edgeEnd(), comp);
  }

  void structureFromFile(const std::string& fname) { Graph::structureFromFile(*this, fname); }

  void structureFromGraph(FileGraph& graph) {
    typedef typename EdgeInfo::value_type EDV;

    numNodes = graph.size();
    numEdges = graph.sizeEdges();
    data.allocate(sizeof(NodeInfo) * numNodes * 2 + sizeof(EdgeInfo) * numEdges);
    nodes.allocate(numNodes);
    NodeInfo* curNode = reinterpret_cast<NodeInfo*>(data.data());
    for (FileGraph::iterator ii = graph.begin(), ee = graph.end(); ii != ee; ++ii) {
      curNode->construct();
      curNode->numEdges = std::distance(graph.neighbor_begin(*ii), graph.neighbor_end(*ii));
      nodes[*ii] = curNode;
      curNode = curNode->next();
    }

    //layout the edges
    for (FileGraph::iterator ii = graph.begin(), ee = graph.end(); ii != ee; ++ii) {
      EdgeInfo* edge = nodes[*ii]->edgeBegin();
      for (FileGraph::neighbor_iterator ni = graph.neighbor_begin(*ii),
          ne = graph.neighbor_end(*ii); ni != ne; ++ni) {
        if (EdgeInfo::has_value)
          edge->construct(graph.getEdgeData<EDV>(ni));
        edge->dst = nodes[*ni];
        ++edge;
      }
    }
  }
};

//! Local computation graph (i.e., graph structure does not change)
//! Specialization of LC_Linear_Graph for NUMA architectures
template<typename NodeTy, typename EdgeTy>
class LC_Numa_Graph: boost::noncopyable {
protected:
  struct NodeInfo;
  typedef EdgeInfoBase<NodeInfo,EdgeTy> EdgeInfo;
  typedef LargeArray<NodeInfo*,true> Nodes;

  struct NodeInfo : public NodeInfoBase<NodeTy> {
    int numEdges;

    EdgeInfo* edgeBegin() {
      NodeInfo* n = this;
      ++n; //start of edges
      return reinterpret_cast<EdgeInfo*>(n);
    }

    EdgeInfo* edgeEnd() {
      EdgeInfo* ei = edgeBegin();
      ei += numEdges;
      return ei;
    }

    NodeInfo* next() {
      NodeInfo* ni = this;
      EdgeInfo* ei = edgeEnd();
      while (reinterpret_cast<char*>(ni) < reinterpret_cast<char*>(ei))
        ++ni;
      return ni;
    }
  };

  struct Header {
    NodeInfo* begin;
    NodeInfo* end;
    size_t size;
  };

  Galois::Runtime::PerThreadStorage<Header*> headers;
  Nodes nodes;
  uint64_t numNodes;
  uint64_t numEdges;

  EdgeInfo* getEdgeIdx(NodeInfo* src, NodeInfo* dst) {
    EdgeInfo* eb = src->edgeBegin();
    EdgeInfo* ee = src->edgeEnd();
    for (; eb != ee; ++eb)
      if (eb->dst == dst)
        return eb;
    return 0;
  }

  struct DistributeInfo {
    uint64_t numNodes;
    uint64_t numEdges;
    FileGraph::iterator begin;
    FileGraph::iterator end;
  };

  //! Divide graph into equal sized chunks
  void distribute(FileGraph& graph, Galois::Runtime::PerThreadStorage<DistributeInfo>& dinfo) {
    size_t total = sizeof(NodeInfo) * numNodes + sizeof(EdgeInfo) * numEdges;
    unsigned int num = Galois::getActiveThreads();
    size_t blockSize = total / num;
    size_t curSize = 0;
    FileGraph::iterator ii = graph.begin();
    FileGraph::iterator ei = graph.end();
    FileGraph::iterator last = ii;
    uint64_t nnodes = 0;
    uint64_t nedges = 0;
    uint64_t runningNodes = 0;
    uint64_t runningEdges = 0;

    unsigned int tid;
    for (tid = 0; tid + 1 < num; ++tid) {
      for (; ii != ei; ++ii) {
        if (curSize >= (tid + 1) * blockSize) {
          DistributeInfo& d = *dinfo.getRemote(tid);
          d.numNodes = nnodes;
          d.numEdges = nedges;
          d.begin = last;
          d.end = ii;

          runningNodes += nnodes;
          runningEdges += nedges;
          nnodes = nedges = 0;
          last = ii;
          break;
        }
        size_t nneighbors = std::distance(graph.neighbor_begin(*ii), graph.neighbor_end(*ii));
        nedges += nneighbors;
        nnodes += 1;
        curSize += sizeof(NodeInfo) + sizeof(EdgeInfo) * nneighbors;
      }
    }

    DistributeInfo& d = *dinfo.getRemote(tid);
    d.numNodes = numNodes - runningNodes;
    d.numEdges = numEdges - runningEdges;
    d.begin = last;
    d.end = ei;
  }

  struct AllocateNodes {
    Galois::Runtime::PerThreadStorage<DistributeInfo>& dinfo;
    Galois::Runtime::PerThreadStorage<Header*>& headers;
    NodeInfo** nodes;
    FileGraph& graph;

    AllocateNodes(
        Galois::Runtime::PerThreadStorage<DistributeInfo>& d,
        Galois::Runtime::PerThreadStorage<Header*>& h, NodeInfo** n, FileGraph& g):
      dinfo(d), headers(h), nodes(n), graph(g) { }

    void operator()(unsigned int tid, unsigned int num) {
      //DistributeInfo& d = dinfo.get(tid);
      DistributeInfo& d = *dinfo.getLocal();

      // extra 2 factors are for alignment purposes
      size_t size =
          sizeof(Header) * 2 +
          sizeof(NodeInfo) * d.numNodes * 2 +
          sizeof(EdgeInfo) * d.numEdges;

      void *raw = Galois::Runtime::MM::largeAlloc(size);
      memset(raw, 0, size);

      Header*& h = *headers.getLocal();
      h = reinterpret_cast<Header*>(raw);
      h->size = size;
      h->begin = h->end = reinterpret_cast<NodeInfo*>(h + 1);

      if (!d.numNodes)
        return;

      for (FileGraph::iterator ii = d.begin, ee = d.end; ii != ee; ++ii) {
        h->end->construct();
        h->end->numEdges = std::distance(graph.neighbor_begin(*ii), graph.neighbor_end(*ii));
        nodes[*ii] = h->end;
        h->end = h->end->next();
      }
    }
  };

  struct AllocateEdges {
    Galois::Runtime::PerThreadStorage<DistributeInfo>& dinfo;
    NodeInfo** nodes;
    FileGraph& graph;

    AllocateEdges(Galois::Runtime::PerThreadStorage<DistributeInfo>& d, NodeInfo** n, FileGraph& g):
      dinfo(d), nodes(n), graph(g) { }

    //! layout the edges
    void operator()(unsigned int tid, unsigned int num) {
      typedef typename EdgeInfo::value_type EDV;
      //DistributeInfo& d = *dinfo.getRemote(tid);
      DistributeInfo& d = *dinfo.getLocal();
      if (!d.numNodes)
        return;

      for (FileGraph::iterator ii = d.begin, ee = d.end; ii != ee; ++ii) {
        EdgeInfo* edge = nodes[*ii]->edgeBegin();
        for (FileGraph::neighbor_iterator ni = graph.neighbor_begin(*ii),
               ne = graph.neighbor_end(*ii); ni != ne; ++ni) {
          if (EdgeInfo::has_value)
            edge->construct(graph.getEdgeData<EDV>(ni));
          edge->dst = nodes[*ni];
          ++edge;
        }
      }
    }
  };

public:
  typedef NodeInfo* GraphNode;
  typedef EdgeTy edge_data_type;
  typedef NodeTy node_data_type;
  typedef typename EdgeInfo::reference edge_data_reference;
  typedef typename NodeInfo::reference node_data_reference;
  typedef EdgeInfo* edge_iterator;
  typedef NodeInfo** iterator;
  typedef NodeInfo*const * const_iterator;

  class local_iterator : public std::iterator<std::forward_iterator_tag, GraphNode> {
    const Galois::Runtime::PerThreadStorage<Header*>* headers;
    unsigned int tid;
    Header* p;
    GraphNode v;

    bool init_thread() {
      p = tid < headers->size() ? *headers->getRemote(tid) : 0;
      v = p ? p->begin : 0;
      return p;
    }

    bool advance_local() {
      if (p) {
        v = v->next();
        return v != p->end;
      }
      return false;
    }

    void advance_thread() {
      while (tid < headers->size()) {
        ++tid;
        if (init_thread())
          return;
      }
    }

    void advance() {
      if (advance_local()) return;
      advance_thread();
    }

  public:
    local_iterator(): headers(0), tid(0), p(0), v(0) { }
    local_iterator(const Galois::Runtime::PerThreadStorage<Header*>* _headers, int _tid):
      headers(_headers), tid(_tid), p(0), v(0)
    {
      //find first valid item
      if (!init_thread())
        advance_thread();
    }

    //local_iterator(const iterator& it): headers(it.headers), tid(it.tid), p(it.p), v(it.v) { }
    local_iterator& operator++() { advance(); return *this; }
    local_iterator operator++(int) { local_iterator tmp(*this); operator++(); return tmp; }
    bool operator==(const local_iterator& rhs) const {
      return (headers == rhs.headers && tid == rhs.tid && p == rhs.p && v == rhs.v);
    }
    bool operator!=(const local_iterator& rhs) const {
      return !(headers == rhs.headers && tid == rhs.tid && p == rhs.p && v == rhs.v);
    }
    GraphNode operator*() const { return v; }
  };

  typedef local_iterator const_local_iterator;

  ~LC_Numa_Graph() {
    for (typename Nodes::iterator ii = nodes.begin(), ei = nodes.end(); ii != ei; ++ii) {
      NodeInfo* n = *ii;
      EdgeInfo* edgeBegin = n->edgeBegin();
      EdgeInfo* edgeEnd = n->edgeEnd();

      n->destruct();
      if (EdgeInfo::has_value) {
        while (edgeBegin != edgeEnd) {
          edgeBegin->destroy();
          ++edgeBegin;
        }
      }
    }

    for (unsigned i = 0; i < headers.size(); ++i) {
      Header* h = *headers.getRemote(i);
      if (h)
        Galois::Runtime::MM::largeFree(h, h->size);
    }
  }

  node_data_reference getData(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::checkWrite(mflag, false);
    Galois::Runtime::acquire(N, mflag);
    return N->getData();
  }
  
//  edge_data_reference getEdgeData(GraphNode src, GraphNode dst, MethodFlag mflag = MethodFlag::ALL) {
//    Galois::Runtime::checkWrite(mflag);
//    Galois::Runtime::acquire(src, mflag);
//    return getEdgeIdx(src,dst)->getData();
//  }

  edge_data_reference getEdgeData(edge_iterator ni, MethodFlag mflag = MethodFlag::NONE) const {
    Galois::Runtime::checkWrite(mflag, false);
    return ni->get();
  }

  GraphNode getEdgeDst(edge_iterator ni) const {
    return ni->dst;
  }

  uint64_t size() const { return numNodes; }
  uint64_t sizeEdges() const { return numEdges; }
  iterator begin() { return &nodes[0]; }
  iterator end() { return &nodes[numNodes]; }
  const_iterator begin() const { return &nodes[0]; }
  const_iterator end() const { return &nodes[numNodes]; }

  local_iterator local_begin() const {
    return local_iterator(&headers, Galois::Runtime::LL::getTID());
  }

  local_iterator local_end() const {
    return local_iterator(&headers, Galois::Runtime::LL::getTID() + 1);
  }

  edge_iterator edge_begin(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    if (Galois::Runtime::shouldLock(mflag)) {
      for (edge_iterator ii = N->edgeBegin(), ee = N->edgeEnd(); ii != ee; ++ii) {
        Galois::Runtime::acquire(ii->dst, mflag);
      }
    }
    return N->edgeBegin();
  }

  edge_iterator edge_end(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    Galois::Runtime::acquire(N, mflag);
    return N->edgeEnd();
  }

  EdgesIterator<LC_Numa_Graph> out_edges(GraphNode N, MethodFlag mflag = MethodFlag::ALL) {
    return EdgesIterator<LC_Numa_Graph>(*this, N, mflag);
  }

  void structureFromFile(const std::string& fname) { Graph::structureFromFile(*this, fname); }

  void structureFromGraph(FileGraph& graph) {
    numNodes = graph.size();
    numEdges = graph.sizeEdges();

    Galois::Runtime::PerThreadStorage<DistributeInfo> dinfo;
    distribute(graph, dinfo);

    nodes.allocate(numNodes);

    Galois::on_each(AllocateNodes(dinfo, headers, nodes.data(), graph));
    Galois::on_each(AllocateEdges(dinfo, nodes.data(), graph));
  }
};


}
}
#endif