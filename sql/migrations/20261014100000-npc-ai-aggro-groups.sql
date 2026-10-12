-- npc ai phase 2 (doc/code_and_design_notes/npc-ai/PLAN.md 4.3, 6.5):  the aggro mode of each dungeon spawn group.
--  proactive (0, the default) groups acquire on their own, as before.  passive (1) groups wait until a member is damaged,
--  ewar'd or locked, or a player ship or drone comes within aggroRange of a member (0 = <npc><PassiveAggroRange>).
--  groups of one dungeon with the same non-zero aggroLink aggro together.
--  the defaults keep every group proactive;  per-mission passive/linked data is later content work.
--  read by DungeonDB::GetSpawnGroups (DungeonDataMgr)
-- +migrate Up
ALTER TABLE `dunSpawnGroups`
  ADD COLUMN `aggroMode` tinyint(3) unsigned NOT NULL DEFAULT 0 COMMENT '0 proactive, 1 passive',
  ADD COLUMN `aggroRange` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'm, 0 = npc.PassiveAggroRange',
  ADD COLUMN `aggroLink` int(10) unsigned NOT NULL DEFAULT 0 COMMENT 'groups with the same non-zero link aggro together';

-- +migrate Down
ALTER TABLE `dunSpawnGroups` DROP COLUMN `aggroLink`, DROP COLUMN `aggroRange`, DROP COLUMN `aggroMode`;
