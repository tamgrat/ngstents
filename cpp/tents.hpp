#ifndef TENTSHEADER
#define TENTSHEADER

#include <solve.hpp>
using namespace ngsolve;
using namespace std;

// A spacetime tent is a macroelement consisting of a tentpole erected at
// a central vertex in space and all the space-time tetrahedra with
// the tentpole as an edge.

// We represent the tent by its projection on space (a vertex patch),
// the central vertex, and the heights (times) of its neighboring
// vertices.

////////////////////////////////////////////////////////////////////////////

// Class to describe one spacetime tent in a mesh of tents

class Tent {

public:

  int vertex;                 // central vertex
  double tbot, ttop;          // bottom and top times of central vertex
  Array<int> nbv;             // neighbour vertices
  Array<double> nbtime;       // height/time of neighbouring vertices
  Array<int> els;             // all elements in the tent's vertex patch
  Array<int> internal_facets; // all internal facets in the tent's vertex patch
  Table<int> elfnums;         /* elfnums[k] lists all internal facets of
				 the k-th element of tent */

  // Tent top and bottom are graphs of phi_top, phi_bot, which are
  // p.w.linear functions on non-curved elements (with p.w.constant gradients).
  Array<Vector<>> gradphi_bot; // gradphi_bot[l], gradphi_top[l] =
  Array<Vector<>> gradphi_top; /* gradients of phi_bot/top at some point in the
				  l-th simplex of the tent */

  // access to global periodicity identifications
  static Array<int> vmap;      // vertex map for periodic spaces

  // access to the finite element & dofs
  mutable class TentDataFE * fedata = nullptr;

  // other global details from a mesh of tents
  int level;                   // parallel layer number
  Array<int> dependent_tents;  // these tents depend on me

  double MaxSlope() const;
};

ostream & operator<< (ostream & ost, const Tent & tent);


////////////////////////////////////////////////////////////////////////////

// Class with dofs, finite element & integration info for a tent:

class TentDataFE
{
public:
  // moved from Tent
  int nd;            // total # interior and interface dofs in space
  Array<int> dofs;   // all interior and interface dof nums, size(dofs)=nd.
  // ranges[k] IntRange (of dof numbers) of k-th element of local matrix
  Array<IntRange> ranges;

  // finite elements for all elements in the tent
  Array<FiniteElement*> fei;
  // integration rules for all elements in the tent
  Array<SIMD_IntegrationRule*> iri;
  // mapped integration rules for all elements in the tent
  Array<SIMD_BaseMappedIntegrationRule*> miri;
  // element transformations for all elements in the tent
  Array<ElementTransformation*> trafoi;
  // mesh size for each element
  Array<double> mesh_size;
  // gradients of tent bottom at integration points (in possibly curved elements)
  Array<FlatMatrix<SIMD<double>>> agradphi_bot;
  // gradient of (tent top) the new advancing front in the IP's
  Array<FlatMatrix<SIMD<double>>> agradphi_top;
  // height of the tent in the IP's
  Array<FlatVector<SIMD<double>>> adelta;
  // local numbers of the neighbors
  Array<INT<2,size_t>> felpos;
  // facet integration rules for all facets in the tent
  // transformed to local coordinated of the neighboring elements
  Array<Vec<2,const SIMD_IntegrationRule*>> firi;
  // mapped facet integration rules for all facets
  Array<SIMD_BaseMappedIntegrationRule*> mfiri1;
  // mapped facet integration rules for all facets
  Array<SIMD_BaseMappedIntegrationRule*> mfiri2;
  // gradient phi face first and second element
  Array<FlatMatrix<SIMD<double>>> agradphi_botf1;
  Array<FlatMatrix<SIMD<double>>> agradphi_topf1;
  Array<FlatMatrix<SIMD<double>>> agradphi_botf2;
  Array<FlatMatrix<SIMD<double>>> agradphi_topf2;
  // normal vectors in the IP's
  Array<FlatMatrix<SIMD<double>>> anormals;
  // height of the tent in the IP's
  Array<FlatVector<SIMD<double>>> adelta_facet;

  TentDataFE(int n, LocalHeap & lh)
    : fei(n, lh), iri(n, lh), miri(n, lh), trafoi(n, lh) { ; }

  TentDataFE(const Tent & tent, const FESpace & fes,
	     const MeshAccess & ma, LocalHeap & lh);
};


////////////////////////////////////////////////////////////////////////////

template <int DIM>
class TentPitchedSlab {
public:
  enum PitchingMethod {EVolGrad = 1, EEdgeGrad};
  Array<Tent*> tents;         // tents between two time slices
  double dt;                  // time step between two time slices
  LocalHeap lh;
  PitchingMethod method;

public:
  // access to base spatial mesh (public for export to Python visualization)
  shared_ptr<MeshAccess> ma;
  // Constructor and initializers
  TentPitchedSlab(shared_ptr<MeshAccess> ama, int heapsize) :
    dt(0), ma(ama), lh(heapsize, "Tents heap") { ; };
  void PitchTents(double dt, double cmax);
  void PitchTents(double dt, shared_ptr<CoefficientFunction> cmax);
  
