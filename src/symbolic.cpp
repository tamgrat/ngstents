#include <solve.hpp>
using namespace ngsolve;

#include "tconservationlaw_tp_impl.hpp"

typedef CoefficientFunction CF;

template <int D, int COMP, int ECOMP>
class SymbolicConsLaw : public T_ConservationLaw<SymbolicConsLaw<D,COMP,ECOMP>, D, COMP, ECOMP, true>
{
  typedef T_ConservationLaw<SymbolicConsLaw<D, COMP, ECOMP>, D, COMP, ECOMP, true> BASE;

  shared_ptr<CF> cf_flux = nullptr;
  shared_ptr<CF> cf_numflux = nullptr;
  shared_ptr<CF> cf_invmap = nullptr;
  // cf's for entropy residual
  shared_ptr<CF> cf_entropy = nullptr;
  shared_ptr<CF> cf_entropyflux = nullptr;
  shared_ptr<CF> cf_numentropyflux = nullptr;
  shared_ptr<CF> cf_visccoeff = nullptr;

  // compiled differentials
  shared_ptr<CF> ddu_invmap = nullptr;
  shared_ptr<CF> ddphi_invmap = nullptr;
  shared_ptr<CF> ddu_entropy = nullptr;

  using BASE::proxy_u;
  using BASE::proxy_uother;
  using BASE::proxy_graddelta;
  using BASE::proxy_res;
  using BASE::tps;
public:
  SymbolicConsLaw (const shared_ptr<GridFunction> & agfu,
		   const shared_ptr<TentPitchedSlab> & atps,
		   const shared_ptr<ProxyFunction> & aproxy_u,
		   const shared_ptr<ProxyFunction> & aproxy_uother,
		   const shared_ptr<CF> & acf_flux,
		   const shared_ptr<CF> & acf_numflux,
		   const shared_ptr<CF> & acf_invmap,
		   const shared_ptr<CF> & acf_entropy,
		   const shared_ptr<CF> & acf_entropyflux,
		   const shared_ptr<CF> & acf_numentropyflux)
    : BASE (agfu, atps, "symbolic"),
      cf_flux{acf_flux}, cf_numflux{acf_numflux}, cf_invmap{acf_invmap},
      cf_entropy{acf_entropy}, cf_entropyflux{acf_entropyflux},
      cf_numentropyflux{acf_numentropyflux}
  {
    // set proxies
    proxy_u = aproxy_u;
    proxy_uother = aproxy_uother;

    if(cf_entropy)
      {
	// precompute derivatives for entropy residual
	ddu_invmap = cf_invmap->Diff(proxy_u.get(), proxy_uother);
	ddphi_invmap = cf_invmap->Diff(BASE::tps->cfgradphi.get(), proxy_graddelta);

	auto temp = cf_entropy - cf_entropyflux*tps->cfgradphi;
	ddu_entropy = temp->Diff(proxy_u.get(), proxy_uother);
      }
  }

  using BASE::Flux;
  using BASE::NumFlux;
  using BASE::InverseMap;

