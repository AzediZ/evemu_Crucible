-- the MOTDs players set in their corp's and alliance's chat channels, kept across restarts (they used to go back to the
--  corp's or alliance's name).  setBy:  the character who set it, setAt:  when (FILETIME)
-- +migrate Up
CREATE TABLE IF NOT EXISTS chatChannelMotds (
    channelID       INT(10) NOT NULL,
    motd            MEDIUMTEXT,
    setBy           INT(10) UNSIGNED NOT NULL DEFAULT 0,
    setAt           BIGINT(20) NOT NULL DEFAULT 0,
    PRIMARY KEY (channelID)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- +migrate Down
DROP TABLE IF EXISTS chatChannelMotds;
