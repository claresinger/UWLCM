/**
 * @file
 * @copyright University of Warsaw
 * @section LICENSE
 * GPLv3+ (see the COPYING file or http://www.gnu.org/licenses/)
 */

#include <libmpdata++/bcond/cyclic_3d.hpp>
#include <libmpdata++/bcond/open_3d.hpp>
#include <libmpdata++/concurr/openmp.hpp>
#include "setup.hpp"

#include "cases/DYCOMS98.hpp"
#include "cases/MoistThermalGrabowskiClark99.hpp"
#include "cases/DryThermalGMD2015.hpp"
#include "cases/LasherTrapp2001.hpp"

#include "opts_lgrngn.hpp"
#include "opts_blk_1m.hpp"
#include "opts_blk_2m.hpp"

#include "panic.hpp"
#include <map>

// copy external profiles into rt_parameters
// TODO: more elegant way
template<class params_t>
void copy_profiles(setup::arr_1D_t &th_e, setup::arr_1D_t &p_e, setup::arr_1D_t &rv_e, setup::arr_1D_t &rl_e, setup::arr_1D_t &th_ref, setup::arr_1D_t &pre_ref, setup::arr_1D_t &rhod, setup::arr_1D_t &w_LS, setup::arr_1D_t &hgt_v, setup::arr_1D_t &hgt_s, params_t &p)
{
  p.hgt_fctr_sclr = new setup::arr_1D_t(hgt_s.dataFirst(), hgt_s.shape(), blitz::neverDeleteData);
  p.hgt_fctr_vctr = new setup::arr_1D_t(hgt_v.dataFirst(), hgt_v.shape(), blitz::neverDeleteData);
  p.th_e = new setup::arr_1D_t(th_e.dataFirst(), th_e.shape(), blitz::neverDeleteData);
  p.p_e = new setup::arr_1D_t(p_e.dataFirst(), p_e.shape(), blitz::neverDeleteData);
  p.rv_e = new setup::arr_1D_t(rv_e.dataFirst(), rv_e.shape(), blitz::neverDeleteData);
  p.rl_e = new setup::arr_1D_t(rl_e.dataFirst(), rl_e.shape(), blitz::neverDeleteData);
  p.th_ref = new setup::arr_1D_t(th_ref.dataFirst(), th_ref.shape(), blitz::neverDeleteData);
  p.pre_ref = new setup::arr_1D_t(pre_ref.dataFirst(), pre_ref.shape(), blitz::neverDeleteData);
  p.rhod = new setup::arr_1D_t(rhod.dataFirst(), rhod.shape(), blitz::neverDeleteData);
  p.w_LS = new setup::arr_1D_t(w_LS.dataFirst(), w_LS.shape(), blitz::neverDeleteData);
}

