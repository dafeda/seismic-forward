#include <seismic_regridding.hpp>

#include <nrlib/geometry/triangle.hpp>
#include <nrlib/eclipsegrid/eclipsegrid.hpp>

#include <physics/zoeppritz.hpp>
#include <physics/zoeppritz_ps.hpp>
#include <physics/zoeppritz_pp.hpp>
#include <physics/wavelet.hpp>
#include <nrlib/random/randomgenerator.hpp>
#include <nrlib/random/normal.hpp>
#include <ctime>
#include <seismic_geometry.hpp>

#ifdef WITH_OMP
#include <omp.h>
#endif

void SeismicRegridding::MakeSeismicRegridding(SeismicParameters &seismic_parameters) {

  //---find number of threads available and specified------------
  int n_threads = seismic_parameters.GetModelSettings()->GetMaxThreads();
  int n_threads_avail = 1;
#ifdef WITH_OMP
  n_threads_avail = omp_get_num_procs();
#endif
  if (n_threads_avail < n_threads)
    n_threads = n_threads_avail;
  if (n_threads == 1)
    std::cout << n_threads << " of " << n_threads_avail << " available threads is used in the regridding.\n";
  else
    std::cout << n_threads << " of " << n_threads_avail << " available threads are used in the regridding.\n";

  //------------------------Resample Z values--------------------------------
  printf("Start finding Zvalues.\n");
  //time_t t1 = time(0);   // get time now
  FindZValues(seismic_parameters, n_threads);
  //seismic_parameters.PrintElapsedTime(t1, "finding Zvalues");
  printf("Zvalues found.\n");

  //------------------------Resample elastic properties----------------------
  printf("Start finding elastic parameters.\n");
  //t1 = time(0);
  FindVp(seismic_parameters, n_threads);
  VpPostProcess(seismic_parameters);
  //seismic_parameters.PrintElapsedTime(t1, "finding elastic parameters");
  printf("Elastic parameters found.\n");

  seismic_parameters.DeleteEclipseGrid();
  NRLib::RegularSurface<double> &toptime = seismic_parameters.GetTopTime();
  NRLib::RegularSurface<double> &bottime = seismic_parameters.GetBottomTime();
  Wavelet* wavelet                       = seismic_parameters.GetWavelet();

  //------------------------Make TWT grid-----------------------------------
  //t1 = time(0);
  FindTWT(seismic_parameters, toptime, bottime, n_threads);
  //seismic_parameters.PrintElapsedTime(t1, "finding twt");

  //---generate, write and delete vrms grid if writing is requested---------
  if (seismic_parameters.GetModelSettings()->GetNMOCorr() && seismic_parameters.GetModelSettings()->GetOutputVrms()){
    if (seismic_parameters.GetModelSettings()->GetPSSeismic()) {
      NRLib::StormContGrid &twtssgrid = seismic_parameters.GetTwtSSGrid();
      NRLib::StormContGrid &twtppgrid = seismic_parameters.GetTwtPPGrid();
      NRLib::StormContGrid &vpgrid    = seismic_parameters.GetVpGrid();
      NRLib::StormContGrid &vsgrid    = seismic_parameters.GetVsGrid();
      FindVrms(seismic_parameters, vpgrid, twtppgrid);
      seismic_parameters.GetSeismicOutput()->WriteVrms(seismic_parameters, "PP");
      FindVrms(seismic_parameters, vsgrid, twtssgrid);
      seismic_parameters.GetSeismicOutput()->WriteVrms(seismic_parameters, "SS");
      seismic_parameters.DeleteVrmsGrid();
    }
    else {
      NRLib::StormContGrid &twtgrid = seismic_parameters.GetTwtGrid();
      NRLib::StormContGrid &vpgrid  = seismic_parameters.GetVpGrid();
      FindVrms(seismic_parameters, vpgrid, twtgrid);
      seismic_parameters.GetSeismicOutput()->WriteVrms(seismic_parameters);
      seismic_parameters.DeleteVrmsGrid();
    }

  }
  //---add wavelet above and below toptime and bottime-------------
  toptime.Add(-1 * wavelet->GetTwtWavelet()); // add one wavelet length to bot and subtract from top
  bottime.Add(     wavelet->GetTwtWavelet());

  double tmin = toptime.Min();
  size_t ns = static_cast<size_t>(floor(tmin / seismic_parameters.GetSeismicGeometry()->dt() + 0.5));
  tmin = ns * seismic_parameters.GetSeismicGeometry()->dt();
  double tmax = bottime.Max();
  size_t nt = static_cast<size_t>(floor((tmax - tmin) / seismic_parameters.GetSeismicGeometry()->dt()+0.5)) + 1;
  seismic_parameters.GetSeismicGeometry()->setNt(nt);
  seismic_parameters.GetSeismicGeometry()->setTRange(tmin, tmax);

  //---write toptime and bottime---------------------------------
  if (seismic_parameters.GetModelSettings()->GetOutputTimeSurfaces()) {
    seismic_parameters.GetSeismicOutput()->WriteTimeSurfaces(seismic_parameters);
  }

  //t1 = time(0);   // get time now
  //---resample, write and delete EXTRA parameter grids---------
  if (seismic_parameters.GetModelSettings()->GetOutputExtraParametersTimeSegy()) {
    seismic_parameters.GetSeismicOutput()->WriteExtraParametersTimeSegy(seismic_parameters, n_threads);
  }
  if (seismic_parameters.GetModelSettings()->GetOutputExtraParametersDepthSegy()) {
    seismic_parameters.GetSeismicOutput()->WriteExtraParametersDepthSegy(seismic_parameters, n_threads);
  }
  seismic_parameters.DeleteExtraParameterGrids();
  //seismic_parameters.PrintElapsedTime(t1, "resampling extra parameters and write to segy");

  //t1 = time(0);   // get time now
  //---resample and write ELASTIC parameters in segy------------
  if (seismic_parameters.GetModelSettings()->GetOutputElasticParametersTimeSegy()) {
    seismic_parameters.GetSeismicOutput()->WriteElasticParametersTimeSegy(seismic_parameters, n_threads);
  }
  if (seismic_parameters.GetModelSettings()->GetOutputElasticParametersDepthSegy()) {
    seismic_parameters.GetSeismicOutput()->WriteElasticParametersDepthSegy(seismic_parameters, n_threads);
  }
  //seismic_parameters.PrintElapsedTime(t1, "resampling elastic parameters and write to segy");

  //---write elastic parameters, z values and twt on storm format---
  if (seismic_parameters.GetModelSettings()->GetOutputVp()) {
    seismic_parameters.GetSeismicOutput()->WriteVpVsRho(seismic_parameters);
  }
  if (seismic_parameters.GetModelSettings()->GetOutputZvalues()) {
    seismic_parameters.GetSeismicOutput()->WriteZValues(seismic_parameters);
  }
  if (seismic_parameters.GetModelSettings()->GetOutputTwt()) {
    seismic_parameters.GetSeismicOutput()->WriteTwt(seismic_parameters);
  }
}


void SeismicRegridding::FindZValues(SeismicParameters &seismic_parameters, size_t n_threads) {
  NRLib::StormContGrid         &zgrid = seismic_parameters.GetZGrid();
  const NRLib::EclipseGeometry &geometry = seismic_parameters.GetEclipseGrid().GetGeometry();
  size_t top_k = seismic_parameters.GetTopK();
  bool use_corner_point = seismic_parameters.GetModelSettings()->GetUseCornerpointInterpol();
  bool rem_neg_delta = seismic_parameters.GetModelSettings()->GetRemoveNegativeDeltaZ();

  double xmin  = zgrid.GetXMin();
  double ymin  = zgrid.GetYMin();
  double dx    = zgrid.GetDX();
  double dy    = zgrid.GetDY();
  double angle = zgrid.GetAngle();

  size_t k = zgrid.GetNK() - 2;
  NRLib::Grid2D<double> values(zgrid.GetNI(), zgrid.GetNJ(), 0);
  if (use_corner_point) {
    geometry.FindLayerSurfaceCornerpoint(values, k + top_k, 1, dx, dy, xmin, ymin, angle, 0);
  }
  else {
    geometry.FindLayerSurface(values, k + top_k, 1, dx, dy, xmin, ymin, angle, 0);
  }

  for (size_t i = 0; i < zgrid.GetNI(); i++) {
    for (size_t j = 0; j < zgrid.GetNJ(); j++) {
      zgrid(i, j, k + 1) = static_cast<float>(values(i, j));
    }
  }

  //if n_threads = 1 the check of neg delta values should be done directly, not after inclusion in grid
  if (rem_neg_delta && n_threads == 1) {
    for (int k = zgrid.GetNK() - 2; k >= 0; --k) {
      NRLib::Grid2D<double> values(zgrid.GetNI(), zgrid.GetNJ(), 0);
      if (use_corner_point) {
        geometry.FindLayerSurfaceCornerpoint(values, k + top_k, 0, dx, dy, xmin, ymin, angle, 0);
      }
      else {
        geometry.FindLayerSurface(values, k + top_k, 0, dx, dy, xmin, ymin, angle, 0);
      }
      for (size_t i = 0; i < zgrid.GetNI(); i++) {
        for (size_t j = 0; j < zgrid.GetNJ(); j++) {
          if (values(i, j) > zgrid(i, j, k + 1)) {
            zgrid(i, j, k) = zgrid(i, j, k + 1);
          }
          else {
            zgrid(i, j, k) = static_cast<float>(values(i, j));
          }
        }
      }
    }
  }
  else {
    int  chunk_size;
    chunk_size = 1;
#ifdef WITH_OMP
#pragma omp parallel for schedule(dynamic, chunk_size) num_threads(n_threads)
#endif
    for (int k = zgrid.GetNK() - 2; k >= 0; --k) {
      NRLib::Grid2D<double> values(zgrid.GetNI(), zgrid.GetNJ(), 0);
      if (use_corner_point) {
        geometry.FindLayerSurfaceCornerpoint(values, k + top_k, 0, dx, dy, xmin, ymin, angle, 0);
      }
      else {
        geometry.FindLayerSurface(values, k + top_k, 0, dx, dy, xmin, ymin, angle, 0);
      }
      for (size_t i = 0; i < zgrid.GetNI(); i++) {
        for (size_t j = 0; j < zgrid.GetNJ(); j++) {
          zgrid(i, j, k) = static_cast<float>(values(i, j));
        }
      }
    }

    if (rem_neg_delta) {
      chunk_size = 10;
#ifdef WITH_OMP
#pragma omp parallel for schedule(dynamic, chunk_size) num_threads(n_threads)
#endif
      for (int i = 0; i < zgrid.GetNI(); i++) {
        for (size_t j = 0; j < zgrid.GetNJ(); j++) {
          for (int k = zgrid.GetNK() - 2; k >= 0; --k) {
            if (zgrid(i, j, k) > zgrid(i, j, k + 1)) {
              zgrid(i, j, k) = zgrid(i, j, k + 1);
            }
          }
        }
      }
    }
  }
}

