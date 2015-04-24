#include <seismic_forward.hpp>

#include <physics/wavelet.hpp>

#include <seismic_geometry.hpp>
#include <seismic_output.hpp>


void SeismicForward::seismicForward(SeismicParameters &seismic_parameters) {
  ModelSettings * model_settings = seismic_parameters.modelSettings();
  size_t nx = seismic_parameters.seismicGeometry()->nx();
  size_t ny = seismic_parameters.seismicGeometry()->ny();
  size_t nz = seismic_parameters.seismicGeometry()->nz();
  size_t nt = seismic_parameters.seismicGeometry()->nt();

  size_t nzrefl = seismic_parameters.seismicGeometry()->zreflectorcount();
  size_t ntheta = seismic_parameters.nTheta();


  double dt = seismic_parameters.seismicGeometry()->dt();

  NRLib::StormContGrid &zgrid                 = seismic_parameters.zGrid();
  NRLib::StormContGrid &twtgrid               = seismic_parameters.twtGrid();
  std::vector<NRLib::StormContGrid> &rgridvec = seismic_parameters.rGrids();

  NRLib::RegularSurface<double> &toptime = seismic_parameters.topTime();
  NRLib::RegularSurface<double> &bottime = seismic_parameters.bottomTime();

  NRLib::Volume volume   = seismic_parameters.seismicGeometry()->createDepthVolume();
  NRLib::Volume volume_t = seismic_parameters.seismicGeometry()->createTimeVolume();

  double tmin            = seismic_parameters.seismicGeometry()->t0();
  double dz              = seismic_parameters.seismicGeometry()->dz();
  double d1              = seismic_parameters.seismicGeometry()->z0();
  double wavelet_scale   = seismic_parameters.waveletScale();
  Wavelet * wavelet      = seismic_parameters.wavelet();

  std::vector<double> constvp = seismic_parameters.modelSettings()->GetConstVp();

  NRLib::RegularSurface<double> &bottom_eclipse = seismic_parameters.bottomEclipse();


  bool time_output      = (model_settings->GetOutputSeismicTime()      || model_settings->GetOutputTimeSegy()  || model_settings->GetOutputSeismicStackTimeStorm()  || model_settings->GetOutputSeismicStackTimeSegy());
  bool depth_output     = (model_settings->GetOutputSeismicDepth()     || model_settings->GetOutputDepthSegy() || model_settings->GetOutputSeismicStackDepthStorm() || model_settings->GetOutputSeismicStackDepthSegy());
  bool timeshift_output = (model_settings->GetOutputSeismicTimeshift() || model_settings->GetOutputTimeshiftSegy());
  double memory_use     = static_cast<double>(4.0 * (nx * ny * nzrefl * (2 + ntheta) + nx * ny * nz * ntheta * depth_output + nx * ny * nt * ntheta * time_output + (0.5 * nx * ny * nz))); // last term is a buffer
  double memory_limit   = model_settings->GetMemoryLimit();
  //  cout << "Number of GB ram needed without writing to file:" << memory_use/1000000000.0 << "\n";
  //  double minimum_mem_use = static_cast<double>(4.0*(nx*ny*nzrefl*(2+ntheta)));
  //  cout << "Number of GB ram needed with writing to file:" << minimum_mem_use/1000000000.0 << "\n";

  NRLib::StormContGrid *twt_timeshift;
  if (model_settings->GetTwtFileName() != "") {
      twt_timeshift = new NRLib::StormContGrid(model_settings->GetTwtFileName());
      if (twtgrid.GetNI() != nx || twtgrid.GetNJ() != ny || twtgrid.GetNK() != nzrefl) {
          printf("TWT from file has wrong dimension. Aborting. \n");
          exit(1);
      }
  } else {
      twt_timeshift = NULL;
  }


  ///-----------------------------------------------------------------------------------------------
  ///----------------------------MEMORY USE < MEMORY LIMIT------------------------------------------
  ///-----------------------------------------------------------------------------------------------
  if (memory_use < memory_limit) {
    // ---- Create grids for seismic data
    std::vector<NRLib::StormContGrid> timegridvec(ntheta);
    std::vector<NRLib::StormContGrid> depthgridvec(ntheta);
    std::vector<NRLib::StormContGrid> timeshiftgridvec(ntheta);

    NRLib::StormContGrid timegrid(0, 0, 0);
    if (time_output) {
      timegrid = NRLib::StormContGrid(volume_t, nx, ny, nt);
    }
    NRLib::StormContGrid timeshiftgrid(0, 0, 0);
    if (timeshift_output) {
      timeshiftgrid = NRLib::StormContGrid(volume_t, nx, ny, nt);
    }
    NRLib::StormContGrid depthgrid(0, 0, 0);
    if (depth_output) {
      depthgrid = NRLib::StormContGrid(volume, nx, ny, nz);
    }

    for (size_t i = 0; i < ntheta; i++) {
      timegridvec[i] = timegrid;
      depthgridvec[i] = depthgrid;
      timeshiftgridvec[i] = timeshiftgrid;
    }

    //-----------------Generate seismic----------------------------
    if (model_settings->GetPSSeismic()) {
      printf("\nGenerating PS seismic:\n");
    }

    generateSeismic(rgridvec,
                    twtgrid,
                    zgrid,
                    *twt_timeshift,
                    timegridvec,
                    depthgridvec,
                    timeshiftgridvec,
                    wavelet,
                    dt,
                    bottom_eclipse,
                    toptime,
                    tmin, dz, d1,
                    constvp,
                    wavelet_scale,
                    time_output,
                    depth_output,
                    timeshift_output);


    seismic_parameters.deleteZandRandTWTGrids();
    if (twt_timeshift != NULL) {
      delete twt_timeshift;
    }


    if (model_settings->GetOutputTimeSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicTimeSegy(seismic_parameters, timegridvec);
    }

    if (model_settings->GetOutputSeismicTime()) {
      seismic_parameters.seismicOutput()->writeSeismicTimeStorm(seismic_parameters, timegridvec);
    }

    if (model_settings->GetOutputTimeshiftSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicTimeshiftSegy(seismic_parameters, timeshiftgridvec);
    }

    if (model_settings->GetOutputSeismicTimeshift()) {
      seismic_parameters.seismicOutput()->writeSeismicTimeshiftStorm(seismic_parameters, timeshiftgridvec);
    }

    if (model_settings->GetOutputDepthSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicDepthSegy(seismic_parameters, depthgridvec);
    }

    if (model_settings->GetOutputSeismicDepth()) {
      seismic_parameters.seismicOutput()->writeSeismicDepthStorm(seismic_parameters, depthgridvec);
    }

    if (model_settings->GetOutputSeismicStackTimeStorm() || model_settings->GetOutputSeismicStackTimeSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicStackTime(seismic_parameters, timeshiftgridvec);
    }

    if (model_settings->GetOutputSeismicStackTimeShiftStorm() || model_settings->GetOutputSeismicStackTimeShiftSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicStackTimeshift(seismic_parameters, timeshiftgridvec);
    }


    if (model_settings->GetOutputSeismicStackDepthStorm() || model_settings->GetOutputSeismicStackDepthSegy()) {
      seismic_parameters.seismicOutput()->writeSeismicStackDepth(seismic_parameters, depthgridvec);
    }

  }
  ///-----------------------------------------------------------------------------------------------
  ///----------------------------END(MEMORY USE < MEMORY LIMIT)-------------------------------------
  ///-----------------------------------------------------------------------------------------------

  ///-----------------------------------------------------------------------------------------------
  ///-------------------------------MEMORY USE TOO BIG----------------------------------------------
  ///-----------------------------------------------------------------------------------------------
  else {
        
    generateSeismicOnFile(rgridvec,
                          twtgrid,
                          zgrid,
                          *twt_timeshift,
                          wavelet,
                          dt,
                          nt, nz, nx, ny,
                          bottom_eclipse,
                          toptime,
                          tmin, dz, d1,
                          constvp,
                          wavelet_scale,
                          time_output,
                          depth_output,
                          timeshift_output);



    seismic_parameters.deleteZandRandTWTGrids();
    if (twt_timeshift != NULL) {
      delete twt_timeshift;
    }
    seismic_parameters.seismicOutput()->writeSeismicTimeSeismicOnFile(seismic_parameters, time_output);
    seismic_parameters.seismicOutput()->writeSeismicDepthSeismicOnFile(seismic_parameters, depth_output);
  }
  //-----------------------------------------------------------------------------------------------
  //-------------------------------END( MEMORY USE TOO BIG)----------------------------------------
  //-----------------------------------------------------------------------------------------------
  seismic_parameters.deleteWavelet();
  seismic_parameters.deleteGeometryAndOutput();
}

