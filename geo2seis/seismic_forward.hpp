#ifndef SEISMIC_FORWARD_HPP
#define SEISMIC_FORWARD_HPP

#include <stdio.h>
#include <string>
#include <vector>
#include "modelsettings.hpp"
#include <seismic_parameters.hpp>
#include <algorithm>

#include <utils/nmo_output.hpp>
#include <utils/output.hpp>
#include <utils/seis_output.hpp>
#include <utils/trace.hpp>
#include <utils/gen_seis_trace_params.hpp>
#include <tbb/concurrent_queue.h>

#include <nrlib/stormgrid/stormcontgrid.hpp>
#include <nrlib/surface/regularsurface.hpp>
//#include "nrlib/geometry/interpolation.hpp"


class SeismicForward {
  public:

    static void DoSeismicForward(SeismicParameters &seismic_parameters);

  private:

    static void MakeNMOSeismic(SeismicParameters &seismic_parameters);
    static void MakeSeismic(SeismicParameters &seismic_parameters);

    static tbb::concurrent_queue<Trace*> FindTracesInForward(SeismicParameters &seismic_parameters,
                                                             size_t            &n_traces);

    static bool GenerateTraceOk(SeismicParameters &seismic_parameters,
                                size_t i,
                                size_t j);

    static void WriteSeismicTraces(GenSeisTraceParams *param,
                                   Output             *seis_output);

    static void WriteTrace(ResultTrace        *result_trace,
                           SeismicParameters &seismic_parameters,
                           Output            *seis_output);

    static void GenerateSeismicTraces(Output             *seis_output,
                                      GenSeisTraceParams *param);

    static void GenerateNMOSeismicTraces(Output             *nmo_output,
                                         GenSeisTraceParams *param);


    static void SeisConvolutionNMO(NRLib::Grid2D<double>               &timegrid_pos,
                                   NRLib::Grid2D<double>               &refl_pos,
                                   NRLib::Grid2D<double>               &twtx,
                                   const NRLib::StormContGrid          &zgrid,
                                   const NRLib::RegularSurface<double> &toptime,
                                   Wavelet                             *wavelet,
                                   double                               waveletScale,
                                   const std::vector<double>           &offset,
                                   double                               t0,
                                   double                               dt,
                                   size_t                               i,
                                   size_t                               j,
                                   const std::vector<size_t>           &n_min,
                                   const std::vector<size_t>           &n_max);

    static void SeisConvolution(NRLib::Grid2D<double>               &timegrid_pos,
                                NRLib::Grid2D<double>               &refl_pos,
                                const std::vector<double>           &twt,
                                const NRLib::StormContGrid          &zgrid,
                                const NRLib::RegularSurface<double> &toptime,
                                Wavelet                             *wavelet,
                                double                               waveletScale,
                                const std::vector<double>           &theta_vec,
                                double                               t0,
                                double                               dt,
                                size_t                               i,
                                size_t                               j,
                                size_t           n_min,
                                  size_t           n_max);

    static void ConvertSeis(const std::vector<double>   &twt_vec,
                            const std::vector<double>   &twt_0,
                            const std::vector<double>   &zgrid_vec,
                            const std::vector<double>   &z_0,
                            const NRLib::Grid2D<double> &seismic,
                            NRLib::Grid2D<double>       &conv_seismic,
                            const size_t                &max_sample);


    static void NMOCorrect(const std::vector<double>   &t_in,
                           const NRLib::Grid2D<double> &data_in,
                           const NRLib::Grid2D<double> &t_out,
                           NRLib::Grid2D<double>       &data_out,
                           const std::vector<size_t>   &n_min,
                           const std::vector<size_t>   &n_max,
                           size_t                      &max_sample);

    static void FindNMOTheta(NRLib::Grid2D<double>     &thetagrid,
                             const std::vector<double> &twt_vec,
                             const std::vector<double> &vrms_vec,
                             const std::vector<double> &offset);


    static void FindTWTx(NRLib::Grid2D<double>     &twtx_grid,
                         const std::vector<double> &twt_vec,
                         const std::vector<double> &vrms_vec,
                         const std::vector<double> &offset);

    static void ExtrapolZandTwtVec(std::vector<double>        &zgrid_vec_extrapol,
                                   std::vector<double>        &twt_vec_extrapol,
                                   const std::vector<double>  &twt_vec,
                                   const NRLib::StormContGrid &zgrid,
                                   double                      z_bot,
                                   double                      vp_bot,
                                   double                      vs_bot,
                                   size_t                      i,
                                   size_t                      j,
                                   bool                        ps_seis);

    static void MonitorInitialize(size_t n_traces,
                                  float &monitor_size,
                                  float &next_monitor);

    static void Monitor(size_t trace,
                        float monitor_size,
                        float &next_monitor);

    static void PrintSeisType(bool                 nmo,
                              bool                 ps_seis,
                              std::vector<double> &off_theta_vec);

    static void PrintTime();
    static void PrintElapsedTime(time_t start_time);

    static std::vector<double> LinInterp1D(const std::vector<double> &x_in,
                                           const std::vector<double> &y_in,
                                           const std::vector<double> &x_out);

    static std::vector<double> SplineInterp1D(const std::vector<double> &x_in,
                                              const std::vector<double> &y_in,
                                              const std::vector<double> &x_out,
                                              double                     extrap_value);

};

#endif