void SeismicRegridding::FindVrms(SeismicParameters          &seismic_parameters,
                                 const NRLib::StormContGrid &vgrid,
                                 const NRLib::StormContGrid &twtgrid)
{
  double v_w = seismic_parameters.GetModelSettings()->GetVw();
  double z_w = seismic_parameters.GetModelSettings()->GetZw();
  NRLib::StormContGrid &zgrid    = seismic_parameters.GetZGrid();
  NRLib::StormContGrid &vrmsgrid = seismic_parameters.GetVrmsGrid();
  

  double v_over;
  double twt_w = 2000*z_w/v_w;
  double tmp, tmp0;
  for (size_t i = 0; i < vrmsgrid.GetNI(); ++i) {
    for (size_t j = 0; j < vrmsgrid.GetNJ(); ++j) {
      if (twtgrid(i,j,0) == -999.0) {
        for (size_t k = 0; k < vrmsgrid.GetNK(); ++k) {
          vrmsgrid(i,j,k) = -999.0;
        }
      }
      else {
        v_over = 2000*(zgrid(i,j,0) - z_w)/(twtgrid(i,j,0) - 2000*z_w/v_w);
        tmp0 = v_w*v_w*twt_w + v_over*v_over*(twtgrid(i,j,0) - twt_w);
        for (size_t k = 0; k < vrmsgrid.GetNK(); ++k) {
          tmp = tmp0;
          for (size_t l = 1; l <= k; ++l) {
            tmp += vgrid(i,j,l)* vgrid(i,j,l)*(twtgrid(i,j,l) - twtgrid(i,j,l-1));
          }
          tmp = tmp / twtgrid(i,j,k);
          vrmsgrid(i,j,k) = float(std::sqrt(tmp));
        }
      }
    }
  }
}

void SeismicRegridding::FindPointZValue(size_t i, size_t j, size_t k,
                                        NRLib::Point &point,
                                        const NRLib::EclipseGeometry &geometry,
                                        const NRLib::Grid<double> &grid,
                                        const NRLib::Grid2D<double> &value_above,
                                        double default_value,
                                        double zlimit,
                                        double default_top)
{

  if (geometry.IsActive(i, j, k)) {
    point.z = grid(i, j, k);
  }
  else if (geometry.GetDZ(i, j, k) < zlimit) {
    point.z = value_above(i, j);
  }
  else if (value_above(i, j) == default_top) {
    point.z = default_top;
  }
  else {
    point.z = default_value;
  }
}

void SeismicRegridding::FillInGridValues(const NRLib::EclipseGeometry &geometry,
                                         NRLib::Grid<double>          &grid_copy,
                                         double                        default_value,
                                         double                        zlimit,
                                         double                        default_top,
                                         size_t                        ni,
                                         size_t                        nj,
                                         size_t                        topk,
                                         size_t                        botk)
{
  for (size_t k = topk; k <= botk; k++) {
    for (size_t i = 0; i < ni; i++) {
      for (size_t j = 0; j < nj; j++) {
        if (geometry.IsActive(i, j, k) == false) {
          if (k > 0 && k > topk) {
            if (geometry.GetDZ(i, j, k) < zlimit) {
              grid_copy(i, j, k) = grid_copy(i, j, k - 1);
            }
            else if (grid_copy(i, j, k - 1) == default_top) {
              grid_copy(i, j, k) = default_top;
            }
            else {
              grid_copy(i, j, k) = default_value;
            }
          }
          else {
            grid_copy(i, j, k) = default_top;
          }
        }
      }
    }
  }
}


void SeismicRegridding::AddNoiseToReflectionsPos(unsigned long           seed,
                                                 double                  std_dev,
                                                 NRLib::Grid2D<double>  &refl)
{
  NRLib::RandomGenerator rg;
  rg.Initialize(seed);
  NRLib::Normal normal_distibrution(0, std_dev);

  for (size_t i = 0; i < refl.GetNI(); ++i) {
    for (size_t j = 0; j < refl.GetNJ(); ++j) {
      refl(i, j) += static_cast<float>(normal_distibrution.Draw(rg));
    }
  }
}




void SeismicRegridding::FindTWT(SeismicParameters &seismic_parameters,
                                NRLib::RegularSurface<double> &toptime, 
                                NRLib::RegularSurface<double> &bottime,
                                size_t n_threads)
{
  NRLib::StormContGrid &vpgrid    = seismic_parameters.GetVpGrid();
  NRLib::StormContGrid &vsgrid    = seismic_parameters.GetVsGrid();
  NRLib::StormContGrid &twtgrid   = seismic_parameters.GetTwtGrid();
  NRLib::StormContGrid &twtssgrid = seismic_parameters.GetTwtSSGrid();
  NRLib::StormContGrid &twtppgrid = seismic_parameters.GetTwtPPGrid();
  NRLib::StormContGrid &zgrid     = seismic_parameters.GetZGrid();
  bool ps_seismic                 = seismic_parameters.GetModelSettings()->GetPSSeismic();
  bool nmo_seismic                = seismic_parameters.GetModelSettings()->GetNMOCorr();
  double v_w                      = seismic_parameters.GetModelSettings()->GetVw();
  double z_w                      = seismic_parameters.GetModelSettings()->GetZw();

  size_t nk = twtgrid.GetNK();
  double dx1 = vpgrid.GetDX();
  double dy1 = vpgrid.GetDY();
  double dx2 = bottime.GetDX();
  double dy2 = bottime.GetDY();
  int  chunk_size;
  chunk_size = 1;
#ifdef WITH_OMP
#pragma omp parallel for schedule(dynamic, chunk_size) num_threads(n_threads)
#endif
  for (int i = 0; i < vpgrid.GetNI(); i++) {
    for (size_t j = 0; j < vpgrid.GetNJ(); j++) {
      double x, y, z;
      vpgrid.FindCenterOfCell(i, j, 0, x, y, z);
      twtgrid(i, j, 0) = static_cast<float>(toptime.GetZ(x, y));
      if (ps_seismic && nmo_seismic) {
        double a = 2.0;
        twtppgrid(i, j, 0) = 2 / (a + 1) * (twtgrid(i, j, 0) + 1000 * (a - 1) * z_w / v_w);
        twtssgrid(i, j, 0) = 2 * twtgrid(i, j, 0) - twtppgrid(i, j, 0);
      }
      if (toptime.IsMissing(twtgrid(i, j, 0)) == false) {
        for (size_t k = 1; k < nk; k++) {
          if(ps_seismic) {
            twtgrid(i, j, k) = twtgrid(i, j, k - 1) + static_cast<float>(1000.0 * (zgrid(i, j, k) - zgrid(i, j, k - 1)) / vpgrid(i, j, k + 1)) + static_cast<float>(1000.0 * (zgrid(i, j, k) - zgrid(i, j, k - 1)) / vsgrid(i, j, k + 1));
          } 
          else {
            twtgrid(i, j, k) = twtgrid(i, j, k - 1) + static_cast<float>(2000.0 * (zgrid(i, j, k) - zgrid(i, j, k - 1)) / vpgrid(i, j, k + 1));
          }
          if (ps_seismic && nmo_seismic){
            twtppgrid(i, j, k) = twtppgrid(i, j, k - 1) + static_cast<float>(2000.0 * (zgrid(i, j, k) - zgrid(i, j, k - 1)) / vpgrid(i, j, k + 1));
            twtssgrid(i, j, k) = twtssgrid(i, j, k - 1) + static_cast<float>(2000.0 * (zgrid(i, j, k) - zgrid(i, j, k - 1)) / vsgrid(i, j, k + 1));
          }
        }

        size_t ii, jj;
        double xstart, xend;
        double ystart, yend;
        xstart = x - dx1;
        ystart = y - dy1;
        xend = x + dx1;
        yend = y + dy1;
        x = xstart;
        y = ystart;
        while (x < xend) {
          y = ystart;
          while (y < yend) {
            bottime.FindIndex(x, y, ii, jj);
            bottime(ii, jj) = twtgrid(i, j, nk - 1);
            y = y + dy2;
          }
          x = x + dx2;
        }
      } 
      else {
        for (size_t k = 0; k < nk; k++) {
          twtgrid(i, j, k) = -999.0;
        }
        if (ps_seismic && nmo_seismic) {
          for (size_t k = 0; k < nk; k++) {
            twtppgrid(i, j, k) = -999.0;
            twtssgrid(i, j, k) = -999.0;
          }
        }
      }
    }
  }
}