  //uses the exact calculation of the gradient for pitching the tent
  void PitchTentsGradient(double dt, double cmax);
  void PitchTentsGradient(double dt, shared_ptr<CoefficientFunction> cmax);
  // Get object features
  int GetNTents() { return tents.Size(); }
  double GetSlabHeight() { return dt; }
  const Tent & GetTent(int i) { return *tents[i];}

  // Return  max(|| gradphi_top||, ||gradphi_bot||)
  double MaxSlope() const;

  // Drawing
  void DrawPitchedTents(int level=1) ;
  void DrawPitchedTentsVTK(string vtkfilename);
  void DrawPitchedTentsGL(Array<int> & tentdata,
                          Array<double> & tenttimes, int & nlevels);

  void SetPitchingMethod(PitchingMethod amethod) {this->method = amethod;}

  // Propagate methods need to access this somehow
  Table<int> tent_dependency; // DAG of tent dependencies
};

//Abstract class with the interface of methods used for pitching a tent
class TentSlabPitcher{
protected:
  //access to base spatial mesh
  shared_ptr<MeshAccess> ma;
  //element-wise maximal wave-speeds
  Array<double> cmax;
  //reference heights for each vertex
  Array<double> vertex_refdt;
public:
  //constructor
  TentSlabPitcher(shared_ptr<MeshAccess> ama);
  //destructor
  virtual ~TentSlabPitcher(){;}
  //calculates the wavespeed for each element. on the child class it might do something else as well, hence it's virtual
  virtual void InitializeMeshData(LocalHeap &lh, BitArray &fine_edges,
                                  shared_ptr<CoefficientFunction> wavespeed ) = 0;

  //compute the vertex based max time-differences assumint tau=0
  //corresponding to a non-periodic vertex
  void ComputeVerticesReferenceHeight(const Table<int> &v2v, const Table<int> &v2e, const Array<double> &tau,
                                      LocalHeap &lh);

  void UpdateNeighbours(const int vi, const double adv_factor, const Table<int> &v2v,const Table<int> &v2e,
                        const Array<double> &tau, const Array<bool> &complete_vertices,
                        Array<double> &ktilde, Array<bool> &vertex_ready,
                        Array<int> &ready_vertices, LocalHeap &lh);
  
  //it does NOT compute, only returns a copy of vertex_refdt
  Array<double> GetVerticesReferenceHeight(){ return Array<double>(vertex_refdt);}

  //Populate the set of ready vertices with vertices satisfying ktilde > adv_factor * refdt. returns false if
  //no such vertex was found
  [[nodiscard]] bool GetReadyVertices(double &adv_factor, bool reset_adv_factor,const Array<double> &ktilde,
                                      Array<bool> &vertex_ready, Array<int> &ready_vertices);

  //Given the current advancing (time) front, calculates the
  //maximum advance on a tent centered on vi that will still
  //guarantee causality
  virtual double GetPoleHeight(const int vi, const Array<double> & tau, const Array<double> & cmax, FlatArray<int> nbv, FlatArray<int> nbe, LocalHeap & lh) const = 0;

  //Returns the position in ready_vertices containing the vertex in which a tent will be pitched (and its level)
  [[nodiscard]] std::tuple<int,int> PickNextVertexForPitching(const Array<int> &ready_vertices, const Array<double> &ktilde, const Array<int> &vertices_level);
};

template <int DIM>
class VolumeGradientPitcher : public TentSlabPitcher{
public:
  
  VolumeGradientPitcher(shared_ptr<MeshAccess> ama) : TentSlabPitcher(ama){;}
  
  void InitializeMeshData(LocalHeap &lh, BitArray &fine_edges, shared_ptr<CoefficientFunction> wavespeed) override;

  double GetPoleHeight(const int vi, const Array<double> & tau, const Array<double> & cmax, FlatArray<int> nbv,
                       FlatArray<int> nbe, LocalHeap & lh) const override;
};

template <int DIM>
class EdgeGradientPitcher : public TentSlabPitcher{
  Array<double> edge_refdt;
public:
  
  EdgeGradientPitcher(shared_ptr<MeshAccess> ama) : TentSlabPitcher(ama), edge_refdt(ama->GetNEdges()) {;}

  void InitializeMeshData(LocalHeap &lh, BitArray &fine_edges, shared_ptr<CoefficientFunction> wavespeed) override;

  double GetPoleHeight(const int vi, const Array<double> & tau, const Array<double> & cmax, FlatArray<int> nbv,
                       FlatArray<int> nbe, LocalHeap & lh) const override;
};
#endif
