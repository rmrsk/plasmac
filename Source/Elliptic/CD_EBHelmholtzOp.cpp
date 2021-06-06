/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*
  @file   CD_EBHelmholtzOp.cpp
  @brief  Implementation of CD_EBHelmholtzOp.H
  @author Robert Marskar
  @todo   Replace EBAMRPoissonOp::staticMaxNorm and don't use EBAMRPoissonOp dependencies
  @todo   Define prolongation/restriction objects. They're undefined. 
*/

// Chombo includes
#include <EBLevelDataOps.H>
#include <EBCellFactory.H>
#include <EBAMRPoissonOp.H>

// Our includes
#include <CD_EBHelmholtzOp.H>
#include <CD_EBHelmholtzOpF_F.H>
#include <CD_NamespaceHeader.H>


EBHelmholtzOp::EBHelmholtzOp(const EBLevelGrid &                                  a_eblgFine,
			     const EBLevelGrid &                                  a_eblg,
			     const EBLevelGrid &                                  a_eblgCoar,
			     const EBLevelGrid &                                  a_eblgCoarMG,
			     const RefCountedPtr<EBMultigridInterpolator>&        a_interpolator,
			     const RefCountedPtr<EBFluxRegister>&                 a_fluxReg,
			     const RefCountedPtr<EbCoarAve>&                      a_coarAve,			       
			     const RefCountedPtr<EBHelmholtzDomainBc>&            a_domainBc,
			     const RefCountedPtr<EBHelmholtzEbBc>&                a_ebBc,
			     const Real    &                                      a_dx,
			     const Real    &                                      a_dxCoar,
			     const int&                                           a_refToFine,
			     const int&                                           a_refToCoar,
			     const bool&                                          a_hasFine,
			     const bool&                                          a_hasCoar,
			     const bool&                                          a_hasMGObjects,
			     const Real&                                          a_alpha,
			     const Real&                                          a_beta,
			     const RefCountedPtr<LevelData<EBCellFAB> >&          a_Acoef,
			     const RefCountedPtr<LevelData<EBFluxFAB> >&          a_Bcoef,
			     const RefCountedPtr<LevelData<BaseIVFAB<Real> > >&   a_BcoefIrreg,
			     const IntVect&                                       a_ghostPhi,
			     const IntVect&                                       a_ghostRhs,
			     const RelaxationMethod&                              a_relaxationMethod) :
  LevelTGAHelmOp<LevelData<EBCellFAB>, EBFluxFAB>(false), // Time-independent
  m_eblgFine(),
  m_eblg(a_eblg),
  m_eblgCoar(),
  m_eblgCoarMG(),
  m_interpolator(a_interpolator),
  m_fluxReg(a_fluxReg),
  m_coarAve(a_coarAve),
  m_domainBc(a_domainBc),
  m_ebBc(a_ebBc),
  m_dx(a_dx),
  m_dxCoar(a_dxCoar),
  m_refToFine(a_hasFine ? a_refToFine : 1),
  m_refToCoar(a_hasCoar ? a_refToCoar : 1),
  m_hasFine(a_hasFine),
  m_hasCoar(a_hasCoar),
  m_hasMGObjects(a_hasMGObjects),
  m_alpha(a_alpha),
  m_beta(a_beta),
  m_Acoef(a_Acoef),
  m_Bcoef(a_Bcoef),
  m_BcoefIrreg(a_BcoefIrreg),
  m_ghostPhi(a_ghostPhi),
  m_ghostRhs(a_ghostRhs),
  m_relaxationMethod(a_relaxationMethod) {

  // Do not touch these. 
  m_nComp      = 1;
  m_comp       = 0;
  m_turnOffBCs = false;


  if(m_hasFine){
    m_eblgFine = a_eblgFine;
    m_dxFine   = m_dx/a_refToFine;
  }

  if(m_hasCoar){
    m_eblgCoar = a_eblgCoar;
    m_dxCoar   = m_dx*a_refToCoar;

    // Define interpolation objects. Need to to think about this one because EBConductivityOp is a bit anal about the way it does this. 
  }
  
  if(m_hasMGObjects){
    constexpr int mgRef = 2;
    
    m_eblgCoarMG = a_eblgCoarMG;

    m_ebAverageMG.define(m_eblg.getDBL(),
			 m_eblgCoarMG.getDBL(),
			 m_eblg.getEBISL(),
			 m_eblgCoarMG.getEBISL(),
			 m_eblgCoarMG.getDomain(),
			 mgRef,
			 m_nComp,
			 m_eblg.getEBIS(),
			 m_ghostPhi);
  }
}

