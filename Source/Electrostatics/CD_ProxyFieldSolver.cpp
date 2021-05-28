/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_ProxyFieldSolver.cpp
  @brief  Implementation of CD_ProxyFieldSolver.H
  @author Robert Marskar
*/

// Chombo includes
#include <AMRMultiGrid.H>
#include <BiCGStabSolver.H>
#include <NWOEBConductivityOpFactory.H>
#include <EBConductivityOpFactory.H>
#include <DirichletConductivityDomainBC.H>
#include <DirichletConductivityEBBC.H>
#include <NeumannConductivityDomainBC.H>
#include <NeumannConductivityEBBC.H>
#include <ParmParse.H>

// Our includes
#include <CD_ProxyFieldSolver.H>
#include <CD_NwoEbQuadCfInterp.H>
#include <CD_DataOps.H>
#include <CD_NamespaceHeader.H>

bool ProxyFieldSolver::solve(MFAMRCellData&       a_potential,
			     const MFAMRCellData& a_rho,
			     const EBAMRIVData&   a_sigma,
			     const bool           a_zerophi) {
  DataOps::setValue(a_potential, 0.0);

  EBAMRCellData gasData = m_amr->alias(phase::gas, a_potential);

  this->solveOnePhase(gasData);
  
  return true;
}

void ProxyFieldSolver::registerOperators()  {
  if(m_amr.isNull()){
    MayDay::Abort("FieldSolverMultigrid::registerOperators - need to set AmrMesh!");
  }
  else{
    // For regridding
    m_amr->registerOperator(s_eb_fill_patch,   m_realm, phase::gas);
    m_amr->registerOperator(s_eb_fill_patch,   m_realm, phase::solid);

    // For coarsening
    m_amr->registerOperator(s_eb_coar_ave,     m_realm, phase::gas);
    m_amr->registerOperator(s_eb_coar_ave,     m_realm, phase::solid);

    // For linearly filling ghost cells
    m_amr->registerOperator(s_eb_pwl_interp,   m_realm, phase::gas);
    m_amr->registerOperator(s_eb_pwl_interp,   m_realm, phase::solid);

    // For interpolating to cell centroids
    m_amr->registerOperator(s_eb_irreg_interp, m_realm, phase::gas);
    m_amr->registerOperator(s_eb_irreg_interp, m_realm, phase::solid);
  }
}

Vector<EBLevelGrid> ProxyFieldSolver::getEBLevelGrids(){
  Vector<EBLevelGrid> ret;

  for (int lvl = 0; lvl <= m_amr->getFinestLevel(); lvl++){
    ret.push_back(*(m_amr->getEBLevelGrid(m_realm, phase::gas)[lvl]));
  }

  return ret;
}

Vector<RefCountedPtr<NWOEBQuadCFInterp> > ProxyFieldSolver::getInterpNWO(){
  const int finestLevel = m_amr->getFinestLevel();
  
  Vector<RefCountedPtr<NWOEBQuadCFInterp> > ret(1 + finestLevel);

  const int nGhosts = m_amr->getNumberOfGhostCells();
  
  for (int lvl = 1; lvl <= finestLevel; lvl++){
    if(lvl > 0){

      // Make the CFIVS
      const DisjointBoxLayout& fineGrids = m_amr->getGrids(m_realm)[lvl];
      LayoutData<IntVectSet> ghostCells(fineGrids);

      for (DataIterator dit = fineGrids.dataIterator(); dit.ok(); ++dit){
	IntVectSet& ghosts = ghostCells[dit()];

	ghosts.makeEmpty();

	Box grownBox = grow(fineGrids[dit()], nGhosts);

	ghosts |= IntVectSet(grownBox);
	ghosts -= fineGrids[dit()];

	for (NeighborIterator nit(fineGrids); nit.ok(); ++nit){
	  ghosts -= nit.box();
	}
      }

      // Choose if you want to fill more than one layer. 
      LayoutData<IntVectSet>* ghostCellsPtr;
      ghostCellsPtr = (m_amr->getEBLevelGrid(m_realm, phase::gas)[lvl]->getCFIVS());
      ghostCellsPtr = &ghostCells;


      ret[lvl] = RefCountedPtr<NWOEBQuadCFInterp> (new NwoEbQuadCfInterp(m_amr->getGrids(m_realm)[lvl],
									 m_amr->getGrids(m_realm)[lvl-1],
									 m_amr->getEBISLayout(m_realm, phase::gas)[lvl],
									 m_amr->getEBISLayout(m_realm, phase::gas)[lvl-1],
									 m_amr->getDomains()[lvl-1],
									 m_amr->getRefinementRatios()[lvl-1],
									 m_amr->getNumberOfGhostCells(),
									 m_amr->getDx()[lvl],
									 nGhosts,
									 *ghostCellsPtr,
									 m_amr->getEBIndexSpace(phase::gas)));
    }
  }

  
  return ret;
}

