-- a market trade (and a market order's placement, change or cancel) is saved in one transaction, so a crash in the middle
--  never leaves half of it:  paid but no items, items twice, an order without its escrow.  the items it makes, moves or
--  deletes (entity, entity_attributes) and corp wallet divisions (crpWalletDivisons) were MyISAM, which ignores
--  transactions:  their rows were kept while the rest of the trade was rolled back.  InnoDB also recovers by itself after
--  a crash, where a MyISAM table can be left marked as crashed until it is repaired.
--  (MariaDB 10.2.4+ and MySQL 8 keep the AUTO_INCREMENT of entity across restarts, as MyISAM did)
-- +migrate Up
ALTER TABLE entity ENGINE=InnoDB;
ALTER TABLE entity_attributes ENGINE=InnoDB;
ALTER TABLE crpWalletDivisons ENGINE=InnoDB;

-- +migrate Down
ALTER TABLE crpWalletDivisons ENGINE=MyISAM;
ALTER TABLE entity_attributes ENGINE=MyISAM;
ALTER TABLE entity ENGINE=MyISAM;
