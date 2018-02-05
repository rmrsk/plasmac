/*!
  @file plasma_engine.cpp
  @brief Implementation of plasma_engine.H
  @author Robert Marskar
  @date Nov. 2017
*/

#include "plasma_engine.H"

#include "LevelData.H"
#include "EBCellFAB.H"
#include "EBAMRIO.H"
#include "EBAMRDataOps.H"

#define warnings 0

plasma_engine::plasma_engine(){
  CH_TIME("plasma_engine::plasma_engine(weak)");
  if(m_verbosity > 5){
    pout() << "plasma_engine::plasma_engine(weak)" << endl;
  }
}

plasma_engine::plasma_engine(const RefCountedPtr<physical_domain>&        a_physdom,
			     const RefCountedPtr<computational_geometry>& a_compgeom,
			     const RefCountedPtr<plasma_kinetics>&        a_plaskin,
			     const RefCountedPtr<time_stepper>&           a_timestepper,
			     const RefCountedPtr<amr_mesh>&               a_amr,
			     const RefCountedPtr<cell_tagger>&            a_celltagger){
  CH_TIME("plasma_engine::plasma_engine(full)");
  if(m_verbosity > 5){
    pout() << "plasma_engine::plasma_engine(full)" << endl;
  }

  this->set_physical_domain(a_physdom);         // Set physical domain
  this->set_computational_geometry(a_compgeom); // Set computational geometry
  this->set_plasma_kinetics(a_plaskin);         // Set plasma kinetics
  this->set_time_stepper(a_timestepper);        // Set time stepper
  this->set_amr(a_amr);                         // Set amr
  this->set_cell_tagger(a_celltagger);          // Set cell tagger

  m_amr->sanity_check();  // Sanity check, make sure everything is set up correctly
  m_amr->build_domains(); // Build domains and resolutions, nothing else

  if(!m_celltagger.isNull()){ 
    m_celltagger->define(m_plaskin, m_timestepper, m_amr, m_compgeom, m_physdom);
  }

  m_potential_set = false;
}

plasma_engine::~plasma_engine(){
  CH_TIME("plasma_engine::~plasma_engine");
}

void plasma_engine::grid_report(){
  CH_TIME("plasma_engine::grid_report");
  if(m_verbosity > 5){
    pout() << "plasma_engine::grid_report" << endl;
  }

#if warnings
  MayDay::Warning("plasma_engine::grid_report - not implemented");
#endif
}

void plasma_engine::initial_regrids(const int a_init_regrids){
  CH_TIME("plasma_engine::initia_regrids");
  if(m_verbosity > 5){
    pout() << "plasma_engine::initial_regrids" << endl;
  }

  for (int i = 0; i < a_init_regrids; i++){
    this->regrid();                       // Regrid base
    m_timestepper->initial_data();        // Fill solvers with initial data
    m_timestepper->solve_poisson();       // Re-solve Poisson equation
    if(m_timestepper->stationary_rte()){  // Solve RTE equations by using initial data and electric field if its stationary
      const Real dummy_dt = 0.0;
      m_timestepper->solve_rte(dummy_dt); // Argument does not matter, it's a stationary solver.
    }    
  }
}