// 2D model run logic - the same for any microphysics
template <class solver_t>
void run(int nx, int nz, const user_params_t &user_params)
{
  using concurr_openmp_rigid_rigid_t = concurr::openmp<
    solver_t,
    bcond::rigid,  bcond::rigid,
    bcond::rigid,  bcond::rigid
  >;

  using concurr_openmp_rigid_t = concurr::openmp<
    solver_t,
    bcond::cyclic, bcond::cyclic,
    bcond::rigid,  bcond::rigid
  >;

  using concurr_openmp_cyclic_t = concurr::openmp<
    solver_t,
    bcond::cyclic, bcond::cyclic,
    bcond::cyclic, bcond::cyclic
  >;

  using concurr_any_t = concurr::any<
    typename solver_t::real_t,
    solver_t::n_dims
  >;

  using case_t = setup::CasesCommon<
    concurr_openmp_rigid_t
  >;

  using case_ptr_t = std::unique_ptr<
    case_t
  >;

  case_ptr_t case_ptr;

  // setup choice
  if (user_params.model_case == "moist_thermal")
    case_ptr.reset(new setup::moist_thermal::MoistThermalGrabowskiClark99_2d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "dry_thermal")
    case_ptr.reset(new setup::dry_thermal::DryThermal_2d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "dycoms")
    case_ptr.reset(new setup::dycoms::Dycoms98_2d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "lasher_trapp")
    case_ptr.reset(new setup::LasherTrapp::LasherTrapp2001_2d<concurr_openmp_rigid_t>());

  // instantiation of structure containing simulation parameters
  typename solver_t::rt_params_t p;

  // copy force constants
  p.ForceParameters = case_ptr->ForceParameters;

  // copy functions used to update surface fluxes
  p.update_surf_flux_sens = std::bind(&case_t::update_surf_flux_sens, case_ptr.get(), std::placeholders::_1,std::placeholders:: _2, std::placeholders::_3);
  p.update_surf_flux_lat = std::bind(&case_t::update_surf_flux_lat, case_ptr.get(), std::placeholders::_1,std::placeholders:: _2, std::placeholders::_3);

  // copy user_params for output
  p.user_params = user_params;

  // output and simulation parameters
  p.grid_size = {nx, nz};

  case_ptr->setopts(p, nx, nz, user_params);
  setopts_micro<solver_t>(p, user_params, case_ptr);

  // reference profiles shared among threads
  setup::arr_1D_t th_e(nz),  p_e(nz), rv_e(nz), rl_e(nz), th_ref(nz), pre_ref(nz), rhod(nz+1), w_LS(nz), hgt_fctr_vctr(nz), hgt_fctr_sclr(nz);
  // rhod needs to be bigger, cause it divides vertical courant number, TODO: should have a halo both up and down, not only up like now; then it should be interpolated in courant calculation

  // assign their values
  case_ptr->env_prof(th_e, p_e, rv_e, rl_e, th_ref, pre_ref, rhod, w_LS, hgt_fctr_vctr, hgt_fctr_sclr, nz, user_params);
  // pass them to rt_params
  copy_profiles(th_e, p_e, rv_e, rl_e, th_ref, pre_ref, rhod, w_LS, hgt_fctr_vctr, hgt_fctr_sclr, p);

  // set outvars
  p.outvars.insert({solver_t::ix::rv, {"rv", "[kg kg-1]"}});
  p.outvars.insert({solver_t::ix::th, {"th", "[K]"}});
  p.outvars.insert({solver_t::ix::u, {"u", "[m/s]"}});
  p.outvars.insert({solver_t::ix::w, {"w", "[m/s]"}});

  // solver instantiation
  std::unique_ptr<concurr_any_t> concurr;

  if(user_params.model_case == "dry_thermal")
  {
    concurr.reset(new concurr_openmp_cyclic_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed); // works only by chance?
  }
  else if(user_params.model_case == "lasher_trapp")
  {
    concurr.reset(new concurr_openmp_rigid_rigid_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed); // works only by chance?
  }
  else
  {
    concurr.reset(new concurr_openmp_rigid_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed);
  }

  // setup panic pointer and the signal handler
  panic = concurr->panic_ptr();
  set_sigaction();

  // timestepping
  concurr->advance(user_params.nt);
}