EBHelmholtzOp::~EBHelmholtzOp(){

}

void EBHelmholtzOp::defineStencils(){
  MayDay::Warning("EBHelmholtzOp::defineStencils - not implemented");
}

unsigned int EBHelmholtzOp::orderOfAccuracy(void) const {
  return 99;
}

void EBHelmholtzOp::enforceCFConsistency(LevelData<EBCellFAB>& a_coarCorr, const LevelData<EBCellFAB>& a_fineCorr){
  m_coarAve->average(a_coarCorr, a_fineCorr, a_coarCorr.interval());
}

void EBHelmholtzOp::setAlphaAndBeta(const Real& a_alpha, const Real& a_beta) {
  m_alpha = a_alpha;
  m_beta  = a_beta;

  // When we change alpha and beta we need to recompute relaxation coefficients...
  this->calculateAlphaWeight(); 
  this->calculateRelaxationCoefficient();
}

void EBHelmholtzOp::residual(LevelData<EBCellFAB>& a_residual, const LevelData<EBCellFAB>& a_phi, const LevelData<EBCellFAB>& a_rhs, bool a_homogeneousPhysBC) {
  CH_assert(m_hasCoar = false);
  
  this->applyOp(a_residual, a_phi, NULL, a_homogeneousPhysBC, true); // Only homogeneous CFBC. This shouldn't break because we shouldn't have a coar level.
  this->axby(a_residual, a_residual, a_rhs, -1.0, 1.0);              // residual = rhs - L(phi). 
}

void EBHelmholtzOp::preCond(LevelData<EBCellFAB>& a_corr, const LevelData<EBCellFAB>& a_residual) {
  EBLevelDataOps::assign(a_corr, a_residual);
  EBLevelDataOps::scale(a_corr,  m_relCoef);

  this->relax(a_corr, a_residual, 25);
}

void EBHelmholtzOp::applyOp(LevelData<EBCellFAB>& a_Lphi, const LevelData<EBCellFAB>& a_phi, bool a_homogeneousPhysBC) {
  this->applyOp(a_Lphi, a_phi, NULL, a_homogeneousPhysBC, true);
}

void EBHelmholtzOp::create(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs) {
  EBCellFactory fact(m_eblg.getEBISL());
  a_lhs.define(m_eblg.getDBL(), a_rhs.nComp(), a_rhs.ghostVect(), fact);
}

void EBHelmholtzOp::assign(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs) {
  a_rhs.localCopyTo(a_lhs);
}

Real EBHelmholtzOp::dotProduct(const LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs) {
  ProblemDomain domain;
  Real volume;

  return EBLevelDataOps::kappaDotProduct(volume, a_lhs, a_rhs, EBLEVELDATAOPS_ALLVOFS, domain);
}

void EBHelmholtzOp::incr(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs, const Real a_scale) {
  EBLevelDataOps::incr(a_lhs, a_rhs, a_scale);
}

void EBHelmholtzOp::axby(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_x, const LevelData<EBCellFAB>& a_y, const Real a_a, const Real a_b) {
  EBLevelDataOps::axby(a_lhs,a_x,a_y,a_a,a_b);
}

void EBHelmholtzOp::scale(LevelData<EBCellFAB>& a_lhs, const Real& a_scale) {
  EBLevelDataOps::scale(a_lhs, a_scale);
}

Real EBHelmholtzOp::norm(const LevelData<EBCellFAB>& a_rhs, const int a_order) {
  Real maxNorm = EBAMRPoissonOp::staticMaxNorm(a_rhs, m_eblg);

#ifdef CH_MPI
  Real tmp = 1;
  MPI_Allreduce(&maxNorm, &tmp, 1, MPI_CH_REAL, MPI_MAX, Chombo_MPI::comm);
  maxNorm = tmp;
#endif

  return maxNorm;
}

