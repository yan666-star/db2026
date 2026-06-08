-- A transaction committed after recovery must not be overwritten by the
-- old loser transaction during a later restart.
UPDATE recovery_full SET amount = 36.0 WHERE id = 3;