// 3D model run logic - the same for any microphysics; still a lot in common with 2d code...
template <class solver_t>
void run(int nx, int ny, int nz, const user_params_t &user_params)
{
  using concurr_openmp_rigid_rigid_t = concurr::openmp<
    solver_t,
    bcond::rigid,  bcond::rigid,
    bcond::rigid,  bcond::rigid,
    bcond::rigid,  bcond::rigid
  >;

  using concurr_openmp_rigid_t = concurr::openmp<
    solver_t,
    bcond::cyclic, bcond::cyclic,
    bcond::cyclic, bcond::cyclic,
    bcond::rigid,  bcond::rigid
  >;

  using concurr_openmp_cyclic_t = concurr::openmp<
    solver_t,
    bcond::cyclic, bcond::cyclic,
    bcond::cyclic, bcond::cyclic,
    bcond::cyclic, bcond::cyclic
  >;

  using concurr_any_t = concurr::any<
    typename solver_t::real_t,
    solver_t::n_dims
  >;

  using case_t = setup::CasesCommon<
    concurr_openmp_rigid_t
  >;

  using case_ptr_t = std::unique_ptr<
    case_t
  >;

  case_ptr_t case_ptr;

  // setup choice
  if (user_params.model_case == "moist_thermal")
    case_ptr.reset(new setup::moist_thermal::MoistThermalGrabowskiClark99_3d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "dry_thermal")
    case_ptr.reset(new setup::dry_thermal::DryThermal_3d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "dycoms")
    case_ptr.reset(new setup::dycoms::Dycoms98_3d<concurr_openmp_rigid_t>());
  else if (user_params.model_case == "lasher_trapp")
    case_ptr.reset(new setup::LasherTrapp::LasherTrapp2001_3d<concurr_openmp_rigid_t>());

  // instantiation of structure containing simulation parameters
  typename solver_t::rt_params_t p;

  // copy force constants
  p.ForceParameters = case_ptr->ForceParameters;

  // copy functions used to update surface fluxes
  p.update_surf_flux_sens = std::bind(&case_t::update_surf_flux_sens, case_ptr.get(), std::placeholders::_1,std::placeholders:: _2, std::placeholders::_3);
  p.update_surf_flux_lat = std::bind(&case_t::update_surf_flux_lat, case_ptr.get(), std::placeholders::_1,std::placeholders:: _2, std::placeholders::_3);

  // copy user_params for output
  p.user_params = user_params;

  // output and simulation parameters
  p.grid_size = {nx, ny, nz};

  case_ptr->setopts(p, nx, ny, nz, user_params);
  setopts_micro<solver_t>(p, user_params, case_ptr);

  // reference profiles shared among threads
  setup::arr_1D_t th_e(nz),  p_e(nz), rv_e(nz), rl_e(nz), th_ref(nz), pre_ref(nz), rhod(nz+1), w_LS(nz), hgt_fctr_vctr(nz), hgt_fctr_sclr(nz);
  // rhod needs to be bigger, cause it divides vertical courant number, TODO: should have a halo both up and down, not only up like now; then it should be interpolated in courant calculation

  // assign their values
  case_ptr->env_prof(th_e, p_e, rv_e, rl_e, th_ref, pre_ref, rhod, w_LS, hgt_fctr_vctr, hgt_fctr_sclr, nz, user_params);
  // pass them to rt_params
  copy_profiles(th_e, p_e, rv_e, rl_e, th_ref, pre_ref, rhod, w_LS, hgt_fctr_vctr, hgt_fctr_sclr, p);

  // set outvars
  p.outvars.insert({solver_t::ix::rv, {"rv", "[kg kg-1]"}});
  p.outvars.insert({solver_t::ix::th, {"th", "[K]"}});
  p.outvars.insert({solver_t::ix::u, {"u", "[m/s]"}});
  p.outvars.insert({solver_t::ix::v, {"v", "[m/s]"}});
  p.outvars.insert({solver_t::ix::w, {"w", "[m/s]"}});

  // solver instantiation
  std::unique_ptr<concurr_any_t> concurr;


  if(user_params.model_case == "dry_thermal")
  {
    concurr.reset(new concurr_openmp_cyclic_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed); // works only by chance?
  }
  else if(user_params.model_case == "lasher_trapp")
  {
    concurr.reset(new concurr_openmp_rigid_rigid_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed); // works only by chance?
  }
  else
  {
    concurr.reset(new concurr_openmp_rigid_t(p));
    case_ptr->intcond(*static_cast<concurr_openmp_rigid_t*>(concurr.get()), rhod, th_e, rv_e, rl_e, p_e, user_params.rng_seed);
  }

  // setup panic pointer and the signal handler
  panic = concurr->panic_ptr();
  set_sigaction();

  // timestepping
  concurr->advance(user_params.nt);
}

// libmpdata++'s compile-time parameters
struct ct_params_common : ct_params_default_t
{
  using real_t = setup::real_t;
  enum { rhs_scheme = solvers::mixed };
  enum { prs_scheme = solvers::cr };
  enum { vip_vab = solvers::expl };
};

struct ct_params_2D_sd : ct_params_common
{
  enum { n_dims = 2 };
  enum { n_eqns = 4 };
  struct ix { enum {
    u, w, th, rv,
    vip_i=u, vip_j=w, vip_den=-1
  }; };
  enum { delayed_step = opts::bit(ix::th) | opts::bit(ix::rv) };
};

struct ct_params_3D_sd : ct_params_common
{
  enum { n_dims = 3 };
  enum { n_eqns = 5 };
  struct ix { enum {
    u, v, w, th, rv,
    vip_i=u, vip_j=v, vip_k=w, vip_den=-1
  }; };
  enum { delayed_step = opts::bit(ix::th) | opts::bit(ix::rv) };
};

struct ct_params_2D_blk_1m : ct_params_common
{
  enum { n_dims = 2 };
  enum { n_eqns = 6 };
  struct ix { enum {
    u, w, th, rv, rc, rr,
    vip_i=u, vip_j=w, vip_den=-1
  }; };
};

