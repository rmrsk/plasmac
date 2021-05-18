/* chombo-discharge
 * This is a copy of Chombos EBConductivityOp and has Chombo copyright. 
 * Please refer to Copyright.txt in the Chombo root directory.
 */

/*!
  @file   CD_EbHelmholtzOp.cpp
  @brief  Implementation of CD_EbHelmholtzOp.H
  @author Robert Marskar
*/

// Chombo includes
#include <LoadBalance.H>
#include <EBArith.H>
#include <EBAMRPoissonOp.H>
#include <EBQuadCFInterp.H>
#include <EBConductivityOpF_F.H>
#include <InterpF_F.H>
#include <EBAMRPoissonOpF_F.H>
#include <BCFunc.H>
#include <CH_Timer.H>
#include <BCFunc.H>
#include <EBLevelGrid.H>
#include <EBAMRPoissonOp.H>
#include <EBAMRPoissonOpF_F.H>
#include <EBAlias.H>
#include <EBCoarseAverage.H>
#include <ParmParse.H>
#include <CH_OpenMP.H>

#define verb 0

// Our includes
#include <CD_EbFastFluxRegister.H>
#include <CD_EbHelmholtzOp.H>
#include <CD_NamespaceHeader.H>
  
//IntVect EbHelmholtzOp::s_ivDebug = IntVect(D_DECL(111, 124, 3));
bool EbHelmholtzOp::s_turnOffBCs       = false; //REALLY needs to default to false
bool EbHelmholtzOp::s_forceNoEBCF      = false; //REALLY needs to default to false
bool EbHelmholtzOp::s_areaFracWeighted = false; // Precondition the system with area fractions

EbHelmholtzOp::EbHelmholtzOp(const EBLevelGrid &                                  a_eblgFine,
			     const EBLevelGrid &                                  a_eblg,
			     const EBLevelGrid &                                  a_eblgCoar,
			     const EBLevelGrid &                                  a_eblgCoarMG,
			     const RefCountedPtr<EBQuadCFInterp>&                 a_quadCFI,
			     const RefCountedPtr<EBFluxRegister>&                 a_fastFR,
			     const RefCountedPtr<ConductivityBaseDomainBC>&       a_domainBC,
			     const RefCountedPtr<ConductivityBaseEBBC>&           a_ebBC,
			     const Real    &                                      a_dx,
			     const Real    &                                      a_dxCoar,
			     const int&                                           a_refToFine,
			     const int&                                           a_refToCoar,
			     const bool&                                          a_hasFine,
			     const bool&                                          a_hasCoar,
			     const bool&                                          a_hasMGObjects,
			     const bool&                                          a_layoutChanged,
			     const Real&                                          a_alpha,
			     const Real&                                          a_beta,
			     const RefCountedPtr<LevelData<EBCellFAB> >&          a_acoef,
			     const RefCountedPtr<LevelData<EBFluxFAB> >&          a_bcoef,
			     const RefCountedPtr<LevelData<BaseIVFAB<Real> > >&   a_bcoIrreg,
			     const IntVect&                                       a_ghostCellsPhi,
			     const IntVect&                                       a_ghostCellsRHS,
			     const int&                                           a_relaxType)
: LevelTGAHelmOp<LevelData<EBCellFAB>, EBFluxFAB>(false), // is time-independent
  m_relaxType(a_relaxType),
  m_ghostCellsPhi(a_ghostCellsPhi),
  m_ghostCellsRHS(a_ghostCellsRHS),
  m_quadCFIWithCoar(a_quadCFI),
  m_eblg(a_eblg),
  m_eblgFine(),
  m_eblgCoar(),
  m_eblgCoarMG(),
  m_eblgCoarsenedFine(),
  m_domainBC(a_domainBC),
  m_ebBC(a_ebBC),
  m_dxFine(),
  m_dx(a_dx),
  m_dxCoar(a_dxCoar),
  m_aCoefficient(a_acoef),
  m_bcoef(a_bcoef),
  m_bcoIrreg(a_bcoIrreg),
  m_alpha(a_alpha),
  m_beta(a_beta),
  m_alphaDiagWeight(),
  m_betaDiagWeight(),
  m_refToFine(a_hasFine ? a_refToFine : 1),
  m_refToCoar(a_hasCoar ? a_refToCoar : 1),
  m_hasFine(a_hasFine),
  m_hasInterpAve(false),
  m_hasCoar(a_hasCoar),
  m_ebAverage(),
  m_ebInterp(),
  m_opEBStencil(),
  m_relCoef(),
  m_vofIterIrreg(),
  m_vofIterMulti(),
  m_vofIterDomLo(),
  m_vofIterDomHi(),
  m_loCFIVS(),
  m_hiCFIVS(),
  m_hasMGObjects(a_hasMGObjects),
  m_layoutChanged(a_layoutChanged),
  m_ebAverageMG(),
  m_ebInterpMG(),
  m_dblCoarMG(),
  m_ebislCoarMG(),
  m_domainCoarMG(),
  m_colors()
{
  CH_TIME("EbHelmholtzOp::ConductivityOp");
  int ncomp = 1;

  m_ext_fastFR = a_fastFR;

  if (m_hasFine){
    m_eblgFine = a_eblgFine;
    m_dxFine   = m_dx/a_refToFine;
  }

  EBCellFactory fact(m_eblg.getEBISL());
  m_relCoef.define(m_eblg.getDBL(), 1, IntVect::Zero, fact);
  if (m_hasCoar){
    m_eblgCoar       = a_eblgCoar;

    DataIterator dit = m_eblg.getDBL().dataIterator(); 
    int nbox = dit.size();
    for (int idir = 0; idir < SpaceDim; idir++)
      {
	m_loCFIVS[idir].define(m_eblg.getDBL());
	m_hiCFIVS[idir].define(m_eblg.getDBL());
#pragma omp parallel
	{
#pragma omp for
	  for (int mybox=0;mybox<nbox;mybox++)
	    {
	      //                pout() << "doing lo cfivs for box " << mybox << endl;
	      m_loCFIVS[idir][dit[mybox]].define(m_eblg.getDomain(), m_eblg.getDBL().get(dit[mybox]),
						 m_eblg.getDBL(), idir,Side::Lo);
	      //                pout() << "doing hi cfivs for box " << mybox << endl;
	      m_hiCFIVS[idir][dit[mybox]].define(m_eblg.getDomain(), m_eblg.getDBL().get(dit[mybox]),
						 m_eblg.getDBL(), idir,Side::Hi);
	    }
	}
      }

    //if this fails, then the AMR grids violate proper nesting.
    ProblemDomain domainCoarsenedFine;
    DisjointBoxLayout dblCoarsenedFine;

    int maxBoxSize = 32;
    bool dumbool;
    bool hasCoarser = EBAMRPoissonOp::getCoarserLayouts(dblCoarsenedFine,
							domainCoarsenedFine,
							m_eblg.getDBL(),
							m_eblg.getEBISL(),
							m_eblg.getDomain(),
							m_refToCoar,
							m_eblg.getEBIS(),
							maxBoxSize, dumbool);

    //should follow from coarsenable
    if (hasCoarser){
      m_eblgCoarsenedFine = EBLevelGrid(dblCoarsenedFine, domainCoarsenedFine, 4, m_eblg.getEBIS());
      m_hasInterpAve = true;
      m_ebInterp.define( m_eblg.getDBL(),     m_eblgCoar.getDBL(),
			 m_eblg.getEBISL(), m_eblgCoar.getEBISL(),
			 domainCoarsenedFine, m_refToCoar, ncomp, m_eblg.getEBIS(),
			 a_ghostCellsPhi);
      m_ebAverage.define(m_eblg.getDBL(),     m_eblgCoarsenedFine.getDBL(),
			 m_eblg.getEBISL(), m_eblgCoarsenedFine.getEBISL(),
			 domainCoarsenedFine, m_refToCoar, ncomp, m_eblg.getEBIS(),
			 a_ghostCellsRHS);
    }
  }

  //special mg objects for when we do not have
  //a coarser level or when the refinement to coarse
  //is greater than two
  //flag for when we need special MG objects
  if (m_hasMGObjects){
    int mgRef = 2;
    m_eblgCoarMG = a_eblgCoarMG;

    m_ebInterpMG.define( m_eblg.getDBL(),     m_eblgCoarMG.getDBL(),
			 m_eblg.getEBISL(), m_eblgCoarMG.getEBISL(),
			 m_eblgCoarMG.getDomain(), mgRef, ncomp, m_eblg.getEBIS(),
			 a_ghostCellsPhi);
    m_ebAverageMG.define(m_eblg.getDBL(),     m_eblgCoarMG.getDBL(),
			 m_eblg.getEBISL(), m_eblgCoarMG.getEBISL(),
			 m_eblgCoarMG.getDomain() , mgRef, ncomp, m_eblg.getEBIS(),
			 a_ghostCellsRHS);

  }

  //define stencils for the operator
  this->defineStencils();
}

EbHelmholtzOp::~EbHelmholtzOp(){
}

void EbHelmholtzOp::fillGrad(const LevelData<EBCellFAB>& a_phi){
}

void EbHelmholtzOp::finerOperatorChanged(const MGLevelOp<LevelData<EBCellFAB> >& a_operator, int a_coarseningFactor){
  const EbHelmholtzOp& op = dynamic_cast<const EbHelmholtzOp&>(a_operator);

  // Perform multigrid coarsening on the operator data.
  const Interval interv(0, 0); // All data is scalar.
  
  EBLevelGrid eblgCoar = m_eblg;
  EBLevelGrid eblgFine = op.m_eblg;
  
  LevelData<EBCellFAB>&       acoefCoar = *m_aCoefficient;
  const LevelData<EBCellFAB>& acoefFine = *(op.m_aCoefficient);
  
  LevelData<EBFluxFAB>&       bcoefCoar = *m_bcoef;
  const LevelData<EBFluxFAB>& bcoefFine = *(op.m_bcoef);
  
  LevelData<BaseIVFAB<Real> >&       bcoefCoarIrreg = *m_bcoIrreg;
  const LevelData<BaseIVFAB<Real> >& bcoefFineIrreg = *(op.m_bcoIrreg);
  
  if (a_coarseningFactor != 1) {
    EBCoarseAverage averageOp(eblgFine.getDBL(), eblgCoar.getDBL(),
			      eblgFine.getEBISL(), eblgCoar.getEBISL(),
			      eblgCoar.getDomain(), a_coarseningFactor, 1,
			      eblgCoar.getEBIS());

    //MayDay::Warning("might want to figure out what harmonic averaging is in this context");
    averageOp.average(acoefCoar, acoefFine, interv);
    averageOp.average(bcoefCoar, bcoefFine, interv);
    averageOp.average(bcoefCoarIrreg, bcoefFineIrreg, interv);
  }

  // Handle parallel domain ghost elements.
  acoefCoar.exchange(interv);
  bcoefCoar.exchange(interv);
  bcoefCoarIrreg.exchange(interv);

  // Recompute the relaxation coefficient for the operator.
  calculateAlphaWeight();
  calculateRelaxationCoefficient();

  // Notify any observers of this change.
  notifyObserversOfChange();
}

