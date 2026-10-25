-- Recruitment adverts' recruiters keyed the advert by a smallint, too small for the advert IDs
-- +migrate Up
ALTER TABLE `crpRecruiters` MODIFY `adID` int(10) NOT NULL;

-- +migrate Down
ALTER TABLE `crpRecruiters` MODIFY `adID` smallint(5) NOT NULL;
