#pragma once
#include <random>
#include <fstream>
#include "Anelastic.hpp"
#include "LasherTrapp2001_sounding/x7221545.adjdec2.hpp"

namespace cases 
{
  namespace CumulusCongestus
  {
    namespace moist_air = libcloudphxx::common::moist_air;
    namespace const_cp = libcloudphxx::common::const_cp;
    namespace theta_std = libcloudphxx::common::theta_std;

    using libcloudphxx::common::theta_std::p_1000;
    using libcloudphxx::common::moist_air::R_d_over_c_pd;
    using libcloudphxx::common::moist_air::c_pd;
    using libcloudphxx::common::moist_air::R_d;
    using libcloudphxx::common::moist_air::R_v;
    using libcloudphxx::common::const_cp::l_tri;
    using libcloudphxx::common::const_cp::p_vs;
    using libcloudphxx::common::theta_std::p_1000;

    const quantity<si::pressure, real_t> 
      p_0 = 101800 * si::pascals;
    const quantity<si::length, real_t> X_def[] = {/*2D*/12000 * si::metres, /*3D*/10000 * si::metres};
    const quantity<si::length, real_t> 
      z_0   = 0     * si::metres,
      Y_def = 10000 * si::metres,
      Z_def = 10000  * si::metres; 
    const real_t z_abs = Z_def / si::metres - 1000;
    const quantity<si::length, real_t> z_rlx = 100 * si::metres;

    template<class case_ct_params_t, int n_dims>
    class CumulusCongestusCommon : public Anelastic<case_ct_params_t, n_dims>
    {

      protected:
      using parent_t = Anelastic<case_ct_params_t, n_dims>;
      using ix = typename case_ct_params_t::ix;
      using rt_params_t = typename case_ct_params_t::rt_params_t;

      // env profiles of th and rv from the sounding
      arr_1D_t th_dry_env;
      arr_1D_t th_std_env;
      arr_1D_t p_env;
      arr_1D_t rv_env;

      template<bool enable_sgs = case_ct_params_t::enable_sgs>
      void setopts_sgs(rt_params_t &params,
                       typename std::enable_if<!enable_sgs>::type* = 0) 
      {
        parent_t::setopts_sgs(params);
      }

      template<bool enable_sgs = case_ct_params_t::enable_sgs>
      void setopts_sgs(rt_params_t &params,
                       typename std::enable_if<enable_sgs>::type* = 0) 
      {
        parent_t::setopts_sgs(params);
        params.fricvelsq = 0.0784;
      }
  
      template <class T, class U>
      void setopts_hlpr(T &params, const U &user_params)
      {
        params.buoyancy_wet = true;
        params.subsidence = false;
        params.vel_subsidence = false;
        params.friction = true;
        params.coriolis = false;
        params.radiation = false;

        this->setopts_sgs(params);
      }
  
      // RH T and p to rv
      quantity<si::dimensionless, real_t> RH_T_p_to_rv(const real_t &RH, const quantity<si::temperature, real_t> &T, const quantity<si::pressure, real_t> &p)
      {
        return RH * const_cp::r_vs<real_t>(T, p);
      }
  