void SeismicRegridding::FindVp(SeismicParameters &seismic_parameters, size_t n_threads)
{
  NRLib::StormContGrid              &vpgrid = seismic_parameters.GetVpGrid();
  NRLib::StormContGrid              &vsgrid = seismic_parameters.GetVsGrid();
  NRLib::StormContGrid              &rhogrid = seismic_parameters.GetRhoGrid();
  std::vector<NRLib::StormContGrid> &extra_parameter_grid = seismic_parameters.GetExtraParametersGrids();
  const NRLib::EclipseGrid          &egrid = seismic_parameters.GetEclipseGrid();
  const NRLib::EclipseGeometry      &geometry = egrid.GetGeometry();

  size_t topk = seismic_parameters.GetTopK();
  size_t botk = seismic_parameters.GetBottomK();
  double zlimit = seismic_parameters.GetModelSettings()->GetZeroThicknessLimit();

  std::vector<double> constvp = seismic_parameters.GetModelSettings()->GetConstVp();
  std::vector<double> constvs = seismic_parameters.GetModelSettings()->GetConstVs();
  std::vector<double> constrho = seismic_parameters.GetModelSettings()->GetConstRho();
  std::vector<std::string> names = seismic_parameters.GetModelSettings()->GetParameterNames();
  std::vector<double> extra_parameter_default_values = seismic_parameters.GetModelSettings()->GetExtraParameterDefaultValues();
  std::vector<std::string> extra_parameter_names;
  if (seismic_parameters.GetModelSettings()->GetOutputExtraParametersTimeSegy()
    || seismic_parameters.GetModelSettings()->GetOutputExtraParametersDepthSegy()) { //only resample extra parameters if requested for output segy.
    extra_parameter_names = seismic_parameters.GetModelSettings()->GetExtraParameterNames();
  }

  //const NRLib::Grid<double> &vp_grid = egrid.GetParameter(names[0]);
  //const NRLib::Grid<double> &vs_grid = egrid.GetParameter(names[1]);
  //const NRLib::Grid<double> &rho_grid = egrid.GetParameter(names[2]);
  //std::vector<NRLib::Grid<double> > parameter_grid_from_eclipse;
  //for (size_t i = 0; i < extra_parameter_names.size(); ++i) {
  //  const NRLib::Grid<double> &one_parameter_grid = egrid.GetParameter(extra_parameter_names[i]);
  //  parameter_grid_from_eclipse.push_back(one_parameter_grid);
  //}


  //---for parallelisation
  //use copy-constructor, need copy as values are filled in.
  NRLib::Grid<double> vp_grid  = egrid.GetParameter(names[0]);
  NRLib::Grid<double> vs_grid  = egrid.GetParameter(names[1]);
  NRLib::Grid<double> rho_grid = egrid.GetParameter(names[2]);
  std::vector<NRLib::Grid<double> > parameter_grid_from_eclipse;
  for (size_t i = 0; i < extra_parameter_names.size(); ++i) {
    NRLib::Grid<double> one_parameter_grid = egrid.GetParameter(extra_parameter_names[i]);
    parameter_grid_from_eclipse.push_back(one_parameter_grid);
  }

  //-----prepare eclipsegrid - include default values and value above where delta < zlimit
  FillInGridValues(geometry, vp_grid,  constvp[1],  zlimit, constvp[0],  egrid.GetNI(), egrid.GetNJ(), topk, botk);
  FillInGridValues(geometry, vs_grid,  constvs[1],  zlimit, constvs[0],  egrid.GetNI(), egrid.GetNJ(), topk, botk);
  FillInGridValues(geometry, rho_grid, constrho[1], zlimit, constrho[0], egrid.GetNI(), egrid.GetNJ(), topk, botk);
  for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
    FillInGridValues(geometry, parameter_grid_from_eclipse[ii], extra_parameter_default_values[ii], zlimit, extra_parameter_default_values[ii], egrid.GetNI(), egrid.GetNJ(), topk, botk);
  }


  //NRLib::Grid2D<double> value_above_vp (egrid.GetNI(), egrid.GetNJ(), constvp[0]);
  //NRLib::Grid2D<double> value_above_vs (egrid.GetNI(), egrid.GetNJ(), constvs[0]);
  //NRLib::Grid2D<double> value_above_rho(egrid.GetNI(), egrid.GetNJ(), constrho[0]);
  //std::vector<NRLib::Grid2D<double> > value_above_extra_param;
  //for (size_t i = 0; i < extra_parameter_names.size(); ++i) {
  //  NRLib::Grid2D<double> value_above_grid(egrid.GetNI(), egrid.GetNJ(), 0.0);
  //  value_above_extra_param.push_back(value_above_grid);
  //}

  double vp_angle   = vpgrid.GetAngle();
  double cosvpangle = cos(vp_angle);
  double sinvpangle = sin(vp_angle);
  double x_min_rot  = vpgrid.GetXMin() * cos(vp_angle) + vpgrid.GetYMin() * sin(vp_angle);
  double y_min_rot  = vpgrid.GetYMin() * cos(vp_angle) - vpgrid.GetXMin() * sin(vp_angle);

  //default value in top
  for (size_t i = 0; i < vpgrid.GetNI(); i++) {
    for (size_t j = 0; j < vpgrid.GetNJ(); j++) {
      vpgrid(i, j, 0)  = static_cast<float>(constvp[0]);
      vsgrid(i, j, 0)  = static_cast<float>(constvs[0]);
      rhogrid(i, j, 0) = static_cast<float>(constrho[0]);
      for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
        extra_parameter_grid[ii](i, j, 0) = 0.0;
      }
    }
  }

  //blocking - for parallelisation - if n_threads > 1
  int nx = static_cast<int>(egrid.GetNI()) - 1;
  int ny = static_cast<int>(egrid.GetNJ()) - 1;
  int n_blocks_x = 1, n_blocks_y = 1, n_blocks = 1;
  int nxb = nx, nyb = ny;
  if (n_threads > 1){
    n_blocks_x = 10;
    n_blocks_y = 10;
    n_blocks = n_blocks_x * n_blocks_y;
    nxb = static_cast<int>(floor(static_cast<double>(nx) / static_cast<double>(n_blocks_x) + 0.5));
    nyb = static_cast<int>(floor(static_cast<double>(ny) / static_cast<double>(n_blocks_y) + 0.5));
  }

  int  chunk_size;
  chunk_size = 1;
