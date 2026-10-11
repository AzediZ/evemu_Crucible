/**
 * @name PlanetResource.cpp
 *   planet resource maps.  see PlanetResource.h
 *
 *   _PlanetResources.dll (Crucible, reverse engineered for F19):  SHBasisFunc is Green's real spherical harmonic,
 *     m > 0:  sqrt(2) K(l,m) cos(m phi) P(l,m,cos theta)
 *     m < 0:  sqrt(2) K(l,-m) sin(-m phi) P(l,-m,cos theta)
 *     m = 0:  K(l,0) P(l,0,cos theta)
 *   with K(l,m) = sqrt((2l+1)/4pi (l-|m|)!/(l+|m|)!) and P the associated Legendre polynomial with the Condon-Shortley phase.
 *   phi (azimuth, 0..2pi) is GetValueAt's first angle, theta (polar, 0..pi) the second.  the dll uses 2048 step lookup
 *   tables; this is exact (they agree to 0.2%).
 *
 * @Author:         F19
 * @date:   October 2026
 */

/*
 * PLANET__ERROR
 * PLANET__DEBUG
 * PLANET__TRACE
 */

#include <algorithm>
#include <random>

#include "eve-server.h"

#include "EVEServerConfig.h"
#include "planet/PlanetResource.h"
#include "system/CrimeWatch.h"


const double PIResource::MaxDisplayQuality = 1.21 * 255 * 0.5;

namespace {
    const double PI2 = 2.0 * M_PI;

    // K(l, m) for l, m < MaxBands
    struct KTable {
        double k[PIResource::MaxBands][PIResource::MaxBands];
        KTable() {
            for (int l = 0; l < PIResource::MaxBands; ++l)
                for (int m = 0; m <= l; ++m) {
                    // (l-m)! / (l+m)!
                    double ratio = 1.0;
                    for (int i = l - m + 1; i <= l + m; ++i)
                        ratio /= i;
                    k[l][m] = std::sqrt((2.0 * l + 1.0) / (4.0 * M_PI) * ratio);
                }
        }
    };
    const KTable& GetKTable()
    {
        static KTable table;
        return table;
    }

    // P(l, m, x) for all l < bands, m <= l (Green's recurrence, Condon-Shortley phase).  p[l * bands + m]
    void Legendre(uint8 bands, double x, std::vector<double>& p)
    {
        p.assign(bands * bands, 0.0);
        double somx2 = std::sqrt(std::max(0.0, (1.0 - x) * (1.0 + x)));
        double pmm = 1.0, fact = 1.0;
        for (int m = 0; m < bands; ++m) {
            if (m > 0) {
                pmm *= (-fact) * somx2;
                fact += 2.0;
            }
            p[m * bands + m] = pmm;
            if (m + 1 < bands)
                p[(m + 1) * bands + m] = x * (2.0 * m + 1.0) * pmm;
            for (int l = m + 2; l < bands; ++l)
                p[l * bands + m] = ((2.0 * l - 1.0) * x * p[(l - 1) * bands + m] - (l + m - 1.0) * p[(l - 2) * bands + m]) / (l - m);
        }
    }

    // P(l, 0, x) for l < bands
    void Legendre0(uint8 bands, double x, std::vector<double>& p)
    {
        p.assign(bands, 0.0);
        p[0] = 1.0;
        if (bands > 1)
            p[1] = x;
        for (int l = 2; l < bands; ++l)
            p[l] = ((2.0 * l - 1.0) * x * p[l - 1] - (l - 1.0) * p[l - 2]) / l;
    }
}

double PIResource::K(int l, int m)
{
    if ((l < 0) or (l >= MaxBands) or (std::abs(m) > l))
        return 0;
    return GetKTable().k[l][std::abs(m)];
}

void PIResource::EvalAll(uint8 bands, double azimuth, double polar, std::vector<double>& out)
{
    if (bands > MaxBands)
        bands = MaxBands;
    out.assign(bands * bands, 0.0);
    std::vector<double> p;
    Legendre(bands, std::cos(polar), p);
    const KTable& kt = GetKTable();
    for (int l = 0; l < bands; ++l) {
        out[l * (l + 1)] = kt.k[l][0] * p[l * bands];
        for (int m = 1; m <= l; ++m) {
            double v = M_SQRT2 * kt.k[l][m] * p[l * bands + m];
            out[l * (l + 1) + m] = v * std::cos(m * azimuth);
            out[l * (l + 1) - m] = v * std::sin(m * azimuth);
        }
    }
}

