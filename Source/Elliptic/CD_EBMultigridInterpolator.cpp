/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_EBMultigridInterpolator.cpp
  @brief  Implementation of CD_EBMultigridInterpolator.H
  @author Robert Marskar
  @todo   coarseFineInterpH is a mess. It doesn't even do the variables correctly. We should do our own regular interpolation. 
*/

// Chombo includes
#include <EBCellFactory.H>
#include <EBLevelDataOps.H>
#include <NeighborIterator.H>
#include <InterpF_F.H>
#include <EBAlias.H>
#include <ParmParse.H> 

// Our includes
#include <CD_EBMultigridInterpolator.H>
#include <CD_VofUtils.H>
#include <CD_LeastSquares.H>
#include <CD_NamespaceHeader.H>

constexpr int EBMultigridInterpolator::m_stenComp;
constexpr int EBMultigridInterpolator::m_nStenComp;
constexpr int EBMultigridInterpolator::m_comp;

EBMultigridInterpolator::EBMultigridInterpolator(const EBLevelGrid& a_eblgFine,
						 const EBLevelGrid& a_eblgCoar,
						 const CellLocation a_cellLocation,
						 const IntVect&     a_minGhost,
						 const int          a_refRat,
						 const int          a_nVar,
						 const int          a_ghostCF,
						 const int          a_order,
						 const int          a_weighting){
  CH_assert(a_ghostCF   > 0);
  CH_assert(a_nVar      > 0);
  CH_assert(a_refRat%2 == 0);
  
  const DisjointBoxLayout& gridsFine = a_eblgFine.getDBL();
  const DisjointBoxLayout& gridsCoar = a_eblgCoar.getDBL();

  QuadCFInterp::define(gridsFine, &gridsCoar, 1.0, a_refRat, a_nVar, a_eblgFine.getDomain());
  
  m_refRat        = a_refRat;
  m_nComp         = a_nVar;
  m_ghostCF       = a_ghostCF;
  m_order         = a_order;
  m_minGhost      = a_minGhost;
  m_cellLocation  = a_cellLocation;
  m_weight        = a_weighting;
  
  this->defineGrids(a_eblgFine, a_eblgCoar);
  this->defineCFIVS();
  this->defineGhostRegions();
  this->defineBuffers();
  this->defineStencils();

  m_isDefined = true;
}

int EBMultigridInterpolator::getGhostCF() const{
  return m_ghostCF;
}

void EBMultigridInterpolator::defineCFIVS(){
  for (int dir = 0; dir < SpaceDim; dir++){
    m_loCFIVS[dir].define(m_eblgFine.getDBL());
    m_hiCFIVS[dir].define(m_eblgFine.getDBL());

    for (DataIterator dit(m_eblgFine.getDBL()); dit.ok(); ++dit){
      m_loCFIVS[dir][dit()].define(m_eblgFine.getDomain(), m_eblgFine.getDBL()[dit()], m_eblgFine.getDBL(), dir, Side::Lo);
      m_hiCFIVS[dir][dit()].define(m_eblgFine.getDomain(), m_eblgFine.getDBL()[dit()], m_eblgFine.getDBL(), dir, Side::Hi);
    }
  }
}

EBMultigridInterpolator::~EBMultigridInterpolator(){

}