      template <class index_t>
      void intcond_hlpr(typename parent_t::concurr_any_t &concurr, arr_1D_t &rhod, int rng_seed, index_t index)
      {
        // we assume here that set_profs was called already, so that *_env profiles are initialized
        int nz = concurr.advectee_global().extent(ix::w);  // ix::w is the index of vertical domension both in 2D and 3D
        real_t dz = (this->Z / si::metres) / (nz-1); 
        // copy the env profiles into 2D/3D arrays
        concurr.advectee(ix::rv) = rv_env(index); 
        concurr.advectee(ix::th) = th_std_env(index); 
  
        concurr.advectee(ix::u) = 0;
        concurr.advectee(ix::w) = 0;  
       
        // absorbers
        concurr.vab_coefficient() = where(index * dz >= z_abs,  1. / 100 * pow(sin(3.1419 / 2. * (index * dz - z_abs)/ (this->Z / si::metres - z_abs)), 2), 0);
        concurr.vab_relaxed_state(0) = concurr.advectee(ix::u);
        concurr.vab_relaxed_state(ix::w) = 0; // vertical relaxed state
  
        // density profile
        concurr.g_factor() = rhod(index); // copy the 1D profile into 2D/3D array
  
        // randomly prtrb tht and rv in the lowest 1km
        // NOTE: all processes do this, but ultimately only perturbation calculated by MPI rank 0 is used
        {
          std::mt19937 gen(rng_seed);
          std::uniform_real_distribution<> dis(-0.1, 0.1);
          auto rand = std::bind(dis, gen);
  
          auto th_global = concurr.advectee_global(ix::th);
          decltype(concurr.advectee(ix::th)) prtrb(th_global.shape()); // array to store perturbation
          std::generate(prtrb.begin(), prtrb.end(), rand); // fill it, TODO: is it officialy stl compatible?

          prtrb = where(index * dz >= 1000., 0., prtrb); // no perturbation above 1km
          th_global += prtrb;
          this->make_cyclic(th_global);
          concurr.advectee_global_set(th_global, ix::th);
        }
        {
          std::mt19937 gen(rng_seed+1); // different seed than in th. NOTE: if the same instance of gen is used in th and rv, for some reason it gives the same sequence in rv as in th despite being advanced in th prtrb
          std::uniform_real_distribution<> dis(-0.025e-3, 0.025e-3);
          auto rand = std::bind(dis, gen);
  
          auto rv_global = concurr.advectee_global(ix::rv);
          decltype(concurr.advectee(ix::rv)) prtrb(rv_global.shape()); // array to store perturbation
          std::generate(prtrb.begin(), prtrb.end(), rand); // fill it, TODO: is it officialy stl compatible?

          prtrb = where(index * dz >= 1000., 0., prtrb); // no perturbation above 1km
          rv_global += prtrb;
          this->make_cyclic(rv_global);
          concurr.advectee_global_set(rv_global, ix::rv);
        }
      }
  
  
      // calculate the initial environmental theta and rv profiles
      // alse set w_LS and hgt_fctrs
      void set_profs(detail::profiles_t &profs, int nz, const user_params_t &user_params)
      {
        using libcloudphxx::common::moist_air::R_d_over_c_pd;
        using libcloudphxx::common::moist_air::c_pd;
        using libcloudphxx::common::moist_air::R_d;
        using libcloudphxx::common::const_cp::l_tri;
        using libcloudphxx::common::theta_std::p_1000;

        parent_t::set_profs(profs, nz, user_params);

        // read the soundings
        // containers for soundings
        std::vector<real_t> pres_s, temp_s, RH_s, z_s;
        for(std::string line : LasherTrapp2001_sounding_file)
        {
          float pres, temp, RH, z;
          sscanf(line.c_str(), "%*f %f %f %*f %f %*f %*f %*f %*f %*f %*f %*f %*f %*f %f %*f %*f %*f %*f %*f %*f", &pres, &temp, &RH, &z);
          pres_s.push_back(pres * 100); 
          temp_s.push_back(temp + 273.16);  // TODO: use libcloud's T_0 
          RH_s.push_back(RH / 100); 
          z_s.push_back(z); 
        }

        real_t dz = (this->Z / si::metres) / (nz-1); 

        // interpolate soundings to centers of cells 
        std::vector<real_t> pres_si(nz), temp_si(nz), RH_si(nz);
        real_t offset = z_s.at(0); // consider the lowest sounding level as ground level
        pres_si[0] = pres_s[0];
        temp_si[0] = temp_s[0];
        RH_si[0] = RH_s[0];
        int cell_no = 1;
        real_t z = cell_no * dz;
        for(int i=1; i<pres_s.size(); ++i)
        {
          real_t z_up = z_s.at(i) - offset;
          real_t z_down = z_s.at(i-1) - offset;
          while(z_down <= z && z < z_up)
          {
            real_t lin_fact = (z - z_down) / (z_up - z_down);
            pres_si[cell_no] = pres_s[i-1] + lin_fact * (pres_s[i] - pres_s[i-1]);
            temp_si[cell_no] = temp_s[i-1] + lin_fact * (temp_s[i] - temp_s[i-1]);
            RH_si[cell_no] = RH_s[i-1] + lin_fact * (RH_s[i] - RH_s[i-1]);
            ++cell_no;
            z = cell_no*dz;
            if(cell_no == nz) break;
          }
          if(cell_no == nz) break;
        }
        if(cell_no != nz) throw std::runtime_error("The initial sounding is not high enough");

        // calc derived profsiles
        std::vector<real_t> th_std(nz), th_dry(nz), rv(nz);
        for(int i=0; i<nz; ++i)
        {
          th_std[i] = temp_si[i] * pow(p_1000<real_t>() / si::pascals / pres_si[i], R_d<real_t>() / c_pd<real_t>());  
          rv[i] = RH_T_p_to_rv(RH_si[i], temp_si[i] * si::kelvins, pres_si[i] * si::pascals); 
          th_dry[i] = theta_dry::std2dry<real_t>(th_std[i] * si::kelvins, quantity<si::dimensionless, real_t>(rv[i])) / si::kelvins;
        }

        // create 1D blitz arrays to wrap the derived profsiles, store the for use in intcond_hlpr
        th_dry_env.resize(nz);
        th_std_env.resize(nz);
        p_env.resize(nz);
        rv_env.resize(nz);
        th_dry_env = arr_1D_t(th_dry.data(), blitz::shape(nz), blitz::neverDeleteData).copy();
        th_std_env = arr_1D_t(th_std.data(), blitz::shape(nz), blitz::neverDeleteData).copy();
        p_env = arr_1D_t(pres_si.data(), blitz::shape(nz), blitz::neverDeleteData).copy();
        rv_env     = arr_1D_t(rv.data(), blitz::shape(nz), blitz::neverDeleteData).copy();

        // TODO: calc hydrostatic env profsiles like in dycoms? w kodzie od S. L-T tego jednak nie ma...
        profs.p_e = p_env;
        profs.rv_e = rv_env;
        profs.rl_e = 0;
        profs.th_e = th_std_env; // temp to calc rhod
  
        // calc reference profiles
        this->ref_prof(profs, nz);

        profs.w_LS = 0.; // no subsidence
        profs.th_LS = 0.; // no large-scale horizontal advection
        profs.rv_LS = 0.; 
      }