double PIResource::Basis(int i, double azimuth, double polar)
{
    int l = (int)std::sqrt((double)i);
    while (l * l > i)
        --l;
    while ((l + 1) * (l + 1) <= i)
        ++l;
    std::vector<double> out;
    EvalAll(l + 1, azimuth, polar, out);
    return out[i];
}

double PIResource::Field::Value(double azimuth, double polar) const
{
    if (bands == 0)
        return 0;
    std::vector<double> basis;
    EvalAll(bands, azimuth, polar, basis);
    double value = 0;
    for (size_t i = 0; (i < basis.size()) and (i < coeff.size()); ++i)
        value += basis[i] * coeff[i];
    return value;
}

std::string PIResource::Field::GetBuffer(uint8 numBands) const
{
    numBands = std::min(numBands, bands);
    std::string buffer;
    buffer.reserve(numBands * numBands * 4);
    for (size_t i = 0; i < (size_t)(numBands * numBands); ++i) {
        // little endian float32 (CreateSHFromBuffer)
        float value = coeff[i];
        uint32 bits = 0;
        memcpy(&bits, &value, 4);
        for (int b = 0; b < 4; ++b)
            buffer.push_back((char)((bits >> (8 * b)) & 0xFF));
    }
    return buffer;
}

void PIResource::Generate(uint32 planetID, uint16 typeID, float trueSec, Field& field, double& richness)
{
    /* a random field with a power spectrum falling off as (l+1)^-PIResourceSpectrum, shifted so 35% of the planet has
     *  nothing, scaled so the richest spot is MaxDisplayQuality x richness x PIResourceScale.
     *  Crucible planets are richer in low and null security and wormholes.
     */
    std::seed_seq seq{planetID, (uint32)typeID, (uint32)sConfig.cosmic.PIResourceSeed};
    std::mt19937 rng(seq);

    double securityBase = sConfig.cosmic.PIRichnessNullsec;
    switch (CrimeWatch::SecurityClass(trueSec)) {
        case Crime::SecClass::High: securityBase = sConfig.cosmic.PIRichnessHighsec; break;
        case Crime::SecClass::Low:  securityBase = sConfig.cosmic.PIRichnessLowsec;  break;
    }
    std::uniform_real_distribution<double> share(0.5, 1.0);
    richness = securityBase * share(rng);

    const uint8 bands = MaxBands;
    std::vector<double> coeff(bands * bands, 0.0);
    for (int l = 1; l < bands; ++l) {
        std::normal_distribution<double> dist(0.0, std::pow(l + 1.0, -(double)sConfig.cosmic.PIResourceSpectrum));
        for (int m = -l; m <= l; ++m)
            coeff[l * (l + 1) + m] = dist(rng);
    }

    // sample the sphere (64 x 128):  the legendre table per row, the azimuth terms per column
    const int rows = 64, cols = 128;
    std::vector<double> values;
    values.reserve(rows * cols);
    std::vector<double> p;
    const KTable& kt = GetKTable();
    std::vector<double> cosm(bands), sinm(bands);
    for (int r = 0; r < rows; ++r) {
        double polar = (r + 0.5) * M_PI / rows;
        Legendre(bands, std::cos(polar), p);
        for (int c = 0; c < cols; ++c) {
            double azimuth = PI2 * c / cols;
            for (int m = 0; m < bands; ++m) {
                cosm[m] = std::cos(m * azimuth);
                sinm[m] = std::sin(m * azimuth);
            }
            double value = 0;
            for (int l = 0; l < bands; ++l) {
                value += coeff[l * (l + 1)] * kt.k[l][0] * p[l * bands];
                for (int m = 1; m <= l; ++m) {
                    double v = M_SQRT2 * kt.k[l][m] * p[l * bands + m];
                    value += coeff[l * (l + 1) + m] * v * cosm[m] + coeff[l * (l + 1) - m] * v * sinm[m];
                }
            }
            values.push_back(value);
        }
    }
    std::vector<double> sorted(values);
    std::sort(sorted.begin(), sorted.end());
    double floor = sorted[(size_t)(sorted.size() * 0.35)];
    double top = sorted.back() - floor;
    // c00 * Y00 is a constant:  Y00 = 1 / (2 sqrt(pi))
    coeff[0] -= floor * 2.0 * std::sqrt(M_PI);
    double peak = MaxDisplayQuality * richness * sConfig.cosmic.PIResourceScale;
    double scale = (top > 0 ? peak / top : 0.0);

    field.bands = bands;
    field.coeff.resize(bands * bands);
    for (size_t i = 0; i < coeff.size(); ++i)
        field.coeff[i] = (float)(coeff[i] * scale);

    _log(PLANET__TRACE, "PIResource::Generate() - planet %u type %u:  richness %.3f, peak %.2f", planetID, typeID, richness, peak);
}