void EBHelmholtzOp::setToZero(LevelData<EBCellFAB>& a_lhs) {
  EBLevelDataOps::setToZero(a_lhs);
}

void EBHelmholtzOp::createCoarser(LevelData<EBCellFAB>& a_coarse, const LevelData<EBCellFAB>& a_fine, bool a_ghosted) {
  const DisjointBoxLayout& dbl = m_eblgCoarMG.getDBL();
  MayDay::Warning("EBHelmholtzOp::createCoarser - not implemented");
}

void EBHelmholtzOp::createCoarsened(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs, const int& a_refRat) {

  DisjointBoxLayout dblCoFi;
  EBISLayout        ebislCoFi;
  ProblemDomain     domainCoFi;

  // Make grids and EBISL
  coarsen(dblCoFi, m_eblg.getDBL(), a_refRat);
  domainCoFi = coarsen(m_eblg.getDomain(), a_refRat);
  m_eblg.getEBIS()->fillEBISLayout(ebislCoFi, dblCoFi, domainCoFi, a_rhs.ghostVect()[0]);

  if(m_refToCoar > 1){
    ebislCoFi.setMaxRefinementRatio(m_refToCoar, m_eblg.getEBIS());
  }

  EBCellFactory factCoFi(ebislCoFi);
  a_lhs.define(dblCoFi, a_rhs.nComp(), a_rhs.ghostVect(), factCoFi);
  
}

void EBHelmholtzOp::relax(LevelData<EBCellFAB>& a_correction, const LevelData<EBCellFAB>& a_residual, int a_iterations){
  switch(m_relaxationMethod){
  case RelaxationMethod::Jacobi:
    this->relaxJacobi(a_correction, a_residual, a_iterations);
    break;
  case RelaxationMethod::GSRB:
    this->relaxGauSai(a_correction, a_residual, a_iterations);
    break;
  case RelaxationMethod::GSRBFast:
    this->relaxGSRBFast(a_correction, a_residual, a_iterations);
    break;
  default:
    MayDay::Abort("EBHelmholtzOp::relax - bogus relaxation method requested");
  };
}

void EBHelmholtzOp::restrictResidual(LevelData<EBCellFAB>& a_resCoar, LevelData<EBCellFAB>& a_phi, const LevelData<EBCellFAB>& a_rhs) {

  // Compute the residual on this level first. Make a temporary for that.
  LevelData<EBCellFAB> res;
  this->create(res, a_phi);
  this->residual(res, a_phi, a_rhs, true);

  m_ebAverageMG.average(a_resCoar, res, a_resCoar.interval());
}

void EBHelmholtzOp::prolongIncrement(LevelData<EBCellFAB>& a_phi, const LevelData<EBCellFAB>& a_correctCoarse) {
  m_ebInterpMG.pwcInterp(a_phi, a_correctCoarse, a_phi.interval());
}

int EBHelmholtzOp::refToCoarser() {
  return m_refToCoar;
}

void EBHelmholtzOp::AMROperator(LevelData<EBCellFAB>&              a_Lphi,
				const LevelData<EBCellFAB>&        a_phiFine,
				const LevelData<EBCellFAB>&        a_phi,
				const LevelData<EBCellFAB>&        a_phiCoar,
				const bool                         a_homogeneousPhysBC,
				AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp){
  MayDay::Warning("EBHelmholtz::AMROperator - not implemented");
}

void EBHelmholtzOp::AMROperatorNF(LevelData<EBCellFAB>&       a_Lphi,
				  const LevelData<EBCellFAB>& a_phi,
				  const LevelData<EBCellFAB>& a_phiCoar,
				  bool                        a_homogeneousPhysBC) {
  this->applyOp(a_Lphi, a_phi, &a_phiCoar, a_homogeneousPhysBC, false);
}

void EBHelmholtzOp::AMROperatorNC(LevelData<EBCellFAB>&              a_Lphi,
				  const LevelData<EBCellFAB>&        a_phiFine,
				  const LevelData<EBCellFAB>&        a_phi,
				  bool                               a_homogeneousPhysBC,
				  AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp) {
  LevelData<EBCellFAB> phiCoar; // Should be safe on the bottom AMR level because only multigrid levels exist below. 
  this->AMROperator(a_Lphi, a_phiFine, a_phi, phiCoar, a_homogeneousPhysBC, a_finerOp);
}

