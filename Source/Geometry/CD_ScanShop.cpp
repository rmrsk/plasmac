/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @brief  CD_ScanShop.cpp
  @brief  Implementation of CD_ScanShop.H
  @author Robert Marskar
*/

// Std includes
#include <chrono>

// Chombo includes
#include <BRMeshRefine.H>
#include <LoadBalance.H>
#include <EBLevelDataOps.H>
#include <ParmParse.H>

// Our includes
#include <CD_ScanShop.H>
#include <CD_LoadBalancing.H>
#include <CD_NamespaceHeader.H>

#define ScanShop_Debug 0

constexpr long relativeCutCellLoad = 1L;

ScanShop::ScanShop(const BaseIF&       a_localGeom,
		   const int           a_verbosity,
		   const Real          a_dx,
		   const RealVect      a_probLo,
		   const ProblemDomain a_finestDomain,
		   const ProblemDomain a_scanLevel,
		   const int           a_maxGhostEB,
		   const Real          a_thrshdVoF)
  : GeometryShop(a_localGeom, a_verbosity, a_dx*RealVect::Unit, a_thrshdVoF) {

  CH_TIME("ScanShop::ScanShop(BaseIF, int, Real, RealVect, ProblemDomain, ProblemDomain, int, Real)");

  m_baseIF       = &a_localGeom;
  m_hasScanLevel = false;
  m_profile      = false;
  m_loadBalance  = false;
  m_maxGhostEB   = 1;//a_maxGhostEB;
  m_fileName     = "ScanShopReport.dat";
  m_boxSorting   = BoxSorting::Morton;

  // EBISLevel doesn't give resolution, origin, and problem domains through makeGrids, so we
  // need to construct these here, and then extract the proper resolution when we actually call makeGrids
  ScanShop::makeDomains(a_dx, a_probLo, a_finestDomain, a_scanLevel);

  // These are settings that I want to hide from the user.
  ParmParse pp("ScanShop");

  std::string str;
  pp.query("profile",      m_profile);
  pp.query("load_balance", m_loadBalance);
  pp.query("box_sorting",  str);

  if(str == "none"){
    m_boxSorting = BoxSorting::None;
  }
  else if(str == "std"){
    m_boxSorting = BoxSorting::Std;
  }
  else if(str == "morton"){
    m_boxSorting = BoxSorting::Morton;
  }
  else if(str == "shuffle"){
    m_boxSorting = BoxSorting::Shuffle;
  }

  m_timer = Timer("ScanShop");
}

ScanShop::~ScanShop(){
  CH_TIME("ScanShop::~ScanShop()");

  if(m_profile){
    m_timer.writeReportToFile(m_fileName);
    m_timer.eventReport(pout(), false);
  }
}

void ScanShop::setProfileFileName(const std::string a_fileName){
  m_fileName = a_fileName;

  m_timer = Timer(m_fileName);
}

void ScanShop::makeDomains(const Real          a_dx,
			   const RealVect      a_probLo,
			   const ProblemDomain a_finestDomain,
			   const ProblemDomain a_scanLevel){
  CH_TIME("ScanShop::makeDomains(Real, RealVect, ProblemDomain, ProblemDomain)");

  m_probLo = a_probLo;
  
  m_dx.     resize(0);
  m_domains.resize(0);

  m_dx.     push_back(a_dx);  
  m_domains.push_back(a_finestDomain);

  
  const int ref = 2;
  for (int lvl=0; ; lvl++){
    Real  dx             = m_dx[lvl];
    ProblemDomain domain = m_domains[lvl];

    if(a_scanLevel.domainBox() == domain.domainBox()){
      m_scanLevel = lvl;
    }
    
    if(domain.domainBox().coarsenable(ref)){
      domain.coarsen(ref);
      dx *= ref;
      
      m_dx.     push_back(dx);
      m_domains.push_back(domain);
    }
    else{
      break;
    }
  }

  // These will be built when they are needed. 
  m_grids.       resize(m_domains.size());
  m_boxMaps.     resize(m_domains.size());
  m_boxMap.      resize(m_domains.size());  
  m_hasThisLevel.resize(m_domains.size(), 0);
}