double PIResource::DepletionFactor(const Depletion& row, int64 now)
{
    if (now <= row.startTime)
        return 0;
    int64 end = row.startTime + row.duration;
    if ((row.duration > 0) and (now < end))
        return double(now - row.startTime) / row.duration;
    double halfLife = std::max(1.0, (double)sConfig.cosmic.PIDepletionRegenHours) * EvE::Time::Hour;
    return std::pow(0.5, double(now - end) / halfLife);
}

void PIResource::SubtractGaussian(std::vector<double>& coeff, uint8 bands, double latitude, double longitude, double sigma, double depth)
{
    /* a zonal function g(theta) has the coefficients g_l = 2pi int g(theta) K(l,0) P_l(cos theta) sin theta dtheta;
     *  centred on h it is sum_lm sqrt(4pi/(2l+1)) g_l Y_lm(h) Y_lm.  the band limited gaussian is scaled to have its
     *  depth at the centre.
     */
    if ((depth <= 0) or (sigma <= 0))
        return;
    const int steps = 512;
    double upper = std::min(M_PI, 8.0 * sigma);
    double h = upper / steps;
    std::vector<double> z(bands, 0.0), p;
    for (int s = 0; s <= steps; ++s) {
        double theta = s * h;
        double w = ((s == 0) or (s == steps) ? 1.0 : ((s % 2) ? 4.0 : 2.0));
        double g = std::exp(-theta * theta / (2.0 * sigma * sigma)) * std::sin(theta);
        Legendre0(bands, std::cos(theta), p);
        for (int l = 0; l < bands; ++l)
            z[l] += w * g * K(l, 0) * p[l];
    }
    double centre = 0;
    for (int l = 0; l < bands; ++l) {
        z[l] *= 2.0 * M_PI * h / 3.0;
        centre += z[l] * std::sqrt((2.0 * l + 1.0) / (4.0 * M_PI));
    }
    if (centre <= 0)
        return;
    std::vector<double> y;
    EvalAll(bands, PI2 - longitude, latitude, y);
    for (int l = 0; l < bands; ++l) {
        double factor = depth / centre * std::sqrt(4.0 * M_PI / (2.0 * l + 1.0)) * z[l];
        for (int m = -l; m <= l; ++m)
            coeff[l * (l + 1) + m] -= factor * y[l * (l + 1) + m];
    }
}

void PIResource::ApplyDepletion(const Field& base, const std::vector<Depletion>& rows, int64 now, Field& final)
{
    final = base;
    if (!sConfig.cosmic.PIDepletionEnabled or rows.empty())
        return;
    std::vector<double> coeff(base.coeff.begin(), base.coeff.end());
    for (auto& cur : rows) {
        double factor = DepletionFactor(cur, now);
        if (factor <= 0)
            continue;
        double baseValue = std::max(0.0, base.Value(PI2 - cur.longitude, cur.latitude));
        double depth = std::min(sConfig.cosmic.PIDepletionMaxFraction * baseValue, cur.amount * sConfig.cosmic.PIDepletionPerUnit) * factor;
        SubtractGaussian(coeff, base.bands, cur.latitude, cur.longitude, cur.headRadius * sConfig.cosmic.PIDepletionRadius, depth);
    }
    for (size_t i = 0; i < coeff.size(); ++i)
        final.coeff[i] = (float)coeff[i];
}