void EBHelmholtzOp::AMRResidual(LevelData<EBCellFAB>&              a_residual,
				const LevelData<EBCellFAB>&        a_phiFine,
				const LevelData<EBCellFAB>&        a_phi,
				const LevelData<EBCellFAB>&        a_phiCoar,
				const LevelData<EBCellFAB>&        a_rhs,
				bool                               a_homogeneousPhysBC,
				AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp) {
  this->AMROperator(a_residual, a_phiFine, a_phi, a_phiCoar, a_homogeneousPhysBC, a_finerOp); // Compute L(phi) on this level.
  this->axby(a_residual, a_residual, a_rhs, -1., 1.);
}

void EBHelmholtzOp::AMRResidualNF(LevelData<EBCellFAB>&              a_residual,
				  const LevelData<EBCellFAB>&        a_phi,
				  const LevelData<EBCellFAB>&        a_phiCoar,
				  const LevelData<EBCellFAB>&        a_rhs,
				  bool                               a_homogeneousPhysBC) {

  // Simple, because we don't need to reflux. 
  this->AMROperatorNF(a_residual, a_phi, a_phiCoar, a_homogeneousPhysBC);
  this->axby(a_residual, a_residual, a_rhs, -1., 1.);
}

void EBHelmholtzOp::AMRResidualNC(LevelData<EBCellFAB>&              a_residual,
				  const LevelData<EBCellFAB>&        a_phiFine,
				  const LevelData<EBCellFAB>&        a_phi,
				  const LevelData<EBCellFAB>&        a_rhs,
				  bool                               a_homogeneousPhysBC,
				  AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp) {
  this->AMROperatorNC(a_residual, a_phiFine, a_phi, a_homogeneousPhysBC, a_finerOp);
  this->axby(a_residual, a_residual, a_rhs, -1., 1.);
}

void EBHelmholtzOp::AMRRestrict(LevelData<EBCellFAB>&       a_residualCoarse,
				const LevelData<EBCellFAB>& a_residual,
				const LevelData<EBCellFAB>& a_correction,
				const LevelData<EBCellFAB>& a_coarseCorrection,
				bool                        a_skip_res) {

  LevelData<EBCellFAB> resThisLevel;
  this->create(resThisLevel, a_residual);
  this->setToZero(resThisLevel);
  
  // We should average a_residual - L(correction, coarCorrection).
  this->applyOp(resThisLevel, a_correction, &a_coarseCorrection, true, false);
  this->incr(resThisLevel, a_residual, -1.0);
  this->scale(resThisLevel, -1.0);

  m_ebAverage.average(a_residualCoarse, resThisLevel, a_residualCoarse.interval());
}

void EBHelmholtzOp::AMRProlong(LevelData<EBCellFAB>& a_correction, const LevelData<EBCellFAB>& a_coarseCorrection) {
  m_ebInterp.pwcInterp(a_correction, a_coarseCorrection, a_correction.interval());
}

void EBHelmholtzOp::AMRUpdateResidual(LevelData<EBCellFAB>&       a_residual,
				      const LevelData<EBCellFAB>& a_correction,
				      const LevelData<EBCellFAB>& a_coarseCorrection) {
  LevelData<EBCellFAB> lcorr;
  this->create(lcorr, a_correction);
  this->applyOp(lcorr, a_correction, &a_coarseCorrection, true, false);
  this->incr(a_residual, a_correction, -1.0);
}

void EBHelmholtzOp::applyOp(LevelData<EBCellFAB>&             a_Lphi,
			    const LevelData<EBCellFAB>&       a_phi,
			    const LevelData<EBCellFAB>* const a_phiCoar,
			    const bool                        a_homogeneousPhysBC,
			    const bool                        a_homogeneousCFBC){
  MayDay::Warning("EBHelmholtzOp::applyOp(big) - not implemented");
}

void EBHelmholtzOp::divideByIdentityCoef(LevelData<EBCellFAB>& a_rhs) {
  for (DataIterator dit(a_rhs.disjointBoxLayout()); dit.ok(); ++dit){
    a_rhs[dit()] /= (*m_Acoef)[dit()];
  }
}