void ScanShop::makeGrids(const ProblemDomain& a_domain,
			 DisjointBoxLayout&   a_grids,
			 const int&           a_maxGridSize,
			 const int&           a_maxIrregGridSize){
  CH_TIME("ScanShop::makeGrids(ProblemDomain, DisjointBoxLayout, int, int)");  

  m_timer.startEvent("Make grids");

  // Build the scan level first
  if(!m_hasScanLevel){
    for (int lvl = m_domains.size()-1; lvl >= m_scanLevel; lvl--){
      ScanShop::buildCoarseLevel(lvl, a_maxGridSize); // Coarser levels built in the same way as the scan level
    }
    ScanShop::buildFinerLevels(m_scanLevel, a_maxGridSize);   // Traverse towards finer levels

    m_hasScanLevel = true;

#if ScanShop_DEBUG
    for (int lvl = 0; lvl < m_domains.size(); lvl++){
      ScanShop::printNumBoxesLevel(lvl);
    }
#endif
  }

  // Find the level corresponding to a_domain
  int whichLevel;
  for (int lvl = 0; lvl < m_domains.size(); lvl++){
    if(m_domains[lvl].domainBox() == a_domain.domainBox()){
      whichLevel = lvl;
      break;
    }
  }

  if(m_hasThisLevel[whichLevel] != 0){
    a_grids = m_grids[whichLevel];
  }
  else{
    // Development code. Break up a_domnain in a_maxGridSize chunks, load balance trivially and return the dbl
    
    MayDay::Warning("ScanShop::makeGrids -- decomposing by breaking up the domain into maxGridSize chunks. This should not happen!");

    Vector<Box> boxes;
    Vector<int> procs;
  
    domainSplit(a_domain, boxes, a_maxGridSize);
    LoadBalancing::sort(boxes, BoxSorting::Morton);
    LoadBalancing::makeBalance(procs, boxes);

    a_grids.define(boxes, procs, a_domain);
  }

  m_timer.stopEvent("Make grids");
}

bool ScanShop::isRegular(const Box a_box, const RealVect a_probLo, const Real a_dx) const {
  CH_TIME("ScanShop::isRegular(Box, RealVect, Real)");

  bool ret = true;
  
  for (BoxIterator bit(a_box); bit.ok(); ++bit){
    const IntVect iv = bit();
    const RealVect a_point = a_probLo + a_dx*(0.5*RealVect::Unit + RealVect(iv));
    if(m_baseIF->value(a_point) >= -0.5*a_dx){
      ret = false;
      break;
    }
  }

  return ret;
}

bool ScanShop::isCovered(const Box a_box, const RealVect a_probLo, const Real a_dx) const {
  CH_TIME("ScanShop::isCovered(Box, RealVect, Real)");

  bool ret = true;
  
  for (BoxIterator bit(a_box); bit.ok(); ++bit){
    const IntVect iv = bit();
    const RealVect a_point = a_probLo + a_dx*(0.5*RealVect::Unit + RealVect(iv));
    if(m_baseIF->value(a_point) <= 0.5*a_dx) {
      ret = false;
      break;
    }
  }

  return ret;
}

