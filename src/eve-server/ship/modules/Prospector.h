 /**
  * @name Prospector.h
  *   prospector module class (salvage, hacking, data mining)
  * @Author:         Allan
  * @date:   11 August 2016   -UD/RW 12 April 2017  -UD/RW 10 February 2018
  * @update: F11 - Crucible per-cycle roll on site containers and wrecks
  */

#ifndef _EVE_SHIP_MODULES_PROSPECTOR_MODULE_H_
#define _EVE_SHIP_MODULES_PROSPECTOR_MODULE_H_

#include "ship/modules/ActiveModule.h"


/* Codebreakers and Analyzers (Data_Miner) and Salvagers.  Crucible (pre-Odyssey) rules:
 *  - a locked site container (spawn container with accessDifficulty) needs the module whose skill is the container's
 *    requiredSkill1 (Hacking, Archaeology or Salvaging) at the container's requiredSkill1Level.  salvagers also salvage wrecks.
 *  - chance per cycle (%) = target accessDifficulty + module accessDifficultyBonus (skills x5/level, rigs, implants via dogma).
 *    one roll at the end of each cycle.  a chance <= 0 can't be attempted.
 *  - success opens the container for everyone (its loot is made then) or salvages the wreck, and the module stops.
 *  - failure keeps cycling.  dungeon triggers (hacking/archaeology/salvaging success/failure) go to the site.
 */
class Prospector: public ActiveModule
{
public:
    Prospector(ModuleItemRef mRef, ShipItemRef sRef);
    virtual ~Prospector();

    virtual Prospector*         GetProspectModule()     { return this; }
    virtual bool        IsProspectModule() const        { return true; }

    /* functions to be handled in derived classes as needed */
    virtual void        Update();  // this is used to set char mods on active modules created before pilot entered ship

    /* ActiveModule overrides */
    virtual void Activate(uint16 effectID, uint32 targetID=0, int16 repeat=0);
    // target checks with the client's messages.  throws
    virtual bool CanActivate();
    // the roll at the end of each cycle
    virtual void CycleComplete();
    // the success message rides on the effect stop (OnGodmaShipEffect error)
    virtual PyTuple* TakeStopMessage();

    // the target went away while we work on it (another salvager got it, it was shot, it despawned)
    void TargetDestroyed();

protected:
    void CheckTarget();
    // chance in % for the current target
    int16 GetChance();
    void OnContainerSuccess();
    void OnSalvageSuccess();
    // EVEDB::invGroups::Salvager module rolling on a wreck
    bool IsSalvaging()                                  { return (m_salvager and (m_targetSE != nullptr) and m_targetSE->IsWreckSE()); }
    // dungeon trigger for this module's skill
    void SendTrigger(uint32 itemID, bool success);

    // OnRemoteMessage to the pilot (in-cycle messages)
    void SendRemote(const char* key, PyDict* args);
    // old style {type} argument:  (UE_TYPEID, typeID)
    PyDict* TypeArg(uint16 typeID);
    // new style {[item]typeID.name} argument:  raw typeID
    PyDict* ItemArg(uint16 typeID);

    bool m_salvager :1;
    bool m_dataMiner :1;
    bool m_targetGone :1;

    uint16 m_targetTypeID;

    PyTuple* m_stopMsg;

private:
    EVEItemFlags m_holdFlag;
};

#endif  //_EVE_SHIP_MODULES_PROSPECTOR_MODULE_H_


/*
{'messageKey': 'LockedContainerOpened'}         Sie haben erfolgreich auf {[item]typeID.name} zugegriffen.  (You successfully access the {[item]typeID.name}.)
{'messageKey': 'LockedContainerNotOpened'}      Your attempt to access the {[item]typeID.name} failed this time.
{'messageKey': 'LockedContainerOpenedAlready'}  No further tools are needed to access the {[item]typeID.name}.
{'messageKey': 'LockedContainerTooComplex'}     The {[item]typeID.name} is too complex for you to gather anything of value.
{'messageKey': 'LockedContainerInaccessible'}   You cannot access the {[item]typeID.name} without specialized equipment.
*/