struct ct_params_3D_blk_1m : ct_params_common
{
  enum { n_dims = 3 };
  enum { n_eqns = 7 };
  struct ix { enum {
    u, v, w, th, rv, rc, rr,
    vip_i=u, vip_j=v, vip_k=w, vip_den=-1
  }; };
};

struct ct_params_2D_blk_2m : ct_params_common
{
  enum { n_dims = 2 };
  enum { n_eqns = 8 };
  struct ix { enum {
    u, w, th, rv, rc, rr, nc, nr,
    vip_i=u, vip_j=w, vip_den=-1
  }; };
};
 struct ct_params_3D_blk_2m : ct_params_common
{
  enum { n_dims = 3 };
  enum { n_eqns = 9 };
  struct ix { enum {
    u, v, w, th, rv, rc, rr, nc, nr,
     vip_i=u, vip_j=v, vip_k=w, vip_den=-1
   }; };
};

// function used to modify ct_params before running
template<template<class... Args_slvr> class slvr, class ct_params_dim_micro, class... Args>
void run_hlpr(bool piggy, std::string type, Args&&... args)
{
  if(!piggy) // no piggybacking
  {
    struct ct_params_piggy : ct_params_dim_micro { enum { piggy = 0 }; };
/*
    if(type == "moist_thermal") // use abs option in moist_thermal
    {
      struct ct_params_final : ct_params_piggy { enum { opts = opts::nug | opts::abs }; };
      run<slvr<ct_params_final>>(args...);
    }
    else // default is the iga | fct option
    {
*/
      struct ct_params_final : ct_params_piggy { enum { opts = opts::nug | opts::fct | opts::abs }; };
      //struct ct_params_final : ct_params_piggy { enum { opts = opts::nug | opts::iga | opts::fct }; };
      run<slvr<ct_params_final>>(args...);
//    }
  }
/*
  else // piggybacking
  {
    struct ct_params_piggy : ct_params_dim_micro { enum { piggy = 1 }; };
    if(type == "moist_thermal") // use abs option in moist_thermal
    {
      struct ct_params_final : ct_params_piggy { enum { opts = opts::nug | opts::abs }; };
      run<slvr<ct_params_final>>(args...);
    }
    else // default is the iga | fct option
    {
      struct ct_params_final : ct_params_piggy { enum { opts = opts::nug | opts::iga | opts::fct }; };
      run<slvr<ct_params_final>>(args...);
    }
  }
*/
}