void ScanShop::buildCoarseLevel(const int a_level, const int a_maxGridSize){
  CH_TIME("ScanShop::buildCoarseLevel(int, int)");

  // This function does the following:
  // 1. Break up the domain into chunks of max size a_maxGridSize and load balance them trivially
  // 2. Search through all the boxes and label them as regular/covered/irregular. Store the result in a LevelData map
  // 3. Gather cut-cell and regular/covered boxes separately and load balance them separately
  // 4. Create a new DBL with the newly load-balanced boxes; there should be approximately the same amount
  //    of cut-cell boxes for each rank
  // 5. Copy the map created over the initial DisjointBoxLayout onto the final grid
  
  // 1.
  Vector<Box> boxes;
  Vector<int> procs;
  domainSplit(m_domains[a_level], boxes, a_maxGridSize);
  LoadBalancing::makeBalance(procs, boxes);

  // 2. 
  DisjointBoxLayout dbl(boxes, procs, m_domains[a_level]);
  LevelData<BoxType> map(dbl, 1, IntVect::Zero, BoxTypeFactory());
  
  Vector<Box> CutCellBoxes;
  Vector<Box> RegularBoxes;
  Vector<Box> CoveredBoxes;

  Vector<long> CoveredTypes;
  Vector<long> RegularTypes;
  Vector<long> CutCellTypes;    
  
  Vector<int> CutCellProcs;
  Vector<int> RegularProcs;
  Vector<int> CoveredProcs;  

  for (DataIterator dit(dbl); dit.ok(); ++dit){
    const Box box = dbl[dit()];
    
    Box grownBox = box;
    grownBox.grow(m_maxGhostEB);
    grownBox &= m_domains[a_level];

    const bool isRegular = ScanShop::isRegular(grownBox, m_probLo, m_dx[a_level]);
    const bool isCovered = ScanShop::isCovered(grownBox, m_probLo, m_dx[a_level]);

    if(isCovered && !isRegular){
      map[dit()].setCovered();
      CoveredBoxes.push_back(box);
      CoveredTypes.push_back(0L);
    }    
    else if(isRegular && !isCovered){
      map[dit()].setRegular();
      RegularBoxes.push_back(box);
      RegularTypes.push_back(1L);
    }
    else if(!isRegular && !isCovered){
      map[dit()].setCutCell();
      CutCellBoxes.push_back(box);
      CutCellTypes.push_back(2L);
    }
    else{
      MayDay::Error("ScanShop::buildCoarseLevel - logic bust");
    }
  }

#if ScanShop_Debug // Debug first map
  for (DataIterator dit(dbl); dit.ok(); ++dit){
    if(map[dit()].m_which == -1) {
      MayDay::Error("ScanShop::buildCoarseLevel - initial map shouldn't get -1");
    }
  }
#endif

  // 3. Gather the uncut boxes and the cut boxes separately.
  LoadBalancing::gatherBoxes(CoveredBoxes);  
  LoadBalancing::gatherBoxes(RegularBoxes);
  LoadBalancing::gatherBoxes(CutCellBoxes);

  LoadBalancing::gatherLoads(CoveredTypes);  
  LoadBalancing::gatherLoads(RegularTypes);
  LoadBalancing::gatherLoads(CutCellTypes);    
  
  LoadBalancing::sort(CoveredBoxes, CoveredTypes, BoxSorting::Morton);
  LoadBalancing::sort(RegularBoxes, RegularTypes, BoxSorting::Morton);
  LoadBalancing::sort(CutCellBoxes, CutCellTypes, BoxSorting::Morton);  

  const Vector<long> CoveredLoads(CoveredBoxes.size(), 1L);      
  const Vector<long> RegularLoads(RegularBoxes.size(), 1L);    
  const Vector<long> CutCellLoads(CutCellBoxes.size(), 1L);

  LoadBalancing::makeBalance(CoveredProcs, CoveredLoads, CoveredBoxes);
  LoadBalancing::makeBalance(RegularProcs, RegularLoads, RegularBoxes);
  LoadBalancing::makeBalance(CutCellProcs, CutCellLoads, CutCellBoxes);  

  Vector<Box>  allBoxes;
  Vector<int>  allProcs;
  Vector<long> allTypes;  

  allBoxes.append(CoveredBoxes);
  allBoxes.append(RegularBoxes);
  allBoxes.append(CutCellBoxes);  

  allProcs.append(CoveredProcs);    
  allProcs.append(RegularProcs);  
  allProcs.append(CutCellProcs);

  allTypes.append(CoveredTypes);
  allTypes.append(RegularTypes);
  allTypes.append(CutCellTypes);

  // This is something that I fucking HATE, but DisjointBoxLayout sorts the boxes using lexicographical sorting, and we must do the same if
  // we want to be able to globally index correctly into the box types. So, create a view of the boxes and box types that is consistent with
  // what we will have in the DataIterator. This means that we must lexicographically sort the boxes. 
  std::vector<std::pair<Box, long> > sortedBoxesAndTypes;
  for (int i = 0; i < allBoxes.size(); i++){
    sortedBoxesAndTypes.emplace_back(std::make_pair(allBoxes[i], allTypes[i]));
  }


  auto comparator = [](const std::pair<Box, long>& a, const std::pair<Box, long>& b) -> bool {
    return a.first < b.first;
  };
  
  std::sort(sortedBoxesAndTypes.begin(), sortedBoxesAndTypes.end(), comparator);


  // 4. Define the grids and map on this level. 
  m_grids  [a_level] = DisjointBoxLayout(allBoxes, allProcs, m_domains[a_level]);
  m_boxMap [a_level] = RefCountedPtr<LayoutData<BoxType> >(new LayoutData<BoxType>(m_grids[a_level]));

  // Go through the boxes and set the grid type. 

  // This is development code -- if we don't sort the boxes we should be able to iterate through them and set the box type from that!. 
  m_boxMaps[a_level] = RefCountedPtr<LevelData<BoxType> > (new LevelData<BoxType>(m_grids[a_level], 1, IntVect::Zero, BoxTypeFactory()));

  for (DataIterator dit(m_grids[a_level]); dit.ok(); ++dit){
    const Box  box  = sortedBoxesAndTypes[dit().intCode()].first;    
    const long type = sortedBoxesAndTypes[dit().intCode()].second;

    // This is an error.
    if(box != m_grids[a_level][dit()]){
      MayDay::Error("ScanShop::buildCoarseLevel -- logic bust");
    }
    
    // Otherwise we are fine, set the map to what it should be. 
    if(type == 0L){
      (*m_boxMap[a_level])[dit()].setCovered();
    }
    else if(type == 1L){
      (*m_boxMap[a_level])[dit()].setRegular();
    }
    else if(type == 2L){
      (*m_boxMap[a_level])[dit()].setCutCell();
    }

    if(type == 0L){
      (*m_boxMaps[a_level])[dit()].setCovered();
    }
    else if(type == 1L){
      (*m_boxMaps[a_level])[dit()].setRegular();
    }
    else if(type == 2L){
      (*m_boxMaps[a_level])[dit()].setCutCell();
    }        
  }

#if ScanShop_Debug // Debug first map
  for (DataIterator dit(m_grids[a_level]); dit.ok(); ++dit){
    if((*m_boxMaps[a_level])[dit()].m_which == -1) {
      MayDay::Error("ScanShop::buildCoarseLevel - final map shouldn't get -1");
    }
  }
#endif

  // We're done!
  m_hasThisLevel[a_level] = 1;
}

