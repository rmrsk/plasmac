#include "CD_Driver.H"
#include "vessel.H"
#include "geometry_stepper.H"
#include "ParmParse.H"

using namespace ChomboDischarge;
using namespace physics::geometry;

int main(int argc, char* argv[]){

#ifdef CH_MPI
  MPI_Init(&argc, &argv);
#endif

  // Build class options from input script and command line options
  const std::string input_file = argv[1];
  ParmParse pp(argc-2, argv+2, NULL, input_file.c_str());

  // Set geometry and AMR 
  RefCountedPtr<computational_geometry> compgeom = RefCountedPtr<computational_geometry> (new vessel());
  RefCountedPtr<AmrMesh> amr                    = RefCountedPtr<AmrMesh> (new AmrMesh());
  RefCountedPtr<geo_coarsener> geocoarsen        = RefCountedPtr<geo_coarsener> (new geo_coarsener());
  RefCountedPtr<cell_tagger> tagger              = RefCountedPtr<cell_tagger> (NULL);

  // Set up basic geometry stepper = 1 
  auto timestepper = RefCountedPtr<geometry_stepper> (new geometry_stepper());

  // Set up the Driver and run it
  RefCountedPtr<Driver> engine = RefCountedPtr<Driver> (new Driver(compgeom, timestepper, amr, tagger, geocoarsen));
  engine->setupAndRun(input_file);

#ifdef CH_MPI
  CH_TIMER_REPORT();
  MPI_Finalize();
#endif
#include "CD_NamespaceFooter.H"