void SeismicForward::generateSeismic(std::vector<NRLib::StormContGrid> &rgridvec,
                                     NRLib::StormContGrid &twtgrid,
                                     NRLib::StormContGrid &zgrid,
                                     NRLib::StormContGrid &twt_timeshift,
                                     std::vector<NRLib::StormContGrid> &timegridvec,
                                     std::vector<NRLib::StormContGrid> &depthgridvec,
                                     std::vector<NRLib::StormContGrid> &timeshiftgridvec,
                                     Wavelet *wavelet,
                                     double dt,
                                     NRLib::RegularSurface<double> &bot,
                                     NRLib::RegularSurface<double> &toptime,
                                     double t0, double dz, double z0,
                                     std::vector<double> &constvp,
                                     double waveletScale,
                                     bool time_output,
                                     bool depth_output,
                                     bool timeshift_output) 
{
  size_t nt = timegridvec[0].GetNK();
  size_t nc = rgridvec[0].GetNK();
  size_t nz = depthgridvec[0].GetNK();
  std::vector<double> seis(rgridvec.size());
  double x, y, z;
  size_t nx, ny;
  double rickerLimit = wavelet->GetDepthAdjustmentFactor();

  if (time_output == true) {
    nx = timegridvec[0].GetNI();
    ny = timegridvec[0].GetNJ();
  } else {
    nx = depthgridvec[0].GetNI();
    ny = depthgridvec[0].GetNJ();
  }

  printf("\nComputing synthetic seismic:");
  float monitorSize = std::max(1.0f, static_cast<float>(nx * ny) * 0.02f);
  float nextMonitor = monitorSize;
  std::cout
    << "\n  0%       20%       40%       60%       80%      100%"
    << "\n  |    |    |    |    |    |    |    |    |    |    |  "
    << "\n  ^";
  double topt = 0.0;
  //  double start, stop;
  //  start=omp_get_wtime();
  //  #ifdef WITH_OMP
  // int n_threads = omp_get_num_procs();
  // int n_threads = 1;
  //  printf("Antall tr�der: %d \n", n_threads);
  //#pragma omp parallel for private(x,y,z,topt)  num_threads(n_threads)
  //#endif
  for (size_t i = 0; i < nx; i++) {
    for (size_t j = 0; j < ny; j++) {
      zgrid.FindCenterOfCell(i, j, 0, x, y, z);
      topt = toptime.GetZ(x, y);
      if (time_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double t = t0 + 0.5 * dt;

          for (size_t k = 0; k < nt; k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              timegridvec[l](i, j, k) = static_cast<float>(seis[l]);
            }
            t = t + dt;
          }
        } 
        else {
          for (size_t k = 0; k < nt; k++){
            for (size_t l = 0; l < rgridvec.size(); l++) {
              timegridvec[l](i, j, k) = 0.0;
            }
          }
        }
      }

      if (depth_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double z = z0 + 0.5 * dz;

          std::vector<double> zvec(zgrid.GetNK() + 2);
          std::vector<double> twt(zgrid.GetNK() + 2);

          zvec[0] = 0.0;
          twt[0] = 0.0;
          for (size_t k = 1; k <= zgrid.GetNK(); k++) {
            zvec[k] = zgrid(i, j, k - 1);
            twt[k] = twtgrid(i, j, k - 1);
          }
          zvec[zvec.size() - 1] = bot.GetZ(x, y);
          twt[twt.size() - 1] = twt[twt.size() - 2] + 2000.0 * (zvec[zvec.size() - 1] - zvec[zvec.size() - 2]) / constvp[2];


          for (size_t k = 0; k < nz; k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            double t = findTFromZ(z, zvec, twt);
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              depthgridvec[l](i, j, k) = static_cast<float>(seis[l]);
            }
            z = z + dz;

          }
        } else {
          for (size_t k = 0; k < nz; k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              depthgridvec[l](i, j, k) = 0.0;
            }
          }
        }
      }

      if (timeshift_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double tt = t0 + 0.5 * dt;

          std::vector<double> twt_shift(twt_timeshift.GetNK() + 1);
          std::vector<double> twt(twtgrid.GetNK() + 1);

          twt_shift[0] = 0.0;
          twt[0] = 0.0;
          for (size_t k = 1; k <= twt_timeshift.GetNK(); k++) {
            twt_shift[k] = twt_timeshift(i, j, k - 1);
            twt[k] = twtgrid(i, j, k - 1);
          }

          for (size_t k = 0; k < nt; k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            double t = findTFromZ(tt, twt_shift, twt);
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              timeshiftgridvec[l](i, j, k) = static_cast<float>(seis[l]);
            }
            tt = tt + dt;

          }
        } 
        else {
          for (size_t k = 0; k < nt; k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              timeshiftgridvec[l](i, j, k) = 0.0;
            }
          }
        }
      }

      if (ny * i + j + 1 >= static_cast<size_t>(nextMonitor)) {
        nextMonitor += monitorSize;
        std::cout << "^";
        fflush(stdout);
      }
    }
  }
  std::cout << "\n";
  // stop=omp_get_wtime();
  // double par_time=stop-start;

  // printf("Time: %f \n", par_time);
}