void EBHelmholtzOp::applyOpNoBoundary(LevelData<EBCellFAB>& a_Lphi, const LevelData<EBCellFAB>& a_phi) {
  m_turnOffBCs = true;
  this->applyOp(a_Lphi, a_phi, true);
  m_turnOffBCs = false;
}

void EBHelmholtzOp::fillGrad(const LevelData<EBCellFAB>& a_phi){
}

void EBHelmholtzOp::getFlux(EBFluxFAB&                  a_flux,
			    const LevelData<EBCellFAB>& a_data,
			    const Box&                  a_grid,
			    const DataIndex&            a_dit,
			    Real                        a_scale) {
  MayDay::Warning("EBHelmholtzOp::getFlux - not implemented (yet)");
}



void EBHelmholtzOp::homogeneousCFInterp(LevelData<EBCellFAB>& a_phi){
  if(m_hasCoar) m_interpolator->coarseFineInterpH(a_phi, a_phi.interval());
}

void EBHelmholtzOp::inhomogeneousCFInterp(LevelData<EBCellFAB>& a_phiFine, const LevelData<EBCellFAB>& a_phiCoar){
  if(m_hasCoar) m_interpolator->coarseFineInterp(a_phiFine, a_phiCoar, a_phiFine.interval());
}

void EBHelmholtzOp::interpolateCF(LevelData<EBCellFAB>& a_phiFine, const LevelData<EBCellFAB>* a_phiCoar, const bool a_homogeneous){
  if(m_hasCoar){
    if(a_homogeneous){
      this->homogeneousCFInterp(a_phiFine);
    }
    else{
      if(a_phiCoar == NULL){
	MayDay::Error("EBHelmholtzOp::interpolateCF - trying inhomogeneous interpolation but phiCoar is NULL");
      }
      else{
	this->inhomogeneousCFInterp(a_phiFine, *a_phiCoar);
      }
    }
  }
}

void EBHelmholtzOp::relaxJacobi(LevelData<EBCellFAB>& a_correction, const LevelData<EBCellFAB>& a_residual, const int a_iterations){
  LevelData<EBCellFAB> Lcorr;
  this->create(Lcorr, a_residual);

  for (int iter = 0; iter < a_iterations; iter++){
    this->homogeneousCFInterp(a_correction);
    this->applyOp(Lcorr, a_correction, true);

    for (DataIterator dit(m_eblg.getDBL()); dit.ok(); ++dit){
      Lcorr[dit()]        -= a_residual[dit()];
      Lcorr[dit()]        *= m_relCoef[dit()];
      Lcorr[dit()]        *= 0.5; // Safety factor for Jacobi        
      
      a_correction[dit()] -= Lcorr[dit()]; 
    }
  }
}

void EBHelmholtzOp::relaxGauSai(LevelData<EBCellFAB>& a_correction, const LevelData<EBCellFAB>& a_residual, const int a_iterations){
  MayDay::Warning("EBHelmholtzOp::relaxGauSai - not implemented");
}

void EBHelmholtzOp::relaxGSRBFast(LevelData<EBCellFAB>& a_correction, const LevelData<EBCellFAB>& a_residual, const int a_iterations){
  MayDay::Warning("EBHelmholtzOp::relaxGauSai - not implemented");
}

void EBHelmholtzOp::calculateAlphaWeight(){
  for (DataIterator dit(m_eblg.getDBL().dataIterator()); dit.ok(); ++dit){
    VoFIterator& vofit = m_vofIterIrreg[dit()];
    for (vofit.reset(); vofit.ok(); ++vofit){
      const VolIndex& vof = vofit();

      const Real volFrac = m_eblg.getEBISL()[dit()].volFrac(vof);
      const Real Aco     = (*m_Acoef)[dit()](vof, m_comp);

      m_alphaDiagWeight[dit()](vof, m_comp) = volFrac*Aco;
    }
  }
}