void ScanShop::buildFinerLevels(const int a_coarserLevel, const int a_maxGridSize){
  CH_TIME("ScanShop::buildFinerLevels(int, int)");

  if(a_coarserLevel > 0){

    const int coarLvl = a_coarserLevel;
    const int fineLvl = coarLvl - 1;

    // Coar stuff
    const DisjointBoxLayout&   dblCoar    =  m_grids  [coarLvl];
    const LevelData<BoxType>&  mapCoar    = *m_boxMaps[coarLvl];
    const LayoutData<BoxType>& newMapCoar = *m_boxMap [coarLvl];

    Vector<Box> CutCellBoxes;
    Vector<Box> ReguCovBoxes;

    Vector<int> CutCellProcs;
    Vector<int> ReguCovProcs;

    // Refined coarse stuff.
    DisjointBoxLayout dblFineCoar;
    refine(dblFineCoar, dblCoar, 2);
    LevelData<BoxType> mapFineCoar(dblFineCoar, 1, IntVect::Zero, BoxTypeFactory());

    // Break up cut cell boxes
    for (DataIterator dit(dblFineCoar); dit.ok(); ++dit){
      mapFineCoar[dit()] = mapCoar[dit()];
      const Box box = dblFineCoar[dit()];
      if(newMapCoar[dit()].isCutCell()){
	Vector<Box> boxes;
	domainSplit(box, boxes, a_maxGridSize);
	CutCellBoxes.append(boxes);
      }
      else{
	ReguCovBoxes.push_back(box);
      }
    }

    // Make a DBL out of the cut-cell boxes and again check if they actually contain cut cells
    LoadBalancing::gatherBoxes(CutCellBoxes);
    LoadBalancing::sort(CutCellBoxes, BoxSorting::Morton);
    LoadBalancing::makeBalance(CutCellProcs, CutCellBoxes);
    DisjointBoxLayout  CutCellDBL(CutCellBoxes, CutCellProcs, m_domains[fineLvl]);
    LevelData<BoxType> CutCellMap(CutCellDBL, 1, IntVect::Zero, BoxTypeFactory());

    // Redo the cut cell boxes
    Vector<long> CutCellLoads(0);
    Vector<long> ReguCovLoads(ReguCovBoxes.size(), 1L);
    CutCellBoxes.resize(0);
    for (DataIterator dit(CutCellDBL); dit.ok(); ++dit){
      const Box box = CutCellDBL[dit()];
      
      Box grownBox = box;
      grownBox.grow(m_maxGhostEB);
      grownBox &= m_domains[fineLvl];

      const bool isRegular = ScanShop::isRegular(grownBox, m_probLo, m_dx[fineLvl]);
      const bool isCovered = ScanShop::isCovered(grownBox, m_probLo, m_dx[fineLvl]);

      // Designate boxes and regular/covered or irregular. For regular covered boxes we use a constant load = 1 and for the cut-cell boxes
      // we compute the load introspectively. We do this because patches can contain a different amount of cut-cells and thus there's inherent
      // load imbalance. Worse, the cost of the implicit function can vary in space and thus we time these things introspectively. 
      if(isRegular){
	CutCellMap[dit()].setRegular();
	ReguCovBoxes.push_back(box);
	ReguCovLoads.push_back(1L);	
      }
      else if(isCovered){
	CutCellMap[dit()].setCovered();
	ReguCovBoxes.push_back(box);
	ReguCovLoads.push_back(1L);
      }
      else if(!isRegular && !isCovered){
	CutCellMap[dit()].setCutCell();
	CutCellBoxes.push_back(box);

	// If this is truly a cut-cell box then we want to find out how much work we have to deal with when computing the cut-cell moments. This
	// is (roughly) equal to the time it takes to evaluate the implicit function times the volume of the box. But since the cost of the implicit
	// function can vary in space (especially when we BVHs and stuff like that), we just time this directly. We compute the load in nanoseconds.
	if(m_loadBalance){
	  long curLoad = 0L;
	  for (BoxIterator bit(grownBox); bit.ok(); ++bit){
	    const RealVect point = m_probLo + m_dx[fineLvl]*(0.5*RealVect::Unit + RealVect(bit()));
	    const Real distance  = m_baseIF->value(point);

	    if(std::abs(distance) < 0.5*m_dx[fineLvl]){
	      curLoad += relativeCutCellLoad;
	    }
	    else {
	      curLoad += 1L;
	    }
	  }

	  CutCellLoads.push_back(curLoad);
	}
	else{
	  CutCellLoads.push_back(1L);
	}
      }
      else{
	MayDay::Error("ScanShop::buildFinerLevels - logic bust!");
      }
    }
    
    // Gather boxes and loads for regular/covered and cut-cell regions.
    LoadBalancing::gatherBoxes(CutCellBoxes);
    LoadBalancing::gatherBoxes(ReguCovBoxes);

    LoadBalancing::gatherLoads(CutCellLoads);
    LoadBalancing::gatherLoads(ReguCovLoads);

    LoadBalancing::sort(CutCellBoxes, CutCellLoads, BoxSorting::Morton);
    LoadBalancing::sort(ReguCovBoxes, ReguCovLoads, BoxSorting::Morton);

    // Load balance the boxes. Recall that we use constant loads for regular/covered boxes but use introspective load balancing
    // for the cut-cell boxes
    LoadBalancing::makeBalance(CutCellProcs, CutCellLoads, CutCellBoxes);
    LoadBalancing::makeBalance(ReguCovProcs, ReguCovLoads, ReguCovBoxes);

    // We load balanced the regular/covered and cut-cell regions independently, but now we need to create a box-to-rank map
    // that is usable by Chombo's DisjointBoxLayout.
    Vector<Box> allBoxes;
    Vector<int> allProcs;
    
    allBoxes.append(CutCellBoxes);
    allBoxes.append(ReguCovBoxes);
    
    allProcs.append(CutCellProcs);
    allProcs.append(ReguCovProcs);


    // Define the grids and copy the maps over. First copy the refine coarse map, then update with the
    // cut cell map
    m_grids  [fineLvl] = DisjointBoxLayout(allBoxes, allProcs, m_domains[fineLvl]);
    m_boxMaps[fineLvl] = RefCountedPtr<LevelData<BoxType> >  (new LevelData<BoxType>(m_grids[fineLvl], 1, IntVect::Zero, BoxTypeFactory()));
    m_boxMap [fineLvl] = RefCountedPtr<LayoutData<BoxType> > (new LayoutData<BoxType>(m_grids[fineLvl]));

    mapFineCoar.copyTo(*m_boxMaps[fineLvl]);
    CutCellMap. copyTo(*m_boxMaps[fineLvl]);

    for (DataIterator dit(m_grids[fineLvl]); dit.ok(); ++dit){
      (*m_boxMap[fineLvl])[dit()] = (*m_boxMaps[fineLvl])[dit()];
    }

#if ScanShop_Debug // Dummy check the box maps
    for (DataIterator dit(CutCellDBL); dit.ok(); ++dit){
      if(CutCellMap[dit()].m_which == -1) {
	MayDay::Error("ScanShop::buildFinerLevels - CutCellMap shouldn't get -1");
      }
    }
    for (DataIterator dit(dblFineCoar); dit.ok(); ++dit){
      if(mapFineCoar[dit()].m_which == -1) {
	MayDay::Error("ScanShop::buildFinerLevels - mapFineCoar shouldn't get -1");
      }
    }
    for (DataIterator dit(m_grids[fineLvl]); dit.ok(); ++dit){
      if((*m_boxMaps[fineLvl])[dit()].m_which == -1){
	MayDay::Error("ScanShop::buildFinerLevels - final map shouldn't get -1");
      }
    }
#endif
    
    m_hasThisLevel[fineLvl] = 1;

    // This does the next level, we stop when level 0 has been built
    ScanShop::buildFinerLevels(fineLvl, a_maxGridSize);
  }
}