Vector<RefCountedPtr<EBQuadCFInterp> > ProxyFieldSolver::getInterpOld(){
  const int finestLevel = m_amr->getFinestLevel();
  
  Vector<RefCountedPtr<EBQuadCFInterp> > ret(1 + finestLevel);

  for (int lvl = 1; lvl <= finestLevel; lvl++){
    if(lvl > 0){
      
      ret[lvl] = RefCountedPtr<EBQuadCFInterp> (new EBQuadCFInterp(m_amr->getGrids(m_realm)[lvl],
								   m_amr->getGrids(m_realm)[lvl-1],
								   m_amr->getEBISLayout(m_realm, phase::gas)[lvl],
								   m_amr->getEBISLayout(m_realm, phase::gas)[lvl-1],
								   m_amr->getDomains()[lvl-1],
								   m_amr->getRefinementRatios()[lvl-1],
								   1,
								   *(m_amr->getEBLevelGrid(m_realm, phase::gas)[lvl]->getCFIVS()),
								   m_amr->getEBIndexSpace(phase::gas)));
									 
    }
  }

  
  return ret;
}

void ProxyFieldSolver::solveOnePhase(EBAMRCellData& a_phi){
  DataOps::setValue(a_phi, 1.23456789);

  ParmParse pp(m_className.c_str());

  // Define coefficients
  EBAMRCellData aco;
  EBAMRFluxData bco;
  EBAMRIVData   bcoIrreg;
  EBAMRCellData rho;

  m_amr->allocate(aco,      m_realm, phase::gas, 1);
  m_amr->allocate(bco,      m_realm, phase::gas, 1);
  m_amr->allocate(bcoIrreg, m_realm, phase::gas, 1);
  m_amr->allocate(rho,      m_realm, phase::gas, 1);

  DataOps::setValue(aco, 0.0);
  DataOps::setValue(bco, 1.0);
  DataOps::setValue(bcoIrreg, 1.0);
  DataOps::setValue(rho, 0.0);

  const Real alpha =  0.;
  const Real beta  =  1.;

  auto levelGrids    = this->getEBLevelGrids();
  auto interpNWO     = this->getInterpNWO();
  auto interpOld     = this->getInterpOld();

  const IntVect ghostPhi = m_amr->getNumberOfGhostCells()*IntVect::Unit;
  const IntVect ghostRHS = m_amr->getNumberOfGhostCells()*IntVect::Unit;

  int  eb_order;
  Real eb_value;
  Real dom_value;

  pp.get("eb_order", eb_order);
  pp.get("eb_val",  eb_value);
  pp.get("dom_val", dom_value);

  auto ebbcFactory = RefCountedPtr<DirichletConductivityEBBCFactory> (new DirichletConductivityEBBCFactory());
  ebbcFactory->setValue(eb_value);
  ebbcFactory->setOrder(eb_order);

  auto domainFactory = RefCountedPtr<DirichletConductivityDomainBCFactory> (new DirichletConductivityDomainBCFactory());
  domainFactory->setValue(dom_value);

  int relaxType;
  pp.get("relax", relaxType);

  auto factoryNWO = RefCountedPtr<NWOEBConductivityOpFactory> (new NWOEBConductivityOpFactory(levelGrids,
											      interpNWO,
											      alpha,
											      beta,
											      aco.getData(),
											      bco.getData(),
											      bcoIrreg.getData(),
											      m_amr->getDx()[0],
											      m_amr->getRefinementRatios(),
											      domainFactory,
											      ebbcFactory,
											      ghostPhi,
											      ghostRHS,
											      relaxType));

  auto factoryOld = RefCountedPtr<EBConductivityOpFactory> (new EBConductivityOpFactory(levelGrids,
											interpOld,
											alpha,
											beta,
											aco.getData(),
											bco.getData(),
											bcoIrreg.getData(),
											m_amr->getDx()[0],
											m_amr->getRefinementRatios(),
											domainFactory,
											ebbcFactory,
											ghostPhi,
											ghostRHS,
											relaxType));

  
  BiCGStabSolver<LevelData<EBCellFAB> > bicgstab;
  AMRMultiGrid<LevelData<EBCellFAB> >   multigridSolver;


  bool useNWO;
  pp.get("use_nwo", useNWO);
  if(useNWO){
    pout() << "using nwo ebconductivityop" << endl;
    multigridSolver.define(m_amr->getDomains()[0], *factoryNWO, &bicgstab, 1 + m_amr->getFinestLevel());
  }
  else{
    pout() << "using old ebconductivityop" << endl;
    multigridSolver.define(m_amr->getDomains()[0], *factoryOld, &bicgstab, 1 + m_amr->getFinestLevel());
  }
  int numSmooth;
  pp.get("smooth", numSmooth);
  multigridSolver.setSolverParameters(numSmooth, numSmooth/2, numSmooth/2, 1, 24, 1E-30, 1E-30, 1E-60);


  // Solve
  Vector<LevelData<EBCellFAB>* > phi;
  Vector<LevelData<EBCellFAB>* > rhs;

  m_amr->alias(phi, a_phi);
  m_amr->alias(rhs, rho);

  multigridSolver.init( phi, rhs, m_amr->getFinestLevel(), 0);
  multigridSolver.solveNoInit(phi, rhs, m_amr->getFinestLevel(), 0);
  multigridSolver.m_verbosity = 10;

  m_amr->averageDown(a_phi, m_realm, phase::gas);
  m_amr->interpGhost(a_phi, m_realm, phase::gas);
  
}

#include <CD_NamespaceFooter.H>