void EBMultigridInterpolator::coarseFineInterp(LevelData<EBCellFAB>& a_phiFine, const LevelData<EBCellFAB>& a_phiCoar, const Interval a_variables){
  CH_TIME("EBMultigridInterpolator::interp");

  LevelData<FArrayBox> fineAlias;
  LevelData<FArrayBox> coarAlias;

  aliasEB(fineAlias, (LevelData<EBCellFAB>&) a_phiFine);
  aliasEB(coarAlias, (LevelData<EBCellFAB>&) a_phiCoar);
  
  QuadCFInterp::coarseFineInterp(fineAlias, coarAlias);

  // Do corrections near the EBCF. 
  for (int icomp = a_variables.begin(); icomp <= a_variables.end(); icomp++){

    // Copy data to scratch data holders.
    const Interval srcInterv = Interval(icomp,   icomp);
    const Interval dstInterv = Interval(m_comp,  m_comp);
    
    // Need coarse data to be accesible by fine data, so I always have to copy here. 
    a_phiCoar.copyTo(srcInterv, m_coarData, dstInterv);

    for (DataIterator dit = a_phiFine.dataIterator(); dit.ok(); ++dit){
      EBCellFAB& dstFine       = a_phiFine[dit()];

      const EBCellFAB& srcFine = a_phiFine[dit()];
      const EBCellFAB& srcCoar = m_coarData[dit()];

      const BaseIVFAB<VoFStencil>& fineStencils = m_fineStencils[dit()];
      const BaseIVFAB<VoFStencil>& coarStencils = m_coarStencils[dit()];

      for (VoFIterator vofit(fineStencils.getIVS(), fineStencils.getEBGraph()); vofit.ok(); ++vofit){
	const VolIndex& ghostVoF = vofit();

	dstFine(ghostVoF, icomp) = 0.0;

	// Fine and coarse stencils for this ghost vof
	const VoFStencil& fineSten = fineStencils(ghostVoF, m_stenComp);
	const VoFStencil& coarSten = coarStencils(ghostVoF, m_stenComp);

	// Apply fine stencil
	for (int ifine = 0; ifine < fineSten.size(); ifine++){
	  dstFine(ghostVoF, icomp) += fineSten.weight(ifine) * srcFine(fineSten.vof(ifine), icomp);
	}

	// Apply coarse stencil
	for (int icoar = 0; icoar < coarSten.size(); icoar++){
	  dstFine(ghostVoF, icomp) += coarSten.weight(icoar) * srcCoar(coarSten.vof(icoar), m_comp);
	}
      }
    }
  }
}

void EBMultigridInterpolator::coarseFineInterpH(LevelData<EBCellFAB>& a_phiFine, const Interval a_variables){
  for (DataIterator dit(m_eblgFine.getDBL()); dit.ok(); ++dit){
    this->coarseFineInterpH(a_phiFine[dit()], a_variables, dit());
  }
}

void EBMultigridInterpolator::coarseFineInterpH(EBCellFAB& a_phi, const Interval a_variables, const DataIndex& a_dit){
  const Real m_dx     = 1.0;
  const Real m_dxCoar = m_dx*m_refRat;

  
  for (int ivar = a_variables.begin(); ivar <= a_variables.end(); ivar++){
    
    // Do the regular interp. 
    for (int dir = 0; dir < SpaceDim; dir++){
      for (SideIterator sit; sit.ok(); ++sit){

	const CFIVS* cfivsPtr = nullptr;
	if(sit() == Side::Lo) {
	  cfivsPtr = &m_loCFIVS[dir][a_dit];
	}
	else{
	  cfivsPtr = &m_hiCFIVS[dir][a_dit];
	}

	const IntVectSet& ivs = cfivsPtr->getFineIVS();
	if (cfivsPtr->isPacked() ){
	  const int ihiorlo = sign(sit());
	  FORT_INTERPHOMO(CHF_FRA(a_phi.getSingleValuedFAB()),
			  CHF_BOX(cfivsPtr->packedBox()),
			  CHF_CONST_REAL(m_dx),
			  CHF_CONST_REAL(m_dxCoar),
			  CHF_CONST_INT(dir),
			  CHF_CONST_INT(ihiorlo));
	}
      }
    }

    // Apply fine stencil near the EB. This might look weird but recall that ghostVof is a ghostCell on the other side of
    // the refinment boundary, whereas the stencil only reaches into cells on the finel level. So, we are not
    // writing to data that we will later fetch. 
    const BaseIVFAB<VoFStencil>& fineStencils = m_fineStencils[a_dit];
  
    for (VoFIterator vofit(fineStencils.getIVS(), fineStencils.getEBGraph()); vofit.ok(); ++vofit){
      const VolIndex& ghostVoF   = vofit();
      const VoFStencil& fineSten = fineStencils(ghostVoF, m_stenComp);
      
      a_phi(ghostVoF, ivar) = 0.0;

      for (int ifine = 0; ifine < fineSten.size(); ifine++){
	CH_assert(ghostVoF != fineSten.vof(ifine));
	a_phi(ghostVoF, ivar) += fineSten.weight(ifine) * a_phi(fineSten.vof(ifine), ivar);
      }
    }
  }
}