/*
{'messageKey': 'SalvageTooMuchLoot', 'dataID': 17879586, 'suppressable': False, 'bodyID': 258062, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': 258061, 'messageID': 2311}
  u'SalvageTooMuchLootBody'}(u'You cannot salvage this wreck because it contains too much loot to fit into a single cargo container. <br>\r\nThe wreck contains <b>{[numeric]volume, useGrouping} m3</b> but can contain no more than <b>{[numeric]maxvolume, useGrouping} m3</b> to be salvageable.', None, {u'{[numeric]maxvolume, useGrouping}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 32, 'kwargs': {}, 'variableName': 'maxvolume'}, u'{[numeric]volume, useGrouping}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 32, 'kwargs': {}, 'variableName': 'volume'}})
{'messageKey': 'SalvagingFailure', 'dataID': 17879772, 'suppressable': False, 'bodyID': 258135, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2114}
  u'SalvagingFailureBody'}(u'Your salvaging attempt failed this time.'
{'messageKey': 'SalvagingPointless', 'dataID': 17880311, 'suppressable': False, 'bodyID': 258344, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2118}
  u'SalvagingPointlessBody'}(u'You cannot salvage the {type} as there is nothing that can be salvaged from it.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingPointlessLoot', 'dataID': 17880317, 'suppressable': False, 'bodyID': 258346, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2121}
  u'SalvagingPointlessLootBody'}(u'You fail to salvage from the {type} because your salvaging equipment cannot be engaged until all loot has been removed from it.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingPointlessOverload', 'dataID': 17880291, 'suppressable': False, 'bodyID': 258337, 'messageType': 'info', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2119}
  u'SalvagingPointlessOverloadBody'}(u'You cannot salvage the {type} as you do not have the cargo space to store all the resources which might be recovered under ideal circumstances. Clear out at least {[numeric]units, decimalPlaces=1} m3 of cargo space and try again.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}, u'{[numeric]units, decimalPlaces=1}': {'conditionalValues': [], 'variableType': 9, 'propertyName': None, 'args': 512, 'kwargs': {'decimalPlaces': 1}, 'variableName': 'units'}})
{'messageKey': 'SalvagingSuccess', 'dataID': 17880302, 'suppressable': False, 'bodyID': 258341, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2115}
  u'SalvagingSuccessBody'}(u'You successfully salvage from the {type}.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingSuccessHollow', 'dataID': 17880214, 'suppressable': False, 'bodyID': 258307, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2125}
  u'SalvagingSuccessHollowBody'}(u'Your salvager successfully completes its cycle. Unfortunately the {type} contains nothing of value.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingSuccessOverloaded', 'dataID': 17880294, 'suppressable': False, 'bodyID': 258338, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2117}
  u'SalvagingSuccessOverloadedBody'}(u'You successfully salvage from the {type}. However due to your lack of cargo space, some or all of the salvaged resources may have been destroyed in the process.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingTooComplex', 'dataID': 17880308, 'suppressable': False, 'bodyID': 258343, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2116}
  u'SalvagingTooComplexBody'}(u'Salvaging the {type} is too difficult for you to be able to do.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
{'messageKey': 'SalvagingTooLate', 'dataID': 17880314, 'suppressable': False, 'bodyID': 258345, 'messageType': 'notify', 'urlAudio': '', 'urlIcon': '', 'titleID': None, 'messageID': 2120}
  u'SalvagingTooLateBody'}(u'Your salvaging attempt failed because the {type} was already salvaged.', None, {u'{type}': {'conditionalValues': [], 'variableType': 10, 'propertyName': None, 'args': 0, 'kwargs': {}, 'variableName': 'type'}})
*/
