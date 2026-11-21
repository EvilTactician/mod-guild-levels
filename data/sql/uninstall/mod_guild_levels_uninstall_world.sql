-- mod-guild-levels: removes the module's world rows. Run this after removing the module and its
-- client patch rows.
DELETE FROM `spell_dbc` WHERE `ID` IN (90450, 90451, 90452, 90454);
DELETE FROM `spell_script_names` WHERE `spell_id` IN (90452, 90454);
DELETE FROM `gameobject_template` WHERE `entry` = 9503400;
DELETE FROM `gameobject` WHERE `id` = 9503400;
