
 /**
  * @name SimEconomy.cpp
  *   market simulation:  price and demand formulas
  *
  * @date:          13Oct26
  *
  */

#include <cmath>
#include <cstdint>

#include "market/sim/SimEconomy.h"

namespace MktSim {
namespace Econ {

double Anchor(double refLocal, double wc, double costNow, double costRef)
{
    if ((wc <= 0.0) or !(costRef > 0.0) or !(costNow > 0.0))
        return refLocal;
    return refLocal * ((1.0 - wc) + wc * costNow / costRef);
}

double TargetValue(double anchor, double stock, double targetStock, double theta, double kMax, bool* clamped)
{
    double lo(anchor / kMax), hi(anchor * kMax), v(hi);
    if ((stock > 0.0) and (targetStock > 0.0))
        v = anchor * std::pow(stock / targetStock, -theta);
    bool hit(false);
    if (v >= hi) {
        v = hi;
        hit = true;
    } else if (v <= lo) {
        v = lo;
        hit = true;
    }
    if (clamped != nullptr)
        *clamped = hit;
    return v;
}

double OUStep(double lnV, double lnTarget, double kappa, double dtDays, double sigma, double xi)
{
    if (!(dtDays > 0.0))
        return lnV;
    double decay(std::exp(-kappa * dtDays));
    // variance of the exact step:  sigma^2 (1 - e^{-2 kappa dt}) / (2 kappa).  sigma^2 dt for kappa -> 0
    double var((kappa > 1e-9) ? (sigma * sigma * (1.0 - decay * decay) / (2.0 * kappa)) : (sigma * sigma * dtDays));
    return lnTarget + (lnV - lnTarget) * decay + std::sqrt(var) * xi;
}

double ExecFeedback(double lnV, double price, double qty, double demandDt, double lambda, double maxMove)
{
    if (!(price > 0.0) or !(qty > 0.0) or (lambda <= 0.0))
        return lnV;
    double weight((demandDt > 0.0) ? std::fmin(1.0, qty / demandDt) : 1.0);
    double move(lambda * (std::log(price) - lnV) * weight);
    if (move > maxMove)
        move = maxMove;
    else if (move < -maxMove)
        move = -maxMove;
    return lnV + move;
}

double HalfSpread(double groupHalfSpread, double minHalfSpread, double feeFloor)
{
    return std::fmax(groupHalfSpread, std::fmax(minHalfSpread, feeFloor));
}

Cents RoundPrice(double isk, bool up)
{
    if (!(isk > 0.01))
        return 1;
    // the step that keeps 4 significant digits, in cents (at least one)
    int digits((int)std::floor(std::log10(isk)) + 1);
    double stepIsk(std::pow(10.0, digits - 4));
    Cents step((stepIsk < 0.01) ? 1 : (Cents)std::llround(stepIsk * 100.0));
    double cents(isk * 100.0);
    Cents units(up ? (Cents)std::ceil(cents / (double)step - 1e-9) : (Cents)std::floor(cents / (double)step + 1e-9));
    Cents out(units * step);
    return (out < 1) ? 1 : out;
}

void QuoteLadder(double value, double h, int levels, Cents* asks, Cents* bids)
{
    double d(h / 2.0);
    for (int i = 0; i < levels; ++i) {
        asks[i] = RoundPrice(value * (1.0 + h) * (1.0 + i * d), true);
        bids[i] = RoundPrice(value * (1.0 - h) * (1.0 - i * d), false);
        if (bids[i] >= asks[0])
            bids[i] = asks[0] - 1;
        if (bids[i] < 1)
            bids[i] = 1;
        // levels stay apart after rounding
        if ((i > 0) and (asks[i] <= asks[i - 1]))
            asks[i] = asks[i - 1] + 1;
        if ((i > 0) and (bids[i] >= bids[i - 1]))
            bids[i] = (bids[i - 1] > 1) ? bids[i - 1] - 1 : 1;
    }
}

double DemandRate(double d0, double weekday, double shock, double budget, double ask, double refAsk, double eta)
{
    if (!(d0 > 0.0) or !(ask > 0.0) or !(refAsk > 0.0))
        return 0.0;
    return d0 * weekday * shock * budget * std::pow(ask / refAsk, -eta);
}

double BudgetFactor(double wallet, double target)
{
    if (!(target > 0.0))
        return 1.0;
    double b(wallet / target);
    return std::fmin(1.2, std::fmax(0.2, b));
}

double ControllerFactor(double money, double moneyTarget, double integral, double kp, double ki)
{
    if (!(money > 0.0) or !(moneyTarget > 0.0))
        return 2.0;
    double x(kp * std::log(moneyTarget / money) + ki * integral);
    return std::fmin(2.0, std::fmax(0.5, std::exp(x)));
}

Cents MulCents(Cents price, Units qty)
{
    if ((price < 0) or (qty < 0))
        return -1;
    if ((qty != 0) and (price > INT64_MAX / qty))
        return -1;
    return price * qty;
}

Units RoundLot(double q, uint32_t lot)
{
    if (lot < 1)
        lot = 1;
    if (!(q >= lot * 0.5))
        return 0;
    Units lots((Units)std::floor(q / (double)lot));
    if (lots < 1)
        lots = 1;
    return lots * (Units)lot;
}

}
}
