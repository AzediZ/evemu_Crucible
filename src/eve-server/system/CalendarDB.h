/*
 *  CalendarDB.h
 *
 *  Allan
*/


#ifndef EVEMU_SYSTEM_CALENDARDB_H_
#define EVEMU_SYSTEM_CALENDARDB_H_


#include "ServiceDB.h"


namespace Calendar {
    struct EventInfo {
        bool important;
        bool isDeleted;
        uint8 flag;
        uint8 autoEventType;
        uint16 duration;            // minutes, 0 = not specified
        uint32 eventID;
        uint32 ownerID;
        uint32 creatorID;
        int64 dateTime;
        std::string title;
    };
}

class CalendarDB {
  public:

      // marks the event deleted (it stays listed as cancelled)
      static void DeleteEvent(uint32 eventID);
      static uint32 SaveSystemEvent(uint32 ownerID, uint32 creatorID, int64 startDateTime, uint8 autoEventType,
                                    std::string title, std::string description, bool important=false);
      // insert or update the character's response
      static void SaveEventResponse(uint32 charID, uint32 eventID, uint32 response);

      // personal, corp and alliance events (flag from the owner).  duration 0 = not specified.  returns the eventID (0 on failure)
      static uint32 SaveNewEvent(uint32 ownerID, uint32 creatorID, int64 dateTime, uint16 duration, const std::string& title,
                                 const std::string& description, bool important);
      static bool GetEventInfo(uint32 eventID, Calendar::EventInfo& into);
      static void UpdateEvent(uint32 eventID, int64 dateTime, uint16 duration, const std::string& title, const std::string& description, bool important);
      // a new start time resets Accepted and Maybe responses to Undecided
      static void ResetResponsesOnReschedule(uint32 eventID);

      // personal event invitations (sysCalendarEventInvitees)
      static void GetInvitees(uint32 eventID, std::vector<uint32>& into);
      static bool IsInvited(uint32 eventID, uint32 charID);
      // invitees get an Undecided response
      static void AddInvitees(uint32 eventID, const std::vector<uint32>& charIDs);
      // removed invitees get an Uninvited response
      static void RemoveInvitees(uint32 eventID, const std::vector<uint32>& charIDs);

      static PyRep* GetEventList(uint32 ownerID, uint32 month, uint32 year);
      // personal events of others the character is invited to
      static PyRep* GetInvitedEventList(uint32 charID, uint32 month, uint32 year);
      static PyRep* GetEventDetails(uint32 eventID);

      // (characterID, status) without the uninvited; invitees that haven't answered show as Undecided
      static PyRep* GetResponsesToEvent(uint32 eventID);
      static PyRep* GetResponsesForCharacter(uint32 charID);
      // events from now to the end of next month (the client's "upcoming events") the character can see and hasn't declined
      static uint32 CountUpcomingEvents(uint32 charID, uint32 corpID, uint32 allianceID);

  protected:
      static PyRep* EventListFromResult(DBQueryResult& res);
};

#endif // EVEMU_SYSTEM_CALENDARDB_H_
