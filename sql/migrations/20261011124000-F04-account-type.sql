-- account.type is now sent to the client as session userType.  it was never honoured before, so the old
-- default (23 = Trial, which the client restricts) was never a deliberate choice:  move those accounts to Mammon (30).
-- set 23 by hand to get the client's trial account restrictions.
-- +migrate Up
ALTER TABLE `account` MODIFY `type` tinyint(3) unsigned NOT NULL DEFAULT 30;
UPDATE `account` SET `type` = 30 WHERE `type` = 23;

-- +migrate Down
ALTER TABLE `account` MODIFY `type` tinyint(3) unsigned NOT NULL DEFAULT 23;