void EBMultigridInterpolator::defineGhostRegions(){
  const DisjointBoxLayout& dbl = m_eblgFine.getDBL();
  const ProblemDomain& domain  = m_eblgFine.getDomain();
  const EBISLayout& ebisl      = m_eblgFine.getEBISL();

  // Define the "regular" ghost interpolation regions. This is just one cell wide since the operator stencil
  // has a width of 1 in regular cells. 
  m_cfivs.define(dbl);
  for (DataIterator dit(dbl); dit.ok(); ++dit){
    const Box cellBox = dbl[dit()];

    std::map<std::pair<int, Side::LoHiSide>, Box>& cfivsBoxes = m_cfivs[dit()];
    
    for (int dir = 0; dir < SpaceDim; dir++){
      for (SideIterator sit; sit.ok(); ++sit){
	const auto dirSide = std::make_pair(dir, sit());

	IntVectSet cfivs = IntVectSet(adjCellBox(cellBox, dir, sit(), 1));

	NeighborIterator nit(dbl); // Subtract the other boxes if they intersect this box. 
	for (nit.begin(dit()); nit.ok(); ++nit){
	  cfivs -= dbl[dit()];
	}

	cfivs.recalcMinBox();
	
	cfivsBoxes.emplace(dirSide, cfivs.minBox());
      }
    }
  }
  

  // Define ghost cells to be interpolated near the EB. 
  m_ghostCells.define(dbl);
  for (DataIterator dit(dbl); dit.ok(); ++dit){
    const Box cellBox      = dbl[dit()];
    const EBISBox& ebisbox = ebisl[dit()];

    if(ebisbox.isAllRegular() || ebisbox.isAllCovered()){
      m_ghostCells[dit()] = IntVectSet();
    }
    else{

      // 1. Define the width of the ghost layer region around current (irregular grid) patch
      Box grownBox = grow(cellBox, m_ghostCF);
      grownBox &= domain;

      m_ghostCells[dit()] = IntVectSet(grownBox);

      NeighborIterator nit(dbl);
      for (nit.begin(dit()); nit.ok(); ++nit){
	m_ghostCells[dit()] -= dbl[nit()];
      }
      m_ghostCells[dit()] -= cellBox;

      // 2. Only include ghost cells that are within range m_ghostCF of an irregular grid cell
      IntVectSet irreg = ebisbox.getIrregIVS(cellBox);
      irreg.grow(m_ghostCF);

      m_ghostCells[dit()] &= irreg;
    }
  }
}

void EBMultigridInterpolator::defineGrids(const EBLevelGrid& a_eblgFine, const EBLevelGrid& a_eblgCoar){
  // Define the fine level grids. Use a shallow copy if we can.
  const int ghostReq  = 2*m_ghostCF;
  const int ghostFine = a_eblgFine.getGhost();
  if(ghostFine >= ghostReq){ 
    m_eblgFine = a_eblgFine;
  }
  else{ // Need to define from scratch
    m_eblgFine.define(m_eblgFine.getDBL(), m_eblgFine.getDomain(), ghostReq, m_eblgFine.getEBIS());
  }

  // Coarse is just a copy.
  m_eblgCoar = a_eblgCoar;

  // Define the coarsened fine grids -- needs same number of ghost cells as m_eblgFine. 
  coarsen(m_eblgCoFi, m_eblgFine, m_refRat);
}

void EBMultigridInterpolator::defineBuffers(){
  const DisjointBoxLayout& fineGrids = m_eblgFine.getDBL();
  const DisjointBoxLayout& coFiGrids = m_eblgCoFi.getDBL();

  const EBISLayout& fineEBISL        = m_eblgFine.getEBISL();
  const EBISLayout& coFiEBISL        = m_eblgCoFi.getEBISL();

  const ProblemDomain& fineDomain    = m_eblgFine.getDomain();
  const ProblemDomain& coarDomain    = m_eblgCoFi.getDomain();

  // Coarsen the fine-grid boxes and make a coarse layout, grown by a pretty big number of ghost cells...
  LayoutData<Box> grownFineBoxes(fineGrids);
  LayoutData<Box> grownCoarBoxes(coFiGrids);
  
  for (DataIterator dit(coFiGrids); dit.ok(); ++dit){
    Box coarBox = grow(coFiGrids[dit()], 2*m_ghostCF);
    Box fineBox = grow(fineGrids[dit()], 2*m_ghostCF);

    grownCoarBoxes[dit()] = coarBox & coarDomain;
    grownFineBoxes[dit()] = fineBox & fineDomain;
  }

  m_fineBoxes.define(grownFineBoxes);
  m_coarBoxes.define(grownCoarBoxes);

  m_fineData.define(m_fineBoxes, m_nComp, EBCellFactory(fineEBISL));
  m_coarData.define(m_coarBoxes, m_nComp, EBCellFactory(coFiEBISL));
}

