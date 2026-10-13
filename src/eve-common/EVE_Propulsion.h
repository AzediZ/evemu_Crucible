 /**
  * @name EVE_Propulsion.h
  *     afterburner and microwarpdrive (propulsion module) rules
  *
  * these are pure functions, so the unit tests (eve-test) can check them
  *
  */


#ifndef EVE_PROPULSION_H
#define EVE_PROPULSION_H


namespace Propulsion {

    /* the velocity multiplier of a running afterburner or microwarpdrive:
     *      maxVelocity x (1 + speedFactor/100 x speedBoostFactor / mass)
     *  speedFactor (20) is the module's speed bonus in %, speedBoostFactor (567) its thrust, mass the ship's mass with the
     *  module's massAddition (796) on it.  a module sized for its hull gives about its speedFactor, a bigger one more.
     *  Crucible's dogma data (effects speedBoostFactorCalculator(2), speedBoostMassAddition/SigRad) only carries
     *  speedFactor x thrust:  the division by the ship's mass was done in code, as here
     */
    inline double SpeedMultiplier(double speedFactor, double thrust, double mass)
    {
        if (!(mass > 0.0) or !(thrust > 0.0))
            return 1.0;
        double mult(1.0 + (speedFactor / 100.0) * thrust / mass);
        return (mult > 0.0 ? mult : 1.0);
    }

    /* microwarpdrive signature radius bloom:  signatureRadius PostPercent signatureRadiusBonus (500 on a tech 1 mwd = x6) */
    inline double SignatureMultiplier(double signatureRadiusBonus)
    {
        double mult(1.0 + signatureRadiusBonus / 100.0);
        return (mult > 0.0 ? mult : 0.0);
    }

}

#endif  // EVE_PROPULSION_H
