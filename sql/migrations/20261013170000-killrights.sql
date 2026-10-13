-- kill rights (CrimeWatch).  30 days, so they're kept in the db
-- +migrate Up
create table if not exists crimeKillRights
(
    holderID int unsigned not null,
    targetID int unsigned not null,
    expiresAt bigint not null,
    primary key (holderID, targetID)
);

-- +migrate Down
drop table crimeKillRights;