#ifdef WITH_OMP
#pragma omp parallel for schedule(dynamic, chunk_size) num_threads(n_threads)
#endif
  for (int block = 0; block < n_blocks; ++block) {
    //std::cout << "block " << block << "\n";
    int block_x = int(block%n_blocks_x);
    int block_y = std::floor(static_cast<double>(block) / static_cast<double>(n_blocks_x));

    // find min and max of block
    int imin = std::max(static_cast<int>(block_x*nxb), 0);
    int imax;
    if (block_x == (n_blocks_x - 1))
      imax = nx;
    else
      imax = std::min(static_cast<int>((block_x + 1)*nxb), nx);

    int jmin = std::max(static_cast<int>(block_y*nyb), 0);
    int jmax;
    if (block_y == (n_blocks_y - 1))
      jmax = ny;
    else
      jmax = std::min(static_cast<int>((block_y + 1)*nyb), ny);

    double                    cell_min_x, cell_max_x, cell_min_y, cell_max_y;
    size_t                    start_ii, start_jj, end_ii, end_jj;
    std::vector<double>       x_rot(4), y_rot(4);
    std::vector<bool>         inside(4);
    std::vector<NRLib::Point> pt_vp(4), pt_vs(4), pt_rho(4);
    std::vector<std::vector<NRLib::Point> > pt_extra_param(extra_parameter_names.size());
    for (size_t i = 0; i < extra_parameter_names.size(); ++i)
      pt_extra_param[i] = pt_vp;

    for (size_t k = topk; k <= botk + 1; k++) {
      for (size_t i = static_cast<size_t>(imin); i < static_cast<size_t>(imax); ++i) {
        for (size_t j = static_cast<size_t>(jmin); j < static_cast<size_t>(jmax); ++j) {

          //for (size_t k = topk; k <= botk + 1; k++) {
            //for (size_t i = 0; i < egrid.GetNI() - 1; i++) {
              //for (size_t j = 0; j < egrid.GetNJ() - 1; j++) {
          if (geometry.IsPillarActive(i, j) && geometry.IsPillarActive(i + 1, j) && geometry.IsPillarActive(i, j + 1) && geometry.IsPillarActive(i + 1, j + 1) &&
            geometry.IsPillarActive(i + 2, j) && geometry.IsPillarActive(i + 2, j + 1) && geometry.IsPillarActive(i, j + 2) && geometry.IsPillarActive(i + 1, j + 2) &&
            geometry.IsPillarActive(i + 2, j + 2)) {
            if (k <= botk) {
              for (size_t pt = 0; pt < 4; ++pt)
                pt_vp[pt] = geometry.FindCellCenterPoint(i + int(pt % 2), j + int(floor(double(pt) / 2)), k);
            }
            else {
              for (size_t pt = 0; pt < 4; ++pt)
                pt_vp[pt] = geometry.FindCellCenterPoint(i + int(pt % 2), j + int(floor(double(pt) / 2)), k - 1);
            }
            for (size_t pt = 0; pt < 4; ++pt)
              inside[pt] = vpgrid.IsInside(pt_vp[pt].x, pt_vp[pt].y);
            if (inside[0] || inside[1] || inside[2] || inside[3]) {
              for (size_t pt = 0; pt < 4; ++pt) {
                pt_vs[pt] = pt_vp[pt];
                pt_rho[pt] = pt_vp[pt];
                for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
                  pt_extra_param[ii][pt] = pt_vp[pt];
                }
              }
              if (k == botk + 1) {
                for (size_t pt = 0; pt < 4; ++pt) {
                  pt_vp[pt].z = constvp[2];
                  pt_vs[pt].z = constvs[2];
                  pt_rho[pt].z = constrho[2];
                  for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
                    pt_extra_param[ii][pt].z = 0.0;
                  }
                }
              }
              else {
                for (size_t pt = 0; pt < 4; ++pt) {
                  pt_vp[pt].z = vp_grid(i + int(pt % 2), j + int(floor(double(pt / 2))), k);
                  pt_vs[pt].z = vs_grid(i + int(pt % 2), j + int(floor(double(pt / 2))), k);
                  pt_rho[pt].z = rho_grid(i + int(pt % 2), j + int(floor(double(pt / 2))), k);
                  //FindPointZValue(i+int(pt%2), j+int(floor(double(pt/2))), k, pt_vp[pt],  geometry, vp_grid,  value_above_vp,  constvp[1],  zlimit, constvp[0]);
                  //FindPointZValue(i+int(pt%2), j+int(floor(double(pt/2))), k, pt_vs[pt],  geometry, vs_grid,  value_above_vs,  constvs[1],  zlimit, constvs[0]);
                  //FindPointZValue(i+int(pt%2), j+int(floor(double(pt/2))), k, pt_rho[pt], geometry, rho_grid, value_above_rho, constrho[1], zlimit, constrho[0]);
                  for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
                    pt_extra_param[ii][pt].z = parameter_grid_from_eclipse[ii](i + int(pt % 2), j + int(floor(double(pt / 2))), k);
                    //FindPointZValue(i + int(pt % 2), j + int(floor(double(pt / 2))), k, pt_extra_param[ii][pt], geometry, parameter_grid_from_eclipse[ii], value_above_extra_param[ii], extra_parameter_default_values[ii], zlimit, extra_parameter_default_values[ii]);
                  }
                }
              }
              //for (size_t pt = 0; pt < 4; ++pt) {
              //  value_above_vp(i + int(pt % 2), j + int(floor(double(pt / 2)))) = pt_vp[pt].z;
              //  value_above_vs(i + int(pt % 2), j + int(floor(double(pt / 2)))) = pt_vs[pt].z;
              //  value_above_rho(i + int(pt % 2), j + int(floor(double(pt / 2)))) = pt_rho[pt].z;
              //  for (size_t ii = 0; ii < extra_parameter_names.size(); ++ii) {
              //    value_above_extra_param[ii](i + int(pt % 2), j + int(floor(double(pt / 2)))) = pt_extra_param[ii][pt].z;
              //  }
              //}

              bool triangulate_124 = Is124Triangulate(pt_vp);

              std::vector<NRLib::Triangle> triangles_elastic(6);
              std::vector<NRLib::Triangle> triangles_extra_param(extra_parameter_names.size() * 2);
              SetElasticTriangles(pt_vp, pt_vs, pt_rho, pt_extra_param, triangulate_124, triangles_elastic, triangles_extra_param);

              for (size_t pt = 0; pt < 4; ++pt) {
                x_rot[pt] = pt_vp[pt].x * cosvpangle + pt_vp[pt].y *sinvpangle;
                y_rot[pt] = pt_vp[pt].y * cosvpangle - pt_vp[pt].x *sinvpangle;
              }

              cell_min_x = min(min(x_rot[0], x_rot[1]), min(x_rot[2], x_rot[3]));
              cell_min_y = min(min(y_rot[0], y_rot[1]), min(y_rot[2], y_rot[3]));
              cell_max_x = max(max(x_rot[0], x_rot[1]), max(x_rot[2], x_rot[3]));
              cell_max_y = max(max(y_rot[0], y_rot[1]), max(y_rot[2], y_rot[3]));

              start_ii = static_cast<unsigned int>(max(0.0, (cell_min_x - x_min_rot) / vpgrid.GetDX() - 0.5));
              start_jj = static_cast<unsigned int>(max(0.0, (cell_min_y - y_min_rot) / vpgrid.GetDY() - 0.5));
              end_ii   = static_cast<unsigned int>(max(0.0, (cell_max_x - x_min_rot) / vpgrid.GetDX() + 1.0));
              end_jj   = static_cast<unsigned int>(max(0.0, (cell_max_y - y_min_rot) / vpgrid.GetDY() + 1.0));
              if (end_ii > vpgrid.GetNI()) {
                end_ii = vpgrid.GetNI();
              }
              if (end_jj > vpgrid.GetNJ()) {
                end_jj = vpgrid.GetNJ();
              }
              for (size_t ii = start_ii; ii < end_ii; ii++) {
                for (size_t jj = start_jj; jj < end_jj; jj++) {
                  double x, y, z;
                  vpgrid.FindCenterOfCell(ii, jj, 0, x, y, z);

                  NRLib::Point p1, p2;
                  p1.x = x;
                  p1.y = y;
                  p1.z = pt_vp[0].z;
                  p2 = p1;
                  p2.z += 1000;
                  NRLib::Line line(p1, p2, false, false);
                  NRLib::Point intersec_pt;
                  bool first_tri = false, sec_tri = false;
                  if (triangles_elastic[0].FindNearestPoint(line, intersec_pt) < 0.00000000001) {
                    first_tri = true;
                  }
                  else if (triangles_elastic[1].FindNearestPoint(line, intersec_pt) < 0.00000000001) {
                    sec_tri = true;
                  }
                  if (first_tri) {
                    vpgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    triangles_elastic[2].FindIntersection(line, intersec_pt, true);
                    vsgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    triangles_elastic[4].FindIntersection(line, intersec_pt, true);
                    rhogrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    for (size_t iii = 0; iii < extra_parameter_names.size(); ++iii) {
                      triangles_extra_param[iii * 2].FindIntersection(line, intersec_pt, true);
                      extra_parameter_grid[iii](ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    }
                  }
                  else if (sec_tri) {
                    vpgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    triangles_elastic[3].FindIntersection(line, intersec_pt, true);
                    vsgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    triangles_elastic[5].FindIntersection(line, intersec_pt, true);
                    rhogrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    for (size_t iii = 0; iii < extra_parameter_names.size(); ++iii) {
                      triangles_extra_param[iii * 2 + 1].FindIntersection(line, intersec_pt, true);
                      extra_parameter_grid[iii](ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
    ////-------------find edges---------------------
  for (size_t k = topk; k <= botk + 1; k++) {
    for (size_t i = 0; i < egrid.GetNI() - 1; i++) {
      //bot edge
      size_t j = 0;
      if (FindBotCell(geometry, egrid.GetNJ(), i, j)){
        FindVpEdges(geometry,
                    extra_parameter_names.size(),
                    seismic_parameters,
                   /* value_above_vp,
                    value_above_vs,
                    value_above_rho,
                    value_above_extra_param,*/
                    vp_grid,
                    vs_grid,
                    rho_grid,
                    parameter_grid_from_eclipse,
                    i, j, k,
                    false, true, false, false);
      }
      //top edge
      j = egrid.GetNJ() - 1;
      if (FindTopCell(geometry, i, j)) {
        FindVpEdges(geometry,
                    extra_parameter_names.size(),
                    seismic_parameters,
                    /*value_above_vp,
                    value_above_vs,
                    value_above_rho,
                    value_above_extra_param,*/
                    vp_grid,
                    vs_grid,
                    rho_grid,
                    parameter_grid_from_eclipse,
                    i, j, k,
                    true, false, false, false);
      }
    }
    for (size_t j = 0; j < egrid.GetNJ() - 1; ++j) {
      //left edge
      size_t i = 0;
      if (FindLeftCell(geometry, egrid.GetNI(), i, j)) {
        FindVpEdges(geometry,
                    extra_parameter_names.size(),
                    seismic_parameters,
                    /*value_above_vp,
                    value_above_vs,
                    value_above_rho,
                    value_above_extra_param,*/
                    vp_grid,
                    vs_grid,
                    rho_grid,
                    parameter_grid_from_eclipse,
                    i, j, k,
                    false, false, false, true);
      }
      //right edge
      i = egrid.GetNI() - 1;
      if (FindRightCell(geometry, i, j)) {
        FindVpEdges(geometry,
                    extra_parameter_names.size(),
                    seismic_parameters,
                    /*value_above_vp,
                    value_above_vs,
                    value_above_rho,
                    value_above_extra_param,*/
                    vp_grid,
                    vs_grid,
                    rho_grid,
                    parameter_grid_from_eclipse,
                    i, j, k,
                    false, false, true, false);
      }
    }
    //-------------find corners---------------------
    //bot left
    size_t i = 0;
    size_t j = 0;
    std::vector<NRLib::Point> pt_vp(4);
    FindCornerCellPoints(geometry,
                         pt_vp,
                         i,
                         j,
                         k,
                         botk);
    FindVpCorners(geometry,
                  extra_parameter_names.size(),
                  seismic_parameters,
                  /*value_above_vp,
                  value_above_vs,
                  value_above_rho,
                  value_above_extra_param,*/
                  vp_grid,
                  vs_grid,
                  rho_grid,
                  parameter_grid_from_eclipse,
                  i, j, k, pt_vp);
    //top left
    j = egrid.GetNJ() - 1;
    FindCornerCellPoints(geometry,
                         pt_vp,
                         i,
                         j,
                         k,
                         botk);
    FindVpCorners(geometry,
                  extra_parameter_names.size(),
                  seismic_parameters,
                  /*value_above_vp,
                  value_above_vs,
                  value_above_rho,
                  value_above_extra_param,*/
                  vp_grid,
                  vs_grid,
                  rho_grid,
                  parameter_grid_from_eclipse,
                  i, j, k, pt_vp);
    //top right
    i = egrid.GetNI() - 1;
    FindCornerCellPoints(geometry,
                         pt_vp,
                         i,
                         j,
                         k,
                         botk);
    FindVpCorners(geometry,
                  extra_parameter_names.size(),
                  seismic_parameters,
                  /*value_above_vp,
                  value_above_vs,
                  value_above_rho,
                  value_above_extra_param,*/
                  vp_grid,
                  vs_grid,
                  rho_grid,
                  parameter_grid_from_eclipse,
                  i, j, k, pt_vp);
    //bot right
    j = 0;
    FindCornerCellPoints(geometry,
                         pt_vp,
                         i,
                         j,
                         k,
                         botk);
    FindVpCorners(geometry,
                  extra_parameter_names.size(),
                  seismic_parameters,
                  /*value_above_vp,
                  value_above_vs,
                  value_above_rho,
                  value_above_extra_param,*/
                  vp_grid,
                  vs_grid,
                  rho_grid,
                  parameter_grid_from_eclipse,
                  i, j, k, pt_vp);
  }
}

void SeismicRegridding::FindVpEdges(const NRLib::EclipseGeometry        &geometry,
                                    size_t                               n_extra_param,
                                    SeismicParameters                   &seismic_parameters,
                                    /*NRLib::Grid2D<double>               &value_above_vp,
                                    NRLib::Grid2D<double>               &value_above_vs,
                                    NRLib::Grid2D<double>               &value_above_rho,
                                    std::vector<NRLib::Grid2D<double> > &value_above_extra_param,*/
                                    const NRLib::Grid<double>           &vp_grid,
                                    const NRLib::Grid<double>           &vs_grid,
                                    const NRLib::Grid<double>           &rho_grid,
                                    std::vector<NRLib::Grid<double> >   & parameter_grid_from_eclipse,
                                    size_t i, size_t j, size_t k,
                                    bool top, bool bot, bool right, bool left)
{
  NRLib::StormContGrid &vpgrid  = seismic_parameters.GetVpGrid();
  NRLib::StormContGrid &vsgrid  = seismic_parameters.GetVsGrid();
  NRLib::StormContGrid &rhogrid = seismic_parameters.GetRhoGrid();
  std::vector<NRLib::StormContGrid> &extra_parameter_grid = seismic_parameters.GetExtraParametersGrids();

  std::vector<double> constvp                        = seismic_parameters.GetModelSettings()->GetConstVp();
  std::vector<double> constvs                        = seismic_parameters.GetModelSettings()->GetConstVs();
  std::vector<double> constrho                       = seismic_parameters.GetModelSettings()->GetConstRho();
  std::vector<double> extra_parameter_default_values = seismic_parameters.GetModelSettings()->GetExtraParameterDefaultValues();

  size_t topk    = seismic_parameters.GetTopK();
  size_t botk    = seismic_parameters.GetBottomK();
  double zlimit  = seismic_parameters.GetModelSettings()->GetZeroThicknessLimit();

  double vp_angle   = vpgrid.GetAngle();
  double cosvpangle = cos(vp_angle);
  double sinvpangle = sin(vp_angle);
  double x_min_rot  = vpgrid.GetXMin() * cos(vp_angle) + vpgrid.GetYMin() * sin(vp_angle);
  double y_min_rot  = vpgrid.GetYMin() * cos(vp_angle) - vpgrid.GetXMin() * sin(vp_angle);

  NRLib::Point              mid_edge1, mid_edge2;
  double                    cell_min_x, cell_max_x, cell_min_y, cell_max_y;
  size_t                    start_ii, start_jj, end_ii, end_jj;
  std::vector<double>       x_rot(4), y_rot(4);
  std::vector<bool>         inside(4);
  std::vector<NRLib::Point> pt_vp(4), pt_vs(4), pt_rho(4);
  std::vector<std::vector<NRLib::Point> > pt_extra_param(n_extra_param);
  for (size_t ii = 0; ii < n_extra_param; ++ii)
    pt_extra_param[ii] = pt_vp;

  std::vector<size_t> a_corn(4), b_corn(4), c_corn(4);
  GetCornerPointDir(a_corn, b_corn, c_corn, left, right, bot, top);

  size_t ic = i;
  size_t jc = j;
  if (bot || top)
    ic = i+1;
  else if (left || right)
    jc = j+1;

  if (k <= botk) {
    pt_vp[0] = geometry.FindCellCenterPoint(i,  j,  k);
    pt_vp[1] = geometry.FindCellCenterPoint(ic, jc, k);
    mid_edge1 = 0.5 * (geometry.FindCornerPoint(i,  j,  k, a_corn[0], b_corn[0], c_corn[0]) + geometry.FindCornerPoint(i,  j,  k, a_corn[1], b_corn[1], c_corn[1])) + 0.5 * (geometry.FindCornerPoint(i,  j,  k, a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(i,  j,  k, a_corn[3], b_corn[3], c_corn[3]));
    mid_edge2 = 0.5 * (geometry.FindCornerPoint(ic, jc, k, a_corn[0], b_corn[0], c_corn[0]) + geometry.FindCornerPoint(ic, jc, k, a_corn[1], b_corn[1], c_corn[1])) + 0.5 * (geometry.FindCornerPoint(ic, jc, k, a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(ic, jc, k, a_corn[3], b_corn[3], c_corn[3]));
  }
  else {
    pt_vp[0] = geometry.FindCellCenterPoint(i,  j,  k - 1);
    pt_vp[1] = geometry.FindCellCenterPoint(ic, jc, k - 1);
    mid_edge1 = 0.5 * (geometry.FindCornerPoint(i,  j,  k - 1, a_corn[0], b_corn[0], c_corn[0]) + geometry.FindCornerPoint(i,  j,  k - 1, a_corn[1], b_corn[1], c_corn[1])) + 0.5 * (geometry.FindCornerPoint(i,  j,  k - 1, a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(i,  j,  k - 1, a_corn[3], b_corn[3], c_corn[3]));
    mid_edge2 = 0.5 * (geometry.FindCornerPoint(ic, jc, k - 1, a_corn[0], b_corn[0], c_corn[0]) + geometry.FindCornerPoint(ic, jc, k - 1, a_corn[1], b_corn[1], c_corn[1])) + 0.5 * (geometry.FindCornerPoint(ic, jc, k - 1, a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(ic, jc, k - 1, a_corn[3], b_corn[3], c_corn[3]));
  }

  pt_vp[2]  = mid_edge1 - pt_vp[0];
  mid_edge1 = 0.5 * mid_edge1;
  pt_vp[3]  = mid_edge2 - pt_vp[1];
  for (size_t pt = 0; pt < 4;++pt)
    inside[pt] = vpgrid.IsInside(pt_vp[pt].x, pt_vp[pt].y);
  mid_edge2 = 0.5 * mid_edge2;

  if (inside[0] || inside[1] || inside[2] || inside[3]) {
    for (size_t pt = 0; pt < 4; ++pt){
      pt_vs[pt]  = pt_vp[pt];
      pt_rho[pt] = pt_vp[pt];
      for (size_t ii = 0; ii < n_extra_param; ++ii) {
        pt_extra_param[ii][pt] = pt_vp[pt];
      }
    }
    if (k == botk + 1) {
      for (size_t pt = 0; pt < 2; ++pt) { //nb, only loop two first points here
        pt_vp[pt].z  = constvp[2];
        pt_vs[pt].z  = constvs[2];
        pt_rho[pt].z = constrho[2];
        for (size_t ii = 0; ii < n_extra_param; ++ii) {
          pt_extra_param[ii][pt].z = 0.0;
        }
      }
    }
    else {
      pt_vp[0].z =   vp_grid (i, j, k);
      pt_vs[0].z =   vs_grid (i, j, k);
      pt_rho[0].z = rho_grid(i, j, k);
      //FindPointZValue(i, j, k, pt_vp[0],  geometry, vp_grid, value_above_vp,   constvp [1], zlimit, constvp [0]);
      //FindPointZValue(i, j, k, pt_vs[0],  geometry, vs_grid, value_above_vs,   constvs [1], zlimit, constvs [0]);
      //FindPointZValue(i, j, k, pt_rho[0], geometry, rho_grid, value_above_rho, constrho[1], zlimit, constrho[0]);
      pt_vp[1].z   = vp_grid(ic, jc, k);
      pt_vs[1].z   = vs_grid(ic, jc, k);
      pt_rho[1].z = rho_grid(ic, jc, k);
      //FindPointZValue(ic, jc, k, pt_vp[1],  geometry, vp_grid,  value_above_vp,  constvp[1],  zlimit, constvp [0]);
      //FindPointZValue(ic, jc, k, pt_vs[1],  geometry, vs_grid,  value_above_vs,  constvs[1],  zlimit, constvs [0]);
      //FindPointZValue(ic, jc, k, pt_rho[1], geometry, rho_grid, value_above_rho, constrho[1], zlimit, constrho[0]);

      for (size_t ii = 0; ii < n_extra_param; ++ii) {
        pt_extra_param[ii][0].z = parameter_grid_from_eclipse[ii](i,  j,  k);
        pt_extra_param[ii][1].z = parameter_grid_from_eclipse[ii](ic, jc, k);
        //FindPointZValue(i,  j,  k, pt_extra_param[ii][0], geometry, parameter_grid_from_eclipse[ii], value_above_extra_param[ii], extra_parameter_default_values[ii], zlimit, extra_parameter_default_values[ii]);
        //FindPointZValue(ic, jc, k, pt_extra_param[ii][1], geometry, parameter_grid_from_eclipse[ii], value_above_extra_param[ii], extra_parameter_default_values[ii], zlimit, extra_parameter_default_values[ii]);
      }
    }
    for (size_t pt = 2; pt < 4; ++pt) { //nb, only loop two last points here
      pt_vp[pt].z  = pt_vp[pt-2].z;
      pt_vs[pt].z  = pt_vs[pt-2].z;
      pt_rho[pt].z = pt_rho[pt-2].z;
      for (size_t ii = 0; ii < n_extra_param; ++ii) {
        pt_extra_param[ii][pt].z = pt_extra_param[ii][pt-2].z;
      }
    }

    bool triangulate_124 = Is124Triangulate(pt_vp);

    std::vector<NRLib::Triangle> triangles_elastic(6);
    std::vector<NRLib::Triangle> triangles_extra_param(n_extra_param*2);
    SetElasticTriangles(pt_vp, pt_vs, pt_rho, pt_extra_param, triangulate_124, triangles_elastic, triangles_extra_param);

    for (size_t pt = 0; pt < 4; ++pt) {
      x_rot[pt] = pt_vp[pt].x * cosvpangle + pt_vp[pt].y *sinvpangle;
      y_rot[pt] = pt_vp[pt].y * cosvpangle - pt_vp[pt].x *sinvpangle;
    }

    cell_min_x = min(min(x_rot[0], x_rot[1]), min(x_rot[2], x_rot[3]));
    cell_min_y = min(min(y_rot[0], y_rot[1]), min(y_rot[2], y_rot[3]));
    cell_max_x = max(max(x_rot[0], x_rot[1]), max(x_rot[2], x_rot[3]));
    cell_max_y = max(max(y_rot[0], y_rot[1]), max(y_rot[2], y_rot[3]));

    start_ii = static_cast<unsigned int>(max(0.0, (cell_min_x - x_min_rot) / vpgrid.GetDX() - 2.0));
    start_jj = static_cast<unsigned int>(max(0.0, (cell_min_y - y_min_rot) / vpgrid.GetDY() - 2.0));
    end_ii   = static_cast<unsigned int>(max(0.0, (cell_max_x - x_min_rot) / vpgrid.GetDX() + 2.0));
    end_jj   = static_cast<unsigned int>(max(0.0, (cell_max_y - y_min_rot) / vpgrid.GetDY() + 2.0));
    if (end_ii > vpgrid.GetNI()) {
      end_ii = vpgrid.GetNI();
    }
    if (end_jj > vpgrid.GetNJ()) {
      end_jj = vpgrid.GetNJ();
    }
    NRLib::Polygon inside_e_cells;
    inside_e_cells.AddPoint(pt_vp[0]);
    inside_e_cells.AddPoint(pt_vp[1]);
    inside_e_cells.AddPoint(mid_edge2);
    if (k <= botk) {
      inside_e_cells.AddPoint(0.5 * (geometry.FindCornerPoint(i, j, k,     a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(i, j, k,     a_corn[3], b_corn[3], c_corn[3])));
    } else {
      inside_e_cells.AddPoint(0.5 * (geometry.FindCornerPoint(i, j, k - 1, a_corn[2], b_corn[2], c_corn[2]) + geometry.FindCornerPoint(i, j, k - 1, a_corn[3], b_corn[3], c_corn[3])));
    }

    inside_e_cells.AddPoint(mid_edge1);
    for (size_t ii = start_ii; ii < end_ii; ii++) {
      for (size_t jj = start_jj; jj < end_jj; jj++) {
        double x, y, z;
        vpgrid.FindCenterOfCell(ii, jj, 0, x, y, z);
        NRLib::Point p1(x, y, 0.0);
        NRLib::Point p2(x, y, 1000.0);
        if (inside_e_cells.IsInsidePolygonXY(p1)) {
          NRLib::Line line(p1, p2, false, false);
          NRLib::Point intersec_pt;
          bool first_tri = false, sec_tri = false;
          if (triangles_elastic[0].FindNearestPoint(line, intersec_pt) < 0.00000000001) {
            first_tri = true;
          }
          else if (triangles_elastic[1].FindNearestPoint(line, intersec_pt) < 0.00000000001) {
            sec_tri = true;
          }
          if (first_tri) {
            vpgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            triangles_elastic[2].FindIntersection(line, intersec_pt, true);
            vsgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            triangles_elastic[4].FindIntersection(line, intersec_pt, true);
            rhogrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            for (size_t iii = 0; iii < n_extra_param; ++iii) {
              triangles_extra_param[iii * 2].FindIntersection(line, intersec_pt, true);
              extra_parameter_grid[iii](ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            }
          }
          else if (sec_tri) {
            vpgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            triangles_elastic[3].FindIntersection(line, intersec_pt, true);
            vsgrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            triangles_elastic[5].FindIntersection(line, intersec_pt, true);
            rhogrid(ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            for (size_t iii = 0; iii < n_extra_param; ++iii) {
              triangles_extra_param[iii * 2 + 1].FindIntersection(line, intersec_pt, true);
              extra_parameter_grid[iii](ii, jj, (k - topk) + 1) = static_cast<float>(intersec_pt.z);
            }
          }
        }
      }
    }
  }
}

void SeismicRegridding::FindVpCorners(const NRLib::EclipseGeometry        &geometry,
                                      size_t                              n_extra_param,
                                      SeismicParameters                   &seismic_parameters,
                                      /*NRLib::Grid2D<double>               &value_above_vp,
                                      NRLib::Grid2D<double>               &value_above_vs,
                                      NRLib::Grid2D<double>               &value_above_rho,
                                      std::vector<NRLib::Grid2D<double> > &value_above_extra_param,*/
                                      const NRLib::Grid<double>           &vp_grid,
                                      const NRLib::Grid<double>           &vs_grid,
                                      const NRLib::Grid<double>           &rho_grid,
                                      std::vector<NRLib::Grid<double> >   &parameter_grid_from_eclipse,
                                      size_t i, size_t j, size_t k,
                                      std::vector<NRLib::Point>           &pt_vp)
{
  NRLib::StormContGrid &vpgrid  = seismic_parameters.GetVpGrid();
  NRLib::StormContGrid &vsgrid  = seismic_parameters.GetVsGrid();
  NRLib::StormContGrid &rhogrid = seismic_parameters.GetRhoGrid();
  std::vector<NRLib::StormContGrid> &extra_parameter_grid = seismic_parameters.GetExtraParametersGrids();

  std::vector<double> constvp                        = seismic_parameters.GetModelSettings()->GetConstVp();
  std::vector<double> constvs                        = seismic_parameters.GetModelSettings()->GetConstVs();
  std::vector<double> constrho                       = seismic_parameters.GetModelSettings()->GetConstRho();
  std::vector<double> extra_parameter_default_values = seismic_parameters.GetModelSettings()->GetExtraParameterDefaultValues();

  size_t topk   = seismic_parameters.GetTopK();
  size_t botk   = seismic_parameters.GetBottomK();
  double zlimit = seismic_parameters.GetModelSettings()->GetZeroThicknessLimit();

  double vp_angle   = vpgrid.GetAngle();
  double cosvpangle = cos(vp_angle);
  double sinvpangle = sin(vp_angle);
  double x_min_rot  = vpgrid.GetXMin() * cos(vp_angle) + vpgrid.GetYMin() * sin(vp_angle);
  double y_min_rot  = vpgrid.GetYMin() * cos(vp_angle) - vpgrid.GetXMin() * sin(vp_angle);

  double                    cell_min_x, cell_max_x, cell_min_y, cell_max_y;
  size_t                    start_ii, start_jj, end_ii, end_jj;
  std::vector<double>       x_rot(4), y_rot(4);
  std::vector<bool>         inside(4);
  std::vector<NRLib::Point> pt_vs(4), pt_rho(4);
  std::vector<std::vector<NRLib::Point> > pt_extra_param(n_extra_param);
  for (size_t ii = 0; ii < n_extra_param; ++ii)
    pt_extra_param[ii] = pt_vs;

  for (size_t pt = 0; pt < 4;++pt)
    inside[pt] = vpgrid.IsInside(pt_vp[pt].x, pt_vp[pt].y);
  if (inside[0] || inside[1] || inside[2] || inside[3]) {
    pt_vs[0]  = pt_vp[0];
    pt_rho[0] = pt_vp[0];
    for (size_t ii = 0; ii < n_extra_param; ++ii) {
      pt_extra_param[ii][0] = pt_vp[0];
    }
    if (k == botk + 1) {
      pt_vp[3].z  = constvp[2];
      pt_vs[3].z  = constvs[2];
      pt_rho[3].z = constrho[2];
      for (size_t ii = 0; ii < n_extra_param; ++ii) {
        pt_extra_param[ii][3].z = 0.0;
      }
    } 
    else {
      pt_vp[3].z   = vp_grid(i, j, k);
      pt_vs[3].z   = vs_grid(i, j, k);
      pt_rho[3].z = rho_grid(i, j, k);
      //FindPointZValue(i, j, k, pt_vp[3],  geometry, vp_grid,  value_above_vp,  constvp [1], zlimit, constvp [0]);
      //FindPointZValue(i, j, k, pt_vs[3],  geometry, vs_grid,  value_above_vs,  constvs [1], zlimit, constvs [0]);
      //FindPointZValue(i, j, k, pt_rho[3], geometry, rho_grid, value_above_rho, constrho[1], zlimit, constrho[0]);

      for (size_t ii = 0; ii < n_extra_param; ++ii) {
        pt_extra_param[ii][3].z = parameter_grid_from_eclipse[ii](i, j, k);
        //FindPointZValue(i, j, k, pt_extra_param[ii][3], geometry, parameter_grid_from_eclipse[ii], value_above_extra_param[ii], extra_parameter_default_values[ii], zlimit, extra_parameter_default_values[ii]);
      }
    }

    for (size_t pt = 0; pt < 4; ++pt) {
      x_rot[pt] = pt_vp[pt].x * cosvpangle + pt_vp[pt].y *sinvpangle;
      y_rot[pt] = pt_vp[pt].y * cosvpangle - pt_vp[pt].x *sinvpangle;
    }

    cell_min_x = min(min(x_rot[0], x_rot[1]), min(x_rot[2], x_rot[3]));
    cell_min_y = min(min(y_rot[0], y_rot[1]), min(y_rot[2], y_rot[3]));
    cell_max_x = max(max(x_rot[0], x_rot[1]), max(x_rot[2], x_rot[3]));
    cell_max_y = max(max(y_rot[0], y_rot[1]), max(y_rot[2], y_rot[3]));

    start_ii = static_cast<unsigned int>(max(0.0, (cell_min_x - x_min_rot) / vpgrid.GetDX() - 2.0));
    start_jj = static_cast<unsigned int>(max(0.0, (cell_min_y - y_min_rot) / vpgrid.GetDY() - 2.0));
    end_ii   = static_cast<unsigned int>(max(0.0, (cell_max_x - x_min_rot) / vpgrid.GetDX() + 2.0));
    end_jj   = static_cast<unsigned int>(max(0.0, (cell_max_y - y_min_rot) / vpgrid.GetDY() + 2.0));

    if (end_ii > vpgrid.GetNI()) {
      end_ii = vpgrid.GetNI();
    }
    if (end_jj > vpgrid.GetNJ()) {
      end_jj = vpgrid.GetNJ();
    }
    NRLib::Polygon inside_e_cells;
    inside_e_cells.AddPoint(pt_vp[0]);
    inside_e_cells.AddPoint(pt_vp[1]);
    inside_e_cells.AddPoint(pt_vp[3]);
    inside_e_cells.AddPoint(pt_vp[2]);
    for (size_t ii = start_ii; ii < end_ii; ii++) {
      for (size_t jj = start_jj; jj < end_jj; jj++) {
        double x, y, z;
        vpgrid.FindCenterOfCell(ii, jj, 0, x, y, z); 
        NRLib::Point p1(x, y, 0.0);
        if (inside_e_cells.IsInsidePolygonXY(p1)) {
          vpgrid(ii, jj, (k - topk) + 1)  = static_cast<float>(pt_vp[3].z);
          vsgrid(ii, jj, (k - topk) + 1)  = static_cast<float>(pt_vs[3].z);
          rhogrid(ii, jj, (k - topk) + 1) = static_cast<float>(pt_rho[3].z);
          for (size_t iii = 0; iii < n_extra_param; ++iii) {
            extra_parameter_grid[iii](ii, jj, (k - topk) + 1) = static_cast<float>(pt_extra_param[iii][3].z);
          }
        }
      }
    }
  }
}

void SeismicRegridding::SetElasticTriangles(std::vector<NRLib::Point>               & pt_vp,
                                            std::vector<NRLib::Point>               & pt_vs,
                                            std::vector<NRLib::Point>               & pt_rho,
                                            std::vector<std::vector<NRLib::Point> > & pt_extra_param,
                                            bool                                      triangulate_124,
                                            std::vector<NRLib::Triangle>            & triangles_elastic,
                                            std::vector<NRLib::Triangle>            & triangles_extra_param)
{
  if (triangulate_124) {
    triangles_elastic[0].SetCornerPoints(pt_vp[0],  pt_vp[1],  pt_vp[3]);
    triangles_elastic[1].SetCornerPoints(pt_vp[0],  pt_vp[2],  pt_vp[3]);
    triangles_elastic[2].SetCornerPoints(pt_vs[0],  pt_vs[1],  pt_vs[3]);
    triangles_elastic[3].SetCornerPoints(pt_vs[0],  pt_vs[2],  pt_vs[3]);
    triangles_elastic[4].SetCornerPoints(pt_rho[0], pt_rho[1], pt_rho[3]);
    triangles_elastic[5].SetCornerPoints(pt_rho[0], pt_rho[2], pt_rho[3]);
    for (size_t ii = 0; ii < triangles_extra_param.size()/2; ++ii){
      triangles_extra_param[ii*2].SetCornerPoints(pt_extra_param[ii][0], pt_extra_param[ii][1], pt_extra_param[ii][3]);
      triangles_extra_param[ii*2 + 1].SetCornerPoints(pt_extra_param[ii][0], pt_extra_param[ii][2], pt_extra_param[ii][3]);
    }
  }
  else {
    triangles_elastic[0].SetCornerPoints(pt_vp[0],  pt_vp[1],  pt_vp[2]);
    triangles_elastic[1].SetCornerPoints(pt_vp[1],  pt_vp[2],  pt_vp[3]);
    triangles_elastic[2].SetCornerPoints(pt_vs[0],  pt_vs[1],  pt_vs[2]);
    triangles_elastic[3].SetCornerPoints(pt_vs[1],  pt_vs[2],  pt_vs[3]);
    triangles_elastic[4].SetCornerPoints(pt_rho[0], pt_rho[1], pt_rho[2]);
    triangles_elastic[5].SetCornerPoints(pt_rho[1], pt_rho[2], pt_rho[3]);
    for (size_t ii = 0; ii < triangles_extra_param.size()/2; ++ii){
      triangles_extra_param[ii*2].SetCornerPoints(pt_extra_param[ii][0], pt_extra_param[ii][1], pt_extra_param[ii][2]);
      triangles_extra_param[ii*2 + 1].SetCornerPoints(pt_extra_param[ii][1], pt_extra_param[ii][2], pt_extra_param[ii][3]);
    }
  }
}

bool SeismicRegridding::Is124Triangulate(std::vector<NRLib::Point> pt_vp)
{
  bool triangulate_124 = true;
  NRLib::Point vec1, vec2;
  vec1   = pt_vp[0] - pt_vp[1];
  vec1.z = 0;
  vec2   = pt_vp[3] - pt_vp[1];
  vec2.z = 0;
  double delaunay_angle = vec1.GetAngle(vec2);
  vec1   = pt_vp[0] - pt_vp[2];
  vec1.z = 0;
  vec2   = pt_vp[3] - pt_vp[2];
  vec2.z = 0;
  delaunay_angle += vec1.GetAngle(vec2);
  if (delaunay_angle > NRLib::Pi) {
    triangulate_124 = false;
  }
  return triangulate_124;
}

void SeismicRegridding::GetCornerPointDir(std::vector<size_t> &a,
                                          std::vector<size_t> &b,
                                          std::vector<size_t> &c, 
                                          bool                 left,
                                          bool                 right,
                                          bool                 bot,
                                          bool                 top)
{
  if (top || bot || left) {
    a[0] = 0;
    a[1] = 0;
  }
  else {
    a[0] = 1;
    a[1] = 1;
  }
  if (bot || left || right) {
    b[0] = 0;
    b[1] = 0;
  }
  else {
    b[0] = 1;
    b[1] = 1;
  }
  if (top || bot || right) {
    a[2] = 1;
    a[3] = 1;
  }
  else {
    a[2] = 0;
    a[3] = 0;
  }
  if (top || left || right) {
    b[2] = 1;
    b[3] = 1;
  }
  else {
    b[2] = 0;
    b[3] = 0;
  }
  c[0] = 0;
  c[1] = 1;
  c[2] = 0;
  c[3] = 1;
}

bool SeismicRegridding::FindTopCell(const NRLib::EclipseGeometry &geometry,
                                    size_t  i,
                                    size_t &jj)
{
  int j = static_cast<int>(jj);
  while (j >= 0 && !(geometry.IsPillarActive(i, j) && geometry.IsPillarActive(i + 1, j) 
    && geometry.IsPillarActive(i, j + 1) && geometry.IsPillarActive(i + 1, j + 1)
    && geometry.IsPillarActive(i + 2, j) && geometry.IsPillarActive(i + 2, j + 1))) {
      j--;
  }
  if (j >= 0){
    jj = static_cast<size_t>(j);
    return true;
  }
  else
    return false;
}

bool SeismicRegridding::FindBotCell(const NRLib::EclipseGeometry &geometry,
                                    size_t  nj,
                                    size_t  i,
                                    size_t &j)
{
  while (j < nj && !(geometry.IsPillarActive(i, j) && geometry.IsPillarActive(i + 1, j) 
    && geometry.IsPillarActive(i, j + 1) && geometry.IsPillarActive(i + 1, j + 1)
    && geometry.IsPillarActive(i + 2, j) && geometry.IsPillarActive(i + 2, j + 1))) {
      j++;
  }
  if (j < nj)
    return true;
  else
    return false;
}

bool SeismicRegridding::FindLeftCell(const NRLib::EclipseGeometry &geometry,
                                     size_t  ni,
                                     size_t &i,
                                     size_t  j)
{
  while (i < ni && !(geometry.IsPillarActive(i, j) && geometry.IsPillarActive(i, j + 1) 
    && geometry.IsPillarActive(i + 1, j) && geometry.IsPillarActive(i + 1, j + 1)
    && geometry.IsPillarActive(i, j + 2) && geometry.IsPillarActive(i + 1, j + 2))) {
      i++;
  }
  if (i < ni)
    return true;
  else
    return false;
}

bool SeismicRegridding::FindRightCell(const NRLib::EclipseGeometry &geometry,
                                      size_t &ii,
                                      size_t  j)
{
  int i = static_cast<int>(ii);
  while (i >= 0 && !(geometry.IsPillarActive(i, j) && geometry.IsPillarActive(i, j + 1)
    && geometry.IsPillarActive(i + 1, j) && geometry.IsPillarActive(i + 1, j + 1)
    && geometry.IsPillarActive(i, j + 2) && geometry.IsPillarActive(i + 1, j + 2))) {
      i--;
  }
  if (i >= 0){
    ii = static_cast<size_t>(i);
    return true;
  }
  else
    return false;
}

void SeismicRegridding::FindCornerCellPoints(const NRLib::EclipseGeometry &geometry,
                                             std::vector<NRLib::Point>    &pt_vp,
                                             size_t                        i,
                                             size_t                        j,
                                             size_t                        k,
                                             size_t                        botk)
{
  if (k > botk)
    k = k - 1;
  if (i == 0 && j == 0) {//bot left 1243 
    pt_vp[0] =        0.5 * (geometry.FindCornerPoint(i, j, k, 0, 0, 0) + geometry.FindCornerPoint(i, j, k, 0, 0, 1));
    pt_vp[1] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 1, 0, 0) + geometry.FindCornerPoint(i, j, k, 1, 0, 1)) + pt_vp[0]);
    pt_vp[3] =               geometry.FindCellCenterPoint(i, j, k);
    pt_vp[2] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 0, 1, 0) + geometry.FindCornerPoint(i, j, k, 0, 1, 1)) + pt_vp[0]);
  }
  else if (i == 0 && j > 0) {//top left 3124
    pt_vp[2] =        0.5 * (geometry.FindCornerPoint(i, j, k, 0, 1, 0) + geometry.FindCornerPoint(i, j, k, 0, 1, 1));
    pt_vp[0] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 0, 0, 0) + geometry.FindCornerPoint(i, j, k, 0, 0, 1)) + pt_vp[2]);
    pt_vp[1] =               geometry.FindCellCenterPoint(i, j, k);
    pt_vp[3] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 1, 1, 0) + geometry.FindCornerPoint(i, j, k, 1, 1, 1)) + pt_vp[2]);
  }
  else if (i > 0 && j == 0) {//bot right 2134
    pt_vp[1] =        0.5 * (geometry.FindCornerPoint(i, j, k, 1, 0, 0) + geometry.FindCornerPoint(i, j, k, 1, 0, 1));
    pt_vp[0] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 0, 0, 0) + geometry.FindCornerPoint(i, j, k, 0, 0, 1)) + pt_vp[1]);
    pt_vp[2] =               geometry.FindCellCenterPoint(i, j, k);
    pt_vp[3] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 1, 1, 0) + geometry.FindCornerPoint(i, j, k, 1, 1, 1)) + pt_vp[1]);
  }
  else {//top right 4213
    pt_vp[3] =        0.5 * (geometry.FindCornerPoint(i, j, k, 1, 1, 0) + geometry.FindCornerPoint(i, j, k, 1, 1, 1));
    pt_vp[1] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 1, 0, 0) + geometry.FindCornerPoint(i, j, k, 1, 0, 1)) + pt_vp[3]);
    pt_vp[0] =               geometry.FindCellCenterPoint(i, j, k);
    pt_vp[2] = 0.5 * (0.5 * (geometry.FindCornerPoint(i, j, k, 0, 1, 0) + geometry.FindCornerPoint(i, j, k, 0, 1, 1)) + pt_vp[3]);
  }
}