void EBMultigridInterpolator::defineStencils(){
  const int comp  = 0;
  const int nComp = 1;

  const Real dxFine = 1.0;
  const Real dxCoar = dxFine*m_refRat;

  const DisjointBoxLayout& dblFine = m_eblgFine.getDBL();
  const DisjointBoxLayout& dblCoar = m_eblgCoFi.getDBL();

  m_fineStencils.define(dblFine);
  m_coarStencils.define(dblFine);

  for (DataIterator dit(dblFine); dit.ok(); ++dit){
    const Box origFineBox    = dblFine[dit()];
    const Box ghostedFineBox = grow(origFineBox, m_minGhost);
    const Box grownFineBox   = m_fineBoxes[dit()];
    const Box grownCoarBox   = m_coarBoxes[dit()];

    // Define the valid regions such that the interpolation does not include coarse grid cells that fall beneath the fine level,
    // and no fine cells outside the CF.
    IntVectSet validFineCells = IntVectSet();
    IntVectSet validCoarCells = IntVectSet(grownCoarBox);

    // Coar mask is false everywhere under the fine grid box, and opposite for the fine mask. 
    for (BoxIterator bit(origFineBox); bit.ok(); ++bit){
      const IntVect fineIV = bit();
      const IntVect coarIV = coarsen(fineIV, m_refRat);

      validFineCells |= fineIV;
      validCoarCells -= coarIV;
    }

    // Same for parts of the current (grown) patch that overlaps with neighboring boxes.
    NeighborIterator nit(dblFine);
    for (nit.begin(dit()); nit.ok(); ++nit){
      const Box overlap  = grownFineBox & dblFine[nit()];
      
      for (BoxIterator bit(overlap); bit.ok(); ++bit){
	const IntVect fineIV = bit();
	validFineCells |= fineIV;
      }

      validCoarCells -= dblCoar[nit()];
    }

    // Restrict to the ghosted input box
    validFineCells &= ghostedFineBox;

    // Now go through each ghost cell and get an interpolation stencil to specified order. 
    const EBISBox& ebisboxFine = m_eblgFine.getEBISL()[dit()];
    const EBISBox& ebisboxCoar = m_eblgCoFi.getEBISL()[dit()];

    const EBGraph& fineGraph   = ebisboxFine.getEBGraph();
    const EBGraph& coarGraph   = ebisboxCoar.getEBGraph();

    m_fineStencils[dit()].define(m_ghostCells[dit()], fineGraph, nComp);
    m_coarStencils[dit()].define(m_ghostCells[dit()], fineGraph, nComp);

    for (VoFIterator vofit(m_ghostCells[dit()], fineGraph); vofit.ok(); ++vofit){
      const VolIndex& ghostVof = vofit();

      VoFStencil& fineSten = m_fineStencils[dit()](ghostVof, comp);
      VoFStencil& coarSten = m_coarStencils[dit()](ghostVof, comp);

      int order         = m_order;
      bool foundStencil = false;
      
      while(order > 0 && !foundStencil){
	foundStencil = this->getStencil(fineSten,
					coarSten,
					m_cellLocation,					
					ghostVof,
					ebisboxFine,
					ebisboxCoar,
					validFineCells,
					validCoarCells,
					dxFine,
					dxCoar,
					order,
					m_weight);
	
	order--;

	if(!foundStencil){
	  pout() << "EBMultigridInterpolator -- on domain = " << m_eblgFine.getDomain() << ", dropping order for vof = " << ghostVof << endl;
	}
      }

      // Drop to order 0 if we never found a stencil, and issue an error code. 
      if(!foundStencil){
	fineSten.clear();

	int numCoarsen = 1;
	VolIndex ghostVofCoar = ghostVof;
	while(numCoarsen < m_refRat){
	  ghostVofCoar = ebisboxFine.coarsen(ghostVofCoar);
	  numCoarsen *= 2;
	}

	coarSten.add(ghostVofCoar, 1.0);
	
	MayDay::Warning("EBMultigridInterpolator::defineStencils -- could not find stencil and dropping to order 0");
      }
    }
  }
}