GeometryService::InOut ScanShop::InsideOutside(const Box&           a_region,
					       const ProblemDomain& a_domain,
					       const RealVect&      a_probLo,
					       const Real&          a_dx,
					       const DataIndex&     a_dit) const{
  CH_TIME("ScanShop::InsideOutSide(Box, ProblemDomain, RealVect, Real, DataIndex)");

  // Find the level corresponding to a_domain
  int whichLevel = -1;
  bool foundLevel = false;
  for (int lvl = 0; lvl < m_domains.size(); lvl++){
    if(m_domains[lvl].domainBox() == a_domain.domainBox()){
      whichLevel = lvl;
      foundLevel = true;
      break;
    }
  }

  // A strang but true thing. This function is used in EBISLevel::simplifyGraphFromGeo and that function can send in a_domain
  // and a_dx on different levels....
  ProblemDomain domain;
  if(a_dx < m_dx[whichLevel] && whichLevel > 0){
    domain = m_domains[whichLevel-1];

    MayDay::Error("ScanShop::InsideOutside - logic bust 1");
  }

  GeometryService::InOut ret;

  if(foundLevel && m_hasThisLevel[whichLevel]){
    const LayoutData<BoxType>& map = (*m_boxMaps[whichLevel]);
    const BoxType& boxType         = map[a_dit];
    
    if(boxType.isRegular()){
      ret = GeometryService::Regular;
    }
    else if(boxType.isCovered()){
      ret = GeometryService::Covered;
    }
    else{
      ret= GeometryService::Irregular;
    }
  }
  else{
    MayDay::Error("ScanShop::InsideOutSide -- logic bust 2");
    
    ret = GeometryService::InsideOutside(a_region, domain, a_probLo, a_dx, a_dit);
  }

  return ret;
}