void SeismicForward::generateSeismicOnFile(std::vector<NRLib::StormContGrid> &rgridvec,
                                           NRLib::StormContGrid &twtgrid,
                                           NRLib::StormContGrid &zgrid,
                                           NRLib::StormContGrid &twt_timeshift,
                                           Wavelet *wavelet,
                                           double dt,
                                           int nt, int nz, int nx, int ny,
                                           NRLib::RegularSurface<double> &bot,
                                           NRLib::RegularSurface<double> &toptime,
                                           double t0, double dz, double z0,
                                           std::vector<double> &constvp,
                                           double waveletScale,
                                           bool time_output,
                                           bool depth_output,
                                           bool timeshift_output) 
{

  //size_t nt = timegridvec[0].GetNK();
  size_t nc = rgridvec[0].GetNK();
  // size_t nz = depthgridvec[0].GetNK();
  std::vector<double> seis(rgridvec.size());
  //double dz;
  double x, y, z;
  //size_t nx, ny;
  //double rickerLimit = 1150.0/wavelet->GetPeakFrequency();
  double rickerLimit = wavelet->GetDepthAdjustmentFactor();
  //  if(time_output == true) {
  //    nx = timegridvec[0].GetNI();
  //    ny = timegridvec[0].GetNJ();
  //  }
  //  else {
  //    nx = depthgridvec[0].GetNI();
  //    ny = depthgridvec[0].GetNJ();
  //  }
  int n_angles = rgridvec.size();
  std::ofstream *time_file = new std::ofstream[n_angles];
  std::ofstream *depth_file = new std::ofstream[n_angles];
  std::ofstream *timeshift_file = new std::ofstream[n_angles];
  if (time_output == true) {

    for (int i = 0; i < n_angles; i++) {
      std::string filename = "time_" + NRLib::ToString(i);
      NRLib::OpenWrite(time_file[i], filename, std::ios::out | std::ios::binary);
    }
  }
  if (depth_output == true) {

    for (int i = 0; i < n_angles; i++) {
      std::string filename = "depth_" + NRLib::ToString(i);
      // printf("%s\n", filename);
      NRLib::OpenWrite(depth_file[i], filename, std::ios::out | std::ios::binary);
    }
  }
  if (timeshift_output == true) {

    for (int i = 0; i < n_angles; i++) {
      std::string filename = "timeshift_" + NRLib::ToString(i);
      NRLib::OpenWrite(timeshift_file[i], filename, std::ios::out | std::ios::binary);
    }
  }
  printf("\nComputing synthetic seismic:");
  float monitorSize = std::max(1.0f, static_cast<float>(nx * ny) * 0.02f);
  float nextMonitor = monitorSize;
  std::cout
    << "\n  0%       20%       40%       60%       80%      100%"
    << "\n  |    |    |    |    |    |    |    |    |    |    |  "
    << "\n  ^";
  double topt = 0.0;

  for (size_t i = 0; i < static_cast<size_t>(nx); i++) {
    for (size_t j = 0; j < static_cast<size_t>(ny); j++) {
      zgrid.FindCenterOfCell(i, j, 0, x, y, z);
      topt = toptime.GetZ(x, y);
      if (time_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double t = t0 + 0.5 * dt;

          for (size_t k = 0; k < static_cast<size_t>(nt); k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(time_file[l], static_cast<float>(seis[l]));
            }
            t = t + dt;
          }
        } else
          for (size_t k = 0; k < static_cast<size_t>(nt); k++)
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(time_file[l], 0.0);
            }

      }

      if (depth_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double z = z0 + 0.5 * dz;

          std::vector<double> zvec(zgrid.GetNK() + 2);
          std::vector<double> twt(zgrid.GetNK() + 2);


          zvec[0] = 0.0;
          twt[0] = 0.0;
          for (size_t k = 1; k <= zgrid.GetNK(); k++) {
            zvec[k] = zgrid(i, j, k - 1);
            twt[k] = twtgrid(i, j, k - 1);
          }
          zvec[zvec.size() - 1] = bot.GetZ(x, y);
          twt[twt.size() - 1] = twt[twt.size() - 2] + 2000.0 * (zvec[zvec.size() - 1] - zvec[zvec.size() - 2]) / constvp[2];


          for (size_t k = 0; k < static_cast<size_t>(nz); k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            double t = findTFromZ(z, zvec, twt);
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(depth_file[l], static_cast<float>(seis[l]));
            }
            z = z + dz;

          }
        } else
          for (size_t k = 0; k < static_cast<size_t>(nz); k++)
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(depth_file[l], 0.0);
            }
      }

      if (timeshift_output == true) {
        if (toptime.IsMissing(topt) == false) {
          double tt = t0 + 0.5 * dt;

          std::vector<double> twt_shift(twt_timeshift.GetNK() + 1);
          std::vector<double> twt(twtgrid.GetNK() + 1);

          twt_shift[0] = 0.0;
          twt[0] = 0.0;
          for (size_t k = 1; k <= twt_timeshift.GetNK(); k++) {
            twt_shift[k] = twt_timeshift(i, j, k - 1);
            twt[k] = twtgrid(i, j, k - 1);
          }

          for (size_t k = 0; k < static_cast<size_t>(nt); k++) {
            for (size_t l = 0; l < rgridvec.size(); l++) {
              seis[l] = 0.0;
            }
            double t = findTFromZ(tt, twt_shift, twt);
            for (size_t kk = 0; kk < nc; kk++) {
              if (fabs(twtgrid(i, j, kk) - t) < rickerLimit) {
                double ricker = waveletScale * wavelet->FindWaveletPoint(twtgrid(i, j, kk) - t);
                for (size_t l = 0; l < rgridvec.size(); l++) {
                  seis[l] += rgridvec[l](i, j, kk) * ricker;
                }
              }
            }
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(timeshift_file[l], static_cast<float>(seis[l]));
            }
            tt = tt + dt;

          }
        } else
          for (size_t k = 0; k < static_cast<size_t>(nt); k++)
            for (size_t l = 0; l < rgridvec.size(); l++) {
              NRLib::WriteBinaryFloat(timeshift_file[l], 0.0);
            }

      }


      if (ny * i + j + 1 >= static_cast<size_t>(nextMonitor)) {
        nextMonitor += monitorSize;
        std::cout << "^";
        fflush(stdout);
      }

    }
  }
  std::cout << "\n";
  // printf("seismic done\n");
  delete[] time_file;
  delete[] depth_file;
  delete[] timeshift_file;
  // stop=omp_get_wtime();
  // double par_time=stop-start;

  // printf("Time: %f \n", par_time);
}

double SeismicForward::findTFromZ(double z, std::vector<double> &zvec, std::vector<double> &tvec) {
  double t;
  size_t i = 0;
  while (i < zvec.size() - 1 && z > zvec[i]) {
    i++;
  }

  if (i > 0) {
    double a = (zvec[i] - z) / (zvec[i] - zvec[i - 1]);
    t = a * tvec[i - 1] + (1 - a) * tvec[i];
  } else {
    t = tvec[0];
  }

  return t;
}