bool EBMultigridInterpolator::getStencil(VoFStencil&         a_stencilFine,
					 VoFStencil&         a_stencilCoar,
					 const CellLocation& a_cellLocation,
					 const VolIndex&     a_ghostVof,
					 const EBISBox&      a_ebisboxFine,
					 const EBISBox&      a_ebisboxCoar,
					 const IntVectSet&   a_validFineCells,
					 const IntVectSet&   a_validCoarCells,
					 const Real&         a_dxFine,
					 const Real&         a_dxCoar,
					 const int&          a_order,
					 const int&          a_weight){
  bool foundStencil = false;
  
  const int fineRadius = a_order;
  const int coarRadius = std::max(1, fineRadius/m_refRat);

  Vector<VolIndex> fineVofs;
  Vector<VolIndex> coarVofs;

  // Get the coarse vof which we obtain by coarsening a_ghostVof
  int numCoarsen = 1;
  VolIndex ghostVofCoar = a_ghostVof;
  while(numCoarsen < m_refRat){
    ghostVofCoar = a_ebisboxFine.coarsen(ghostVofCoar);
    numCoarsen *= 2;
  }

  // Get all Vofs in specified radii. Don't use cells that are not in a_validFineCells.
  fineVofs = VofUtils::getVofsInRadius(a_ghostVof,   a_ebisboxFine, fineRadius, VofUtils::Connectivity::MonotonePath, false);
  coarVofs = VofUtils::getVofsInRadius(ghostVofCoar, a_ebisboxCoar, coarRadius, VofUtils::Connectivity::MonotonePath, true );
  
  VofUtils::includeCells(fineVofs, a_validFineCells);
  VofUtils::includeCells(coarVofs, a_validCoarCells);
  
  VofUtils::onlyUnique(fineVofs);
  VofUtils::onlyUnique(coarVofs);

  const int numEquations = coarVofs.size() + fineVofs.size();
  const int numUnknowns  = LeastSquares::getTaylorExpansionSize(a_order);

  if(numEquations >= numUnknowns) { // We have enough equations to get a stencil

    // Build displacement vectors
    Vector<RealVect> fineDisplacements;
    Vector<RealVect> coarDisplacements;

    for (const auto& fineVof : fineVofs.stdVector()){
      fineDisplacements.push_back(LeastSquares::displacement(a_cellLocation, a_cellLocation, a_ghostVof, fineVof, a_ebisboxFine, a_dxFine));
    }

    for (const auto& coarVof : coarVofs.stdVector()){
      coarDisplacements.push_back(LeastSquares::displacement(a_cellLocation, a_cellLocation, a_ghostVof, coarVof, a_ebisboxFine, a_ebisboxCoar, a_dxFine, a_dxCoar));
    }

    // LeastSquares computes all unknown terms in a Taylor expansion up to specified order. We want the 0th order term, i.e. the interpolated value,
    // which in multi-index notation is the term (0,0), i.e. IntVect::Zero. The format of the two-level least squares routine is such that the
    // fine stencil lies on the first index. This can be confusing, but the LeastSquares uses a very compact notation. 
    IntVect interpStenIndex = IntVect::Zero;
    IntVectSet derivs       = IntVectSet(interpStenIndex);
    IntVectSet knownTerms   = IntVectSet();

    std::map<IntVect, std::pair<VoFStencil, VoFStencil> > stencils = LeastSquares::computeDualLevelStencils(derivs,
													    knownTerms,
													    fineVofs,
													    coarVofs,
													    fineDisplacements,
													    coarDisplacements,
													    a_weight,
													    a_order);

    a_stencilFine = stencils.at(interpStenIndex).first;
    a_stencilCoar = stencils.at(interpStenIndex).second;

    if(a_stencilFine.size() != 0 || a_stencilCoar.size() != 0){
      foundStencil = true;
    }
    else{
      foundStencil = false;
    }
  }

  return foundStencil;
}



#include <CD_NamespaceFooter.H>