void SeismicRegridding::VpPostProcess(SeismicParameters &seismic_parameters)
{
  NRLib::StormContGrid &vpgrid  = seismic_parameters.GetVpGrid();
  NRLib::StormContGrid &vsgrid  = seismic_parameters.GetVsGrid();
  NRLib::StormContGrid &rhogrid = seismic_parameters.GetRhoGrid();

  std::vector<double> constvp   = seismic_parameters.GetModelSettings()->GetConstVp();
  std::vector<double> constvs   = seismic_parameters.GetModelSettings()->GetConstVs();
  std::vector<double> constrho  = seismic_parameters.GetModelSettings()->GetConstRho();

  bool default_underburden = seismic_parameters.GetModelSettings()->GetDefaultUnderburden();


  float missing = seismic_parameters.GetMissingVal();
  bool found_bot = false;
  for (size_t i = 0; i < vpgrid.GetNI(); ++i) {
    for (size_t j = 0; j < vpgrid.GetNJ(); ++j) {
      if (default_underburden) {
        found_bot = false;
        for (size_t k = vpgrid.GetNK() - 1; k > 0; --k) {
          if (found_bot && vpgrid(i, j, k) == missing) {
            vpgrid (i, j, k) = constvp [1];
            vsgrid (i, j, k) = constvs [1];
            rhogrid(i, j, k) = constrho[1];
          }
          else if (found_bot == false && vpgrid(i, j, k) != missing) {
            found_bot = true;
            for (size_t kk = vpgrid.GetNK() - 1; kk > k; --kk) {
              vpgrid (i, j, kk) = constvp [2];
              vsgrid (i, j, kk) = constvs [2];
              rhogrid(i, j, kk) = constrho[2];
            }
          }
        }
        if (found_bot == false) {
          for (size_t k = 0; k < vpgrid.GetNK();++k) {
            vpgrid (i, j, k) = constvp [1];
            vsgrid (i, j, k) = constvs [1];
            rhogrid(i, j, k) = constrho[1];
          }
        }
      }
      else {
        found_bot = false;
        for (size_t k = vpgrid.GetNK() - 1; k > 0; --k) {
          if (found_bot && vpgrid(i, j, k) == missing) {
            vpgrid (i, j, k) = constvp [1];
            vsgrid (i, j, k) = constvs [1];
            rhogrid(i, j, k) = constrho[1];
          }
          else if (found_bot == false && vpgrid(i, j, k) != missing) {
            found_bot = true;
            for (size_t kk = vpgrid.GetNK() - 1; kk > k; --kk) {
              vpgrid (i, j, kk) = vpgrid (i, j, k);
              vsgrid (i, j, kk) = vsgrid (i, j, k);
              rhogrid(i, j, kk) = rhogrid(i, j, k);
            }
          }
        }
        if (found_bot == false) {
          for (size_t k = 0; k < vpgrid.GetNK();++k) {
            vpgrid (i, j, k) = constvp [1];
            vsgrid (i, j, k) = constvs [1];
            rhogrid(i, j, k) = constrho[1];
          }
        }
      }
    }
  }
}


