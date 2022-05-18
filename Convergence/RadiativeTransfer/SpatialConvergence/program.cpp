#include "CD_Driver.H"
#include "CD_EddingtonSP1.H"
#include "CD_CoaxialCable.H"
#include <CD_RadiativeTransferStepper.H>
#include "ParmParse.H"

using namespace ChomboDischarge;
using namespace Physics::RadiativeTransfer;

// This program runs convergence testing on a uniform grid (with EB) for a stationary radiative transfer
// problem. It solves the EddingtonSP1 model on various resolutions. When there are two solutions
// available (coarse and fine solutions), the fine solutions is coarsened onto the coarse-grid problem
// and we compute the error as
//
//     e = coarsen(phi_fine) - phi_coar
//
// We then compute the various error norms.

int main(int argc, char* argv[]){

#ifdef CH_MPI
  MPI_Init(&argc, &argv);
#endif

  // These are the grid resolutions that we run this program for. For the 32^2 grid the "exact solution" is the
  // coarsened solution of the 64^2 grid etc.
  constexpr int refRat = 2;
  constexpr int nComp  = 1;

  std::vector<IntVect> nCells{16 * IntVect::Unit,
                              32 * IntVect::Unit,
                              64 * IntVect::Unit,
                              128 * IntVect::Unit,
                              256 * IntVect::Unit,
                              512 * IntVect::Unit};  

  // Build class options from input script and command line options
  const std::string input_file = argv[1];
  ParmParse pp(argc-2, argv+2, NULL, input_file.c_str());

  // This stuff is required because the old/new solutions and grids are discarded
  // every time we reinitialize AmrMesh. So we store it here.
  //
  // coarPhi => Solution on coarser grid
  // finePhi => Coarsened fine-grid solution (i.e., coarsen(phi_fine) as explained above)
  // coarDBL => Coarse grid
  // fineDBL => Fine grid
  // coarEBISL => Coarse EB layout
  // fineEBISL => Fine EB layout
  // coarDomain => Coarse grid domain
  EBAMRCellData coarPhi;
  EBAMRCellData finePhi;

  DisjointBoxLayout coarDBL;
  DisjointBoxLayout fineDBL;

  EBISLayout coarEBISL;
  EBISLayout fineEBISL;

  ProblemDomain coarDomain;

  bool notCoarsest = false;

  // Storage for max, L1, and L2 solution error norms.
  std::vector<std::array<Real, 3>> norms;  

  // Set geometry and AMR 
  RefCountedPtr<ComputationalGeometry> compgeom = RefCountedPtr<ComputationalGeometry> (new CoaxialCable());
  RefCountedPtr<AmrMesh> amr                    = RefCountedPtr<AmrMesh> (new AmrMesh());
  RefCountedPtr<GeoCoarsener> geocoarsen        = RefCountedPtr<GeoCoarsener> (new GeoCoarsener());
  RefCountedPtr<CellTagger> tagger              = RefCountedPtr<CellTagger> (NULL);

  // Set up basic Poisson, potential = 1 
  auto timestepper = RefCountedPtr<RadiativeTransferStepper<EddingtonSP1> >
     (new RadiativeTransferStepper<EddingtonSP1>());

  // Run simulations at various resolutions.
  for (const auto& cells : nCells) {

    // Reinitialize AmrMesh so it uses a different base grid.
    amr->setCoarsestGrid(cells);
    amr->buildDomains();

    // Set up the Driver and run our program.
    RefCountedPtr<Driver> engine = RefCountedPtr<Driver>(new Driver(compgeom, timestepper, amr, tagger, geocoarsen));
    engine->setupAndRun(input_file);

    // Compute the solution errors if there is a coarser solution available.
    if (notCoarsest) {
      fineDBL   = amr->getGrids("primal")[0];
      fineEBISL = amr->getEBISLayout("primal", phase::gas)[0];

      // Coarsen the fine-grid solution onto the coarse-grid layout
      EbCoarAve aveOp(fineDBL,
                      coarDBL,
                      fineEBISL,
                      coarEBISL,
                      coarDomain,
                      refRat,
                      nComp,
                      &(*(amr->getEBIndexSpace(phase::gas))));
      aveOp.average(*finePhi[0], *timestepper->getPhi()[0], Interval(0, 0));

      // Compute the error average(phi_fine) - coar_phi
      DataOps::incr(finePhi, coarPhi, -1.0);

      // Computes max, L1, and L2 norms.
      const Real Linf = DataOps::norm(*finePhi[0], amr->getDomains()[0], 0, true);
      const Real L1   = DataOps::norm(*finePhi[0], amr->getDomains()[0], 1, true);
      const Real L2   = DataOps::norm(*finePhi[0], amr->getDomains()[0], 2, true);

      norms.emplace_back(std::array<Real, 3>{Linf, L1, L2});
    }

    // Store coarse stuff for next time step.
    coarDBL    = amr->getGrids("primal")[0];
    coarDomain = amr->getDomains()[0];
    coarEBISL  = amr->getEBISLayout("primal", phase::gas)[0];

    // Allocate storage for coarse solution and coarsened fine solution.
    amr->allocate(coarPhi, "primal", phase::gas, 1);
    amr->allocate(finePhi, "primal", phase::gas, 1);

    // Copy the current solution, it becomes the "coarse" solution for the next iteration.
    const EBAMRCellData& phi = timestepper->getPhi();
    coarPhi.copy(phi);

    notCoarsest = true;
  }  

  // Print the convergence rates
#ifdef CH_MPI
  if (procID() == 0) {
#endif
    // clang-format off
    std::cout << "# cells\t" << "Linf error\t" << "L1 error\t" << "L2 error\n";

    for (int i = 0; i < norms.size(); i++) {
      std::cout << nCells[i][0] << "\t"
		<< std::get<0>(norms[i]) << "\t"
		<< std::get<1>(norms[i]) << "\t"
                << std::get<2>(norms[i]) << "\n";
    }
    // clang-format on
#ifdef CH_MPI
  }
  MPI_Finalize();
#endif
}