void plasma_engine::run(const Real a_start_time, const Real a_end_time, const int a_max_steps){
  CH_TIME("plasma_engine::run");
  if(m_verbosity > 5){
    pout() << "plasma_engine::run" << endl;
  }

  if(m_verbosity > 0){
    pout() << "=================================" << endl;
    if(!m_restart){
      pout() << "PlasmaEngine::run -- starting run" << endl;
    }
    else{
      pout() << "PlasmaEngine::run -- restarting run" << endl;
    }
  }

  if(a_max_steps > 0){
    if(!m_restart){
      m_time = a_start_time;
      m_step = 0;
    }

    m_timestepper->compute_dt(m_dt, m_timecode);
    m_timestepper->synchronize_solver_times(m_step, m_time, m_dt);

    bool first_step    = true;
    const Real init_dt = m_dt;

    if(m_verbosity > 0){
      this->grid_report();
    }

    m_wallclock_start = MPI_Wtime();
    while(m_time < a_end_time && m_step < a_max_steps){

      const int max_sim_depth = m_amr->get_max_sim_depth();
      const int max_amr_depth = m_amr->get_max_amr_depth();     
      if(m_step%m_regrid_interval == 0 && m_regrid_interval > 0 && max_sim_depth > 0 && max_amr_depth > 0){
	if(!first_step){
	  this->regrid();
	  if(m_verbosity > 0){
	    this->grid_report();
	  }
	}
      }

      if(!first_step){
	m_timestepper->compute_dt(m_dt, m_timecode);
      }

      if(first_step){
	first_step = false;
      }

      // Did the time step become too small?
      if(m_dt < 1.5E-5*init_dt){
	m_step++;

#ifdef CH_USE_HDF5
	this->write_plot_file(m_output_mode);
	this->write_checkpoint_file();
#endif

	MayDay::Abort("plasma_engine::run - the time step became too small");
      }

      // Last time step can be smaller than m_dt so that we end on a_end_time
      if(m_time + m_dt > a_end_time){
	m_dt = a_end_time - m_time;
      }

      // Time stepper advances solutions
      m_wallclock1 = MPI_Wtime();
      const Real actual_dt = m_timestepper->advance(m_dt);
      m_wallclock2 = MPI_Wtime();

      // Synchronize times
      m_dt    = actual_dt;
      m_time += actual_dt;
      m_step += 1;
      m_timestepper->synchronize_solver_times(m_step, m_time, m_dt);

      if(m_verbosity > 0){
	this->step_report(a_start_time, a_end_time, a_max_steps);
      }


#ifdef CH_USE_HDF5
      if(m_step%m_plot_interval == 0 && m_plot_interval > 0){
	if(m_verbosity > 1){
	  pout() << "plasma_engine::run -- Writing plot file" << endl;
	}
	this->write_plot_file(m_output_mode);
      }

      // Write checkpoint file
      if(m_step % m_chk_interval == 0 && m_chk_interval > 0){
	if(m_verbosity > 1){
	  pout() << "plasma_engine::run -- Writing checkpoint file" << endl;
	}
	this->write_checkpoint_file();
      }
#endif
    }
  }

  if(m_verbosity > 0){
    pout() << "==================================" << endl;
    pout() << "plasma_engine::run -- ending run  " << endl;
    pout() << "==================================" << endl;
  }

}

void plasma_engine::set_verbosity(const int a_verbosity){
  CH_TIME("plasma_engine::set_verbosity");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_verbosity" << endl;
  }
  m_verbosity = a_verbosity;
}

void plasma_engine::set_computational_geometry(const RefCountedPtr<computational_geometry>& a_compgeom){
  CH_TIME("plasma_engine::set_computational_geometry");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_computational_geometry" << endl;
  }
  m_compgeom = a_compgeom;
  m_mfis     = a_compgeom->get_mfis();
}

void plasma_engine::set_plasma_kinetics(const RefCountedPtr<plasma_kinetics>& a_plaskin){
  CH_TIME("plasma_engine::set_plasma_kinetics");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_plasma_kinetics" << endl;
  }
  m_plaskin = a_plaskin;
}

void plasma_engine::set_time_stepper(const RefCountedPtr<time_stepper>& a_timestepper){
  CH_TIME("plasma_engine::set_time_stepper");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_time_stepper" << endl;
  }
  m_timestepper = a_timestepper;
}

void plasma_engine::set_cell_tagger(const RefCountedPtr<cell_tagger>& a_celltagger){
  CH_TIME("plasma_engine::set_cell_tagger");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_cell_tagger" << endl;
  }
  m_celltagger = a_celltagger;
}

void plasma_engine::set_geom_refinement_depth(const int a_depth){
  CH_TIME("plasma_engine::set_geom_refinement_depth");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_geom_refinement_depth" << endl;
  }

  const int max_depth = m_amr->get_max_amr_depth();
  const int depth     = (a_depth == -1) ? max_depth : a_depth;
  
  this->set_geom_refinement_depth(depth, depth, depth, depth, depth, depth);
}

void plasma_engine::set_geom_refinement_depth(const int a_depth1,
					      const int a_depth2,
					      const int a_depth3,
					      const int a_depth4,
					      const int a_depth5,
					      const int a_depth6){
  CH_TIME("plasma_engine::set_geom_refinement_depth(full");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_geom_refinement_depth(full)" << endl;
  }
  
  const int max_depth = m_amr->get_max_amr_depth();

  m_conductor_tag_depth                = Min(a_depth1, max_depth);
  m_dielectric_tag_depth               = Min(a_depth2, max_depth);
  m_gas_conductor_interface_tag_depth  = Min(a_depth3, max_depth);
  m_gas_dielectric_interface_tag_depth = Min(a_depth4, max_depth);
  m_gas_solid_interface_tag_depth      = Min(a_depth5, max_depth);
  m_solid_solid_interface_tag_depth    = Min(a_depth6, max_depth);

  m_geom_tag_depth = 0;
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth1);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth2);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth3);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth4);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth5);
  m_geom_tag_depth = Max(m_geom_tag_depth, a_depth6);
}

void plasma_engine::set_output_mode(const output_mode::which_mode a_mode){
  CH_TIME("plasma_engine::set_output_mode");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_output_mode" << endl;
  }

  m_output_mode = a_mode;
}

