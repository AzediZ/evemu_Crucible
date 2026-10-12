/**
 * @name PlanetResource.h
 *   planet resource maps as the client reads them:  real spherical harmonics in the basis of the client's
 *   _PlanetResources.dll (Robin Green, "Spherical Harmonic Lighting: The Gritty Details"), coefficient i = l(l+1)+m,
 *   evaluated as GetValueAt(sh, azimuth, polar).  CreateSHFromBuffer takes bands^2 little endian float32 coefficients.
 *
 *   CCP's maps are not available.  the base map of a resource is generated from the planet and type (deterministic, no
 *   table), extraction programs deplete it around their heads and it grows back over time (piResourceDepletion).
 *
 * @Author:         F19
 * @date:   October 2026
 */


#ifndef EVEMU_PLANET_PLANETRESOURCE_H_
#define EVEMU_PLANET_PLANETRESOURCE_H_

#include "eve-server.h"


class PIResource
{
public:
    // bands of the server's maps.  the client gets up to 30 (planetResourceProximityLimits)
    static constexpr uint8 MaxBands = 30;     // constexpr:  std::min binds it by reference (needs a definition otherwise)
    // const.planetResourceMaxValue * 255 * 0.5:  a resource of this quality fills the client's quality bar
    static const double MaxDisplayQuality;

    struct Field {
        Field() : bands(0) { }
        uint8 bands;
        std::vector<float> coeff;       // bands^2

        // builder.GetValueAt(sh, azimuth, polar)
        double Value(double azimuth, double polar) const;
        // the first bands^2 coefficients as the client's buffer
        std::string GetBuffer(uint8 numBands) const;
    };

    // one extraction program's head, taking resource out of the map
    struct Depletion {
        uint32 depletionID;
        double latitude;
        double longitude;
        double headRadius;
        double amount;          // units the head extracts over the program
        int64 startTime;
        int64 duration;
    };

    // K(l, m) of the basis
    static double K(int l, int m);
    // the basis value of coefficient i = l(l+1)+m
    static double Basis(int i, double azimuth, double polar);
    // all basis values of the first bands^2 coefficients at a point
    static void EvalAll(uint8 bands, double azimuth, double polar, std::vector<double>& out);

    // the base map of a resource on a planet and its richness (the share of the security's richness base, 0.5 to 1)
    static void Generate(uint32 planetID, uint16 typeID, float trueSec, Field& field, double& richness);
    // how much of a depletion is in effect:  builds up over the program, then grows back (half-life PIDepletionRegenHours)
    static double DepletionFactor(const Depletion& row, int64 now);
    // the map with the depletions taken out
    static void ApplyDepletion(const Field& base, const std::vector<Depletion>& rows, int64 now, Field& final);
    // removes a zonal gaussian (angular sigma) of the given depth at a point from the field's coefficients
    static void SubtractGaussian(std::vector<double>& coeff, uint8 bands, double latitude, double longitude, double sigma, double depth);
};

#endif  // EVEMU_PLANET_PLANETRESOURCE_H_
