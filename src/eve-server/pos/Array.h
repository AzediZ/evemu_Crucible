
/**
 * @name Array.h
 *   Class for POS Array Modules.
 *
 * @Author:         Allan
 * @date:   28 December 17
 */


#ifndef EVEMU_POS_ARRAY_H_
#define EVEMU_POS_ARRAY_H_


#include "pos/Structure.h"


class ArraySE
: public StructureSE
{
public:
    ArraySE(StructureItemRef structure, EVEServiceManager& services, SystemManager* system, const FactionData& data);
    virtual ~ArraySE()                                  { /* do nothing here */ }

    /* class type pointer querys. */
    virtual ArraySE*            GetArraySE()            { return this; }

    /* class type tests. */
    virtual bool                IsArraySE()             { return true; }

    /* SystemEntity interface */
    virtual void                Process();

    /* virtual functions default to base class and overridden as needed */
    virtual void Init();
    virtual void SetOnline();
    virtual void SetOffline();
    virtual void Killed(Damage &damage);

    // assembly arrays and mobile laboratories with R.A.M. assembly lines
    bool                        IsRamInstallation() const   { return m_ram; }

    /* refining arrays:  the ore in the processing area (flagCargo) is refined in one batch after
     *  operationalDuration x refiningDelayMultiplier, at the array's refiningYieldMultiplier.  throws the reasons it can't
     */
    void                        StartRefining(Client* pClient);

private:
    void                        FinishRefining();

    bool m_ram;
    bool m_refinery;
    Timer m_refineTimer;


};

#endif  // EVEMU_POS_ARRAY_H_