void plasma_engine::set_output_directory(const std::string a_output_dir){
  CH_TIME("plasma_engine::set_output_directory");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_output_directory" << endl;
  }

  m_output_dir = a_output_dir;
}

void plasma_engine::set_output_file_names(const std::string a_output_names){
  CH_TIME("plasma_engine::set_output_names");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_output_names" << endl;
  }

  m_output_names = a_output_names;  
}

void plasma_engine::set_amr(const RefCountedPtr<amr_mesh>& a_amr){
  CH_TIME("plasma_engine::set_amr");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_amr" << endl;
  }

  m_amr = a_amr;
  m_amr->set_mfis(m_compgeom->get_mfis());
}

void plasma_engine::set_potential(Real (*a_potential)(const Real a_time)){
  CH_TIME("plasma_engine::set_potential");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_potential" << endl;
  }
  
  m_potential     = a_potential;
  m_potential_set = true;
}

void plasma_engine::setup_fresh(const int a_init_regrids){
  CH_TIME("plasma_engine::setup_fresh");
  if(m_verbosity > 5){
    pout() << "plasma_engine::setup_fresh" << endl;
  }

  this->sanity_check();                                    // Sanity check before doing anything expensive

  m_compgeom->build_geometries(*m_physdom,                 // Build the multifluid geometries
			       m_amr->get_finest_domain(),
			       m_amr->get_finest_dx(),
			       m_amr->get_max_box_size());

  this->get_geom_tags();       // Get geometric tags.
  
  m_amr->set_num_ghost(m_timestepper->query_ghost()); // Query solvers for ghost cells. Give it to amr_mesh before grid gen. 
  m_amr->regrid(m_geom_tags, m_geom_tag_depth);       // Regrid using geometric tags for now

  m_timestepper->set_amr(m_amr);
  m_timestepper->set_plasma_kinetics(m_plaskin);
  m_timestepper->set_computational_geometry(m_compgeom);  // Set computational geometry
  m_timestepper->set_physical_domain(m_physdom);          // Physical domain
  m_timestepper->set_potential(m_potential);              // Potential
  m_timestepper->instantiate_solvers();                   // Instantiate sigma and cdr with initial data (and rte, if transient)
  m_timestepper->initial_data();                          // Fill cdr and rte with initial data

  if (a_init_regrids >= 0){
    m_timestepper->solve_poisson();                       // Solve Poisson equation by using initial data
    if(m_timestepper->stationary_rte()){                  // Solve RTE equations by using initial data and electric field
      const Real dummy_dt = 0.0;
      m_timestepper->solve_rte(dummy_dt);                 // Argument does not matter, it's a stationary solver.
    }    
  }

  this->initial_regrids(a_init_regrids);

  m_restart = false;
}

void plasma_engine::setup_for_restart(const std::string a_restart_file){
  CH_TIME("plasma_engine::setup_for_restart");
  if(m_verbosity > 5){
    pout() << "plasma_engine::setup_for_restart" << endl;
  }

  m_restart = true;
}

void plasma_engine::tag_cells(Vector<IntVectSet>& a_tags){
  CH_TIME("plasma_engine::tag_cells");
  if(m_verbosity > 5){
    pout() << "plasma_engine::tag_cells" << endl;
  }

  const int finest_level = m_amr->get_finest_level();
  a_tags.resize(1 + finest_level);

  // Add tags from tagger
  if(!m_celltagger.isNull()){
    m_celltagger->tag_cells(a_tags, m_layout_tags, finest_level);
  }

  // Add geometric tags. We don't add tags on the finest level because
  for (int lvl = 0; lvl < finest_level; lvl++){
    a_tags[lvl] |= m_geom_tags[lvl];
  }
}

void plasma_engine::set_physical_domain(const RefCountedPtr<physical_domain>& a_physdom){
  CH_TIME("plasma_engine::set_physical_domain");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_physical_domain" << endl;
  }
  m_physdom = a_physdom;
}

void plasma_engine::set_regrid_interval(const int a_regrid_interval){
  m_regrid_interval = a_regrid_interval;
}

void plasma_engine::set_plot_interval(const int a_plot_interval){
  m_plot_interval = a_plot_interval;
}

void plasma_engine::set_checkpoint_interval(const int a_chk_interval){
  m_chk_interval = a_chk_interval;
}

void plasma_engine::sanity_check(){
  CH_TIME("plasma_engine::sanity_check");
  if(m_verbosity > 4){
    pout() << "plasma_engine::sanity_check" << endl;
  }

  CH_assert(!m_timestepper.isNull());
  CH_assert(m_potential_set);
}

