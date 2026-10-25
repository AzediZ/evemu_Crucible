-- The count of logins since the server started.  A smallint stopped at 32,767:  every login after that failed the
--  update and logged a database error
-- +migrate Up
ALTER TABLE `srvStatus` MODIFY `Connections` INT UNSIGNED NOT NULL DEFAULT 0;

-- +migrate Down
ALTER TABLE `srvStatus` MODIFY `Connections` SMALLINT(6) NOT NULL;
