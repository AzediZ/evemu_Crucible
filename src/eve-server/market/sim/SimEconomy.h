
 /**
  * @name SimEconomy.h
  *   market simulation:  the price and demand formulas of PLAN 6.2, 6.3, 6.8 and the no-arbitrage floors of 7.6, as pure
  *   functions
  *
  * @date:          13Oct26
  *
  */

#ifndef _EVE_SERVER_MARKET_SIM_ECONOMY_H__
#define _EVE_SERVER_MARKET_SIM_ECONOMY_H__

#include <cstdint>

#include "market/sim/SimTypes.h"

namespace MktSim {
namespace Econ {

    // Vanchor = refPrice * regionRatio * [(1 - wc) + wc * C(t) / Cref].  refLocal = refPrice * regionRatio
    double Anchor(double refLocal, double wc, double costNow, double costRef);

    // V* = clamp(Vanchor * (S / S*)^-theta, Vanchor / Kmax, Vanchor * Kmax).  clamped tells whether a bound was hit
    double TargetValue(double anchor, double stock, double targetStock, double theta, double kMax, bool* clamped = nullptr);

    /* one log-OU step of dt days toward lnTarget:  the exact discretisation of
     *  d lnV = kappa (lnV* - lnV) dt + sigma dW,  so long steps (cold cells, catch-up) stay stable.  xi ~ N(0,1) */
    double OUStep(double lnV, double lnTarget, double kappa, double dtDays, double sigma, double xi);

    // the horizon in days over which the calibration measures volatility (utils/market-calibration/derive.py VOL_LAG):
    //  mktCalType.volatility = 1.4826 MAD(ln p[d+h] / p[d]) / sqrt(h)
    const double VolatilityHorizonDays = 5.0;

    /* the OU sigma whose stationary h-day moves have the calibrated volatility per sqrt(day):  a stationary OU path
     *  moves sigma^2 (1 - e^{-kappa h}) / kappa over h days, less than a random walk's sigma^2 h, so
     *  sigma = volatility * sqrt(kappa h / (1 - e^{-kappa h}))  (x1.30 at a 3 day half life and h = 5) */
    double HorizonSigma(double volatility, double kappa, double horizonDays = VolatilityHorizonDays);

    // executions feed back:  lnV + lambda * ln(P / V) * min(1, q / (D * dt)), the move capped at +-maxMove
    double ExecFeedback(double lnV, double price, double qty, double demandDt, double lambda, double maxMove = 0.05);

    // h = max(group half spread, MinHalfSpread, fees + tax + margin)
    double HalfSpread(double groupHalfSpread, double minHalfSpread, double feeFloor);

    // prices round to 4 significant digits and whole cents:  asks up, bids down (so rounding never narrows the spread)
    Cents RoundPrice(double isk, bool up);

    // ask_i = V (1 + h)(1 + i d), bid_i = V (1 - h)(1 - i d), d = h / 2, i = 0..levels-1, rounded.  bids stay at least a
    //  cent under asks and at least a cent
    void QuoteLadder(double value, double h, int levels, Cents* asks, Cents* bids);

    /* final demand, units per day:  D0 * weekday * shock * B * (ask / refAsk)^-eta.  refAsk is the ask at the anchor, so
     *  a book quoted at fair value sells the calibrated volume */
    double DemandRate(double d0, double weekday, double shock, double budget, double ask, double refAsk, double eta);

    // B in [0.2, 1.2]:  a consumer's wallet against its target
    double BudgetFactor(double wallet, double target);

    // the money controller's income multiplier:  clamp(exp(kp ln(M*/M) + ki integral), lo, hi)  (PLAN 6.8:  0.5, 2)
    double ControllerFactor(double money, double moneyTarget, double integral, double kp, double ki, double lo = 0.5, double hi = 2.0);

    // value of qty units at a price, or -1 when it would not fit (no trade is that big)
    Cents MulCents(Cents price, Units qty);

    // q rounded down to whole lots (at least one lot when q >= half a lot), 0 when q < lot / 2
    Units RoundLot(double q, uint32_t lot);

    inline Cents ToCents(double isk)                   { return (Cents)(isk * 100.0 + (isk >= 0 ? 0.5 : -0.5)); }
    inline double ToIsk(Cents c)                        { return (double)c / 100.0; }

}
}

#endif  // _EVE_SERVER_MARKET_SIM_ECONOMY_H__