      // functions that set surface fluxes per timestep
      void update_surf_flux_sens(blitz::Array<real_t, n_dims> surf_flux_sens,
                                       blitz::Array<real_t, n_dims> th_ground,   
                                       blitz::Array<real_t, n_dims> U_ground,   
                                       const real_t &U_ground_z,
                                       const int &timestep, const real_t &dt, const real_t &dx, const real_t &dy) override
      {
        if(timestep == 0) 
          surf_flux_sens = .1 * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters) * theta_std::exner(p_0); // [K kg / (m^2 s)]; -1 because negative gradient of upward flux means inflow
        else if(int((3600. / dt) + 0.5) == timestep)
        {
          if(surf_flux_sens.rank() == 3) // TODO: make it a compile-time decision
            surf_flux_sens = .3 * exp( - ( pow(blitz::tensor::i * dx - real_t(0.5) * this->X / si::metres, 2) +  pow(blitz::tensor::j * dy - real_t(0.5) * this->Y / si::metres, 2) ) / (1700. * 1700.) ) * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters) * theta_std::exner(p_0);
          else if(surf_flux_sens.rank() == 2)
            surf_flux_sens = .3 * exp( - ( pow(blitz::tensor::i * dx - real_t(0.5) * this->X / si::metres, 2)  ) / (1700. * 1700.) ) * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters) * theta_std::exner(p_0);
        }
      }
      

      void update_surf_flux_lat(blitz::Array<real_t, n_dims> surf_flux_lat,
                                       blitz::Array<real_t, n_dims> rt_ground,   
                                       blitz::Array<real_t, n_dims> U_ground,   
                                       const real_t &U_ground_z,
                                       const int &timestep, const real_t &dt, const real_t &dx, const real_t &dy) override
      {
        if(timestep == 0)
          surf_flux_lat = .4e-4 * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters); // [m/s]
        else if(int((3600. / dt) + 0.5) == timestep)
        {
          if(surf_flux_lat.rank() == 3) // TODO: make it a compile-time decision
            surf_flux_lat = 1.2e-4 * exp( - ( pow(blitz::tensor::i * dx - real_t(0.5) * this->X / si::metres, 2) +  pow(blitz::tensor::j * dy - real_t(0.5) * this->Y / si::metres, 2) ) / (1700. * 1700.) ) * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters);
          else if(surf_flux_lat.rank() == 2)
            surf_flux_lat = 1.2e-4 * exp( - ( pow(blitz::tensor::i * dx - real_t(0.5) * this->X / si::metres, 2)  ) / (1700. * 1700.) ) * -1 * (this->rhod_0 / si::kilograms * si::cubic_meters);
        }
      }

      // one function for updating u or v
      // the n_dims arrays have vertical extent of 1 - ground calculations only in here
      void update_surf_flux_uv(blitz::Array<real_t, n_dims>  surf_flux_uv, // output array
                               blitz::Array<real_t, n_dims>  uv_ground,    // value of u or v on the ground
                               blitz::Array<real_t, n_dims>  U_ground,     // magnitude of horizontal ground wind
                               const real_t &U_ground_z,
                               const int &timestep, const real_t &dt, const real_t &dx, const real_t &dy)
      {
        surf_flux_uv = where(U_ground < 1e-4, 
            - 0.0784 * uv_ground / real_t(1e-4) * -1  * (this->rhod_0 / si::kilograms * si::cubic_meters), // 0.0784 m^2 / s^2 is the square of friction velocity = 0.28 m / s
            - 0.0784 * uv_ground / U_ground * -1  * (this->rhod_0 / si::kilograms * si::cubic_meters)
          );
      }


      // ctor
      CumulusCongestusCommon()
      {
        this->p_0 = p_0;
        //aerosol bimodal lognormal dist. - as in RICO with 11x conc following the ICMW2020 setup
        this->mean_rd1 = real_t(.03e-6) * si::metres,
        this->mean_rd2 = real_t(.14e-6) * si::metres;
        this->sdev_rd1 = real_t(1.28),
        this->sdev_rd2 = real_t(1.75);
        this->n1_stp = real_t(11*90e6) / si::cubic_metres, 
        this->n2_stp = real_t(11*15e6) / si::cubic_metres;
        this->z_rlx = z_rlx;
      }
    };
    
    template<class case_ct_params_t, int n_dims>
    class CumulusCongestus;

    template<class case_ct_params_t>
    class CumulusCongestus<case_ct_params_t, 2> : public CumulusCongestusCommon<case_ct_params_t, 2>
    {
      using parent_t = CumulusCongestusCommon<case_ct_params_t, 2>;
      using ix = typename case_ct_params_t::ix;
      using rt_params_t = typename case_ct_params_t::rt_params_t;

      void setopts(rt_params_t &params, const int nps[], const user_params_t &user_params)
      {
        this->setopts_hlpr(params, user_params);
        params.di = (this->X / si::metres) / (nps[0]-1); 
        params.dj = (this->Z / si::metres) / (nps[1]-1);
        params.dz = params.dj;
      }

      void intcond(typename parent_t::concurr_any_t &concurr,
                   arr_1D_t &rhod, arr_1D_t &th_e, arr_1D_t &rv_e, arr_1D_t &rl_e, arr_1D_t &p_e, int rng_seed)
      {
        blitz::secondIndex k;
        this->intcond_hlpr(concurr, rhod, rng_seed, k);
      }

      public:
      CumulusCongestus(const real_t _X=-1, const real_t _Y=-1, const real_t _Z=-1)
      {
        this->X = _X < 0 ? X_def[0] : _X * si::meters;
        this->Z = _Z < 0 ? Z_def : _Z * si::meters;
      }
    };

    template<class case_ct_params_t>
    class CumulusCongestus<case_ct_params_t, 3> : public CumulusCongestusCommon<case_ct_params_t, 3>
    {
      using parent_t = CumulusCongestusCommon<case_ct_params_t, 3>;
      using ix = typename case_ct_params_t::ix;
      using rt_params_t = typename case_ct_params_t::rt_params_t;

      void setopts(rt_params_t &params, const int nps[], const user_params_t &user_params)
      {
        this->setopts_hlpr(params, user_params);
        params.di = (this->X / si::metres) / (nps[0]-1); 
        params.dj = (this->Y / si::metres) / (nps[1]-1);
        params.dk = (this->Z / si::metres) / (nps[2]-1);
        params.dz = params.dk;
      }

      void intcond(typename parent_t::concurr_any_t &concurr,
                   arr_1D_t &rhod, arr_1D_t &th_e, arr_1D_t &rv_e, arr_1D_t &rl_e, arr_1D_t &p_e, int rng_seed)
      {
        blitz::thirdIndex k;
        this->intcond_hlpr(concurr, rhod, rng_seed, k);
  
        int nz = concurr.advectee_global().extent(ix::w);
        real_t dz = (this->Z / si::metres) / (nz-1); 
  
        concurr.advectee(ix::v)= 0;
        concurr.vab_relaxed_state(1) = concurr.advectee(ix::v);
      }

      public:
      CumulusCongestus(const real_t _X=-1, const real_t _Y=-1, const real_t _Z=-1)
      {
        this->X = _X < 0 ? X_def[1] : _X * si::meters;
        this->Y = _Y < 0 ? Y_def : _Y * si::meters;
        this->Z = _Z < 0 ? Z_def : _Z * si::meters;
      }
    };
  };
};