void EBHelmholtzOp::calculateRelaxationCoefficient(){
  for (DataIterator dit(m_eblg.getDBL()); dit.ok(); ++dit){
    const Box cellBox = m_eblg.getDBL()[dit()];

    // Set relaxation coefficient = aco*alpha
    m_relCoef[dit()].setVal(0.0);
    m_relCoef[dit()].plus((*m_Acoef)[dit()], m_comp, m_comp, m_nComp);
    m_relCoef[dit()] *= m_alpha;

    // Add in the diagonal term for the variable-coefficient Laplacian operator
    BaseFab<Real>& regRel = m_relCoef[dit()].getSingleValuedFAB();
    for (int dir = 0; dir < SpaceDim; dir++){

      // This adds -(beta*bcoef(loFace) + beta*bcoef(hiFace))/dx^2 to the relaxation term.
      BaseFab<Real>& regBcoDir = (*m_Bcoef)[dit()][dir].getSingleValuedFAB();
      FORT_ADDBCOTERMTOINVRELCOEF(CHF_FRA1(regRel, m_comp),
				  CHF_CONST_FRA1(regBcoDir, m_comp),
				  CHF_CONST_REAL(m_beta),
				  CHF_CONST_REAL(m_dx),
				  CHF_CONST_INT(dir),
				  CHF_BOX(cellBox));
    }

    // Invert the relaxation coefficient (in the irregular cells)
    FORT_INVERTRELAXATIONCOEFFICIENT(CHF_FRA1(regRel, m_comp),
				     CHF_BOX(cellBox));

    // Do the same for the irregular cells.
    VoFIterator& vofit = m_vofIterIrreg[dit()];
    for (vofit.reset(); vofit.ok(); ++vofit){
      const VolIndex& vof = vofit();

      const Real alphaWeight = m_alpha * m_alphaDiagWeight[dit()](vof, m_comp);
      const Real  betaWeight = m_beta  * m_betaDiagWeight [dit()](vof, m_comp);

      m_relCoef[dit()](vof, m_comp) = 1./(alphaWeight + betaWeight);
    }
  }
}

VoFStencil EBHelmholtzOp::getFaceCenterFluxStencil(const FaceIndex& a_face, const DataIndex& a_dit) const {
  VoFStencil fluxStencil;
  
  if(!a_face.isBoundary()){ // BC handles the boundary fluxes. 
    fluxStencil.add(a_face.getVoF(Side::Hi),  1.0/m_dx);
    fluxStencil.add(a_face.getVoF(Side::Lo), -1.0/m_dx);
    fluxStencil *= (*m_Bcoef)[a_dit][a_face.direction()](a_face, m_comp);
  }

  return fluxStencil;
}

VoFStencil EBHelmholtzOp::getFaceCentroidFluxStencil(const FaceIndex& a_face, const DataIndex& a_dit) const {
  VoFStencil fluxStencil;
  
  const FaceStencil interpolationStencil = EBArith::getInterpStencil(a_face, IntVectSet(), m_eblg.getEBISL()[a_dit], m_eblg.getDomain());

  for (int i = 0; i < interpolationStencil.size(); i++){
    const FaceIndex& iface = interpolationStencil.face(i);
    const Real& iweight    = interpolationStencil.weight(i);

    VoFStencil fluxCenterStencil = this->getFaceCenterFluxStencil(iface, a_dit);
    fluxCenterStencil *= iweight;

    fluxStencil += fluxCenterStencil;
  }

  return fluxStencil;
}

VoFStencil EBHelmholtzOp::getDivFStencil(const VolIndex& a_vof, const DataIndex& a_dit) const {
  VoFStencil divStencil;

  const EBISBox& ebisbox = m_eblg.getEBISL()[a_dit];

  for (int dir = 0; dir < SpaceDim; dir++){
    for (SideIterator sit; sit.ok(); ++sit){
      const int isign = sign(sit());
      
      Vector<FaceIndex> faces = ebisbox.getFaces(a_vof, dir, sit());
      for (auto f : faces.stdVector()){
	VoFStencil centroidFluxStencil = this->getFaceCentroidFluxStencil(f, a_dit);
	centroidFluxStencil *= sign(sit())*ebisbox.areaFrac(f)/m_dx;

	divStencil += centroidFluxStencil;
      }
    }
  }

  return divStencil;
}


#include <CD_NamespaceFooter.H>