  // solve for û: Û = ĝ(x̂, t̂, û) - ∇̂ φ(x̂, t̂) ⋅ f̂(x̂, t̂, û)
  // at all points in an integration rule
  void InverseMap(const SIMD_BaseMappedIntegrationRule & mir,
		  FlatMatrix<SIMD<double>> gradphi, FlatMatrix<SIMD<double>> u) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = u;                  // set values for u
    ud.GetAMemory(BASE::tps->cfgradphi.get()) = gradphi;  // set values for grad(phi)
    cf_invmap->Evaluate(mir, u);
  }

  void InverseMap(const SIMD_BaseMappedIntegrationRule & mir,
		  FlatMatrix<SIMD<double>> gradphi,
		  FlatMatrix<SIMD<double>> graddelta,
		  FlatMatrix<SIMD<double>> u,
		  FlatMatrix<SIMD<double>> ut) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = u;                  // set values for u
    ud.GetAMemory(proxy_uother.get()) = ut;            // abuse other proxy for derivatives
    ud.GetAMemory(BASE::tps->cfgradphi.get()) = gradphi;  // set values for grad(phi)
    ud.GetAMemory(proxy_graddelta.get()) = graddelta;     // set values for graddelta

    // map derivative
    ddu_invmap->Evaluate(mir, ut);
    ddphi_invmap->Evaluate(mir, graddelta);
    ut += graddelta;
    // map function value
    cf_invmap->Evaluate(mir, u);
  }

  // flux f(u)
  void Flux (const SIMD_BaseMappedIntegrationRule & mir,
             FlatMatrix<SIMD<double>> u, FlatMatrix<SIMD<double>> flux) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = u; // set values for u
    cf_flux->Evaluate(mir, flux);
  }

  // numerical flux
  void NumFlux(const SIMD_BaseMappedIntegrationRule & mir,
	       FlatMatrix<SIMD<double>> ul, FlatMatrix<SIMD<double>> ur,
	       FlatMatrix<SIMD<double>> normals, FlatMatrix<SIMD<double>> fna) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = ul; // set values for ul
    ud.GetAMemory(proxy_uother.get()) = ur; // set values for ur
    cf_numflux->Evaluate(mir,fna);
  }

  // calc \d\hat{t}(\hat{E}) for \hat{E} = E(u) - F(u)*grad(phi) and F(u)
  void CalcEntropy (const SIMD_BaseMappedIntegrationRule & mir,
		    FlatMatrix<SIMD<double>> u, FlatMatrix<SIMD<double>> ut,
		    FlatMatrix<SIMD<double>> gradphi, FlatMatrix<SIMD<double>> graddelta,
		    FlatMatrix<SIMD<double>> dEdt, FlatMatrix<SIMD<double>> F) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = u;                  // set values for u
    ud.GetAMemory(proxy_uother.get()) = ut;            // abuse other proxy for derivatives
    ud.GetAMemory(BASE::tps->cfgradphi.get()) = gradphi;   // set values for grad(phi)
    ud.GetAMemory(proxy_graddelta.get()) = graddelta;      // set values for graddelta

    ddu_entropy->Evaluate(mir, dEdt);
    cf_entropyflux->Evaluate(mir, F);
    // add linear part to derivative
    for( size_t i : Range(dEdt.Width()))
      dEdt(0,i) -= InnerProduct(F.Col(i), graddelta.Col(i));
  }

  // numerical entropy flux
  void EntropyFlux(const SIMD_BaseMappedIntegrationRule & mir,
		   FlatMatrix<SIMD<double>> ul, FlatMatrix<SIMD<double>> ur,
		   FlatMatrix<SIMD<double>> normals, FlatMatrix<SIMD<double>> fna) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = ul;      // set values for ul
    ud.GetAMemory(proxy_uother.get()) = ur; // set values for ur
    cf_numentropyflux->Evaluate(mir,fna);
  }
  void SetViscosityCoefficient(shared_ptr<CoefficientFunction> cf_visc)
  {
    cf_visccoeff = cf_visc;
  }

  // compute the viscosity coefficient
  void CalcViscCoeffEl(const SIMD_BaseMappedIntegrationRule & mir,
                       FlatMatrix<SIMD<double>> u,
                       FlatMatrix<SIMD<double>> res,
                       const double hi, double & coeff) const
  {
    ProxyUserData & ud = *static_cast<ProxyUserData*>(mir.GetTransformation().userdata);
    ud.GetAMemory(proxy_u.get()) = u; // set values for u
    ud.GetAMemory(proxy_res.get()) = res;
    cf_visccoeff->Evaluate(mir,res);
    coeff = 0.0;
    for(size_t i : Range(res.Width()))
      for(size_t j : Range(SIMD<double>::Size()))
	if(res(0,i)[j] > coeff)
	  coeff = res(0,i)[j];
  }
};

/////////////////////////////////////////////////////////////////////////

shared_ptr<ConservationLaw> CreateSymbolicConsLaw (const shared_ptr<GridFunction> & gfu,
						   const shared_ptr<TentPitchedSlab> & tps,
						   const shared_ptr<ProxyFunction> & proxy_u,
						   const shared_ptr<ProxyFunction> & proxy_uother,
						   const shared_ptr<CF> & flux,
						   const shared_ptr<CF> & numflux,
						   const shared_ptr<CF> & invmap,
						   const shared_ptr<CF> & entropy,
						   const shared_ptr<CF> & entropyflux,
						   const shared_ptr<CF> & numentropyflux)
{
  const int dim = tps->ma->GetDimension();
  constexpr int MAXCOMP = 6;
  const int comp_space = gfu->GetFESpace()->GetDimension();
  shared_ptr<ConservationLaw> cl = nullptr;
  switch(dim){
  case 1:
    Switch<MAXCOMP>(comp_space, [&](auto COMP) {
	cl = make_shared<SymbolicConsLaw<1, COMP, 1>>(gfu, tps, proxy_u, proxy_uother,
						      flux, numflux, invmap,
						      entropy, entropyflux, numentropyflux);
      });
    break;
  case 2:
    Switch<MAXCOMP>(comp_space, [&](auto COMP) {
	cl = make_shared<SymbolicConsLaw<2, COMP, 1>>(gfu, tps, proxy_u, proxy_uother,
						      flux, numflux, invmap,
						      entropy, entropyflux, numentropyflux);
      });
    break;
  case 3:
    Switch<MAXCOMP>(comp_space, [&](auto COMP) {
	cl = make_shared<SymbolicConsLaw<3, COMP, 1>>(gfu, tps, proxy_u, proxy_uother,
						      flux, numflux, invmap,
						      entropy, entropyflux, numentropyflux);
      });
    break;
  }
  if(cl)
    return cl;
  else
    throw Exception ("Illegal dimension for SymbolicConsLaw");
}