// all starts here with handling general options
int main(int argc, char** argv)
{
  omp_set_nested(1); // to allow openmp calls from libcloudphxx multi_CUDA backend
  // making argc and argv global
  ac = argc;
  av = argv;

  {
    // note: all options should have default values here to make "--micro=? --help" work
    opts_main.add_options()
      ("micro", po::value<std::string>()->required(), "one of: blk_1m, blk_2m, lgrngn")
      ("case", po::value<std::string>()->required(), "one of: dry_thermal, moist_thermal, dycoms, lasher_trapp")
      ("nx", po::value<int>()->default_value(76) , "grid cell count in horizontal")
      ("ny", po::value<int>()->default_value(0) , "grid cell count in horizontal")
      ("nz", po::value<int>()->default_value(76) , "grid cell count in vertical")
      ("nt", po::value<int>()->default_value(3600) , "timestep count")
      ("rng_seed", po::value<int>()->default_value(-1) , "rng seed, negative for random")
      ("dt", po::value<setup::real_t>()->required() , "timestep length")
      ("z_rlx_sclr", po::value<setup::real_t>()->default_value(25) , "scalars surface flux charasteristic heihjt")
      ("outdir", po::value<std::string>(), "output file name (netCDF-compatible HDF5)")
      ("outfreq", po::value<int>(), "output rate (timestep interval)")
      ("spinup", po::value<int>()->default_value(2400) , "number of initial timesteps during which rain formation is to be turned off")
      ("serial", po::value<bool>()->default_value(false), "force advection and microphysics to be computed on single thread")
      ("th_src", po::value<bool>()->default_value(true) , "temp src")
      ("rv_src", po::value<bool>()->default_value(true) , "water vap source")
      ("rc_src", po::value<bool>()->default_value(true) , "cloud water source (in blk_1/2m)")
      ("rr_src", po::value<bool>()->default_value(true) , "rain water source (in blk_1/2m)")
      ("nc_src", po::value<bool>()->default_value(true) , "cloud water concentration source (in blk_2m)")
      ("nr_src", po::value<bool>()->default_value(true) , "rain water concentration source (in blk_2m)")
      ("uv_src", po::value<bool>()->default_value(true) , "horizontal vel src")
      ("w_src", po::value<bool>()->default_value(true) , "vertical vel src")
      ("piggy", po::value<bool>()->default_value(false) , "is it a piggybacking run")
      ("help", "produce a help message (see also --micro X --help)")
    ;
    po::variables_map vm;
    po::store(po::command_line_parser(ac, av).options(opts_main).allow_unregistered().run(), vm); // ignores unknown

    // hendling the "help" option
    if (ac == 1 || (vm.count("help") && !vm.count("micro")))
    {
      std::cout << opts_main;
      exit(EXIT_SUCCESS);
    }

    // checking if all required options present
    po::notify(vm);

    // instantiating user params container
    user_params_t user_params;

    if (!vm.count("help"))
    {
      if (!vm.count("outdir")) throw po::required_option("outdir");
      if (!vm.count("outfreq")) throw po::required_option("outfreq");
      user_params.outdir = vm["outdir"].as<std::string>();
      user_params.outfreq = vm["outfreq"].as<int>();
    }

    int
      nx = vm["nx"].as<int>(),
      ny = vm["ny"].as<int>(),
      nz = vm["nz"].as<int>();

    user_params.nt = vm["nt"].as<int>(),
    user_params.spinup = vm["spinup"].as<int>();

    // handling rng_seed
    user_params.rng_seed = vm["rng_seed"].as<int>();
    if(user_params.rng_seed < 0) //if negative, get random seed
    {
      std::random_device rd;
      user_params.rng_seed = rd();
    }
    std::cout << "rng seed: " << user_params.rng_seed << std::endl;

    //handling timestep length
    user_params.dt = vm["dt"].as<setup::real_t>();

    //handling z_rlx_sclr
    user_params.z_rlx_sclr = vm["z_rlx_sclr"].as<setup::real_t>();

    // handling serial-advection-forcing flag
    if(vm["serial"].as<bool>()) setenv("OMP_NUM_THREADS", "1", 1);

    // handling sources flags
    user_params.th_src = vm["th_src"].as<bool>();
    user_params.rv_src = vm["rv_src"].as<bool>();
    user_params.rc_src = vm["rc_src"].as<bool>();
    user_params.rr_src = vm["rr_src"].as<bool>();
    user_params.uv_src = vm["uv_src"].as<bool>();
    user_params.w_src = vm["w_src"].as<bool>();

    bool piggy = vm["piggy"].as<bool>();

    // handling the "micro" option
    std::string micro = vm["micro"].as<std::string>();

    // handling the "case" option
    user_params.model_case = vm["case"].as<std::string>();

    // run the simulation
//    if (micro == "lgrngn" && ny == 0) // 2D super-droplet
  //    run_hlpr<slvr_lgrngn, ct_params_2D_sd>(piggy, user_params.model_case, nx, nz, user_params);

 //   else if (micro == "lgrngn" && ny > 0) // 3D super-droplet
//      run_hlpr<slvr_lgrngn, ct_params_3D_sd>(piggy, user_params.model_case, nx, ny, nz, user_params);

 //   else if (micro == "blk_1m" && ny == 0) // 2D one-moment
   //   run_hlpr<slvr_blk_1m, ct_params_2D_blk_1m>(piggy, user_params.model_case, nx, nz, user_params);

 //   else if (micro == "blk_1m" && ny > 0) // 3D one-moment
  //    run_hlpr<slvr_blk_1m, ct_params_3D_blk_1m>(piggy, user_params.model_case, nx, ny, nz, user_params);

    /*else*/ if (micro == "blk_2m" && ny == 0) // 2D two-moment
      run_hlpr<slvr_blk_2m, ct_params_2D_blk_2m>(piggy, user_params.model_case, nx, nz, user_params);

  //  else if (micro == "blk_2m" && ny > 0) // 3D two-moment
    //  run_hlpr<slvr_blk_2m, ct_params_3D_blk_2m>(piggy, user_params.model_case, nx, ny, nz, user_params);

    // TODO: not only micro can be wrong
    else throw
      po::validation_error(
        po::validation_error::invalid_option_value, micro, "micro"
      );
  }
}
