#ifndef SEISMIC_OUTPUT_HPP
#define SEISMIC_OUTPUT_HPP

#include <stdio.h>
#include <string>
#include <vector>
#include "modelsettings.hpp"
#include "seismic_parameters.hpp"


#include <nrlib/surface/regularsurface.hpp>
#include <nrlib/stormgrid/stormcontgrid.hpp>

class SeismicParameters;

class SeismicOutput {
  public:
    SeismicOutput(ModelSettings *model_settings);

    void writeDepthSurfaces(const NRLib::RegularSurface<double> &top_eclipse, const NRLib::RegularSurface<double> &bottom_eclipse);

    void writeReflections(SeismicParameters &seismic_parameters, bool noise_added);

    void writeTimeSurfaces(SeismicParameters &seismic_parameters);
    void writeElasticParametersTimeSegy(SeismicParameters &seismic_parameters);
    void writeElasticParametersDepthSegy(SeismicParameters &seismic_parameters);
    void writeExtraParametersTimeSegy(SeismicParameters &seismic_parameters);
    void writeExtraParametersDepthSegy(SeismicParameters &seismic_parameters);

    void writeVpVsRho(SeismicParameters &seismic_parameters);
    void writeZValues(SeismicParameters &seismic_parameters);
    void writeTwt(SeismicParameters &seismic_parameters);

    void writeSeismicTimeSegy(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timegridvec);
    void writeSeismicTimeStorm(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timegridvec);
    void writeSeismicTimeshiftSegy(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timeshiftgridvec);
    void writeSeismicTimeshiftStorm(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timeshiftgridvec);
    void writeSeismicDepthSegy(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &depthgridvec);
    void writeSeismicDepthStorm(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &depthgridvec);

    void writeSeismicStackTime(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timegridvec);
    void writeSeismicStackTimeshift(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &timeshiftgridvec);
    void writeSeismicStackDepth(SeismicParameters &seismic_parameters, std::vector<NRLib::StormContGrid> &depthgridvec);
    void writeSeismicTimeSeismicOnFile(SeismicParameters &seismic_parameters, bool time_output);
    void writeSeismicDepthSeismicOnFile(SeismicParameters &seismic_parameters, bool depth_output);

  private:
    void generateParameterGridForOutput(NRLib::StormContGrid &input_grid, NRLib::StormContGrid &time_or_depth_grid, NRLib::StormContGrid &output_grid, double delta_time_or_depth, double zero_time_or_depth, NRLib::RegularSurface<double> &toptime);
    size_t findCellIndex(size_t i, size_t j, double target_k, NRLib::StormContGrid &grid);

    double top_time_window_;
    double bot_time_window_;
    bool   time_window_;
    bool   depth_window_;
    double top_depth_window_;
    double bot_depth_window_;

    std::string prefix_;
    std::string suffix_;

    std::vector<std::string> extra_parameter_names_;

    int inline_start_;
    int xline_start_;
    std::string inline_direction_;
    short scalco_;
    bool xline_x_axis_;
    int inline_step_;
    int xline_step_;
};

#endif
