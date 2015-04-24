#ifndef SEISMIC_PARAMETERS_HPP
#define SEISMIC_PARAMETERS_HPP

#include <stdio.h>
#include <string>
#include <vector>
#include <nrlib/surface/regularsurface.hpp>
#include <nrlib/volume/volume.hpp>
#include "modelsettings.hpp"
#include "seismic_output.hpp"

class Wavelet;
class ModelSettings;
class SeismicGeometry;
class SeismicOutput;

namespace NRLib {
    class EclipseGrid;
    class EclipseGeometry;
    class SegyGeometry;
    class StormContGrid;
}


class SeismicParameters {
  public:
    SeismicParameters(ModelSettings *model_settings);

    ~SeismicParameters() {
    };

    NRLib::StormContGrid &zGrid()                              { return *zgrid_; };
    NRLib::StormContGrid &vpGrid()                             { return *vpgrid_; };
    NRLib::StormContGrid &vsGrid()                             { return *vsgrid_; };
    NRLib::StormContGrid &rhoGrid()                            { return *rhogrid_; };
    NRLib::StormContGrid &twtGrid()                            { return *twtgrid_; };
    std::vector<NRLib::StormContGrid> &rGrids()                { return *rgridvec_; };
    std::vector<NRLib::StormContGrid> &extraParametersGrids()  { return *extra_parameter_grid_; };
    NRLib::EclipseGrid &eclipseGrid()                          { return *eclipse_grid_; };

    void deleteEclipseGrid();
    void deleteParameterGrids();
    void deleteZandRandTWTGrids();
    void deleteWavelet();
    void deleteGeometryAndOutput();

    size_t topK()    { return top_k_; }
    size_t bottomK() { return bottom_k_; }
    double theta0()  { return theta_0_; }
    double dTheta()  { return dtheta_; }
    size_t nTheta()  { return ntheta_; }

    NRLib::RegularSurface<double> &topTime()       { return top_time_; };
    NRLib::RegularSurface<double> &bottomTime()    { return bot_time_; };

    NRLib::RegularSurface<double> &topEclipse()    { return topeclipse_; };
    NRLib::RegularSurface<double> &bottomEclipse() { return boteclipse_; };


    ModelSettings*       modelSettings()   { return model_settings_;};

    SeismicOutput*       seismicOutput()   { return seismic_output_; };
    SeismicGeometry*     seismicGeometry() { return seismic_geometry_; };

    NRLib::SegyGeometry* segyGeometry()    { return segy_geometry_; };
    Wavelet*             wavelet()         { return wavelet_; };
    double               waveletScale()    { return  wavelet_scale_; };

  private:
    ModelSettings *model_settings_;

    SeismicGeometry *seismic_geometry_;
    SeismicOutput *seismic_output_;

    size_t ntheta_;
    double theta_0_;
    double dtheta_;
    double theta_max_;

    Wavelet *wavelet_;
    double wavelet_scale_;

    NRLib::EclipseGrid *eclipse_grid_;

    size_t top_k_;
    size_t bottom_k_;

    NRLib::RegularSurface<double> top_time_;
    NRLib::RegularSurface<double> bot_time_;

    NRLib::RegularSurface<double> topeclipse_;
    NRLib::RegularSurface<double> boteclipse_;

    NRLib::SegyGeometry *segy_geometry_;

    NRLib::StormContGrid *zgrid_;
    NRLib::StormContGrid *vpgrid_;
    NRLib::StormContGrid *vsgrid_;
    NRLib::StormContGrid *rhogrid_;
    NRLib::StormContGrid *twtgrid_;
    std::vector<NRLib::StormContGrid> *rgridvec_;
    std::vector<NRLib::StormContGrid> *extra_parameter_grid_;

    void setupWavelet();

    void readEclipseGrid();

    void findGeometry();

    void findSurfaceGeometry();

    void calculateAngleSpan();

    void createGrids();
};

#endif