//-----------------------------------------------------------------------
Real
EbHelmholtzOp::
getSafety() const
{
  Real safety = 1.0;
  return safety;
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
calculateAlphaWeight()
{
  DataIterator dit = m_eblg.getDBL().dataIterator(); 
  int nbox = dit.size();
#pragma omp parallel
  {
#pragma omp for
    for(int mybox=0; mybox<nbox; mybox++)
      {

	VoFIterator& vofit = m_vofIterIrreg[dit[mybox]];
	for (vofit.reset(); vofit.ok(); ++vofit)
	  {
	    const VolIndex& VoF = vofit();
	    Real volFrac = m_eblg.getEBISL()[dit[mybox]].volFrac(VoF);
	    Real alphaWeight = (*m_aCoefficient)[dit[mybox]](VoF, 0);
	    alphaWeight *= volFrac;
	    if(s_areaFracWeighted){
	      alphaWeight *= m_eblg.getEBISL()[dit[mybox]].areaFracScaling(VoF);

	      // const EBISBox& ebisbox = m_eblg.getEBISL()[dit[mybox]];
	      // Real area = ebisbox.bndryArea(VoF);
	      // for (int dir = 0; dir < SpaceDim; dir++){
	      // 	area += ebisbox.sumArea(VoF, dir, Side::Lo);
	      // 	area += ebisbox.sumArea(VoF, dir, Side::Hi);
	      // }

	      // alphaWeight *= 1./area;
	    }

	    m_alphaDiagWeight[dit[mybox]](VoF, 0) = alphaWeight;
	  }
      }
  }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getDivFStencil(VoFStencil&      a_vofStencil,
	       const VolIndex&  a_vof,
	       const DataIndex& a_dit)
{
  CH_TIME("EbHelmholtzOp::getDivFStencil");
  const EBISBox& ebisBox = m_eblg.getEBISL()[a_dit];
  a_vofStencil.clear();
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      for (SideIterator sit; sit.ok(); ++sit)
	{
	  int isign = sign(sit());
	  Vector<FaceIndex> faces = ebisBox.getFaces(a_vof, idir, sit());
	  for (int iface = 0; iface < faces.size(); iface++)
	    {
	      VoFStencil fluxStencil;
	      getFluxStencil(fluxStencil, faces[iface], a_dit);
	      Real areaFrac = ebisBox.areaFrac(faces[iface]);
	      fluxStencil *= Real(isign)*areaFrac/m_dx;
	      a_vofStencil += fluxStencil;
	    }
	}
    }

  if(s_areaFracWeighted){
    a_vofStencil *= ebisBox.areaFracScaling(a_vof);

    // Real area = ebisBox.bndryArea(a_vof);
    // for (int dir = 0; dir < SpaceDim; dir++){
    //   area += ebisBox.sumArea(a_vof, dir, Side::Lo);
    //   area += ebisBox.sumArea(a_vof, dir, Side::Hi);
    // }

    // a_vofStencil *= 1./area;
  }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getFluxStencil(VoFStencil&      a_fluxStencil,
	       const FaceIndex& a_face,
	       const DataIndex& a_dit)
{
  /// stencil for flux computation.   the truly ugly part of this computation
  /// beta and eta are multiplied in here

  CH_TIME("EbHelmholtzOp::getFluxStencil");
  //need to do this by interpolating to centroids
  //so get the stencil at each face center and add with
  //interpolation weights
  FaceStencil interpSten = EBArith::getInterpStencil(a_face,
						     (*m_eblg.getCFIVS())[a_dit],
						     m_eblg.getEBISL()[a_dit],
						     m_eblg.getDomain());

  a_fluxStencil.clear();
  for (int isten = 0; isten < interpSten.size(); isten++)
    {
      const FaceIndex& face = interpSten.face(isten);
      const Real&    weight = interpSten.weight(isten);
      VoFStencil faceCentSten;
      getFaceCenteredFluxStencil(faceCentSten, face, a_dit);
      faceCentSten *= weight;
      a_fluxStencil += faceCentSten;
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getFaceCenteredFluxStencil(VoFStencil&      a_fluxStencil,
			   const FaceIndex& a_face,
			   const DataIndex& a_dit)
{
  CH_TIME("EbHelmholtzOp::getFaceCenteredFluxStencil");
  //face centered gradient is just a centered diff
  int faceDir= a_face.direction();
  a_fluxStencil.clear();

  if (!a_face.isBoundary())
    {
      a_fluxStencil.add(a_face.getVoF(Side::Hi),  1.0/m_dx, 0);
      a_fluxStencil.add(a_face.getVoF(Side::Lo), -1.0/m_dx, 0);
      a_fluxStencil *= (*m_bcoef)[a_dit][faceDir](a_face,0);
    }
  else
    {
      //the boundary condition handles this one.
    }
}

void EbHelmholtzOp::setAlphaAndBeta(const Real& a_alpha, const Real& a_beta){
  CH_TIME("EbHelmholtzOp::setAlphaAndBeta");
  
  m_alpha = a_alpha;
  m_beta  = a_beta;
  
  this->calculateAlphaWeight();
  this->calculateRelaxationCoefficient();
}

void
EbHelmholtzOp::
setTime(Real a_oldTime, Real a_mu, Real a_dt){
}

void EbHelmholtzOp::kappaScale(LevelData<EBCellFAB> & a_rhs){
  EBLevelDataOps::kappaWeight(a_rhs);
}

void EbHelmholtzOp::diagonalScale(LevelData<EBCellFAB> & a_rhs, bool a_kappaWeighted){
  CH_TIME("EbHelmholtzOp::diagonalScale");

  // Scale by volume fraction if asked. 
  if(a_kappaWeighted) EBLevelDataOps::kappaWeight(a_rhs);

  // Scale by a-coefficient and alpha, too.
  DataIterator dit = m_eblg.getDBL().dataIterator();
  for (dit.reset(); dit.ok(); ++dit){
    a_rhs[dit()] *= (*m_aCoefficient)[dit()];
  }
}

void EbHelmholtzOp::divideByIdentityCoef(LevelData<EBCellFAB> & a_rhs){
  for(DataIterator dit = m_eblg.getDBL().dataIterator(); dit.ok(); ++dit){
    a_rhs[dit()] /= (*m_aCoefficient)[dit()];
  }
}

void
EbHelmholtzOp::
calculateRelaxationCoefficient()
{
  CH_TIME("ebco::calculateRelCoef");
  // define regular relaxation coefficent
  Real safety = getSafety();
  DataIterator dit = m_eblg.getDBL().dataIterator();
  int nbox=dit.size();
#pragma omp parallel
  {
#pragma omp for
    for (int mybox=0;mybox<nbox;mybox++)
      {
	const Box& grid = m_eblg.getDBL().get(dit[mybox]);
      
	// For time-independent acoef, initialize lambda = alpha * acoef.
	const EBCellFAB& acofab = (*m_aCoefficient)[dit[mybox]];
	m_relCoef[dit[mybox]].setVal(0.);
	m_relCoef[dit[mybox]].plus(acofab, 0, 0, 1);
	m_relCoef[dit[mybox]]*= m_alpha;
      
	// Compute the relaxation coefficient in regular cells.
	BaseFab<Real>& regRel = m_relCoef[dit[mybox]].getSingleValuedFAB();
	for (int idir = 0; idir < SpaceDim; idir++)
	  {
	    BaseFab<Real>& regBCo = (*m_bcoef)[dit[mybox]][idir].getSingleValuedFAB();
	    FORT_DECRINVRELCOEFEBCO(CHF_FRA1(regRel,0),
				    CHF_FRA1(regBCo,0),
				    CHF_CONST_REAL(m_beta),
				    CHF_BOX(grid),
				    CHF_REAL(m_dx),
				    CHF_INT(idir));
	  }
      
	//now invert so lambda = stable lambda for variable coef lapl
	//(according to phil, this is the correct lambda)
	FORT_INVERTLAMBDAEBCO(CHF_FRA1(regRel,0),
			      CHF_REAL(safety),
			      CHF_BOX(grid));
      
	// Now go over the irregular cells.
	VoFIterator& vofit = m_vofIterIrreg[dit[mybox]];
	for (vofit.reset(); vofit.ok(); ++vofit)
	  {
	    const VolIndex& VoF = vofit();
          
	    Real alphaWeight = m_alphaDiagWeight[dit[mybox]](VoF, 0);
	    Real  betaWeight =  m_betaDiagWeight[dit[mybox]](VoF, 0);
	    alphaWeight *= m_alpha;
	    betaWeight  *= m_beta;
          
	    Real diagWeight  = alphaWeight + betaWeight;
          
	    // Set the irregular relaxation coefficients.
	    if (Abs(diagWeight) > 1.0e-9)
	      {
		m_relCoef[dit[mybox]](VoF, 0) = safety/diagWeight;
	      }
	    else
	      {
		m_relCoef[dit[mybox]](VoF, 0) = 0.;
	      }
	  }
      }
  }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
defineStencils()
{
  CH_TIME("EbHelmholtzOp::defineStencils");
  // create ebstencil for irregular applyOp
  m_opEBStencil.define(m_eblg.getDBL());
  // create vofstencils for applyOp and

  Real fakeBeta = 1;
  m_domainBC->setCoef(m_eblg,   fakeBeta ,      m_bcoef   );
  m_ebBC->setCoef(    m_eblg,   fakeBeta ,      m_bcoIrreg);

  Real dxScale = 1.0/m_dx;
  m_ebBC->define((*m_eblg.getCFIVS()), dxScale); //has to happen AFTER coefs are set

  LayoutData<BaseIVFAB<VoFStencil> >* fluxStencil = m_ebBC->getFluxStencil(0);

  m_vofIterIrreg.define(     m_eblg.getDBL()); // vofiterator cache
  m_vofIterMulti.define(     m_eblg.getDBL()); // vofiterator cache
  m_alphaDiagWeight.define(  m_eblg.getDBL());
  m_betaDiagWeight.define(   m_eblg.getDBL());
  Box sideBoxLo[SpaceDim];
  Box sideBoxHi[SpaceDim];
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      Box domainBox = m_eblg.getDomain().domainBox();
      sideBoxLo[idir] = adjCellLo(domainBox, idir, 1);
      sideBoxLo[idir].shift(idir,  1);
      sideBoxHi[idir] = adjCellHi(domainBox, idir, 1);
      sideBoxHi[idir].shift(idir, -1);
      m_vofIterDomLo[idir].define( m_eblg.getDBL()); // vofiterator cache for domain lo
      m_vofIterDomHi[idir].define( m_eblg.getDBL()); // vofiterator cache for domain hi
    }
  EBArith::getMultiColors(m_colors);

  DataIterator dit = m_eblg.getDBL().dataIterator(); 
  int nbox = dit.size();
  //#pragma omp parallel
  {
    // this breaks 
    //#pragma omp parallel for
    for(int mybox=0; mybox<nbox; mybox++)
      {

	const Box& curBox = m_eblg.getDBL().get(dit[mybox]);
	const EBISBox& ebisBox = m_eblg.getEBISL()[dit[mybox]];
	const EBGraph& ebgraph = ebisBox.getEBGraph();

	IntVectSet irregIVS = ebisBox.getIrregIVS(curBox);
	IntVectSet multiIVS = ebisBox.getMultiCells(curBox);

	BaseIVFAB<VoFStencil> opStencil(irregIVS,ebgraph, 1);

	//cache the vofIterators
	m_alphaDiagWeight[dit[mybox]].define(irregIVS,ebisBox.getEBGraph(), 1);
	m_betaDiagWeight [dit[mybox]].define(irregIVS,ebisBox.getEBGraph(), 1);
	m_vofIterIrreg   [dit[mybox]].define(irregIVS,ebisBox.getEBGraph());
	m_vofIterMulti   [dit[mybox]].define(multiIVS,ebisBox.getEBGraph());

	for (int idir = 0; idir < SpaceDim; idir++)
	  {
	    IntVectSet loIrreg = irregIVS;
	    IntVectSet hiIrreg = irregIVS;
	    loIrreg &= sideBoxLo[idir];
	    hiIrreg &= sideBoxHi[idir];
	    m_vofIterDomLo[idir][dit[mybox]].define(loIrreg,ebisBox.getEBGraph());
	    m_vofIterDomHi[idir][dit[mybox]].define(hiIrreg,ebisBox.getEBGraph());
	  }

	VoFIterator& vofit = m_vofIterIrreg[dit[mybox]];
	for (vofit.reset(); vofit.ok(); ++vofit)
	  {
	    const VolIndex& VoF = vofit();

	    VoFStencil& curStencil = opStencil(VoF,0);

	    //bcoef is included here in the flux consistent
	    //with the regular
	    getDivFStencil(curStencil,VoF, dit[mybox]);
	    if (fluxStencil != NULL)
	      {
		BaseIVFAB<VoFStencil>& fluxStencilBaseIVFAB = (*fluxStencil)[dit[mybox]];
		//this fills the stencil with the gradient*beta*bcoef
		VoFStencil  fluxStencilPt = fluxStencilBaseIVFAB(VoF,0);
		curStencil += fluxStencilPt;
	      }
	    Real betaWeight = EBArith::getDiagWeight(curStencil, VoF);
	    const IntVect& iv = VoF.gridIndex();
	    for (int idir = 0; idir < SpaceDim; idir++)
	      {
		Box loSide = bdryLo(m_eblg.getDomain(),idir);
		loSide.shiftHalf(idir,1);
		Real adjust = 0;
		if (loSide.contains(iv))
		  {
		    Real faceAreaFrac = 0.0;
		    Real weightedAreaFrac = 0;
		    Vector<FaceIndex> faces = ebisBox.getFaces(VoF,idir,Side::Lo);
		    for (int i = 0; i < faces.size(); i++)
		      {
			weightedAreaFrac = ebisBox.areaFrac(faces[i]);
			weightedAreaFrac *= (*m_bcoef)[dit[mybox]][idir](faces[i],0);
			faceAreaFrac +=  weightedAreaFrac;
		      }
		    adjust += -weightedAreaFrac /(m_dx*m_dx);
		  }
		Box hiSide = bdryHi(m_eblg.getDomain(),idir);
		hiSide.shiftHalf(idir,-1);
		if (hiSide.contains(iv))
		  {
		    Real faceAreaFrac = 0.0;
		    Real weightedAreaFrac = 0;
		    Vector<FaceIndex> faces = ebisBox.getFaces(VoF,idir,Side::Hi);
		    for (int i = 0; i < faces.size(); i++)
		      {
			weightedAreaFrac = ebisBox.areaFrac(faces[i]);
			weightedAreaFrac *= (*m_bcoef)[dit[mybox]][idir](faces[i],0);
			faceAreaFrac +=  weightedAreaFrac;
		      }
		    adjust += -weightedAreaFrac /(m_dx*m_dx);
		  }
		betaWeight += adjust;
	      }

	    //add in identity term
	    Real volFrac = ebisBox.volFrac(VoF);
	    Real alphaWeight = (*m_aCoefficient)[dit[mybox]](VoF, 0);
	    alphaWeight *= volFrac;
	    if(s_areaFracWeighted){
	      alphaWeight *= ebisBox.areaFracScaling(VoF);
	    }

	    m_alphaDiagWeight[dit[mybox]](VoF, 0) = alphaWeight;
	    m_betaDiagWeight[dit[mybox]](VoF, 0)  = betaWeight;
	  }

	//Operator ebstencil
	m_opEBStencil[dit[mybox]] = RefCountedPtr<EBStencil>
	  (new EBStencil(m_vofIterIrreg[dit[mybox]].getVector(), opStencil, m_eblg.getDBL().get(dit[mybox]),
			 m_eblg.getEBISL()[dit[mybox]], m_ghostCellsPhi, m_ghostCellsRHS, 0, true));
      }//dit
  } //pragma

  calculateAlphaWeight();
  calculateRelaxationCoefficient();

  if (m_hasFine)
    {
      int ncomp = 1;
      if(m_ext_fastFR.isNull()){
	m_fastFR = RefCountedPtr<EBFluxRegister> (new EbFastFluxRegister(m_eblgFine, m_eblg, m_refToFine, ncomp, s_forceNoEBCF));
      }
      else{
	m_fastFR = m_ext_fastFR;
      }
      m_hasEBCF = m_fastFR->hasEBCF();
    }
  else{
    m_fastFR = RefCountedPtr<EBFluxRegister>();
  }
  defineEBCFStencils();
  defineColorStencils(sideBoxLo, sideBoxHi);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
defineColorStencils(Box a_sideBoxLo[SpaceDim],
		    Box a_sideBoxHi[SpaceDim])
{
  LayoutData<BaseIVFAB<VoFStencil> >* fluxStencil = m_ebBC->getFluxStencil(0);
  //define the stencils and iterators specific to gsrb.
  for (int icolor=0; icolor < m_colors.size(); ++icolor)
    {
      m_colorEBStencil[icolor].define(m_eblg.getDBL());
      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  m_vofItIrregColorDomLo[icolor][idir].define( m_eblg.getDBL());
	  m_vofItIrregColorDomHi[icolor][idir].define( m_eblg.getDBL());
	  m_cacheEBxDomainFluxLo[icolor][idir].define( m_eblg.getDBL());
	  m_cacheEBxDomainFluxHi[icolor][idir].define( m_eblg.getDBL());
	}
      DataIterator dit = m_eblg.getDBL().dataIterator(); 

      int nbox = dit.size();
      //this breaks
      //#pragma omp parallel for
      for(int mybox=0; mybox<nbox; mybox++)
	{
	  const EBISBox& curEBISBox = m_eblg.getEBISL()[dit[mybox]];
	  const EBGraph& curEBGraph = curEBISBox.getEBGraph();
	  Box dblBox( m_eblg.getDBL().get(dit[mybox]) );

	  IntVectSet ivsColor(DenseIntVectSet(dblBox, false));

	  VoFIterator& vofit = m_vofIterIrreg[dit[mybox]];
	  for (vofit.reset(); vofit.ok(); ++vofit)
	    {
	      const VolIndex& vof = vofit();
	      const IntVect& iv = vof.gridIndex();

	      bool doThisVoF = true;
	      for (int idir = 0; idir < SpaceDim; idir++)
		{
		  if (iv[idir] % 2 != m_colors[icolor][idir])
		    {
		      doThisVoF = false;
		      break;
		    }
		}

	      if (doThisVoF)
		{
		  ivsColor |= iv;
		}
	    }

	  BaseIVFAB<VoFStencil> colorStencilBaseIVFAB(ivsColor, curEBGraph, 1);

	  for (int idir = 0; idir < SpaceDim; idir++)
	    {
	      IntVectSet loIrregColor = ivsColor;
	      IntVectSet hiIrregColor = ivsColor;
	      loIrregColor &= a_sideBoxLo[idir];
	      hiIrregColor &= a_sideBoxHi[idir];
	      m_vofItIrregColorDomLo[icolor][idir][dit[mybox]].define(loIrregColor,curEBGraph);
	      m_vofItIrregColorDomHi[icolor][idir][dit[mybox]].define(hiIrregColor,curEBGraph);
	      m_cacheEBxDomainFluxLo[icolor][idir][dit[mybox]].define(loIrregColor,curEBGraph,1);
	      m_cacheEBxDomainFluxHi[icolor][idir][dit[mybox]].define(hiIrregColor,curEBGraph,1);
	    }

	  VoFIterator vofitcolor(ivsColor, curEBGraph);
	  int ivof = 0;
	  for (vofitcolor.reset(); vofitcolor.ok(); ++vofitcolor)
	    {
	      const VolIndex& vof = vofitcolor();

	      VoFStencil& colorStencil =   colorStencilBaseIVFAB(vof,0);
	      getDivFStencil(colorStencil,vof, dit[mybox]);

	      if (fluxStencil != NULL)
		{
		  BaseIVFAB<VoFStencil>& ebFluxStencilBaseIVFAB = (*fluxStencil)[dit[mybox]];
		  //this fills the stencil with the gradient
		  const VoFStencil&  ebFluxStencilPt = ebFluxStencilBaseIVFAB(vof,0);
		  //if the stencil returns empty, this means that our
		  //geometry is underresolved and we just set the
		  //stencil to zero.   This might not work.
		  if (ebFluxStencilPt.size() != 0)
		    {
		      colorStencil += ebFluxStencilPt;
		    }
		}
	      ivof++;
	    }

	  const Vector<VolIndex>& srcVofs = vofitcolor.getVector();

	  m_colorEBStencil[icolor][dit[mybox]]  = RefCountedPtr<EBStencil>
	    (new EBStencil(srcVofs, colorStencilBaseIVFAB,  m_eblg.getDBL().get(dit[mybox]),
			   m_eblg.getEBISL()[dit[mybox]], m_ghostCellsPhi, m_ghostCellsRHS, 0, true));
	}//dit
    }//color
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
defineEBCFStencils()
{
  ///this routine is ugly and complicated.
  //I will attempt to comment it but I fear it is a lost cause
  //because the algorithm is so arcane.
  //We are attempting to only do stuff at the very specific
  //points where there is an embedded boundary crossing a coarse-fine
  //interface.   We happen to know that EBFastFR already has done this
  //choreography and we want to leverage it.

  //EBFastFR has data structures in it that serve as buffers and so on
  //that we will (thankfully) be able to leave alone.   We are only
  //going to access the data structures wherein it identifies which
  //coarse cells are associated with the coarse-fine interface
  // where the EB crosses and use this list to build up  which faces
  // need to be cal

  //important factoid: beta gets multiplied in at the last minute
  //(on evaluation) because it can change as diffusion solvers progress.
  if (m_hasFine && m_hasEBCF)
    {
      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  for (SideIterator sit; sit.ok(); ++sit)
	    {
	      //coarse fine stuff is between me and next finer level
	      //fine stuff lives over m_eblgfine
	      //coar stuff lives over m_eblg
	      int index = m_fastFR->index(idir, sit());
	      m_stencilCoar[index].define(m_eblg.getDBL());
	      m_faceitCoar [index].define(m_eblg.getDBL());

	      DataIterator dit = m_eblg.getDBL().dataIterator(); 
	      int nbox = dit.size();

#pragma omp parallel for
	      for(int mybox=0; mybox<nbox; mybox++)
		{
		  Vector<FaceIndex>& facesEBCFCoar =  m_faceitCoar[index][dit[mybox]];
		  Vector<VoFStencil>& stencEBCFCoar= m_stencilCoar[index][dit[mybox]];
		  Vector<VoFIterator>& vofitlist = m_fastFR->getVoFItCoar(dit[mybox], idir, sit());
		  //first build up the list of the faces
		  for (int ivofit = 0; ivofit < vofitlist.size(); ivofit++)
		    {
		      VoFIterator& vofit = vofitlist[ivofit];
		      for (vofit.reset(); vofit.ok(); ++vofit)
			{
			  //on the coarse side of the CF interface we are
			  //looking in the flip direction because we look
			  //back at the interface
			  Vector<FaceIndex> facespt = m_eblg.getEBISL()[dit[mybox]].getFaces(vofit(), idir, flip(sit()));
			  facesEBCFCoar.append(facespt);
			}
		    }

		  stencEBCFCoar.resize(facesEBCFCoar.size());
		  for (int iface = 0; iface < stencEBCFCoar.size(); iface++)
		    {
		      IntVectSet cfivs; //does not apply here
		      getFluxStencil(stencEBCFCoar[iface], facesEBCFCoar[iface], dit[mybox]);
		    }
		}
	    }
	}
    }
}

//-----------------------------------------------------------------------
void
EbHelmholtzOp::
residual(LevelData<EBCellFAB>&       a_residual,
	 const LevelData<EBCellFAB>& a_phi,
	 const LevelData<EBCellFAB>& a_rhs,
	 bool                        a_homogeneousPhysBC)
{
  CH_TIME("EbHelmholtzOp::residual");
  //this is a multigrid operator so only homogeneous CF BC
  //and null coar level
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_phi.ghostVect() == m_ghostCellsPhi);
  applyOp(a_residual,a_phi,NULL, a_homogeneousPhysBC, true);
  axby(a_residual,a_residual,a_rhs,-1.0, 1.0);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
preCond(LevelData<EBCellFAB>&       a_lhs,
	const LevelData<EBCellFAB>& a_rhs)
{
  CH_TIME("EbHelmholtzOp::preCond");
  EBLevelDataOps::assign(a_lhs, a_rhs);
  EBLevelDataOps::scale(a_lhs, m_relCoef);

  relax(a_lhs, a_rhs, 40);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyOp(LevelData<EBCellFAB>&             a_opPhi,
	const LevelData<EBCellFAB>&       a_phi,
	bool                              a_homogeneousPhysBC)
{
  //homogeneous CFBCs because that is all we can do.
  applyOp(a_opPhi, a_phi, NULL, a_homogeneousPhysBC, true);
}
//-----------------------------------------------------------------------
/***/
void
EbHelmholtzOp::
incrOpRegularAllDirs(Box * a_loBox,
		     Box * a_hiBox,
		     int * a_hasLo,
		     int * a_hasHi,
		     Box & a_curDblBox,
		     Box & a_curPhiBox,
		     int a_nComps,
		     BaseFab<Real> & a_curOpPhiFAB,
		     const BaseFab<Real> & a_curPhiFAB,
		     bool a_homogeneousPhysBC,
		     const DataIndex& a_dit)
{
  CH_TIME("EbHelmholtzOp::incrOpRegularAllDirs");
  CH_assert(m_domainBC != NULL);

  //need to monkey with the ghost cells to account for boundary conditions
  if (!s_turnOffBCs)
    {
      BaseFab<Real>& phiFAB = (BaseFab<Real>&) a_curPhiFAB;
      applyDomainFlux(a_loBox, a_hiBox, a_hasLo, a_hasHi,
		      a_curDblBox, a_nComps, phiFAB,
		      a_homogeneousPhysBC, a_dit);
    }

  for (int comp = 0; comp<a_nComps; comp++)
    {
      //data ptr fusses if it is truly zero size
      BaseFab<Real> dummy(Box(IntVect::Zero, IntVect::Zero), 1);

      BaseFab<Real>* bc[3];
      //need three coeffs because this has to work in 3d
      //this is my klunky way to make the call dimension-independent
      for (int iloc = 0; iloc < 3; iloc++)
	{
	  if (iloc >= SpaceDim)
	    {
	      bc[iloc]= &dummy;
	    }
	  else
	    {
	      bc[iloc] = &((*m_bcoef)[a_dit][iloc].getSingleValuedFAB());
	    }
	}
      FORT_CONDUCTIVITYINPLACE(CHF_FRA1(a_curOpPhiFAB,comp),
			       CHF_CONST_FRA1(a_curPhiFAB,comp),
			       CHF_CONST_FRA1((*bc[0]),comp),
			       CHF_CONST_FRA1((*bc[1]),comp),
			       CHF_CONST_FRA1((*bc[2]),comp),
			       CHF_CONST_REAL(m_beta),
			       CHF_CONST_REAL(m_dx),
			       CHF_BOX(a_curDblBox));
    }
}

/***/
void
EbHelmholtzOp::
applyDomainFlux(Box * a_loBox,
		Box * a_hiBox,
		int * a_hasLo,
		int * a_hasHi,
		Box & a_dblBox,
		int a_nComps,
		BaseFab<Real> & a_phiFAB,
		bool a_homogeneousPhysBC,
		const DataIndex& a_dit)
{
  CH_TIME("EbHelmholtzOp::applyDomainFlux");
  CH_assert(m_domainBC != NULL);

  for (int idir=0; idir<SpaceDim; idir++)
    {

      EBArith::loHi(a_loBox[idir], a_hasLo[idir],
		    a_hiBox[idir], a_hasHi[idir],
		    m_eblg.getDomain(),a_dblBox, idir);

      for (int comp = 0; comp<a_nComps; comp++)
	{

	  if (a_hasLo[idir] == 1)
	    {
	      Box lbox=a_loBox[idir];
	      lbox.shift(idir,-1);
	      FArrayBox loFaceFlux(a_loBox[idir],a_nComps);
	      int side = -1;
	      Real time = 0;
	      m_domainBC->getFaceFlux(loFaceFlux,a_phiFAB,RealVect::Zero,m_dx*RealVect::Unit,idir,Side::Lo,a_dit,time,a_homogeneousPhysBC);

	      BaseFab<Real>& bc = ((*m_bcoef)[a_dit][idir].getSingleValuedFAB());
	      //again, following the odd convention of EBAMRPoissonOp
	      //(because I am reusing its BC classes),
	      //the input flux here is CELL centered and the input box
	      //is the box adjacent to the domain boundary on the valid side.
	      //because I am not insane (yet) I will just shift the flux's box
	      //over and multiply by the appropriate coefficient
	      bc.shiftHalf(idir, 1);
	      FORT_EBCOREGAPPLYDOMAINFLUX(CHF_FRA1(a_phiFAB,comp),
					  CHF_CONST_FRA1(loFaceFlux,comp),
					  CHF_CONST_FRA1(bc,comp),
					  CHF_CONST_REAL(m_dx),
					  CHF_CONST_INT(side),
					  CHF_CONST_INT(idir),
					  CHF_BOX(lbox));
	      bc.shiftHalf(idir, -1);
	    }

	  if (a_hasHi[idir] == 1)
	    {
	      Box hbox=a_hiBox[idir];
	      hbox.shift(idir,1);
	      FArrayBox hiFaceFlux(a_hiBox[idir],a_nComps);
	      int side = 1;
	      Real time = 0;
	      m_domainBC->getFaceFlux(hiFaceFlux,a_phiFAB,RealVect::Zero,m_dx*RealVect::Unit,idir,Side::Hi,a_dit,time,a_homogeneousPhysBC);

	      BaseFab<Real>& bc = ((*m_bcoef)[a_dit][idir].getSingleValuedFAB());
	      //again, following the odd convention of EBAMRPoissonOp
	      //(because I am reusing its BC classes),
	      //the input flux here is CELL centered and the input box
	      //is the box adjacent to the domain boundary on the valid side.
	      //because I am not insane (yet) I will just shift the flux's box
	      //over and multiply by the appropriate coefficient
	      bc.shiftHalf(idir, -1);
	      FORT_EBCOREGAPPLYDOMAINFLUX(CHF_FRA1(a_phiFAB,comp),
					  CHF_CONST_FRA1(hiFaceFlux,comp),
					  CHF_CONST_FRA1(bc,comp),
					  CHF_CONST_REAL(m_dx),
					  CHF_CONST_INT(side),
					  CHF_CONST_INT(idir),
					  CHF_BOX(hbox));
	      bc.shiftHalf(idir, 1);

	    }

	}
    }
}

//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyOp(LevelData<EBCellFAB>&                    a_lhs,
	const LevelData<EBCellFAB>&              a_phi,
	const LevelData<EBCellFAB>* const        a_phiCoar,
	const bool&                              a_homogeneousPhysBC,
	const bool&                              a_homogeneousCFBC,
	const bool&                              a_doexchange)
{
  DataIterator dit = a_lhs.dataIterator();
  this->applyOp(a_lhs, a_phi, a_phiCoar, a_homogeneousPhysBC, a_homogeneousCFBC, dit, a_doexchange);
}

void
EbHelmholtzOp::
applyOp(LevelData<EBCellFAB>&                    a_lhs,
	const LevelData<EBCellFAB>&              a_phi,
	const LevelData<EBCellFAB>* const        a_phiCoar,
	const bool&                              a_homogeneousPhysBC,
	const bool&                              a_homogeneousCFBC,
	DataIterator&                            a_dit,
	const bool&                              a_doexchange)
{
  CH_TIME("ebco::applyOp");
  LevelData<EBCellFAB>& phi = const_cast<LevelData<EBCellFAB>&>(a_phi);
  if (m_hasCoar && (!s_turnOffBCs))
    {
      applyCFBCs(phi, a_phiCoar, a_homogeneousCFBC);
    }
  if(a_doexchange){
    phi.exchange(phi.interval());
  }

  EBLevelDataOps::setToZero(a_lhs);
  incr( a_lhs, a_phi, m_alpha); //this multiplies by alpha
  
  for (a_dit.reset(); a_dit.ok(); ++a_dit){
    {
      EBCellFAB      & phi = (EBCellFAB&)(a_phi[a_dit()]);
      const EBISBox& ebisbox = phi.getEBISBox();

      if(!ebisbox.isAllCovered()){
	a_lhs[a_dit()].mult((*m_aCoefficient)[a_dit()], 0, 0, 1);

	Box loBox[SpaceDim],hiBox[SpaceDim];
	int hasLo[SpaceDim],hasHi[SpaceDim];

	EBCellFAB      & lph = a_lhs[a_dit()];
	//phi.setCoveredCellVal(0.0, 0);

	const BaseFab<Real>  & phiFAB = phi.getSingleValuedFAB();
	BaseFab<Real>        & lphFAB = lph.getSingleValuedFAB();
	Box dblBox = m_eblg.getDBL()[a_dit()];
	int nComps = 1;
	Box curPhiBox = phiFAB.box();



	if (!s_turnOffBCs)
	  {
	    incrOpRegularAllDirs( loBox, hiBox, hasLo, hasHi,
				  dblBox, curPhiBox, nComps,
				  lphFAB,
				  phiFAB,
				  a_homogeneousPhysBC,
				  a_dit());
	  }
	else
	  {
	    //the all dirs code is wrong for no bcs = true
	    for (int idir = 0; idir < SpaceDim; idir++)
	      {
		incrOpRegularDir(a_lhs[a_dit()], a_phi[a_dit()], a_homogeneousPhysBC, idir, a_dit());
	      }
	  }

	applyOpIrregular(a_lhs[a_dit()], a_phi[a_dit()], a_homogeneousPhysBC, a_dit());
      }
    }
  }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
incrOpRegularDir(EBCellFAB&             a_lhs,
		 const EBCellFAB&       a_phi,
		 const bool&            a_homogeneous,
		 const int&             a_dir,
		 const DataIndex&       a_datInd)
{
  CH_TIME("ebco::incrOpReg");
  const Box& grid = m_eblg.getDBL()[a_datInd];
  Box domainFaces = m_eblg.getDomain().domainBox();
  domainFaces.surroundingNodes(a_dir);
  Box interiorFaces = grid;
  interiorFaces.surroundingNodes(a_dir);
  interiorFaces.grow(a_dir, 1);
  interiorFaces &=  domainFaces;
  interiorFaces.grow( a_dir, -1);

  //do flux difference for interior points
  FArrayBox interiorFlux(interiorFaces, 1);
  const FArrayBox& phi  = (FArrayBox&)(a_phi.getSingleValuedFAB());
  getFlux(interiorFlux, phi,  interiorFaces, a_dir, m_dx, a_datInd);

  Box loBox, hiBox, centerBox;
  int hasLo, hasHi;
  EBArith::loHiCenter(loBox, hasLo, hiBox, hasHi, centerBox, m_eblg.getDomain(),grid, a_dir);

  //do the low high center thing
  BaseFab<Real>& reglhs        = a_lhs.getSingleValuedFAB();
  Box dummyBox(IntVect::Zero, IntVect::Unit);
  FArrayBox domainFluxLo(dummyBox, 1);
  FArrayBox domainFluxHi(dummyBox, 1);

  RealVect origin = RealVect::Zero;
  Real time = 0.0;
  RealVect dxVect = m_dx*RealVect::Unit;
  if (hasLo==1)
    {
      Box loBoxFace = loBox;
      loBoxFace.shiftHalf(a_dir, -1);
      domainFluxLo.resize(loBoxFace, 1);
      if (!s_turnOffBCs)
	{
	  //using EBAMRPoissonOp BCs which require a cell centered box.
	  domainFluxLo.shiftHalf(a_dir, 1);
	  m_domainBC->getFaceFlux(domainFluxLo,phi,origin,dxVect,a_dir,Side::Lo,a_datInd,time,a_homogeneous);
	  domainFluxLo *= m_beta;
	  domainFluxLo.shiftHalf(a_dir,-1);
	}
      else
	{
	  //extrapolate to domain flux if there is no bc
	  for (BoxIterator boxit(loBoxFace); boxit.ok(); ++boxit)
	    {
	      const IntVect& iv = boxit();
	      IntVect ivn= iv;
	      ivn[a_dir]++;
	      domainFluxLo(iv, 0) = interiorFlux(ivn, 0);
	    }
	}
    }
  if (hasHi==1)
    {
      Box hiBoxFace = hiBox;
      hiBoxFace.shiftHalf(a_dir, 1);
      domainFluxHi.resize(hiBoxFace, 1);
      if (!s_turnOffBCs)
	{
	  //using EBAMRPoissonOp BCs which require a cell centered box.
	  domainFluxHi.shiftHalf(a_dir, -1);
	  m_domainBC->getFaceFlux(domainFluxHi,phi,origin,dxVect,a_dir,Side::Hi,a_datInd,time,a_homogeneous);
	  domainFluxHi *= m_beta;
	  domainFluxHi.shiftHalf(a_dir,  1);
	}
      else
	{
	  //extrapolate to domain flux if there is no bc
	  for (BoxIterator boxit(hiBoxFace); boxit.ok(); ++boxit)
	    {
	      const IntVect& iv = boxit();
	      IntVect ivn= iv;
	      ivn[a_dir]--;
	      domainFluxHi(iv, 0) = interiorFlux(ivn, 0);
	    }
	}
    }
  Real unity = 1.0;  //beta already in the flux
  FORT_INCRAPPLYEBCO(CHF_FRA1(reglhs,0),
		     CHF_CONST_FRA1(interiorFlux, 0),
		     CHF_CONST_FRA1(domainFluxLo, 0),
		     CHF_CONST_FRA1(domainFluxHi, 0),
		     CHF_CONST_REAL(unity),
		     CHF_CONST_REAL(m_dx),
		     CHF_BOX(loBox),
		     CHF_BOX(hiBox),
		     CHF_BOX(centerBox),
		     CHF_CONST_INT(hasLo),
		     CHF_CONST_INT(hasHi),
		     CHF_CONST_INT(a_dir));
}

//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyOpIrregular(EBCellFAB&             a_lhs,
		 const EBCellFAB&       a_phi,
		 const bool&            a_homogeneous,
		 const DataIndex&       a_datInd)
{
  CH_TIME("ebco::applyOpIrr");

  //  bool stopHere = false;
  //  if (a_lhs.box().contains(IntVect(26, 1)))
  //    {
  //      stopHere = true;
  //    }

  RealVect vectDx = m_dx*RealVect::Unit;
  //  m_opEBStencil[a_datInd]->apply(a_lhs, a_phi,
  //  m_alphaDiagWeight[a_datInd], m_alpha, m_beta, false, s_ivDebug,
  //  EBCellFAB::s_verbose);
  m_opEBStencil[a_datInd]->apply(a_lhs, a_phi, m_alphaDiagWeight[a_datInd], m_alpha, m_beta, false);

  const Real factor = m_beta/m_dx; //beta and bcoef handled within applyEBFlux
  m_ebBC->applyEBFlux(a_lhs, a_phi, m_vofIterIrreg[a_datInd], (*m_eblg.getCFIVS()),
		      a_datInd, RealVect::Zero, vectDx, factor,
		      a_homogeneous, 0.0);
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      int comp = 0;
      for (m_vofIterDomLo[idir][a_datInd].reset(); m_vofIterDomLo[idir][a_datInd].ok();  ++m_vofIterDomLo[idir][a_datInd])
	{
	  Real flux;
	  const VolIndex& vof = m_vofIterDomLo[idir][a_datInd]();
	  m_domainBC->getFaceFlux(flux,vof,comp,a_phi,
				  RealVect::Zero,vectDx,idir,Side::Lo, a_datInd, 0.0,
				  a_homogeneous);
	  //area gets multiplied in by bc operator
	  a_lhs(vof,comp) -= flux*m_beta/m_dx;
	}
      for (m_vofIterDomHi[idir][a_datInd].reset(); m_vofIterDomHi[idir][a_datInd].ok();  ++m_vofIterDomHi[idir][a_datInd])
	{
	  Real flux;
	  const VolIndex& vof = m_vofIterDomHi[idir][a_datInd]();
	  m_domainBC->getFaceFlux(flux,vof,comp,a_phi,
				  RealVect::Zero,vectDx,idir,Side::Hi,a_datInd,0.0,
				  a_homogeneous);
	  //area gets multiplied in by bc operator
	  a_lhs(vof,comp) += flux*m_beta/m_dx;
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyOpNoBoundary(LevelData<EBCellFAB>&        a_opPhi,
		  const LevelData<EBCellFAB>&  a_phi)
{
  s_turnOffBCs = true;
  applyOp(a_opPhi, a_phi, true);
  s_turnOffBCs = false;
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
void
EbHelmholtzOp::
create(LevelData<EBCellFAB>&       a_lhs,
       const LevelData<EBCellFAB>& a_rhs)
{
  CH_TIME("ebco::create");
  int ncomp = a_rhs.nComp();
  EBCellFactory ebcellfact(m_eblg.getEBISL());
  a_lhs.define(m_eblg.getDBL(), ncomp, a_rhs.ghostVect(), ebcellfact);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
createCoarsened(LevelData<EBCellFAB>&       a_lhs,
		const LevelData<EBCellFAB>& a_rhs,
		const int &                 a_refRat)
{
  CH_TIME("ebco::createCoar");
  int ncomp = a_rhs.nComp();
  IntVect ghostVect = a_rhs.ghostVect();

  CH_assert(m_eblg.getDBL().coarsenable(a_refRat));

  //fill ebislayout
  DisjointBoxLayout dblCoarsenedFine;
  coarsen(dblCoarsenedFine, m_eblg.getDBL(), a_refRat);

  EBISLayout ebislCoarsenedFine;
  IntVect ghostVec = a_rhs.ghostVect();

  ProblemDomain coarDom = coarsen(m_eblg.getDomain(), a_refRat);
  m_eblg.getEBIS()->fillEBISLayout(ebislCoarsenedFine, dblCoarsenedFine, coarDom , ghostVec[0]);
  if (m_refToCoar > 1)
    {
      ebislCoarsenedFine.setMaxRefinementRatio(m_refToCoar, m_eblg.getEBIS());
    }

  //create coarsened data
  EBCellFactory ebcellfactCoarsenedFine(ebislCoarsenedFine);
  a_lhs.define(dblCoarsenedFine, ncomp,ghostVec, ebcellfactCoarsenedFine);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
assign(LevelData<EBCellFAB>&       a_lhs,
       const LevelData<EBCellFAB>& a_rhs)
{
  CH_TIME("ebco::assign");
  EBLevelDataOps::assign(a_lhs,a_rhs);
}
//-----------------------------------------------------------------------
Real
EbHelmholtzOp::
dotProduct(const LevelData<EBCellFAB>& a_1,
	   const LevelData<EBCellFAB>& a_2)
{
  CH_TIME("ebco::dotProd");
  ProblemDomain domain;
  Real volume;

  return EBLevelDataOps::kappaDotProduct(volume,a_1,a_2,EBLEVELDATAOPS_ALLVOFS,domain);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
incr(LevelData<EBCellFAB>&       a_lhs,
     const LevelData<EBCellFAB>& a_x,
     Real                        a_scale)
{
  CH_TIME("ebco::incr");
  EBLevelDataOps::incr(a_lhs,a_x,a_scale);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
axby(LevelData<EBCellFAB>&       a_lhs,
     const LevelData<EBCellFAB>& a_x,
     const LevelData<EBCellFAB>& a_y,
     Real                        a_a,
     Real                        a_b)
{
  CH_TIME("ebco::axby");
  EBLevelDataOps::axby(a_lhs,a_x,a_y,a_a,a_b);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
scale(LevelData<EBCellFAB>& a_lhs,
      const Real&           a_scale)
{
  CH_TIME("ebco::scale");
  EBLevelDataOps::scale(a_lhs,a_scale);
}
//-------------------------------
Real 
EbHelmholtzOp::
norm(const LevelData<EBCellFAB>& a_rhs,
     int                         a_ord)
{
  CH_TIMERS("EbHelmholtzOp::norm");
  CH_TIMER("mpi_allreduce",t1);

  Real maxNorm = 0.0;

  maxNorm = localMaxNorm(a_rhs);

  CH_START(t1);
#ifdef CH_MPI
  Real tmp = 1.;
  int result = MPI_Allreduce(&maxNorm, &tmp, 1, MPI_CH_REAL,
			     MPI_MAX, Chombo_MPI::comm);
  if (result != MPI_SUCCESS)
    { //bark!!!
      MayDay::Error("sorry, but I had a communcation error on norm");
    }
  maxNorm = tmp;
#endif
  //  Real volume=1.;
  //  EBLevelDataOps::gatherBroadCast(maxNorm, volume, 0);
  CH_STOP(t1);

  return maxNorm;
}

Real 
EbHelmholtzOp::
localMaxNorm(const LevelData<EBCellFAB>& a_rhs)
{
  CH_TIME("EBAMRPoissonOp::localMaxNorm");
  return  EBAMRPoissonOp::staticMaxNorm(a_rhs, m_eblg);
  //  ProblemDomain domain;
  //  Real volume;
  //  return EBLevelDataOps::kappaNorm(volume,a_rhs,EBLEVELDATAOPS_ALLVOFS,domain,0);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
setToZero(LevelData<EBCellFAB>& a_lhs)
{
  CH_TIME("ebco::setToZero");
  EBLevelDataOps::setToZero(a_lhs);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
setVal(LevelData<EBCellFAB>& a_lhs, const Real& a_value)
{
  CH_TIME("ebco::setVal");
  EBLevelDataOps::setVal(a_lhs, a_value);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
createCoarser(LevelData<EBCellFAB>&       a_coar,
	      const LevelData<EBCellFAB>& a_fine,
	      bool                        a_ghosted)
{
  CH_TIME("ebco::createCoarser");
  CH_assert(a_fine.nComp() == 1);
  const DisjointBoxLayout& dbl = m_eblgCoarMG.getDBL();
  ProblemDomain coarDom = coarsen(m_eblg.getDomain(), 2);

  int nghost = a_fine.ghostVect()[0];
  EBISLayout coarEBISL;

  const EBIndexSpace* const ebisPtr = m_eblg.getEBIS();
  ebisPtr->fillEBISLayout(coarEBISL,
			  dbl, coarDom, nghost);

  EBCellFactory ebcellfact(coarEBISL);
  a_coar.define(dbl, 1,a_fine.ghostVect(),ebcellfact);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
relax(LevelData<EBCellFAB>&       a_phi,
      const LevelData<EBCellFAB>& a_rhs,
      int                         a_iterations)
{
  CH_TIME("ebco::relax");
  if (m_relaxType == 0)
    {
      relaxPoiJac(a_phi, a_rhs, a_iterations);
    }
  else if (m_relaxType == 1)
    {
      relaxGauSai(a_phi, a_rhs, a_iterations);
    }
  else if (m_relaxType == 2)
    {
      relaxGSRBFast(a_phi, a_rhs, a_iterations);
    }
  else
    {
      MayDay::Error("EbHelmholtzOp::bogus relaxtype");
    }
}

void EbHelmholtzOp::relax_mf(LevelData<EBCellFAB>& a_phi, const LevelData<EBCellFAB>& a_rhs, const int a_iterations){
  CH_TIME("EbHelmholtzOp::relax_mf");

  const bool homogeneous       = true;
  const int ncomps             = a_phi.nComp();
  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();
  

  // Apply domain flux
  for (DataIterator dit = a_phi.dataIterator(); dit.ok(); ++dit){

    Box dblbox(m_eblg.getDBL().get(dit()));
    BaseFab<Real>& phifab = a_phi[dit()].getSingleValuedFAB();
    Box lo_box[SpaceDim], hi_box[SpaceDim];
    int has_lo[SpaceDim], has_hi[SpaceDim];
    
    // Apply the domain flux to phi
    this->applyDomainFlux(lo_box, hi_box, has_lo, has_hi, dblbox, ncomps, phifab, homogeneous, dit());
  }

  // Red-black iteration
  for (int redBlack = 0; redBlack <= 1; redBlack++){
    a_phi.exchange();

    if(m_hasCoar){
      this->applyCFBCs(a_phi, NULL, true);
    }

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      EBCellFAB& phi       = a_phi[dit()];
      const EBCellFAB& rhs = a_rhs[dit()];
    }
  }
  MayDay::Abort("EbHelmholtzOp::relax_mf - stop here");
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
relaxGSRBFast(LevelData<EBCellFAB>&       a_phi,
	      const LevelData<EBCellFAB>& a_rhs,
	      int                         a_iterations)
{
  CH_TIME("EbHelmholtzOp::relaxGSRBFast");

  CH_assert(a_phi.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_phi.nComp() == 1);
  CH_assert(a_rhs.nComp() == 1);


  for (int whichIter =0; whichIter < a_iterations; whichIter++)
    {

      //this is a multigrid operator so only homogeneous CF BC and null coar level
      CH_assert(a_rhs.ghostVect()    == m_ghostCellsRHS);
      CH_assert(a_phi.ghostVect()    == m_ghostCellsPhi);

      const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();

      int nComps = a_phi.nComp();
      int ibox = 0;

      DataIterator dit = a_phi.dataIterator(); 
      int nbox=dit.size();
#pragma omp parallel
      {
#pragma omp for
	for (int mybox=0;mybox<nbox; mybox++)
	  {
	    Box dblBox(m_eblg.getDBL().get(dit[mybox]));
	    EBCellFAB& phi = a_phi[dit[mybox]];
	    BaseFab<Real>& phiFAB       = phi.getSingleValuedFAB();

	    Box loBox[SpaceDim],hiBox[SpaceDim];
	    int hasLo[SpaceDim],hasHi[SpaceDim];

	    {
	      //           CH_TIME("EbHelmholtzOp::levelGSRB::applyDomainFlux");
	      applyDomainFlux(loBox, hiBox, hasLo, hasHi,
			      dblBox, nComps, phiFAB,
			      true, dit[mybox]);
	    }
	    ibox++;
	  }

      }

      // do first red, then black passes
      // int id = omp_get_thread_num();
      // pout() << "my thread " << id << endl;
      for (int redBlack =0; redBlack <= 1; redBlack++)
	{
	  //          CH_TIME("EbHelmholtzOp::levelGSRB::Compute");
            
	  a_phi.exchange();
            
	  if (m_hasCoar)
	    {
	      //              CH_TIME("EbHelmholtzOp::levelGSRB::homogeneousCFInterp");
	      applyCFBCs(a_phi, NULL, true);
	    }
	  ibox = 0;
#pragma omp parallel
	  {
#pragma omp for
	    for (int mybox=0;mybox<nbox; mybox++)
	      {
		EBCellFAB& phifab = a_phi[dit[mybox]];
		const EBCellFAB& rhsfab = a_rhs[dit[mybox]];
                
		//cache phi
		for (int c = 0; c < m_colors.size()/2; ++c)
		  {
		    m_colorEBStencil[m_colors.size()/2*redBlack+c][dit[mybox]]->cachePhi(phifab);
		  }
                
		//reg cells
		const Box& region = dbl.get(dit[mybox]);
		Box dblBox(m_eblg.getDBL().get(dit[mybox]));
		//dummy has to be real because basefab::dataPtr is retarded
		BaseFab<Real> dummy(Box(IntVect::Zero, IntVect::Zero), 1);
                
		BaseFab<Real>      & reguPhi =      (a_phi[dit[mybox]]).getSingleValuedFAB();
		const BaseFab<Real>& reguRHS =     (a_rhs[dit[mybox]] ).getSingleValuedFAB();
		const BaseFab<Real>& relCoef = (m_relCoef[dit[mybox]] ).getSingleValuedFAB();
		const BaseFab<Real>& regACoe =((*m_aCoefficient)[dit[mybox]] ).getSingleValuedFAB();
		const BaseFab<Real>* regBCoe[3];
		//need three coeffs because this has to work in 3d
		//this is my klunky way to make the call dimension-independent
		for (int iloc = 0; iloc < 3; iloc++)
		  {
		    if (iloc >= SpaceDim)
		      {
			regBCoe[iloc]= &dummy;
		      }
		    else
		      {
			regBCoe[iloc] = &((*m_bcoef)[dit[mybox]][iloc].getSingleValuedFAB());
		      }
		  }
                
                
		for (int comp = 0; comp < a_phi.nComp(); comp++)
		  {
		    FORT_CONDUCTIVITYGSRB(CHF_FRA1(        reguPhi,    comp),
					  CHF_CONST_FRA1(  reguRHS,    comp),
					  CHF_CONST_FRA1(  relCoef,    comp),
					  CHF_CONST_FRA1(  regACoe,    comp),
					  CHF_CONST_FRA1((*regBCoe[0]),comp),
					  CHF_CONST_FRA1((*regBCoe[1]),comp),
					  CHF_CONST_FRA1((*regBCoe[2]),comp),
					  CHF_CONST_REAL(m_alpha),
					  CHF_CONST_REAL(m_beta),
					  CHF_CONST_REAL(m_dx),
					  CHF_BOX(region),
					  CHF_CONST_INT(redBlack));
		  }

		//uncache phi
		for (int c = 0; c < m_colors.size()/2; ++c)
		  {
		    m_colorEBStencil[m_colors.size()/2*redBlack+c][dit[mybox]]->uncachePhi(phifab);
		  }
                
		for (int c = 0; c < m_colors.size()/2; ++c)
		  {
		    GSColorAllIrregular(phifab, rhsfab, m_colors.size()/2*redBlack+c, dit[mybox]);
		  }
		ibox++;
	      }
	  }
	} // end pragma
    } //end red black


}//end loop over iterations
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
GSColorAllIrregular(EBCellFAB&                   a_phi,
		    const EBCellFAB&             a_rhs,
		    const int&                   a_icolor,
		    const DataIndex&             a_dit)
{
  CH_TIME("EBConductivyOp::GSColorAllIrregular");

  int comp = 0;
  Real time = 0;
  RealVect vdx = m_dx*RealVect::Unit;
  const BaseIVFAB<Real>& curAlphaWeight = m_alphaDiagWeight[a_dit];
  const BaseIVFAB<Real>& curBetaWeight  = m_betaDiagWeight[a_dit];
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      CH_TIME("domain cross eb stuff cache");
      for (m_vofItIrregColorDomLo[a_icolor][idir][a_dit].reset(); m_vofItIrregColorDomLo[a_icolor][idir][a_dit].ok();  ++m_vofItIrregColorDomLo[a_icolor][idir][a_dit])
	{
	  const VolIndex& vof = m_vofItIrregColorDomLo[a_icolor][idir][a_dit]();
	  Real flux;
	  m_domainBC->getFaceFlux(flux,vof,comp,a_phi,
				  RealVect::Zero,vdx,idir,Side::Lo, a_dit, time, true);

	  m_cacheEBxDomainFluxLo[a_icolor][idir][a_dit](vof, comp) = flux;
	}

      for (m_vofItIrregColorDomHi[a_icolor][idir][a_dit].reset(); m_vofItIrregColorDomHi[a_icolor][idir][a_dit].ok();  ++m_vofItIrregColorDomHi[a_icolor][idir][a_dit])
	{
	  const VolIndex& vof = m_vofItIrregColorDomHi[a_icolor][idir][a_dit]();
	  Real flux;
	  m_domainBC->getFaceFlux(flux,vof,comp,a_phi,
				  RealVect::Zero,vdx,idir,Side::Hi,a_dit,time, true);

	  m_cacheEBxDomainFluxHi[a_icolor][idir][a_dit](vof, comp) = flux;
	}
    }
  {
    CH_TIME("color ebstencil bit");
    //phi = (I-lambda*L)phiOld
    Real safety = getSafety();
    m_colorEBStencil[a_icolor][a_dit]->relax(a_phi, a_rhs, curAlphaWeight, curBetaWeight, m_alpha, m_beta, safety);
  }


  //apply domain bcs to (I-lambda*L)phi (already done in colorStencil += fluxStencil, and hom only here))
  //apply domain bcs to (I-lambda*L)phi
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      CH_TIME("domain cross eb stuff uncache");
      for (m_vofItIrregColorDomLo[a_icolor][idir][a_dit].reset(); m_vofItIrregColorDomLo[a_icolor][idir][a_dit].ok();  ++m_vofItIrregColorDomLo[a_icolor][idir][a_dit])
	{
	  const VolIndex& vof = m_vofItIrregColorDomLo[a_icolor][idir][a_dit]();
	  Real weightIrreg = m_alpha*curAlphaWeight(vof,0) + m_beta*curBetaWeight(vof,0);
	  Real lambda = 0.0;
	  if (Abs(weightIrreg) > 1.e-12)
	    {
	      lambda = 1./weightIrreg;
	    }
	  a_phi(vof,comp) += lambda * m_cacheEBxDomainFluxLo[a_icolor][idir][a_dit](vof, comp) * m_beta/m_dx;
	}

      for (m_vofItIrregColorDomHi[a_icolor][idir][a_dit].reset(); m_vofItIrregColorDomHi[a_icolor][idir][a_dit].ok();  ++m_vofItIrregColorDomHi[a_icolor][idir][a_dit])
	{
	  const VolIndex& vof = m_vofItIrregColorDomHi[a_icolor][idir][a_dit]();
	  Real weightIrreg = m_alpha*curAlphaWeight(vof,0) + m_beta*curBetaWeight(vof,0);
	  Real lambda = 0.0;
	  if (Abs(weightIrreg) > 1.e-12)
	    {
	      lambda = 1./weightIrreg;
	    }
	  a_phi(vof,comp) -= lambda * m_cacheEBxDomainFluxHi[a_icolor][idir][a_dit](vof, comp) * m_beta/m_dx;
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
relaxGauSai(LevelData<EBCellFAB>&       a_phi,
	    const LevelData<EBCellFAB>& a_rhs,
	    int                         a_iterations)
{
  CH_TIME("EbHelmholtzOp::relaxGauSai");

  CH_assert(a_phi.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);

  CH_assert(a_phi.nComp() == 1);
  CH_assert(a_rhs.nComp() == 1);

  LevelData<EBCellFAB> lphi;
  create(lphi, a_rhs);
  for (int whichIter =0; whichIter < a_iterations; whichIter++)
    {
      for (int icolor = 0; icolor < m_colors.size(); icolor++)
	{
	  applyHomogeneousCFBCs(a_phi);



	  //after this lphi = L(phi)
	  //this call contains bcs and exchange
	  applyOp(  lphi,  a_phi, true);
	  gsrbColor(a_phi, lphi, a_rhs, m_colors[icolor]);
	}
    }
}

void EbHelmholtzOp::lazyGauSai(LevelData<EBCellFAB>&       a_phi,
			       const LevelData<EBCellFAB>& a_rhs){
  CH_TIME("EbHelmholtzOp::lazyGauSai");

  CH_assert(a_phi.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);

  CH_assert(a_phi.nComp() == 1);
  CH_assert(a_rhs.nComp() == 1);

  LevelData<EBCellFAB> lphi;
  create(lphi, a_rhs);

  for (int icolor = 0; icolor < m_colors.size(); icolor++){
    applyHomogeneousCFBCs(a_phi);


    //after this lphi = L(phi)
    applyOp(lphi, a_phi, NULL, true, true, false);
    gsrbColor(a_phi, lphi, a_rhs, m_colors[icolor]);

    if((icolor-1) % 2 == 0 && icolor - 1 < m_colors.size()){
      a_phi.exchange();
    }
  }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
relaxPoiJac(LevelData<EBCellFAB>&       a_phi,
	    const LevelData<EBCellFAB>& a_rhs,
	    int                         a_iterations)
{
  CH_TIME("EbHelmholtzOp::relaxPoiJac");

  CH_assert(a_phi.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);

  CH_assert(a_phi.nComp() == 1);
  CH_assert(a_rhs.nComp() == 1);

  LevelData<EBCellFAB> lphi;
  create(lphi, a_rhs);
  for (int whichIter =0; whichIter < a_iterations; whichIter++)
    {
      applyHomogeneousCFBCs(a_phi);

      //after this lphi = L(phi)
      //this call contains bcs and exchange
      applyOp(  lphi,  a_phi, true);

      DataIterator dit = m_eblg.getDBL().dataIterator(); 
      int nbox = dit.size();
#pragma omp parallel for
      for(int mybox=0; mybox<nbox; mybox++)
	{

	  lphi[dit[mybox]] -=     a_rhs[dit[mybox]];
	  lphi[dit[mybox]] *= m_relCoef[dit[mybox]];
	  //this is a safety factor because pt jacobi needs a smaller
	  //relaxation param
	  lphi[dit[mybox]] *= -0.5;
	  a_phi[dit[mybox]] += lphi[dit[mybox]];
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
gsrbColor(LevelData<EBCellFAB>&       a_phi,
	  const LevelData<EBCellFAB>& a_lph,
	  const LevelData<EBCellFAB>& a_rhs,
	  const IntVect&              a_color)
{
  CH_TIME("EbHelmholtzOp::gsrbColor");

  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();

  DataIterator dit = dbl.dataIterator(); 
  int nbox=dit.size();
#pragma omp parallel
  {
#pragma omp for
    for( int mybox=0; mybox<nbox; mybox++)
      {
	const EBISBox& ebisbox = a_phi[dit[mybox]].getEBISBox();
	Box dblBox  = dbl.get(dit[mybox]);
	BaseFab<Real>&       regPhi =     a_phi[dit[mybox]].getSingleValuedFAB();
	const BaseFab<Real>& regLph =     a_lph[dit[mybox]].getSingleValuedFAB();
	const BaseFab<Real>& regRhs =     a_rhs[dit[mybox]].getSingleValuedFAB();
	IntVect loIV = dblBox.smallEnd();
	IntVect hiIV = dblBox.bigEnd();
            
	for (int idir = 0; idir < SpaceDim; idir++)
	  {
	    if (loIV[idir] % 2 != a_color[idir])
	      {
		loIV[idir]++;
	      }
	  }
            
	const BaseFab<Real>& regRel = m_relCoef[dit[mybox]].getSingleValuedFAB();
	if (loIV <= hiIV)
	  {
	    Box coloredBox(loIV, hiIV);
	    FORT_GSRBEBCO(CHF_FRA1(regPhi,0),
			  CHF_CONST_FRA1(regLph,0),
			  CHF_CONST_FRA1(regRhs,0),
			  CHF_CONST_FRA1(regRel,0),
			  CHF_BOX(coloredBox));
	  }
            
	for (m_vofIterMulti[dit[mybox]].reset(); m_vofIterMulti[dit[mybox]].ok(); ++m_vofIterMulti[dit[mybox]])
	  {
	    const VolIndex& vof = m_vofIterMulti[dit[mybox]]();
	    const IntVect& iv = vof.gridIndex();
                
	    bool doThisVoF = true;
	    for (int idir = 0; idir < SpaceDim; idir++)
	      {
		if (iv[idir] % 2 != a_color[idir])
		  {
		    doThisVoF = false;
		    break;
		  }
	      }
                
	    if (doThisVoF)
	      {
		Real lph    = a_lph[dit[mybox]](vof, 0);
		Real rhs    = a_rhs[dit[mybox]](vof, 0);
		Real resid  = rhs - lph;
		Real lambda = m_relCoef[dit[mybox]](vof, 0);
		a_phi[dit[mybox]](vof, 0) += lambda*resid;
	      }
	  }
      }
  }// end pragma
}

void
EbHelmholtzOp::nwo_gsrbColor(LevelData<EBCellFAB>&       a_phi,
			     const LevelData<EBCellFAB>& a_lph,
			     const LevelData<EBCellFAB>& a_rhs,
			     const IntVect&              a_color){
  CH_TIME("EbHelmholtzOp::nwo_gsrbColor");

  const DisjointBoxLayout& dbl = a_phi.disjointBoxLayout();
  
  for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
    this->gsrbColor(a_phi[dit()], a_lph[dit()], a_rhs[dit()], dbl.get(dit()), dit(), a_color);
  }

}

void EbHelmholtzOp::gsrbColor(EBCellFAB&       a_phi,
			      const EBCellFAB& a_lph,
			      const EBCellFAB& a_rhs,
			      const Box&       a_box,
			      const DataIndex& a_dit,
			      const IntVect&   a_color){
  CH_TIME("EbHelmholtzOp::gsrbColor (ebcellfabs)");

  const EBISBox& ebisbox = a_phi.getEBISBox();
  Box dblBox  = a_box;
  BaseFab<Real>&       regPhi =     a_phi.getSingleValuedFAB();
  const BaseFab<Real>& regLph =     a_lph.getSingleValuedFAB();
  const BaseFab<Real>& regRhs =     a_rhs.getSingleValuedFAB();
  IntVect loIV = dblBox.smallEnd();
  IntVect hiIV = dblBox.bigEnd();
            
  for (int idir = 0; idir < SpaceDim; idir++)
    {
      if (loIV[idir] % 2 != a_color[idir])
	{
	  loIV[idir]++;
	}
    }
            
  const BaseFab<Real>& regRel = m_relCoef[a_dit].getSingleValuedFAB();
  if (loIV <= hiIV)
    {
      Box coloredBox(loIV, hiIV);
      FORT_GSRBEBCO(CHF_FRA1(regPhi,0),
		    CHF_CONST_FRA1(regLph,0),
		    CHF_CONST_FRA1(regRhs,0),
		    CHF_CONST_FRA1(regRel,0),
		    CHF_BOX(coloredBox));
    }
            
  for (m_vofIterMulti[a_dit].reset(); m_vofIterMulti[a_dit].ok(); ++m_vofIterMulti[a_dit])
    {
      const VolIndex& vof = m_vofIterMulti[a_dit]();
      const IntVect& iv = vof.gridIndex();
                
      bool doThisVoF = true;
      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  if (iv[idir] % 2 != a_color[idir])
	    {
	      doThisVoF = false;
	      break;
	    }
	}
                
      if (doThisVoF)
	{
	  Real lph    = a_lph(vof, 0);
	  Real rhs    = a_rhs(vof, 0);
	  Real resid  = rhs - lph;
	  Real lambda = m_relCoef[a_dit](vof, 0);
	  a_phi(vof, 0) += lambda*resid;
	}
    }
}

//-----------------------------------------------------------------------
void EbHelmholtzOp::
restrictResidual(LevelData<EBCellFAB>&       a_resCoar,
		 LevelData<EBCellFAB>&       a_phiThisLevel,
		 const LevelData<EBCellFAB>& a_rhsThisLevel)
{
  CH_TIME("EbHelmholtzOp::restrictResidual");

  CH_assert(a_resCoar.nComp() == 1);
  CH_assert(a_phiThisLevel.nComp() == 1);
  CH_assert(a_rhsThisLevel.nComp() == 1);

  LevelData<EBCellFAB> resThisLevel;
  bool homogeneous = true;

  EBCellFactory ebcellfactTL(m_eblg.getEBISL());
  IntVect ghostVec = a_rhsThisLevel.ghostVect();

  resThisLevel.define(m_eblg.getDBL(), 1, ghostVec, ebcellfactTL);

  // Get the residual on the fine grid
  residual(resThisLevel,a_phiThisLevel,a_rhsThisLevel,homogeneous);

  // now use our nifty averaging operator
  Interval variables(0, 0);
  if (m_layoutChanged)
    {
      m_ebAverageMG.average(a_resCoar, resThisLevel, variables);
    }
  else
    {
      m_ebAverageMG.averageMG(a_resCoar, resThisLevel, variables);
    }
}
//-----------------------------------------------------------------------
void EbHelmholtzOp::
prolongIncrement(LevelData<EBCellFAB>&       a_phiThisLevel,
		 const LevelData<EBCellFAB>& a_correctCoar)
{
  CH_TIME("EbHelmholtzOp::prolongIncrement");
  Interval vars(0, 0);
  if (m_layoutChanged)
    {
      m_ebInterpMG.pwcInterp(a_phiThisLevel, a_correctCoar, vars);
    }
  else
    {
      m_ebInterpMG.pwcInterpMG(a_phiThisLevel, a_correctCoar, vars);
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyCFBCs(LevelData<EBCellFAB>&             a_phi,
	   const LevelData<EBCellFAB>* const a_phiCoar,
	   bool a_homogeneousCFBC)
{
  CH_TIMERS("EbHelmholtzOp::applyCFBCs");
  CH_TIMER("inhomogeneous_cfbcs_define",t1);
  CH_TIMER("inhomogeneous_cfbcs_execute",t3);
  CH_TIMER("homogeneous_cfbs",t2);
  CH_assert(a_phi.nComp() == 1);

  if (m_hasCoar)
    {
      if (!a_homogeneousCFBC)
	{
	  CH_START(t1);
	  if (a_phiCoar==NULL)
	    {
	      MayDay::Error("cannot enforce inhomogeneous CFBCs with NULL coar");
	    }
	  //define coarse fine interpolation object on the fly
	  //because most operators do not need it
	  CH_assert(a_phiCoar->nComp() == 1);
	  CH_STOP(t1);

	  CH_START(t3);
	  Interval interv(0,0);
	  m_quadCFIWithCoar->interpolate(a_phi, *a_phiCoar, interv);
	  //          dumpEBLevelGhost(&a_phi);
	  CH_STOP(t3);

	}
      else
	{
	  CH_START(t2);
	  applyHomogeneousCFBCs(a_phi);
	  CH_STOP(t2);
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyHomogeneousCFBCs(LevelData<EBCellFAB>&   a_phi)
{
  CH_TIME("EbHelmholtzOp::applyHomogeneousCFBCs");
  CH_assert(a_phi.nComp() == 1);
  CH_assert( a_phi.ghostVect() >= IntVect::Unit);
  DataIterator dit = m_eblg.getDBL().dataIterator(); 
  int nbox = dit.size();
#pragma omp parallel for
  for(int mybox=0; mybox<nbox; mybox++)
    {

      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  for (SideIterator sit; sit.ok(); sit.next())
	    {
	      applyHomogeneousCFBCs(a_phi[dit[mybox]],dit[mybox],idir,sit());
	    }
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
applyHomogeneousCFBCs(EBCellFAB&            a_phi,
		      const DataIndex&      a_datInd,
		      int                   a_idir,
		      Side::LoHiSide        a_hiorlo)
{
  if (m_hasCoar)
    {
      CH_TIMERS("EbHelmholtzOp::applyHomogeneousCFBCs2");
      CH_TIMER("packed_applyHomogeneousCFBCs",t1);
      CH_TIMER("unpacked_applyHomogeneousCFBCs",t2);
      CH_assert((a_idir >= 0) && (a_idir  < SpaceDim));
      CH_assert((a_hiorlo == Side::Lo )||(a_hiorlo == Side::Hi ));
      CH_assert(a_phi.nComp() == 1);
      int ivar = 0;

      const CFIVS* cfivsPtr = NULL;

      if (a_hiorlo == Side::Lo)
	{
	  cfivsPtr = &m_loCFIVS[a_idir][a_datInd];
	}
      else
	{
	  cfivsPtr = &m_hiCFIVS[a_idir][a_datInd];
	}

      const IntVectSet& interpIVS = cfivsPtr->getFineIVS();
      if (cfivsPtr->isPacked() )
	{
	  CH_START(t1);
	  const int ihiorlo = sign(a_hiorlo);
	  FORT_INTERPHOMO(CHF_FRA(a_phi.getSingleValuedFAB()),
			  CHF_BOX(cfivsPtr->packedBox()),
			  CHF_CONST_REAL(m_dx),
			  CHF_CONST_REAL(m_dxCoar),
			  CHF_CONST_INT(a_idir),
			  CHF_CONST_INT(ihiorlo));

	  CH_STOP(t1);
	}
      else
	{
	  if (!interpIVS.isEmpty())
	    {
	      CH_START(t2);
	      Real halfdxcoar = m_dxCoar/2.0;
	      Real halfdxfine = m_dx/2.0;
	      Real xg = halfdxcoar -   halfdxfine;
	      Real xc = halfdxcoar +   halfdxfine;
	      Real xf = halfdxcoar + 3*halfdxfine;
	      Real hf = m_dx;
	      Real denom = xf*xc*hf;

	      const EBISBox&  ebisBox = m_eblg.getEBISL()[a_datInd];
	      const EBGraph&  ebgraph = m_eblg.getEBISL()[a_datInd].getEBGraph();
	      for (VoFIterator vofit(interpIVS, ebgraph); vofit.ok(); ++vofit)
		{
		  const VolIndex& VoFGhost = vofit();

		  IntVect ivGhost  = VoFGhost.gridIndex();
		  IntVect ivClose =  ivGhost;
		  IntVect ivFar   =  ivGhost;

		  Vector<VolIndex> farVoFs;
		  Vector<VolIndex> closeVoFs = ebisBox.getVoFs(VoFGhost,
							       a_idir,
							       flip(a_hiorlo),
							       1);
		  bool hasClose = (closeVoFs.size() > 0);
		  bool hasFar = false;
		  Real phic = 0.0;
		  Real phif = 0.0;
		  if (hasClose)
		    {
		      const int& numClose = closeVoFs.size();
		      for (int iVof=0;iVof<numClose;iVof++)
			{
			  const VolIndex& vofClose = closeVoFs[iVof];
			  phic += a_phi(vofClose,0);
			}
		      phic /= Real(numClose);

		      farVoFs = ebisBox.getVoFs(VoFGhost,
						a_idir,
						flip(a_hiorlo),
						2);
		      hasFar   = (farVoFs.size()   > 0);
		      if (hasFar)
			{
			  const int& numFar = farVoFs.size();
			  for (int iVof=0;iVof<numFar;iVof++)
			    {
			      const VolIndex& vofFar = farVoFs[iVof];
			      phif += a_phi(vofFar,0);
			    }
			  phif /= Real(numFar);
			}
		    }

		  Real phiGhost;
		  if (hasClose && hasFar)
		    {
		      // quadratic interpolation  phi = ax^2 + bx + c
		      Real A = (phif*xc - phic*xf)/denom;
		      Real B = (phic*hf*xf - phif*xc*xc + phic*xf*xc)/denom;

		      phiGhost = A*xg*xg + B*xg;
		    }
		  else if (hasClose)
		    {
		      //linear interpolation
		      Real slope =  phic/xc;
		      phiGhost   =  slope*xg;
		    }
		  else
		    {
		      phiGhost = 0.0; //nothing to interpolate from
		    }
		  a_phi(VoFGhost, ivar) = phiGhost;
		}
	      CH_STOP(t2);
	    }
	}
    }
}
//-----------------------------------------------------------------------
int EbHelmholtzOp::
refToCoarser()
{
  return m_refToCoar;
}
//-----------------------------------------------------------------------
int EbHelmholtzOp::
refToFiner()
{
  return m_refToFine;
}
//-----------------------------------------------------------------------
void EbHelmholtzOp::
AMRResidual(LevelData<EBCellFAB>&       a_residual,
	    const LevelData<EBCellFAB>& a_phiFine,
	    const LevelData<EBCellFAB>& a_phi,
	    const LevelData<EBCellFAB>& a_phiCoar,
	    const LevelData<EBCellFAB>& a_rhs,
	    bool a_homogeneousPhysBC,
	    AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp)
{
  CH_TIMERS("EbHelmholtzOp::AMRResidual");
  CH_TIMER("AMROperator", t1);
  CH_TIMER("axby", t2);
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_residual.nComp() == 1);
  CH_assert(a_phi.nComp() == 1);
  CH_assert(a_rhs.nComp() == 1);

  CH_START(t1);
  AMROperator(a_residual, a_phiFine, a_phi, a_phiCoar,
	      a_homogeneousPhysBC, a_finerOp);
  CH_STOP(t1);

  //  dumpLevelPoint(a_residual, string("EbHelmholtzOp: AMRResidual: lphi = "));
  //  dumpLevelPoint(a_rhs,      string("EbHelmholtzOp: AMRResidual: rhs = "));
  //multiply by -1 so a_residual now holds -L(phi)
  //add in rhs so a_residual = rhs - L(phi)
  CH_START(t2);
  axby(a_residual,a_residual,a_rhs,-1.0, 1.0);
  CH_STOP(t2);
  //  dumpLevelPoint(a_residual, string("EbHelmholtzOp: AMRResidual: resid = "));
}

//-----------------------------------------------------------------------
void EbHelmholtzOp::
AMROperator(LevelData<EBCellFAB>&       a_LofPhi,
	    const LevelData<EBCellFAB>& a_phiFine,
	    const LevelData<EBCellFAB>& a_phi,
	    const LevelData<EBCellFAB>& a_phiCoar,
	    bool a_homogeneousPhysBC,
	    AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp)
{
  CH_TIMERS("EbHelmholtzOp::AMROperator");
  CH_TIMER("applyOp", t1);
  CH_TIMER("reflux", t2);
  CH_assert(a_LofPhi.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_LofPhi.nComp() == 1);
  CH_assert(a_phi.nComp() == 1);

  //apply the operator between this and the next coarser level.
  CH_START(t1);

#if verb
  pout() << "EbHelmholtzOp::amroperator - begin applyOp" << endl;
#endif
  applyOp(a_LofPhi, a_phi, &a_phiCoar,  a_homogeneousPhysBC, false);
#if verb
  pout() << "EbHelmholtzOp::amroperator - end applyOp" << endl;
#endif
  CH_STOP(t1);

  //now reflux to enforce flux-matching from finer levels
  if (m_hasFine)
    {
      CH_assert(a_finerOp != NULL);
      CH_START(t2);
#if verb
      pout() << "EbHelmholtzOp::amroperator - begin reflux" << endl;
#endif
      reflux(a_LofPhi, a_phiFine, a_phi, a_finerOp);
#if verb
      pout() << "EbHelmholtzOp::amroperator - end reflux" << endl;
#endif

      CH_STOP(t2);
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
reflux(LevelData<EBCellFAB>& a_residual,
       const LevelData<EBCellFAB>& a_phiFine,
       const LevelData<EBCellFAB>& a_phi,
       AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp)
{
  CH_TIMERS("EbHelmholtzOp::fastReflux");
  CH_TIMER("setToZero",t2);
  CH_TIMER("incrementCoar",t3);
  CH_TIMER("incrementFine",t4);
  CH_TIMER("reflux_from_reg",t5);
  Interval interv(0,0);

  CH_START(t2);
  m_fastFR->setToZero();
  CH_STOP(t2);
  CH_START(t3);
#if verb
  pout() << "EbHelmholtzOp::reflux - increment coar" << endl;
#endif
  incrementFRCoar(*m_fastFR, a_phiFine, a_phi);
#if verb
  pout() << "EbHelmholtzOp::reflux - done increment coar" << endl;
#endif
  CH_STOP(t3);

  CH_START(t4);
#if verb
  pout() << "EbHelmholtzOp::reflux - increment fine" << endl;
#endif
  incrementFRFine(*m_fastFR, a_phiFine, a_phi, a_finerOp);
#if verb
  pout() << "EbHelmholtzOp::reflux - done increment fine" << endl;
#endif
  CH_STOP(t4);
  CH_START(t5);

  Real scale = 1.0/m_dx;
#if verb
  pout() << "EbHelmholtzOp::refluxing" << endl;
#endif
  m_fastFR->reflux(a_residual, interv, scale);

#if verb
  pout() << "EbHelmholtzOp::reflux - done reflux" << endl;
#endif

  CH_STOP(t5);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
incrementFRCoar(EBFluxRegister&             a_fluxReg,
		const LevelData<EBCellFAB>& a_phiFine,
		const LevelData<EBCellFAB>& a_phi)
{
  CH_TIME("EbHelmholtzOp::incrementFRCoar");
  CH_assert(a_phiFine.nComp() == 1);
  CH_assert(a_phi.nComp() == 1);

  int ncomp = 1;
  Interval interv(0,0);

  DataIterator dit = m_eblg.getDBL().dataIterator(); 
  int nbox = dit.size();
#pragma omp parallel for
  for(int mybox=0; mybox<nbox; mybox++)
    {

      const EBCellFAB& coarfab = a_phi[dit[mybox]];
      const EBISBox& ebisBox = m_eblg.getEBISL()[dit[mybox]];
      const Box&  box = m_eblg.getDBL().get(dit[mybox]);
      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  //no boundary faces here.

	  Box ghostedBox = box;
	  ghostedBox.grow(1);
	  ghostedBox.grow(idir,-1);
	  ghostedBox &= m_eblg.getDomain();

	  EBFaceFAB coarflux(ebisBox, ghostedBox, idir, ncomp);

	  //old way
	  //getFlux(coarflux, coarfab, ghostedBox, box, m_eblg.getDomain(), ebisBox, m_dx, dit[mybox], idir);
	  
	  // new way
	  getFluxRegOnly(coarflux, coarfab, ghostedBox, m_dx, dit[mybox], idir);
	  for (SideIterator sit; sit.ok(); ++sit)
	    {
	      Vector<FaceIndex>*  faceit;
	      Vector<VoFStencil>* stencil;
	      int index = EBFastFR::index(idir, sit());
	      if (m_hasEBCF)
		{
		  faceit  = &( m_faceitCoar[index][dit[mybox]]);
		  stencil = &(m_stencilCoar[index][dit[mybox]]);
		}
	      getFluxEBCF(coarflux, coarfab, ghostedBox, *faceit, *stencil);
	    }

	  //          dumpFlux(coarflux, idir,  string("incrementFRCoar: flux = "));
	  Real scale = 1.0; //beta and bcoef already in flux
	  for (SideIterator sit; sit.ok(); ++sit)
	    {
	      a_fluxReg.incrementCoarseBoth(coarflux, scale, dit[mybox], interv, idir, sit());
	    }
	}
    }

}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getFluxEBCF(EBFaceFAB&                    a_flux,
	    const EBCellFAB&              a_phi,
	    const Box&                    a_ghostedBox,
	    Vector<FaceIndex>&            a_faceitEBCF,
	    Vector<VoFStencil>&           a_stenEBCF)
{
  CH_TIME("EbHelmholtzOp::getFluxEBCF");

  //only do the evil stuff if you have a coarse-fine /  EB crossing situation

  if (m_hasEBCF)
    {
      CH_TIME("EBCF stuff");
      for (int iface = 0; iface < a_faceitEBCF.size(); iface++)
	{
	  const FaceIndex& face =     a_faceitEBCF[iface];
	  const VoFStencil& stencil   = a_stenEBCF[iface];
	  Real fluxval = 0;

	  for (int isten = 0; isten < stencil.size(); isten++)
	    {
	      fluxval += stencil.weight(isten)*(a_phi(stencil.vof(isten), 0));
	    }
	  //note the last minute beta
	  a_flux(face, 0) = m_beta*fluxval;
	}
    }
}

void EbHelmholtzOp::getFlux(EBFluxFAB&                    a_flux,
			    const LevelData<EBCellFAB>&   a_data,
			    const Box&                    a_grid,
			    const DataIndex&              a_dit,
			    Real                          a_scale){
  CH_TIME("ebco::getflux1");
  a_flux.define(m_eblg.getEBISL()[a_dit], a_grid, 1);
  a_flux.setVal(0.);
  for (int idir = 0; idir < SpaceDim; idir++){
    Box ghostedBox = a_grid;
    ghostedBox.grow(1);
    ghostedBox.grow(idir,-1);
    ghostedBox &= m_eblg.getDomain();

    this->getFlux(a_flux[idir], a_data[a_dit], ghostedBox, a_grid,
	    m_eblg.getDomain(), m_eblg.getEBISL()[a_dit], m_dx, a_dit, idir);
  }
}

void EbHelmholtzOp::getFlux(EBFaceFAB&                    a_fluxCentroid,
			    const EBCellFAB&              a_phi,
			    const Box&                    a_ghostedBox,
			    const Box&                    a_fabBox,
			    const ProblemDomain&          a_domain,
			    const EBISBox&                a_ebisBox,
			    const Real&                   a_dx,
			    const DataIndex&              a_datInd,
			    const int&                    a_idir){
  CH_TIME("ebco::getFlux2");
  //has some extra cells so...
  a_fluxCentroid.setVal(0.);
  int ncomp = a_phi.nComp();
  CH_assert(ncomp == a_fluxCentroid.nComp());
  Box cellBox = a_ghostedBox;
  //want only interior faces
  cellBox.grow(a_idir, 1);
  cellBox &= a_domain;
  cellBox.grow(a_idir,-1);

  Box faceBox = surroundingNodes(cellBox, a_idir);
  EBFaceFAB fluxCenter(a_ebisBox, a_ghostedBox, a_idir,1);

  //make a EBFaceFAB (including ghost cells) that will hold centered gradients
  BaseFab<Real>& regFlux  = fluxCenter.getSingleValuedFAB();
  const BaseFab<Real>& regPhi = a_phi.getSingleValuedFAB();
  const EBFaceFAB& bcoebff = (*m_bcoef)[a_datInd][a_idir];
  const FArrayBox& regBCo = (const FArrayBox&)(bcoebff.getSingleValuedFAB());

  FORT_GETFLUXEBCO(CHF_FRA1(regFlux,0),
		   CHF_CONST_FRA1(regBCo,0),
		   CHF_CONST_FRA1(regPhi, 0),
		   CHF_BOX(faceBox),
		   CHF_CONST_REAL(a_dx),
		   CHF_CONST_INT(a_idir));


  a_fluxCentroid.copy(fluxCenter);

  IntVectSet ivsCell = a_ebisBox.getIrregIVS(cellBox);
  if (!ivsCell.isEmpty()){
    FaceStop::WhichFaces stopCrit = FaceStop::SurroundingNoBoundary;

    for (FaceIterator faceit(ivsCell, a_ebisBox.getEBGraph(), a_idir,stopCrit); faceit.ok(); ++faceit) {
      const FaceIndex& face = faceit();
      Real phiHi = a_phi(face.getVoF(Side::Hi), 0);
      Real phiLo = a_phi(face.getVoF(Side::Lo), 0);
      Real fluxFace = bcoebff(face, 0)*(phiHi - phiLo)/a_dx;

      fluxCenter(face, 0) = fluxFace;
    }
    
    //interpolate from face centers to face centroids
    Box cellBox = a_fluxCentroid.getCellRegion();
    EBArith::interpolateFluxToCentroids(a_fluxCentroid,
					fluxCenter,
					a_fabBox,
					a_ebisBox,
					a_domain,
					a_idir);
  }

  a_fluxCentroid *= m_beta;
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getFluxRegOnly(EBFaceFAB&                    a_fluxCentroid,
	       const EBCellFAB&              a_phi,
	       const Box&                    a_ghostedBox,
	       const Real&                   a_dx,
	       const DataIndex&              a_datInd,
	       const int&                    a_idir)
{
  CH_TIME("ebco::getFluxRegOnly");
  const ProblemDomain& domain = m_eblg.getDomain();

  //has some extra cells so...
  a_fluxCentroid.setVal(0.);
  int ncomp = a_phi.nComp();
  CH_assert(ncomp == a_fluxCentroid.nComp());
  Box cellBox = a_ghostedBox;
  //want only interior faces
  cellBox.grow(a_idir, 1);
  cellBox &= domain;
  cellBox.grow(a_idir,-1);

  Box faceBox = surroundingNodes(cellBox, a_idir);

  //make a EBFaceFAB (including ghost cells) that will hold centered gradients
  BaseFab<Real>& regFlux = a_fluxCentroid.getSingleValuedFAB();
  const BaseFab<Real>& regPhi = a_phi.getSingleValuedFAB();
  const EBFaceFAB& bcoebff = (*m_bcoef)[a_datInd][a_idir];
  const FArrayBox& regBCo = (const FArrayBox&)(bcoebff.getSingleValuedFAB());

  FORT_GETFLUXEBCO(CHF_FRA1(regFlux,0),
		   CHF_CONST_FRA1(regBCo,0),
		   CHF_CONST_FRA1(regPhi, 0),
		   CHF_BOX(faceBox),
		   CHF_CONST_REAL(a_dx),
		   CHF_CONST_INT(a_idir));

  a_fluxCentroid *= m_beta;
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
incrementFRFine(EBFluxRegister&             a_fluxReg,
		const LevelData<EBCellFAB>& a_phiFine,
		const LevelData<EBCellFAB>& a_phi,
		AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp)
{
  CH_TIME("EbHelmholtzOp::incrementFRFine");
  CH_assert(a_phiFine.nComp() == 1);
  CH_assert(a_phi.nComp() == 1);
  CH_assert(m_hasFine);
  int ncomp = 1;
  Interval interv(0,0);
  EbHelmholtzOp& finerEBAMROp = (EbHelmholtzOp& )(*a_finerOp);

#if verb
  pout() << "EbHelmholtzOp::filling ghosts" << endl;
#endif
  //ghost cells of phiFine need to be filled
  LevelData<EBCellFAB>& phiFine = (LevelData<EBCellFAB>&) a_phiFine;
  finerEBAMROp.m_quadCFIWithCoar->interpolate(phiFine, a_phi, interv);
  phiFine.exchange(interv);

#if verb
  pout() << "EbHelmholtzOp::done interpolate" << endl;
#endif

  DataIterator ditf = a_phiFine.dataIterator();
  int nbox = ditf.size();
#pragma omp parallel for
  for(int mybox=0; mybox<nbox; mybox++)
    {
      const Box&     boxFine = m_eblgFine.getDBL().get(ditf[mybox]);
      const EBISBox& ebisBoxFine = m_eblgFine.getEBISL()[ditf[mybox]];
      const EBCellFAB& phiFine = a_phiFine[ditf[mybox]];

      for (int idir = 0; idir < SpaceDim; idir++)
	{
	  for (SideIterator sit; sit.ok(); sit.next())
	    {
	      Box fabBox = adjCellBox(boxFine, idir, sit(), 1);
	      fabBox.shift(idir, -sign(sit()));

	      Box ghostedBox = fabBox;
	      ghostedBox.grow(1);
	      ghostedBox.grow(idir,-1);
	      ghostedBox &= m_eblgFine.getDomain();

	      EBFaceFAB fluxFine(ebisBoxFine, ghostedBox, idir, ncomp);
	      finerEBAMROp.getFlux(fluxFine, phiFine, ghostedBox, fabBox, m_eblgFine.getDomain(),
				   ebisBoxFine, m_dxFine, ditf[mybox], idir);

	      Real scale = 1.0; //beta and bcoef already in flux

	      a_fluxReg.incrementFineBoth(fluxFine, scale, ditf[mybox], interv, idir, sit());
	    }
	}
    }
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
getFlux(FArrayBox&                    a_flux,
	const FArrayBox&              a_phi,
	const Box&                    a_faceBox,
	const int&                    a_idir,
	const Real&                   a_dx,
	const DataIndex&              a_datInd)
{
  CH_TIME("ebco::getflux3");
  const EBFaceFAB& bcoebff = (*m_bcoef)[a_datInd][a_idir];
  const FArrayBox& regBCo = (const FArrayBox&)(bcoebff.getSingleValuedFAB());
  FORT_GETFLUXEBCO(CHF_FRA1(a_flux,0),
		   CHF_CONST_FRA1(regBCo,0),
		   CHF_CONST_FRA1(a_phi, 0),
		   CHF_BOX(a_faceBox),
		   CHF_CONST_REAL(a_dx),
		   CHF_CONST_INT(a_idir));

  a_flux *= m_beta;
}

void EbHelmholtzOp::AMRResidualNC(LevelData<EBCellFAB>&              a_residual,
				  const LevelData<EBCellFAB>&        a_phiFine,
				  const LevelData<EBCellFAB>&        a_phi,
				  const LevelData<EBCellFAB>&        a_rhs,
				  bool                               a_homogeneousPhysBC,
				  AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp) {
  //dummy. there is no coarse when this is called
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);
  LevelData<EBCellFAB> phiC;
  AMRResidual(a_residual, a_phiFine, a_phi, phiC, a_rhs, a_homogeneousPhysBC, a_finerOp);
}


void
EbHelmholtzOp::
AMRResidualNF(LevelData<EBCellFAB>&       a_residual,
	      const LevelData<EBCellFAB>& a_phi,
	      const LevelData<EBCellFAB>& a_phiCoar,
	      const LevelData<EBCellFAB>& a_rhs,
	      bool a_homogeneousPhysBC)
{
  CH_TIME("ebco::amrresNF");
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_rhs.ghostVect() == m_ghostCellsRHS);

  AMROperatorNF(a_residual, a_phi, a_phiCoar,
		a_homogeneousPhysBC);
  axby(a_residual,a_residual,a_rhs,-1.0, 1.0);
}
//-----------------------------------------------------------------------

void EbHelmholtzOp::AMROperatorNC(LevelData<EBCellFAB>&              a_LofPhi,
				  const LevelData<EBCellFAB>&        a_phiFine,
				  const LevelData<EBCellFAB>&        a_phi,
				  bool                               a_homogeneousPhysBC,
				  AMRLevelOp<LevelData<EBCellFAB> >* a_finerOp){
  CH_TIME("ebco::amrOpNC");
  CH_assert(a_LofPhi.ghostVect() == m_ghostCellsRHS);
  LevelData<EBCellFAB> phiC;
  AMROperator(a_LofPhi, a_phiFine, a_phi, phiC, a_homogeneousPhysBC, a_finerOp);
}
//-----------------------------------------------------------------------

void
EbHelmholtzOp::
AMROperatorNF(LevelData<EBCellFAB>&       a_LofPhi,
	      const LevelData<EBCellFAB>& a_phi,
	      const LevelData<EBCellFAB>& a_phiCoar,
	      bool a_homogeneousPhysBC)
{
  CH_assert(a_LofPhi.ghostVect() == m_ghostCellsRHS);

  applyOp(a_LofPhi,a_phi, &a_phiCoar,  a_homogeneousPhysBC, false);

}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
AMRRestrict(LevelData<EBCellFAB>&       a_resCoar,
	    const LevelData<EBCellFAB>& a_residual,
	    const LevelData<EBCellFAB>& a_correction,
	    const LevelData<EBCellFAB>& a_coarCorrection, 
	    bool a_skip_res )
{
  CH_TIME("EbHelmholtzOp::AMRRestrict");
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_correction.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_coarCorrection.ghostVect() == m_ghostCellsPhi);
  CH_assert(!a_skip_res);

  CH_assert(a_residual.nComp() == 1);
  CH_assert(a_resCoar.nComp() == 1);
  CH_assert(a_correction.nComp() == 1);

  LevelData<EBCellFAB> resThisLevel;
  bool homogeneousPhys = true;
  bool homogeneousCF =   false;

  EBCellFactory ebcellfactTL(m_eblg.getEBISL());
  IntVect ghostVec = a_residual.ghostVect();

  resThisLevel.define(m_eblg.getDBL(), 1, ghostVec, ebcellfactTL);
  EBLevelDataOps::setVal(resThisLevel, 0.0);

  //API says that we must average(a_residual - L(correction, coarCorrection))
  applyOp(resThisLevel, a_correction, &a_coarCorrection, homogeneousPhys, homogeneousCF);
  incr(resThisLevel, a_residual, -1.0);
  scale(resThisLevel,-1.0);

  //use our nifty averaging operator
  Interval variables(0, 0);
  CH_assert(m_hasInterpAve);
  m_ebAverage.average(a_resCoar, resThisLevel, variables);

}
//-----------------------------------------------------------------------
Real
EbHelmholtzOp::
AMRNorm(const LevelData<EBCellFAB>& a_coarResid,
	const LevelData<EBCellFAB>& a_fineResid,
	const int& a_refRat,
	const int& a_ord)

{
  // compute norm over all cells on coarse not covered by finer
  CH_TIME("EbHelmholtzOp::AMRNorm");
  MayDay::Error("never called");
  //return norm of temp
  return norm(a_coarResid, a_ord);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
AMRProlong(LevelData<EBCellFAB>&       a_correction,
	   const LevelData<EBCellFAB>& a_coarCorrection)
{
  CH_TIME("EbHelmholtzOp::AMRProlong");
  //use cached interpolation object
  Interval variables(0, 0);
  CH_assert(m_hasInterpAve);
  m_ebInterp.pwcInterp(a_correction, a_coarCorrection, variables);
}
//-----------------------------------------------------------------------
void
EbHelmholtzOp::
AMRUpdateResidual(LevelData<EBCellFAB>&       a_residual,
		  const LevelData<EBCellFAB>& a_correction,
		  const LevelData<EBCellFAB>& a_coarCorrection)
{
  CH_TIME("EbHelmholtzOp::AMRUpdateResidual");
  CH_assert(a_residual.ghostVect() == m_ghostCellsRHS);
  CH_assert(a_correction.ghostVect() == m_ghostCellsPhi);
  CH_assert(a_coarCorrection.ghostVect() == m_ghostCellsPhi);

  LevelData<EBCellFAB> lcorr;
  bool homogeneousPhys = true;
  bool homogeneousCF   = false;

  EBCellFactory ebcellfactTL(m_eblg.getEBISL());
  IntVect ghostVec = a_residual.ghostVect();

  lcorr.define(m_eblg.getDBL(), 1, ghostVec, ebcellfactTL);

  applyOp(lcorr, a_correction, &a_coarCorrection, homogeneousPhys, homogeneousCF);

  incr(a_residual, lcorr, -1);
}


#include <CD_NamespaceFooter.H>