void ScanShop::fillGraph(BaseFab<int>&        a_regIrregCovered,
                         Vector<IrregNode>&   a_nodes,
                         const Box&           a_validRegion,
                         const Box&           a_ghostRegion,
                         const ProblemDomain& a_domain,
                         const RealVect&      a_probLo,
                         const Real&          a_dx,
                         const DataIndex&     a_di) const {
  CH_TIME("ScanShop::fillGraph");

  if(m_profile){
    m_timer.startEvent("Fill graph");
  }

  GeometryShop::fillGraph(a_regIrregCovered, a_nodes, a_validRegion, a_ghostRegion, a_domain, a_probLo, a_dx, a_di);

  if(m_profile){
    m_timer.stopEvent("Fill graph");
  }
}

void ScanShop::printNumBoxesLevel(const int a_level) const {
  CH_TIME("ScanShop::printNumBoxesLevel(int)");

  const DisjointBoxLayout&     dbl = m_grids[a_level];
  const LevelData<BoxType>& boxMap = *m_boxMaps[a_level];

  int myNumRegular = 0;
  int myNumCovered = 0;
  int myNumCutCell = 0;
  
  for (DataIterator dit(dbl); dit.ok(); ++dit){
    const BoxType& bType = boxMap[dit()];

    if(bType.isRegular()){
      myNumRegular++;
    }
    else if(bType.isCovered()){
      myNumCovered++;
    }
    else if(bType.isCutCell()){
      myNumCutCell++;
    }
  }

  const int numRegular = EBLevelDataOps::parallelSum(myNumRegular);
  const int numCovered = EBLevelDataOps::parallelSum(myNumCovered);
  const int numCutCell = EBLevelDataOps::parallelSum(myNumCutCell);

  if(procID() == 0){
    std::cout << "ScanShop::printNumBoxesLevel on domain = " << m_domains[a_level] << "\n"
	      << "\t Regular boxes = " << numRegular << "\n"
	      << "\t Covered boxes = " << numCovered << "\n"
	      << "\t CutCell boxes = " << numCutCell << "\n"
	      << std::endl;
  }
}

#include <CD_NamespaceFooter.H>