void plasma_engine::step_report(const Real a_start_time, const Real a_end_time, const int a_max_steps){
  CH_TIME("plasma_engine::step_report");
  if(m_verbosity > 5){
    pout() << "plasma_engine::step_report" << endl;
  }

#if warning
  MayDay::Warning("plasma_engine::step_report - not implemented");
#endif
}

void plasma_engine::regrid(){
  CH_TIME("plasma_engine::regrid");
  if(m_verbosity > 2){
    pout() << "plasma_engine::regrid" << endl;
  }

  Vector<IntVectSet> tags;

  const int old_finest_level = m_amr->get_finest_level();
  this->tag_cells(tags);
  m_amr->regrid(tags, old_finest_level + 1); 
  const int new_finest_level = m_amr->get_finest_level();
  
  m_timestepper->regrid_solvers(old_finest_level, new_finest_level); // Regrid solvers
  m_timestepper->regrid_internals();                                 // Regrid internal storage
}

void plasma_engine::write_plot_file(const output_mode::which_mode a_mode){
  CH_TIME("plasma_engine::write_plot_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::write_plot_file" << endl;
  }
}

void plasma_engine::write_checkpoint_file(){
  CH_TIME("plasma_engine::write_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::write_checkpoint_file" << endl;
  }
}

void plasma_engine::read_checkpoint_file(){
  CH_TIME("plasma_engine::read_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::read_checkpoint_file" << endl;
  }  
}

void plasma_engine::get_geom_tags(){
  CH_TIME("plasma_engine::get_geom_tags");
  if(m_verbosity > 5){
    pout() << "plasma_engine::get_geom_tags" << endl;
  }

  const int maxdepth = m_amr->get_max_amr_depth();

  m_geom_tags.resize(maxdepth);

  const RefCountedPtr<EBIndexSpace> ebis_gas = m_mfis->get_ebis(phase::gas);
  const RefCountedPtr<EBIndexSpace> ebis_sol = m_mfis->get_ebis(phase::solid);

  CH_assert(ebis_gas != NULL);

  for (int lvl = 0; lvl < maxdepth; lvl++){ // Don't need tags on maxdepth, we will never generate grids below that. 
    const ProblemDomain& cur_dom = m_amr->get_domains()[lvl];
    const int which_level = ebis_gas->getLevel(cur_dom);

    IntVectSet cond_tags;
    IntVectSet diel_tags;
    IntVectSet gas_tags;
    IntVectSet solid_tags;
    IntVectSet gas_diel_tags;
    IntVectSet gas_solid_tags;
    
    // Conductor cells
    if(m_conductor_tag_depth > lvl){ 
      cond_tags = ebis_gas->irregCells(which_level);
      if(!ebis_sol.isNull()){
	cond_tags |= ebis_sol->irregCells(which_level);
	cond_tags -= m_mfis->interface_region(cur_dom);
      }
    }

    // Dielectric cells
    if(m_dielectric_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	diel_tags = ebis_sol->irregCells(which_level);
      }
    }

    // Gas-solid interface cells
    if(m_gas_solid_interface_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	gas_tags = ebis_gas->irregCells(which_level);
      }
    }

     // Gas-dielectric interface cells
    if(m_gas_dielectric_interface_tag_depth > lvl){
      if(!ebis_sol.isNull()){
	gas_diel_tags = m_mfis->interface_region(cur_dom);
      }
    }

    // Gas-conductor interface cells
    if(m_gas_conductor_interface_tag_depth > lvl){ 
      gas_solid_tags = ebis_gas->irregCells(which_level);
      if(!ebis_sol.isNull()){
	gas_solid_tags -= m_mfis->interface_region(cur_dom);
      }
    }

    // Solid-solid interfaces
    if(m_solid_solid_interface_tag_depth > lvl){ 
      if(!ebis_sol.isNull()){
	solid_tags = ebis_sol->irregCells(which_level);

	// Do the intersection with the conductor cells
	IntVectSet tmp = ebis_gas->irregCells(which_level);
	tmp |= ebis_sol->irregCells(which_level);
	tmp -= m_mfis->interface_region(cur_dom);

	solid_tags &= tmp;
      }
    }

    m_geom_tags[lvl].makeEmpty();
    m_geom_tags[lvl] |= diel_tags;
    m_geom_tags[lvl] |= cond_tags;
    m_geom_tags[lvl] |= gas_diel_tags;
    m_geom_tags[lvl] |= gas_solid_tags;
    m_geom_tags[lvl] |= gas_tags;
    m_geom_tags[lvl] |= solid_tags;
  }

  // Grow tags by 2, this is an ad-hoc fix that prevents ugly grid near EBs
  for (int lvl = 0; lvl < maxdepth; lvl++){
    m_geom_tags[lvl].grow(1);
  }
}
